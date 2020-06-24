#pragma once
#include <ctime>
#include <jvmtiprof/jvmtiprof.h>

namespace jvmtiprof
{
class JvmtiProfEnv;

/// Posts an event every interval of CPU-time in the execution of the
/// application.
///
/// This is implemented through itimer and signals, therefore the thread that
/// handles the event is choosen randomly. Furthermore, the code that handles
/// the event must be async-signal safe.
class ExecutionSampler
{
public:
    /// Adds the capability to post a event every interval of CPU-time.
    ///
    /// Behaviour is undefined if an instance of `ExecutionSampler` exists
    /// but its capability isn't added.
    ///
    /// This may be called multiple times, but for each call a corresponding
    /// `relinquish_capability` must be made.
    ///
    /// Returns false in case it's not possible to add such a capability.
    static auto add_capability() -> bool;

    /// Relinquishes the capability added by `add_capability`.
    static void relinquish_capability();

    /// Checks whether any call to `add_capability` hasn't yet received a
    /// corresponding `relinquish_capability`.
    static auto has_capability() -> bool;

    /// Converts an opaque pointer (coming from a signal) to its corresponding
    /// `ExecutionSampler` instance, or `nullptr` if the pointer is unrelated
    /// to such instance.
    static auto from_opaque_pointer(void* ptr) -> ExecutionSampler*;

    /// Constructs a sampler that posts a event every interval of CPU-time into
    /// `jvmtiprof_env`.
    explicit ExecutionSampler(JvmtiProfEnv& jvmtiprof_env);

    ExecutionSampler(const ExecutionSampler&) = delete;
    ExecutionSampler& operator=(const ExecutionSampler&) = delete;

    ExecutionSampler(ExecutionSampler&&) = delete;
    ExecutionSampler& operator=(ExecutionSampler&&) = delete;

    ~ExecutionSampler();

    /// Sets the interval (in nanoseconds of CPU-time) between sample
    /// deliveries.
    ///
    /// The given CPU-time is distributed across every thread. So, for instance,
    /// two threads executing in parallel in two cores for 5ms would trigger
    /// a event if this interval is set to 10ms.
    void set_sampling_interval(jlong nanos_interval);

    /// Starts the delivery of samples.
    void start();

    /// Stops the delivery of samples.
    void stop();

    /// Posts a sample to its attached `JvmtiProfEnv`.
    void post_execution_sample();

private:
    /// Restarts the sample delivery timer.
    void restart();

private:
    JvmtiProfEnv* m_jvmtiprof_env;
    timer_t m_timer_id;
    jlong m_interval_nanos{50000000}; // 50ms
    bool m_has_started{false};
};
}
