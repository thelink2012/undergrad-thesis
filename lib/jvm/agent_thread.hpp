#pragma once
#include "jni_ref.hpp"
#include <jvmti.h>

namespace jvmtiprof
{
/// A JVMTI Agent Thread as specified in [RunAgentThread][1].
///
/// Threads begin execution once `start()` is called. If this object owns a
/// thread, the owner of this object must call `join()` before destruction.
///
/// The thread execution happens in the abstract method `run()`.
///
/// [1]:
/// https://docs.oracle.com/en/java/javase/11/docs/specs/jvmti.html#RunAgentThread
class JvmtiAgentThread
{
public:
    /// Constructs an thread wrapper for the given environment.
    explicit JvmtiAgentThread(jvmtiEnv& jvmti_env) :
        JvmtiAgentThread(jvmti_env, nullptr, JVMTI_THREAD_NORM_PRIORITY)
    {}

    /// Constructs a thread wrapper with the given priority.
    JvmtiAgentThread(jvmtiEnv& jvmti_env, jint priority) :
        JvmtiAgentThread(jvmti_env, nullptr, priority)
    {}

    /// Constructs a thread wrapper with the given name and priority.
    JvmtiAgentThread(jvmtiEnv& jvmti_env, const char* name, jint priority) :
        m_jvmti_env(&jvmti_env), m_name(name), m_priority(priority)
    {}

    JvmtiAgentThread(const JvmtiAgentThread&) = delete;
    JvmtiAgentThread& operator=(const JvmtiAgentThread&) = delete;

    JvmtiAgentThread(JvmtiAgentThread&& rhs) noexcept = delete;
    JvmtiAgentThread& operator=(JvmtiAgentThread&& rhs) noexcept = delete;

    /// Terminates the program if the thread hasn't been joined.
    virtual ~JvmtiAgentThread();

    /// Sets the name of the wrapped thread.
    ///
    /// The thread must not have started.
    void set_name(const char* name);

    /// Constructs and starts a `java.lang.Thread` through `RunAgentThread`.
    ///
    /// Behaviour is undefined if called in any other VM phase other than the
    /// live phase.
    virtual void start(JNIEnv& jni_env);

    /// Blocks the current thread until the wrapped thread finishes execution.
    ///
    /// After execution finishes, the `java.lang.Thread` is dettached from this
    /// wrapper object.
    ///
    /// Behaviour is undefined if called in any other VM phase other than the
    /// live phase.
    void join(JNIEnv& jni_env);

    /// Checks whether there's any thread wrapped in this.
    bool joinable() const { return m_thread_obj != nullptr; }

    /// This method should be overriden as the thread execution body.
    virtual void run() = 0;

protected:
    /// Yields the CPU to the operating-system scheduler as if by calling
    /// `java.lang.Thread.yield`.
    static void yield();

    /// Returns the JVMTI environment associated with this wrapper.
    auto jvmti_env() -> jvmtiEnv& { return *m_jvmti_env; }

    /// Detaches the `java.lang.Thread` from this wrapper.
    ///
    /// This is invoked at the end of `join()`.
    virtual void detach(JNIEnv& jni_env);

private:
    JNIGlobalRef<jthread> m_thread_obj;
    jvmtiEnv* m_jvmti_env;
    const char* m_name{};
    jint m_priority{JVMTI_THREAD_NORM_PRIORITY};
};
}
