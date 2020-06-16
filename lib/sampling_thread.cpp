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
    jvmtiError jvmti_err = jvmti_env.CreateRawMonitor(temp_name_buffer,
                                                      &m_should_stop_monitor);
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

SamplingThread::~SamplingThread()
{
    if(m_should_stop_monitor != nullptr)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
}

void SamplingThread::set_sampling_interval(jlong nanos_interval)
{
    assert(nanos_interval >= 0);

    // The ordering of this store may be relaxed since the interval value is
    // independent from any other operation.
    m_interval_millis.store(nanos_to_millis(nanos_interval).first,
                            std::memory_order_relaxed);
}

void SamplingThread::start(JNIEnv* jni_env)
{
    assert(!joinable());

    // The ordering of this store is relaxed since starting a thread is a fence.
    m_should_stop_flag.store(false, std::memory_order_relaxed);

    JvmtiAgentThread::start(jni_env);
}

void SamplingThread::stop_and_join(JNIEnv* jni_env)
{
    assert(joinable());

    // The ordering of this store is relaxed since leaving a synchronized block
    // is a memory fence.
    m_should_stop_flag.store(true, std::memory_order_relaxed);

    {
        jvmtiError jvmti_err;

        // TODO(thelink2012): RAII wrapper for RawMonitorEnter/Exit
        jvmti_err = jvmti_env().RawMonitorEnter(m_should_stop_monitor);
        assert(jvmti_err == JVMTI_ERROR_NONE);

        jvmti_err = jvmti_env().RawMonitorNotifyAll(m_should_stop_monitor);
        assert(jvmti_err == JVMTI_ERROR_NONE);

        jvmti_err = jvmti_env().RawMonitorExit(m_should_stop_monitor);
        assert(jvmti_err == JVMTI_ERROR_NONE);
    }

    join(jni_env);
}

void SamplingThread::detach(JNIEnv* jni_env)
{
    assert(m_should_stop_monitor != nullptr);

    // TODO(thelink2012): RAII wrapper except it release()s
    jvmtiError jvmti_err = jvmti_env().RawMonitorEnter(m_should_stop_monitor);
    assert(jvmti_err == JVMTI_ERROR_NONE);

    jvmti_err = jvmti_env().DestroyRawMonitor(m_should_stop_monitor);
    assert(jvmti_err == JVMTI_ERROR_NONE);
    m_should_stop_monitor = nullptr;

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
    // Monitors may wake spuriously as such we have to manually keep track of
    // how many milliseconds we still have to wait.
    jlong timeout = m_interval_millis.load(std::memory_order_relaxed);

    if(timeout <= 0)
        return yield();

    while(timeout > 0
          && !m_should_stop_flag.load(std::memory_order_acquire))
    {
        jlong nanos_before_wait, nanos_after_wait;
        jvmtiError jvmti_err;

        jvmti_err = jvmti_env().GetTime(&nanos_before_wait);
        assert(jvmti_err == JVMTI_ERROR_NONE);
        
        {
        // TODO(thelink2012): RAII wrapper for RawMonitorEnter/Exit
        jvmti_err = jvmti_env().RawMonitorEnter(m_should_stop_monitor);
        assert(jvmti_err == JVMTI_ERROR_NONE);

        jvmti_err = jvmti_env().RawMonitorWait(m_should_stop_monitor, timeout);
        assert(jvmti_err == JVMTI_ERROR_NONE);

        jvmti_err = jvmti_env().RawMonitorExit(m_should_stop_monitor);
        assert(jvmti_err == JVMTI_ERROR_NONE);
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
