#pragma once
#include "jvmti_agent_thread.hpp"
#include <atomic>

namespace jvmtiprof
{
class JvmtiProfEnv;

/// Thread used to deliver sampling events to `JvmtiProfEnv`.
class SamplingThread final : public JvmtiAgentThread
{
public:
    /// Constructs a sampling thread on top of the given environments.
    ///
    /// The `jvmtiprof_env` may be barely constructed. Only its
    /// pointer value is taken during this constructor.
    SamplingThread(jvmtiEnv& jvmti_env, JvmtiProfEnv& jvmtiprof_env);

    SamplingThread(const SamplingThread&) = delete;
    SamplingThread& operator=(const SamplingThread&) = delete;

    SamplingThread(SamplingThread&&) noexcept = delete;
    SamplingThread& operator=(SamplingThread&&) noexcept = delete;

    ~SamplingThread();

    /// Sets the interval (in nanoseconds) between sample deliveries.
    void set_sampling_interval(jlong nanos_interval);

    /// Asks the sampling thread to stop and block the current thread until
    /// the sampling one has stopped.
    void stop_and_join(JNIEnv* jni_env);

    /// Starts the sampling thread.
    void start(JNIEnv* jni_env) override;

    /// Executes the sampling thread logic in the current thread.
    void run() override;

protected:
    /// Releases resources held by the sampling thread.
    void detach(JNIEnv* jni_env) override;

private:
    /// Returns a name for the sampling thread monitor into `buffer`.
    ///
    /// It is guaranteed the only operation done on `jvmtiprof_env` is taking
    /// its address, since this may be invoked while its constructor is still
    /// running.
    static void name_for_monitor(char* buffer, size_t buffer_size,
                                 const JvmtiProfEnv& jvmtiprof_env);

    /// Returns a name for the sampling thread into `buffer`.
    ///
    /// Behaves much like `name_for_monitor`, see it for more details.
    static void name_for_thread(char* buffer, size_t buffer_size,
                                const JvmtiProfEnv& jvmtiprof_env);

    /// Waits until the sampling interval has passed or the thread has
    /// been asked to exit.
    void wait_sampling_interval();

private:
    JvmtiProfEnv* m_jvmtiprof_env;
    jrawMonitorID m_should_stop_monitor{};
    std::atomic<jlong> m_interval_millis{10};
    std::atomic<jboolean> m_should_stop_flag{false};
};
}
