#include "agent_thread.hpp"
#include "jni_ref.hpp"
#include <cassert>
#include <exception> // for std::terminate
#include <sched.h>   // for sched_yield
#include <utility>   // for std::exchange

namespace
{
using jvmtiprof::JNIGlobalRef;
using jvmtiprof::JNILocalRef;

/// Returns the jclass corresponding to `java.lang.Thread`.
auto find_java_lang_Thread(JNIEnv* jni_env) -> jclass
{
    jclass thread_class = jni_env->FindClass("java/lang/Thread");
    if(!thread_class)
    {
        // TODO(thelink2012): log failure
        std::terminate();
    }
    return thread_class;
}

/// Returns the jmethodID corresponding to `<init>` in `klass` with the argument
/// signature `sig`.
auto find_init_method(JNIEnv* jni_env, jclass klass, const char* sig = "()V")
        -> jmethodID
{
    jmethodID init_method_id = jni_env->GetMethodID(klass, "<init>", sig);
    if(!init_method_id)
    {
        // TODO(thelink2012): log failure
        std::terminate();
    }
    return init_method_id;
}

/// Returns the jmethodID corresponding to 'join()' in 'thread_class'.
auto find_join_method(JNIEnv* jni_env, jclass thread_class) -> jmethodID
{
    jmethodID join_method_id = jni_env->GetMethodID(thread_class, "join",
                                                    "()V");
    if(!join_method_id)
    {
        // TODO(thelink2012): log failure
        std::terminate();
    }
    return join_method_id;
}

/// Returns a new `java.lang.Thread`. Behaves as if doing `new
/// java.lang.Thread()` in Java.
auto new_thread_object(JNIEnv* jni_env) -> JNIGlobalRef<jthread>
{
    const jclass thread_class = find_java_lang_Thread(jni_env);
    const jmethodID init_method = find_init_method(jni_env, thread_class);

    JNILocalRef<jthread> thread_obj(
            *jni_env, jni_env->NewObject(thread_class, init_method));
    if(!thread_obj)
    {
        // TODO(thelink2012): log failure
        std::terminate();
    }

    return JNIGlobalRef<jthread>(*jni_env, thread_obj);
}

/// Returns a new `java.lang.Thread`. Behaves as if doing `new
/// java.lang.Thread(name)` in Java.
auto new_thread_object(JNIEnv* jni_env, const char* name)
        -> JNIGlobalRef<jthread>
{
    assert(name != nullptr);

    const jclass thread_class = find_java_lang_Thread(jni_env);
    const jmethodID init_method = find_init_method(jni_env, thread_class,
                                                   "(Ljava/lang/String;)V");

    JNILocalRef<jstring> name_string(*jni_env, jni_env->NewStringUTF(name));
    if(!name_string)
    {
        // TODO(thelink2012): log failure
        std::terminate();
    }

    JNILocalRef<jthread> thread_obj(
            *jni_env,
            jni_env->NewObject(thread_class, init_method, name_string.get()));
    if(!thread_obj)
    {
        // TODO(thelink2012): log failure
        std::terminate();
    }

    return JNIGlobalRef<jthread>(*jni_env, thread_obj);
}

void JNICALL run(jvmtiEnv* jvmti_env, JNIEnv* jni_env, void* data)
{
    return (static_cast<jvmtiprof::JvmtiAgentThread*>(data))->run();
}
}

namespace jvmtiprof
{
JvmtiAgentThread::~JvmtiAgentThread()
{
    if(m_thread_obj != nullptr)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
}

void JvmtiAgentThread::set_name(const char* name)
{
    assert(m_thread_obj == nullptr);
    assert(name != nullptr);
    m_name = name;
}

void JvmtiAgentThread::start(JNIEnv* jni_env)
{
    assert(m_thread_obj == nullptr);

    m_thread_obj = m_name ? new_thread_object(jni_env, m_name)
                          : new_thread_object(jni_env);

    // No memory fence is required to publish the above changes in `this` since
    // the creation of a thread is a barrier by itself.
    jvmtiError jvmti_err = m_jvmti_env->RunAgentThread(m_thread_obj.get(),
                                                       ::run, this, m_priority);
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

void JvmtiAgentThread::join(JNIEnv* jni_env)
{
    assert(m_thread_obj != nullptr);

    const jclass thread_class = find_java_lang_Thread(jni_env);
    const jmethodID join_method_id = find_join_method(jni_env, thread_class);

    // It's possible to implement some kind of joining purely in native by
    // using raw monitors and setting a status variable at the end of `run`,
    // but join is an expensive operation by itself, so we don't bother and
    // forward it to `java.lang.Thread`.
    jni_env->CallVoidMethod(m_thread_obj.get(), join_method_id);

    detach(jni_env);
}

void JvmtiAgentThread::detach(JNIEnv* jni_env)
{
    assert(m_thread_obj != nullptr);
    m_thread_obj.reset(*jni_env);
}

void JvmtiAgentThread::yield()
{
    // We could forward this to `java.lang.Thread` but unlike e.g. `sleep`
    // yielding to the scheduler (in case the VM uses OS threads instead
    // of green ones) is an operation that should not really affect any VM
    // state since yielding is something done automatically every once in
    // a while by the OS. Therefore we use the native system call directly.
    sched_yield();
}
}
