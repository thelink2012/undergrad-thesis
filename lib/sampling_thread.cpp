#include "sampling_thread.hpp"
#include "env.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <exception>
#include <tuple>

namespace
{
/// Converts from nanoseconds to milliseconds.
///
/// Returns a pair containing the milliseconds value and the remaining
/// nanoseconds lost during conversion.
constexpr auto nanos_to_millis(jlong nanos_time) -> std::pair<jlong, jlong>
{
    constexpr jlong factor = 1000000;
    const jlong millis = nanos_time / factor;
    const jlong rem_nanos = nanos_time % factor;
    return {millis, rem_nanos};
}
}

namespace jvmtiprof
{
SamplingThread::SamplingThread(jvmtiEnv& jvmti_env,
                               JvmtiProfEnv& jvmtiprof_env) :
    JvmtiAgentThread(jvmti_env, JVMTI_THREAD_MAX_PRIORITY),
    m_jvmtiprof_env(&jvmtiprof_env)
{
    // Be really careful with the usage of ` jvmtiprof_env` here since
    // it may be barely constructed. Do not assume any of its invariants.
    // We should probably only take its pointer value for names.

    char temp_name_buffer[256];

    name_for_thread(temp_name_buffer, sizeof(temp_name_buffer), jvmtiprof_env);
    set_name(temp_name_buffer);

    name_for_monitor(temp_name_buffer, sizeof(temp_name_buffer), jvmtiprof_env);
    m_should_stop_monitor = JvmtiMonitor(jvmti_env, temp_name_buffer);
}

SamplingThread::~SamplingThread()
{
}

void SamplingThread::set_sampling_interval(jlong nanos_interval)
{
    assert(nanos_interval >= 0);

    // The ordering of this store may be relaxed since the interval value is
    // independent from any other operation.
    m_interval_millis.store(nanos_to_millis(nanos_interval).first,
                            std::memory_order_relaxed);
}

void SamplingThread::start(JNIEnv& jni_env)
{
    assert(!joinable());

    // The ordering of this store is relaxed since starting a thread is a fence.
    m_should_stop_flag.store(false, std::memory_order_relaxed);

    JvmtiAgentThread::start(jni_env);
}

void SamplingThread::stop_and_join(JNIEnv& jni_env)
{
    assert(joinable());

    // The ordering of this store is relaxed since leaving a synchronized block
    // is a memory fence.
    m_should_stop_flag.store(true, std::memory_order_relaxed);

    {
        JvmtiMonitorGuard guard(jvmti_env(), m_should_stop_monitor);
        m_should_stop_monitor.notify_all(jvmti_env());
    }

    join(jni_env);
}

void SamplingThread::detach(JNIEnv& jni_env)
{
    assert(m_should_stop_monitor);

    m_should_stop_monitor.lock(jvmti_env());
    m_should_stop_monitor.reset(jvmti_env());

    JvmtiAgentThread::detach(jni_env);
}

void SamplingThread::run()
{
    while(!m_should_stop_flag.load(std::memory_order_acquire))
    {
        m_jvmtiprof_env->post_application_state_sample();
        wait_sampling_interval();
    }
}

void SamplingThread::wait_sampling_interval()
{
    // Monitors may wakeup spuriously as such we have to manually keep track of
    // how many milliseconds we still have to wait.
    jlong timeout = m_interval_millis.load(std::memory_order_relaxed);

    if(timeout <= 0)
        return yield();

    while(timeout > 0 && !m_should_stop_flag.load(std::memory_order_acquire))
    {
        jlong nanos_before_wait, nanos_after_wait;
        jvmtiError jvmti_err;

        jvmti_err = jvmti_env().GetTime(&nanos_before_wait);
        assert(jvmti_err == JVMTI_ERROR_NONE);

        {
            JvmtiMonitorGuard guard(jvmti_env(), m_should_stop_monitor);
            m_should_stop_monitor.wait_for(jvmti_env(), timeout);
        }

        jvmti_err = jvmti_env().GetTime(&nanos_after_wait);
        assert(jvmti_err == JVMTI_ERROR_NONE);

        jlong elapsed_millis;
        std::tie(elapsed_millis, std::ignore) = nanos_to_millis(
                nanos_after_wait - nanos_before_wait);

        // Reduce the timeout by at least one to guarantee progress.
        timeout -= std::max(jlong(1), elapsed_millis);
    }
}

void SamplingThread::name_for_monitor(char* buffer, size_t buffer_size,
                                      const JvmtiProfEnv& jvmtiprof_env)
{
    const auto result_size = snprintf(buffer, buffer_size,
                                      "jvmtiprof[%p] sampling thread monitor",
                                      static_cast<const void*>(&jvmtiprof_env));
    assert(result_size >= 0);
}

void SamplingThread::name_for_thread(char* buffer, size_t buffer_size,
                                     const JvmtiProfEnv& jvmtiprof_env)
{
    const auto result_size = snprintf(buffer, buffer_size,
                                      "jvmtiprof[%p] sampling thread",
                                      static_cast<const void*>(&jvmtiprof_env));
    assert(result_size >= 0);
}
}

// TODO(thelink2012): Should the sampling thread have maximum priority?
// TODO(thelink2012): What should be the default sampling interval?
