#pragma once
#include <atomic>

/// This structure holds information that is concurrently mutated during
/// a phase.
//
// Please only use commutative operations to update this structure.
// See the top comment of `phase.cpp` for details.
struct AtomicPhase
{
    static constexpr int MAX_THREADS = 300;

    static constexpr uint8_t THREAD_STATE_UNCHANGED = 0;
    static constexpr uint8_t THREAD_STATE_RUNNING = 1;
    static constexpr uint8_t THREAD_STATE_CONTENDED = 2;
    static constexpr uint8_t THREAD_STATE_WAITING = 3;
    static constexpr uint8_t THREAD_STATE_PARKING = 4;
    static constexpr uint8_t THREAD_STATE_DIED = 10;

    /// The amount of time contended in a monitor.
    std::atomic<uint64_t> phase_cs_time {0};

    /// The amount of time waiting on monitor (e.g. obj.wait()).
    std::atomic<uint64_t> phase_wait_time {0};

    /// The amount of time parked.
    std::atomic<uint64_t> phase_park_time {0};

    /// The amount of threads (spawned - died) during this phase.
    std::atomic<int32_t> phase_thread_change_count {0};

    /// The amount of mutators still mutating this phase.
    std::atomic<int32_t> refcount {0};

    /// Records changes on the state of the thread.
    ///
    /// A value of 0 implies no change on the thread state happened.
    /// A value of 1 implies the thread went into a running state.
    /// A value of 2 implies the thread became contended.
    /// A value of 3 implies the thread went into a wait state.
    /// A value of 4 implies the thread parked.
    /// A value of 10 implies the thread died.
    ///
    /// Also see the `THREAD_STATE_*` constants.
    std::atomic<uint8_t> phase_thread_state_change[MAX_THREADS];

    /// Records changes on the CPU on which the thread is running.
    ///
    /// A value of zero implies no change, a value of `cpu+1` implies the
    /// thread was reescheduled to `cpu`.
    ///
    /// Take these values with a grain of salt. It is an approximation.
    /// The events are only recorded before/after events of contention,
    /// waiting and parking.
    std::atomic<uint8_t> phase_cpu_change[MAX_THREADS];

    /// Records the CPU the calling thread is scheduled into and records it
    /// in the `phase_cpu_change[thread_id]` array.
    void record_cpu(int thread_id);

    /// Resets the state of the phase.
    void reset()
    {
        phase_cs_time.store(0, std::memory_order_relaxed);
        phase_wait_time.store(0, std::memory_order_relaxed);
        phase_park_time.store(0, std::memory_order_relaxed);
        phase_thread_change_count.store(0, std::memory_order_relaxed);

        for(int i = 0; i < MAX_THREADS; ++i)
        {
            phase_thread_state_change[i].store(0, std::memory_order_relaxed);
            phase_cpu_change[i].store(0, std::memory_order_relaxed);
        }

        // This should commit the above stores for the next acquirer.
        refcount.store(0, std::memory_order_release);
    }
};

/// Provides ownership semantics to a phase.
///
/// That is, while alive, you have ownership of a phase. Once destructed,
/// it decrements the reference counter of the owned phase.
class AtomicPhasePtr
{
public:
    AtomicPhasePtr(const AtomicPhasePtr&) = delete;

    AtomicPhasePtr(AtomicPhasePtr&& rhs)
    {
        this->phase = rhs.phase;
        rhs.phase = nullptr;
    }

    ~AtomicPhasePtr()
    {
        if(phase != nullptr)
            phase->refcount.fetch_sub(1, std::memory_order_release);
    }

    AtomicPhasePtr& operator=(const AtomicPhasePtr&) = delete;
    AtomicPhasePtr& operator=(AtomicPhasePtr&&) = delete;

    AtomicPhase* operator->() { return phase; }

protected:
    // The caller is responsible for incrementing refcount.
    explicit AtomicPhasePtr(AtomicPhase* phase) :
        phase(phase)
    {}

    friend AtomicPhasePtr get_phase();

private:
    AtomicPhase* phase;
};

/// Initialises the phases subsystem.
extern void phase_init();

/// Initialises the rest of the phase subsystem when the VM starts.
extern void phase_vm_init();

/// Shutdowns part of the phase subsystem when the VM dies.
extern void phase_vm_die();

/// Shutdowns the phase subsystem.
extern void phase_shutdown();

/// Tries to reach a phase checkpoint.
///
/// A thread shall never call this while having ownership of a
/// reference to a phase or the thread will deadlock.
extern void phase_checkpoint(uint64_t curr_time);

/// Gets an owned reference to the current phase.
extern auto get_phase() -> AtomicPhasePtr;

/// Allocates a new unique thread id. The ids start from 1.
extern int phase_alloc_thread();
