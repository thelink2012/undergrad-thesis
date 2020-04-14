#include "perf.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unistd.h>
#include <asm/unistd.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>
#include <linux/perf_event.h>

/// Maximum events that can be recorded simultaneously.
///
/// The Cortex A7 is a limiting factor here because it contains
/// only four performance counting registers.
constexpr int MAX_EVENTS_PER_GROUP = 5;

/// Maximum number of processor cores we are going to use.
constexpr int MAX_PROCESSORS = 8;

/// Number of software counters to collect.
constexpr int NUM_SOFTWARE_COUNTERS = 2;

struct PerfEvent
{
    int fd;
    uint64_t id;
    uint64_t prev_value;
};

static PerfEvent perf_cpu[MAX_PROCESSORS][MAX_EVENTS_PER_GROUP];
static PerfEvent perf_sw[NUM_SOFTWARE_COUNTERS];
static int num_processors;

void perf_init()
{
    auto perf_event_open = [](struct perf_event_attr *hw_event, pid_t pid,
                               int cpu, int group_fd, unsigned long flags) {
        return syscall(__NR_perf_event_open, hw_event, pid, cpu,
                       group_fd, flags);
    };

    num_processors = get_nprocs_conf();
    assert(num_processors <= MAX_PROCESSORS);
    
    fprintf(stderr, "sync_jvmti: detected %d processors\n", num_processors);

    for(int cpu = 0; cpu < num_processors; ++cpu)
    {
        for(int i = 0; i < MAX_EVENTS_PER_GROUP; ++i)
        {
            uint64_t config;
            int group_fd;

            switch(i)
            {
                case 0:
		    config = PERF_COUNT_HW_CPU_CYCLES;
		    group_fd = -1;
		    break;
                case 1:
		    config = PERF_COUNT_HW_INSTRUCTIONS;
		    group_fd = perf_cpu[cpu][0].fd;
		    break;
                case 2:
		    config = PERF_COUNT_HW_CACHE_MISSES;
		    group_fd = perf_cpu[cpu][0].fd;
		    break;
		case 3:
		    config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
		    group_fd = perf_cpu[cpu][0].fd;
		    break;
		case 4:
		    config = PERF_COUNT_HW_BRANCH_MISSES;
		    group_fd = perf_cpu[cpu][0].fd;
		    break;
                default:
		    perf_cpu[cpu][i].fd = -1;
		    perf_cpu[cpu][i].id = -1;
		    continue;
            }

            struct perf_event_attr pe;
            memset(&pe, 0, sizeof(pe));
            pe.size = sizeof(pe);
            pe.type = PERF_TYPE_HARDWARE;
            pe.config = config;
            pe.exclude_hv = true;
            pe.exclude_kernel = true;
            pe.disabled = true;
            pe.read_format = PERF_FORMAT_ID | PERF_FORMAT_GROUP;

            const auto fd = perf_event_open(&pe, -1, cpu, group_fd, 0);
            if(fd == -1)
            {
                perror("sync_jvmti: failed to initialise perf");
                abort();
            }

            perf_cpu[cpu][i].fd = fd;
            ioctl(fd, PERF_EVENT_IOC_ID, &perf_cpu[cpu][i].id);
            ioctl(fd, PERF_EVENT_IOC_RESET, 0);
            perf_cpu[cpu][i].prev_value = 0;
        }
    }

    for(int i = 0; i < NUM_SOFTWARE_COUNTERS; ++i)
    {
	uint64_t config;
	int group_fd;

	switch(i)
	{
	    case 0:
		config = PERF_COUNT_SW_CPU_MIGRATIONS;
		group_fd = -1;
		break;
	    case 1:
		config = PERF_COUNT_SW_CONTEXT_SWITCHES;
		group_fd = perf_sw[0].fd;
		break;
	    default:
		perf_sw[i].fd = -1;
		perf_sw[i].id = -1;
		continue;
	}

	struct perf_event_attr pe;
	memset(&pe, 0, sizeof(pe));
	pe.size = sizeof(pe);
	pe.type = PERF_TYPE_SOFTWARE;
	pe.config = config;
	pe.exclude_hv = true;
	pe.exclude_kernel = false;
	pe.disabled = true;
	pe.read_format = PERF_FORMAT_ID | PERF_FORMAT_GROUP;

	const auto fd = perf_event_open(&pe, 0, -1, group_fd, 0);
	if(fd == -1)
	{
	    perror("sync_jvmti: failed to initialise perf");
	    abort();
	}

	perf_sw[i].fd = fd;
	ioctl(fd, PERF_EVENT_IOC_ID, &perf_sw[i].id);
	ioctl(fd, PERF_EVENT_IOC_RESET, 0);
	perf_sw[i].prev_value = 0;
    }

    for(int cpu = 0; cpu < num_processors; ++cpu)
    {
        const auto leader_fd = perf_cpu[cpu][0].fd;
        ioctl(leader_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }

    if(true)
    {
        const auto leader_fd = perf_sw[0].fd;
        ioctl(leader_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
}

void perf_shutdown()
{
    for(int cpu = 0; cpu < num_processors; ++cpu)
    {
        for(int i = 0; i < MAX_EVENTS_PER_GROUP; ++i)
        {
            const auto fd = perf_cpu[cpu][i].fd;
            if(fd != -1)
            {
                close(perf_cpu[cpu][i].fd);
                perf_cpu[cpu][i].fd = -1;
                perf_cpu[cpu][i].id = -1;
                perf_cpu[cpu][i].prev_value = 0;
            }
        }
    }

    for(int i = 0; i < NUM_SOFTWARE_COUNTERS; ++i)
    {
	close(perf_sw[i].fd);
	perf_sw[i].fd = -1;
	perf_sw[i].id = -1;
	perf_sw[i].prev_value = 0;
    }
}

int perf_nprocs()
{
    return num_processors;
}

auto perf_consume_hw(int cpu) -> PerfHardwareData
{
    struct
    {
        uint64_t nr;    /* The number of events */
        struct {
            uint64_t value; /* The value of the event */
            uint64_t id;    /* if PERF_FORMAT_ID */
        } values[MAX_EVENTS_PER_GROUP];
    } data;

    assert(cpu < num_processors);

    const auto fd = perf_cpu[cpu][0].fd;

    if(fd == -1)
        return PerfHardwareData{};

    if(read(fd, &data, sizeof(data)) == -1)
    {
        perror("sync_jvmti: failed to read hardware counters");
        abort();
    }

    uint64_t counters[MAX_EVENTS_PER_GROUP];
    memset(counters, -1, sizeof(counters));

    for(uint64_t s = 0; s < data.nr; ++s)
    {
        for(int pi = 0; pi < MAX_EVENTS_PER_GROUP; ++pi)
        {
            if(data.values[s].id == perf_cpu[cpu][pi].id)
            {
                const auto value = data.values[s].value;
                const auto prev_value = perf_cpu[cpu][pi].prev_value;
                const auto u64_max = std::numeric_limits<uint64_t>::max();

                if(value >= prev_value)
                {
                    counters[pi] = value - prev_value;
                }
                else
                {
                    counters[pi] = 0;
                    counters[pi] += u64_max - prev_value;
                    counters[pi] += value;
                }

                perf_cpu[cpu][pi].prev_value = value;
            }
        }
    }

    return PerfHardwareData {
        counters[0],
        counters[1],
        counters[2],
        counters[3],
	counters[4],
    };
}

auto perf_consume_sw() -> PerfSoftwareData
{
    struct
    {
        uint64_t nr;    /* The number of events */
        struct {
            uint64_t value; /* The value of the event */
            uint64_t id;    /* if PERF_FORMAT_ID */
        } values[NUM_SOFTWARE_COUNTERS];
    } data;

    const auto fd = perf_sw[0].fd;

    if(fd == -1)
        return PerfSoftwareData{};

    if(read(fd, &data, sizeof(data)) == -1)
    {
        perror("sync_jvmti: failed to read software counters");
        abort();
    }

    uint64_t counters[NUM_SOFTWARE_COUNTERS];
    memset(counters, -1, sizeof(counters));

    for(uint64_t s = 0; s < data.nr; ++s)
    {
        for(int pi = 0; pi < NUM_SOFTWARE_COUNTERS; ++pi)
        {
            if(data.values[s].id == perf_sw[pi].id)
            {
                const auto value = data.values[s].value;
                const auto prev_value = perf_sw[pi].prev_value;
                const auto u64_max = std::numeric_limits<uint64_t>::max();

                if(value >= prev_value)
                {
                    counters[pi] = value - prev_value;
                }
                else
                {
                    counters[pi] = 0;
                    counters[pi] += u64_max - prev_value;
                    counters[pi] += value;
                }

                perf_sw[pi].prev_value = value;
            }
        }
    }

    return PerfSoftwareData {
        counters[0],
        counters[1],
    };
}
