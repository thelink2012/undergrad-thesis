#include <cstdio>
#include <memory>
#include <atomic>
#include <thread>
#include <cinttypes>
#include <cstring>
#include <cassert>
#include <shared_mutex>
#include <unistd.h>
#include <sched.h>
#include "phase.hpp"
#include "perf.hpp"
#include "time.hpp"

using std::memory_order_acquire;
using std::memory_order_release;
using std::memory_order_relaxed;

static void phase_init_settings();
static void phase_checkpoint_run(uint64_t curr_time);
static void phase_checkpoint_safe(AtomicPhase*, uint64_t curr_time);

// It is beneficial to use some kind of pause on a spin-loop to avoid burning
// too much CPU. Do note this pause does not reliquinsh to the scheduler.
//
// https://software.intel.com/en-us/articles/benefitting-power-and-performance-sleep-loops
#if defined(__x86_64__) || defined(__i386__)
#define cpu_relax()     __asm__ __volatile__ ("pause")
#elif defined(__arm__)
#define cpu_relax()     __asm__ __volatile__ ("yield")
#else
#error Please implement cpu_relax() for this platform.
#endif

// The phase mechanism accumulates and reports informations about a phase.
//
// Applications have phases. A phase is a finite time frame of execution.
// We usually give a phase the time frame of 1 millisecond.
//
// Once a phase expires, we run a checkpoint. The checkpoint is responsible
// for reporting all the information about the current phase and switching
// to the next one.
//
// We want to measure contention on each phase. How to do this efficiently
// with no extra synchronizations, threads, signals, etc?
//
// We use a data structure to accumulate all information about a phase.
// Any thread owning a reference to this structure is said to be a mutator of
// this phase.
//
// During the time between the beggining and end of a phase the mutators
// acquire ownership of the current phase and mutate it (concurrently).
//
// Once a phase ends, a checkpoint is run. A checkpoint takes all the
// information accumulated regarding the current phase, registers it, and
// begins a new phase.
//
// We have the following challenges for this design:
//
//  1) How to let the mutators mutate the current phase data concurrently?
//  2) How to let the checkpoint acquire all the accumulated data for this
//     phase without racing with the phase mutators?
//  3) How to not introduce more contention while doing both of this?
//
// The first problem is easy to solve. Let the phase data be commutative.
// The order on which operations happen on the data becomes irrelevant.
// We only allow mutators to perform atomic adds to the phase data.
//
// For the second problem, we solve it using a double-buffered phase data.
// Once the checkpoint is run, the current phase is swaped with the previous
// phase (which is empty). Now, the mutators are mutating the previous phase
// data (which becomes the next phase), and the checkpoint is reading the
// current phase data.
//
// This however still does not ensure the current phase data is exclusively
// owned by the checkpoint. Some mutator may have acquired a reference to 
// the phase before the swap, and may still be [briefly] mutating the phase.
//
// To solve this issue we use a multiple readers, single writer lock on top
// of the buffer index and reference counter of the buffer (we cannot do this
// using atomic primitives. That is two data words we are dealing with).
//
// Once the lock is held by readers (the mutators), it may **read** the current
// buffer index and increment the reference counter of the acquired index. Do
// note multiple mutators may hold the lock at once. So this only contends
// when a writer (checkpoint) wishes to touch the data, which happens rarely
// and quickly (see below).
//
// Once the lock is held by a writer (the checkpoint), it may swap the buffer
// index. Every mutator that follows this will use and increment the reference
// counter of the next buffer.
//
// Now the checkpoint must wait (spinning, as this is brief) until the
// last mutator of the current phase relinquishes ownership of it by
// turning the phase reference counter to 0.
//
// Now the checkpoint have exclusive access to the current phase data and
// the mutators are still running at full speed by mutating the next phase
// data.
//
// I know, not simple, but fast and beautiful. This achieves our third
// goal of not introducing any more contention. We want to measure contention,
// more contention would disturb the measurements.

static std::shared_timed_mutex phase_mutex;
static AtomicPhase phase_buffer[2];
static int phase_buf_index;

// This and the `phase_buffer` are the only data we need to share with the
// mutators. The mutators need to know when to give a chance to the checkpoint
// to run by knowning the difference between now and the last checkpoint run.
static std::atomic<uint64_t> prev_phase_time;

/// The duration of a phase in milliseconds.
static uint64_t phase_duration;

// The total time of the application spent contended in a lock.
static uint64_t total_cs_time;

// The total time of the application spent waiting for a condvar.
static uint64_t total_wait_time;

/// The total time of the application spent parked.
static uint64_t total_park_time;

/// The number of threads seen on the previous phase.
static int32_t prev_phase_thread_count;

/// The state of the running thread.
///
/// See `AtomicPhase::THREAD_STATE_*` constants for details. Note the value on
/// this array is the value of the constant minus one.
///
/// For example, the running state is equal `0`.
static uint8_t prev_phase_thread_state[AtomicPhase::MAX_THREADS];

/// The index on which each thread is running (approximation).
static uint8_t prev_phase_cpu_index[AtomicPhase::MAX_THREADS];

/// Number of threads created so far (even if already dead).
static std::atomic<int> thread_alloc_id;

/// Time the application started;
static uint64_t app_start_time;

/// CSV stream producing the output of the phase profiler.
static FILE* csv_stream;

/// Sometimes, we want to profile over fixed periods of time (instead of
/// relying on JVMTI events), so we use a thread for this.
static bool use_fixed_intervals;
static std::thread profiler_thread;
static std::atomic<bool> profiler_thread_kill;

void phase_init()
{
    std::unique_lock<std::shared_timed_mutex> lock(phase_mutex);

    const auto curr_time = get_time();

    phase_buf_index = 0;
    phase_buffer[0].reset();
    phase_buffer[1].reset();

    total_cs_time = 0;
    total_wait_time = 0;
    total_park_time = 0;

    prev_phase_thread_count = 0;

    for(int i = 0; i < AtomicPhase::MAX_THREADS; ++i)
    {
        prev_phase_thread_state[i] = 0;
        prev_phase_cpu_index[i] = 0;
    }

    thread_alloc_id = 0;

    app_start_time = 0;

    use_fixed_intervals = false;
    phase_duration = 50;

    phase_init_settings();
    perf_init();

    // Must be the last statement in this function. This should
    // fence the execution of the memory operations above.
    prev_phase_time.store(curr_time);
}

void phase_init_settings()
{
    char csvname[128];
    sprintf(csvname, "sync_jvmti.%ld.csp", (long) getpid());

    csv_stream = fopen(csvname, "w");
    if(!csv_stream)
    {
        perror("sync_jvmti: Failed to open output CSV file");
    }
    else
    {
        fprintf(stderr, "sync_jvmti: Printing to CSV file %s\n", csvname);
        fprintf(csv_stream, "Elapsed Time (ms),CSP (%%),Num Threads,Thread State,Thread CPU,CPU Cycles,CPU Instructions,CPU Cache Miss,CPU Branch Instructions,CPU Branch Misses,SW CPU Migrations,SW Context Switches\n");
    }

    if(auto s = std::getenv("JINN_PHASE_INTERVAL"))
    {
        sscanf(s, "%" PRIu64, &phase_duration);
        fprintf(stderr, "sync_jvmti: Phase interval set to %" PRIu64 "ms\n",
                phase_duration);
    }

    if(auto s = std::getenv("JINN_PHASE_FIXED"))
    {
	if(!strcmp(s, "false") || !strcmp(s, "0"))
	{
	    use_fixed_intervals = false;
	}
	else if(!strcmp(s, "true") || !strcmp(s, "1"))
	{
	    use_fixed_intervals = true;
	    fprintf(stderr, "sync_jvmti: Using fixed sampling intervals.\n");
	}
	else
	{
            fprintf(stderr, "sync_jvmti: Unrecognized JINN_PHASE_FIXED: %s\n",
                    s);
	}
    }

    if(auto s = std::getenv("JINN_SCHED_POLICY"))
    {
        if(!strcmp(s, "SimpleSM"))
            ;//st.reset(new SimpleSM());
        else
            fprintf(stderr, "sync_jvmti: Unrecognized scheduling policy: %s\n",
                    s);
    }
}

void phase_shutdown()
{
    perf_shutdown();

    if(csv_stream)
    {
        fclose(csv_stream);
        csv_stream = 0;
    }
}

void phase_vm_init()
{
    app_start_time = get_time();

    if(use_fixed_intervals)
    {
	profiler_thread_kill.store(false);
	profiler_thread = std::thread([] {
	    const auto sleep_time = std::chrono::milliseconds(phase_duration);
	    while(!profiler_thread_kill.load(memory_order_relaxed))
	    {
		std::this_thread::sleep_for(sleep_time);
		phase_checkpoint(get_time());
	    }
	});
    }
}

void phase_vm_die()
{
    if(use_fixed_intervals)
    {
	profiler_thread_kill.store(true);
	profiler_thread.join();
    }

    phase_checkpoint_run(get_time());
    fprintf(stderr, "sync_jvmti: total cs time: %" PRIu64 "ms\n", to_millis(total_cs_time));
    fprintf(stderr, "sync_jvmti: total wait time: %" PRIu64 "ms\n", to_millis(total_wait_time));
    fprintf(stderr, "sync_jvmti: total park time: %" PRIu64 "ms\n", to_millis(total_park_time));
}

int phase_alloc_thread()
{
    int id = ++thread_alloc_id;
    assert(id < AtomicPhase::MAX_THREADS);
    return id;
}

void AtomicPhase::record_cpu(int thread_id)
{
    this->phase_cpu_change[thread_id].store(
            1 + sched_getcpu(),
            memory_order_relaxed);
}

void phase_checkpoint(uint64_t curr_time)
{
    const auto prev_phase_time = ::prev_phase_time.load(memory_order_relaxed);
    if(to_millis(curr_time - prev_phase_time) < phase_duration)
        return;

    phase_checkpoint_run(curr_time);
}

auto get_phase() -> AtomicPhasePtr
{
    // This is a multiple-reader lock, so this almost never contends for us.
    std::shared_lock<std::shared_timed_mutex> lock(phase_mutex);

    const auto phase_raw_ptr = &phase_buffer[phase_buf_index];
    phase_raw_ptr->refcount.fetch_add(1, memory_order_relaxed);

    return AtomicPhasePtr(phase_raw_ptr);
}

void phase_checkpoint_run(uint64_t curr_time)
{
    // For specific details on the checkpoint mechanism please read the
    // long comment at the top of this source file.

    // No two threads can run the checkpoint at once.
    static std::atomic_flag checkpoint_running = ATOMIC_FLAG_INIT;
    if(checkpoint_running.test_and_set(memory_order_acquire))
        return;

    // Delay the checkpoint for now if the VM has not started.
    if(!app_start_time)
    {
        checkpoint_running.clear(memory_order_release);
        return;
    }

    // Swap the phase indices and get the pointer to the current phase.
    const auto phase_ptr = [] {
        std::unique_lock<std::shared_timed_mutex> lock(phase_mutex);

        const auto current_phase_id = ::phase_buf_index;
        const auto next_phase_id = !::phase_buf_index;

        ::phase_buf_index = next_phase_id;
        return &phase_buffer[current_phase_id];
    }();

    // Busy-wait until there are no more mutators of the current phase.
    //
    // The number of mutators of the current phase are (at this time frame)
    // monotonically decreasing because any new mutator is acquiring the next
    // phase pointer (as we swaped phases above).
    //
    // Therefore, this loop will break at some point. Since mutators perform
    // very brief operations, this should happen quickly and we do not wish
    // to relinquish to the operating system scheduler.
    while(phase_ptr->refcount.load(memory_order_acquire) != 0)
    {
        cpu_relax();
    }

    // Great. Now we are the exclusive owner of the current phase data. We are
    // safe to read and manipulate it with no data races.
    phase_checkpoint_safe(phase_ptr, curr_time);

    // Clear the phase data so it can be used for another phase.
    phase_ptr->reset();

    // Update the last time we ran the checkpoint.
    prev_phase_time.store(curr_time, memory_order_relaxed);

    // Release the checkpoint atomic lock.
    checkpoint_running.clear(memory_order_release);
}

void phase_checkpoint_safe(AtomicPhase* phase_ptr, uint64_t curr_time)
{
    // This is a point where we are safe to manipulate the data in `phase_ptr`
    // since we have its exclusive ownership. Do note this is not a Java
    // Safepoint, there are still Java threads running concurrently with this.

    // Load phase_ptr stats into local variables for easy access.
    const auto phase_thread_change_count = (
            phase_ptr->phase_thread_change_count.load(memory_order_relaxed));
    const auto phase_cs_time = (
            phase_ptr->phase_cs_time.load(memory_order_relaxed));
    const auto phase_wait_time = (
            phase_ptr->phase_wait_time.load(memory_order_relaxed));
    const auto phase_park_time = (
            phase_ptr->phase_park_time.load(memory_order_relaxed));

    const int max_threads = thread_alloc_id;

    // This will tell us how many threads are running at the present moment.
    const auto curr_thread_count = (
            prev_phase_thread_count + phase_thread_change_count);

    // This is the number of threads we use when multiplying the accumulated
    // runing time of the applicaton. It should be at least one, and is the
    // maximum between the number of threads running on the previous phase
    // and the number of threads running now. Should be a good average for
    // the amount of threads ran during the phase time frame.
    const auto thread_factor = std::max(1, std::max(prev_phase_thread_count,
                                                    curr_thread_count));

    // The accumulated running time of the application during this phase.
    // It is the time spent on threads minus time spent waiting for work.
    const auto phase_accum_time = (((curr_time - prev_phase_time) * thread_factor)
                                    - phase_wait_time - phase_park_time);

    // The critical section pressure of the phase is the ratio between
    // time spent waiting for a lock and the accumulated running time.
    const auto csp = (phase_cs_time / (double)phase_accum_time) * 100;

    const auto elapsed_time = to_millis(curr_time - app_start_time);
    const double bounded_csp = std::min(100.0, csp);

    // Update unshared global variables.
    ::total_cs_time += phase_cs_time;
    ::total_park_time += phase_park_time;
    ::total_wait_time += phase_wait_time;
    ::prev_phase_thread_count = curr_thread_count;

    // Update variables related to individual threads.
    for(int i = 0; i < AtomicPhase::MAX_THREADS; ++i)
    {
        const auto thread_state = (
                phase_ptr->phase_thread_state_change[i].load(memory_order_relaxed));
        const auto cpu_number = (
                phase_ptr->phase_cpu_change[i].load(memory_order_relaxed));

        if(thread_state)
            prev_phase_thread_state[i] = thread_state - 1;

        if(cpu_number)
            prev_phase_cpu_index[i] = cpu_number - 1;
    }

    // Print the profiled values into the CSV.
    if(csv_stream)
    {
	static constexpr int MAX_CPUS = 16;

        const auto nprocs = perf_nprocs();

        assert(nprocs < MAX_CPUS);

        PerfHardwareData hw_data[MAX_CPUS];
	PerfSoftwareData sw_data;

        for(int cpu = 0; cpu < nprocs; ++cpu)
            hw_data[cpu] = perf_consume_hw(cpu);

	sw_data = perf_consume_sw();

        char buffer_cpu_cycles[24 * MAX_CPUS];
        char buffer_instructions[24 * MAX_CPUS];
        char buffer_cache_miss[24 * MAX_CPUS];
        char buffer_branch_inst[24 * MAX_CPUS];
        char buffer_branch_miss[24 * MAX_CPUS];
        size_t size_cpu_cycles = 0;
        size_t size_instructions = 0;
        size_t size_cache_miss = 0;
        size_t size_branch_inst = 0;
        size_t size_branch_miss = 0;

        for(int cpu = 0; cpu < nprocs; ++cpu)
        {
            size_cpu_cycles += sprintf(&buffer_cpu_cycles[size_cpu_cycles],
                                       "%" PRIu64 ":", hw_data[cpu].cpu_cycles);

            size_instructions += sprintf(&buffer_instructions[size_instructions],
                                       "%" PRIu64 ":", hw_data[cpu].instructions);

            size_cache_miss += sprintf(&buffer_cache_miss[size_cache_miss],
                                       "%" PRIu64 ":", hw_data[cpu].cache_misses);

            size_branch_inst += sprintf(&buffer_branch_inst[size_branch_inst],
                                       "%" PRIu64 ":", hw_data[cpu].branch_instructions);

            size_branch_miss += sprintf(&buffer_branch_miss[size_branch_miss],
                                       "%" PRIu64 ":", hw_data[cpu].branch_misses);
        }

        // Remove trailing colon.
        buffer_cpu_cycles[--size_cpu_cycles] = 0;
        buffer_instructions[--size_instructions] = 0;
        buffer_cache_miss[--size_cache_miss] = 0;
        buffer_branch_inst[--size_branch_inst] = 0;
        buffer_branch_miss[--size_branch_miss] = 0;

        char buffer_thread_state[16 + AtomicPhase::MAX_THREADS];
        for(int i = 1; i <= max_threads; ++i)
        {
            assert(prev_phase_thread_state[i] >= 0 
                    && prev_phase_thread_state[i] <= 9);
            buffer_thread_state[i-1] = ('0' + prev_phase_thread_state[i]);
        }

        if(max_threads == 0)
            strcpy(buffer_thread_state, "0");
        else
            buffer_thread_state[max_threads] = 0;

        char buffer_thread_cpus[4 * AtomicPhase::MAX_THREADS];
        size_t size_thread_cpus = 0;
        for(int i = 1; i <= max_threads; ++i)
        {
            size_thread_cpus += sprintf(&buffer_thread_cpus[size_thread_cpus],
                                        "%" PRIu8 ":", prev_phase_cpu_index[i]);
        }
        if(size_thread_cpus == 0)
            strcpy(buffer_thread_cpus, "0");
        else
            buffer_thread_cpus[--size_thread_cpus] = 0;

        fprintf(csv_stream, "%lld,%.2f,%d,%s,%s,%s,%s,%s,%s,%s,%llu,%llu\n", 
                (long long) elapsed_time,
                bounded_csp,
                (int) thread_factor,
                buffer_thread_state, buffer_thread_cpus,
                buffer_cpu_cycles, buffer_instructions,
                buffer_cache_miss, buffer_branch_inst,
		buffer_branch_miss, sw_data.cpu_migrations,
		sw_data.context_switches);
    }
}
