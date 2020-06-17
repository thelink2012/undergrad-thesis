#pragma once
#include <cassert>
#include <exception> // for std::terminate
#include <jvmti.h>

namespace jvmtiprof
{
/// JVMTI Raw Monitors as specified in [Raw Monitors][1].
///
/// In order to reduce redundancies in data storage (i.e. monitor owners
/// usually knows its jvmti environment) the environment must be passed
/// to every method operating on the monitor state.
///
/// [1]:
/// https://docs.oracle.com/en/java/javase/11/docs/specs/jvmti.html#RawMonitors
class JvmtiMonitor
{
public:
    /// Constructs an wrapper with no monitor.
    JvmtiMonitor() = default;

    /// Creates a monitor in the given environment and wraps it in this object.
    JvmtiMonitor(jvmtiEnv& jvmti_env, const char* name);

    JvmtiMonitor(const JvmtiMonitor&) = delete;
    JvmtiMonitor& operator=(const JvmtiMonitor&) = delete;

    JvmtiMonitor(JvmtiMonitor&&) noexcept;
    JvmtiMonitor& operator=(JvmtiMonitor&&) noexcept;

    /// Terminates execution if there's still any raw monitor wrapped in this.
    ~JvmtiMonitor();

    /// Gain exclusive ownership of the raw monitor or block the current thread
    /// until that is possible.
    ///
    /// Behaves as if calling `RawMonitorEnter`.
    void lock(jvmtiEnv& jvmti_env);

    /// Releases exclusive ownership of the raw monitor.
    ///
    /// Behaves as if calling `RawMonitorExit`.
    ///
    /// Terminates execution if the monitor is not owned by the current thread.
    void unlock(jvmtiEnv& jvmti_env);

    /// Waits for a notification in the raw monitor.
    ///
    /// Behaves as if calling `RawMonitorWait(.., 0)`.
    ///
    /// Terminates execution if the monitor is not owned by the current thread.
    void wait(jvmtiEnv& jvmti_env);

    /// Waits for a notification in the raw monitor or the specified timeout to
    /// be elapsed.
    ///
    /// Behaves as if calling `RawMonitorWait`.
    ///
    /// Terminates execution if the monitor is not owned by the current thread.
    void wait_for(jvmtiEnv& jvmti_env, jlong millis);

    /// Notify a single thread waiting on the raw monitor.
    ///
    /// Behaves as if calling `RawMonitorNotify`.
    ///
    /// Terminates execution if the monitor is not owned by the current thread.
    void notify_one(jvmtiEnv& jvmti_env);

    /// Notify all threads waiting on the raw monitor.
    ///
    /// Behaves as if calling `RawMonitorNotifyAll`.
    ///
    /// Terminates execution if the monitor is not owned by the current thread.
    void notify_all(jvmtiEnv& jvmti_env);

    /// Destroys the wrapped monitor and replaces it with the given one.
    ///
    /// Behaves as if calling `DestroyRawMonitor` on the currently wrapped
    /// monitor.
    ///
    /// Terminates execution if the monitor is not owned by the current thread.
    void reset(jvmtiEnv& jvmti_env, jrawMonitorID new_value = nullptr);

    /// Unwraps and returns the raw monitor from this wrapper.
    auto release() noexcept -> jrawMonitorID;

    /// Checks whether there's an wrapped monitor.
    explicit operator bool() const noexcept;

private:
    jrawMonitorID m_raw_monitor{};
};

/// Provides RAII ownership of a monitor's lock.
///
/// Similar to `std::lock_guard` but compatible with `JvmtiMonitor` interface.
class JvmtiMonitorGuard
{
public:
    JvmtiMonitorGuard(jvmtiEnv& jvmti_env, JvmtiMonitor& monitor);

    JvmtiMonitorGuard(const JvmtiMonitorGuard&) = delete;
    JvmtiMonitorGuard& operator=(const JvmtiMonitorGuard&) = delete;

    JvmtiMonitorGuard(JvmtiMonitorGuard&&) = delete;
    JvmtiMonitorGuard& operator=(JvmtiMonitorGuard&&) = delete;

    ~JvmtiMonitorGuard();

private:
    jvmtiEnv* m_jvmti_env;
    JvmtiMonitor* m_monitor;
};

inline JvmtiMonitor::JvmtiMonitor(jvmtiEnv& jvmti_env, const char* name)
{
    assert(name != nullptr);
    jvmtiError jvmti_err = jvmti_env.CreateRawMonitor(name, &m_raw_monitor);
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

inline JvmtiMonitor::JvmtiMonitor(JvmtiMonitor&& rhs) noexcept :
    m_raw_monitor(rhs.m_raw_monitor)
{
    rhs.m_raw_monitor = nullptr;
}

inline JvmtiMonitor& JvmtiMonitor::operator=(JvmtiMonitor&& rhs) noexcept
{
    m_raw_monitor = rhs.m_raw_monitor;
    rhs.m_raw_monitor = nullptr;
    return *this;
}

inline JvmtiMonitor::~JvmtiMonitor()
{
    if(m_raw_monitor != nullptr)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
}

inline void JvmtiMonitor::lock(jvmtiEnv& jvmti_env)
{
    assert(m_raw_monitor != nullptr);
    jvmtiError jvmti_err = jvmti_env.RawMonitorEnter(m_raw_monitor);
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

inline void JvmtiMonitor::unlock(jvmtiEnv& jvmti_env)
{
    assert(m_raw_monitor != nullptr);
    jvmtiError jvmti_err = jvmti_env.RawMonitorExit(m_raw_monitor);
    if(jvmti_err == JVMTI_ERROR_NOT_MONITOR_OWNER)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

inline void JvmtiMonitor::wait(jvmtiEnv& jvmti_env)
{
    assert(m_raw_monitor != nullptr);
    jvmtiError jvmti_err = jvmti_env.RawMonitorWait(m_raw_monitor, 0);
    if(jvmti_err == JVMTI_ERROR_NOT_MONITOR_OWNER)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

inline void JvmtiMonitor::wait_for(jvmtiEnv& jvmti_env, jlong millis)
{
    assert(m_raw_monitor != nullptr);
    assert(millis > 0); // otherwise would wait until notified
    jvmtiError jvmti_err = jvmti_env.RawMonitorWait(m_raw_monitor, millis);
    if(jvmti_err == JVMTI_ERROR_NOT_MONITOR_OWNER)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

inline void JvmtiMonitor::notify_one(jvmtiEnv& jvmti_env)
{
    assert(m_raw_monitor != nullptr);
    jvmtiError jvmti_err = jvmti_env.RawMonitorNotify(m_raw_monitor);
    if(jvmti_err == JVMTI_ERROR_NOT_MONITOR_OWNER)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

inline void JvmtiMonitor::notify_all(jvmtiEnv& jvmti_env)
{
    assert(m_raw_monitor != nullptr);
    jvmtiError jvmti_err = jvmti_env.RawMonitorNotifyAll(m_raw_monitor);
    if(jvmti_err == JVMTI_ERROR_NOT_MONITOR_OWNER)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

inline void JvmtiMonitor::reset(jvmtiEnv& jvmti_env, jrawMonitorID new_value)
{
    if(m_raw_monitor != nullptr)
    {
        jvmtiError jvmti_err = jvmti_env.DestroyRawMonitor(m_raw_monitor);
        if(jvmti_err == JVMTI_ERROR_NOT_MONITOR_OWNER)
        {
            // TODO(thelink2012): log error
            std::terminate();
        }
        assert(jvmti_err == JVMTI_ERROR_NONE);
    }

    m_raw_monitor = new_value;
}

inline auto JvmtiMonitor::release() noexcept -> jrawMonitorID
{
    const auto old_raw_monitor = m_raw_monitor;
    m_raw_monitor = nullptr;
    return old_raw_monitor;
}

inline JvmtiMonitor::operator bool() const noexcept
{
    return m_raw_monitor != nullptr;
}

inline JvmtiMonitorGuard::JvmtiMonitorGuard(jvmtiEnv& jvmti_env,
                                            JvmtiMonitor& monitor) :
    m_jvmti_env(&jvmti_env), m_monitor(&monitor)
{
    monitor.lock(jvmti_env);
}

inline JvmtiMonitorGuard::~JvmtiMonitorGuard()
{
    m_monitor->unlock(*m_jvmti_env);
}
}
