#pragma once
#include <cstdint>

/// Software hardware counters.
struct PerfSoftwareData
{
    uint64_t cpu_migrations = -1;
    uint64_t context_switches = -1;
};

/// Hardware performance counters.
struct PerfHardwareData
{
    uint64_t cpu_cycles = -1;
    uint64_t instructions = -1;
    uint64_t cache_misses = -1;
    uint64_t branch_instructions = -1;
    uint64_t branch_misses = -1;
};

/// Initialises the performance counting subsystem.
extern void perf_init();

/// Shutdowns the performance counting subsystem.
extern void perf_shutdown();

/// Gets the number of processors configured on the system (even if offline).
extern int perf_nprocs();

/// Consumes the hardware performance counters regarding the specified
/// CPU index.
///
/// A consume operation obtains counters as if they were reset during
/// the previous consume operation.
extern auto perf_consume_hw(int cpu) -> PerfHardwareData;

/// Consumes the software performance counters regarding this process.
///
/// A consume operation obtains counters as if they were reset during
/// the previous consume operation.
extern auto perf_consume_sw() -> PerfSoftwareData;
