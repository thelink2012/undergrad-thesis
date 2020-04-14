#include <jvmti.h>
#include <cassert>
#include <cstring>
#include <atomic>
#include "phase.hpp"
#include "time.hpp"
using std::memory_order_relaxed;

/// We need to watch for when the VM is alive.
/// 
/// Details on the reason are given on NativeMethodBind below.
/// 
/// Due to multiple events being concurrent with VMInit this flag is atomic.
static std::atomic<bool> is_vm_alive {false};

/// We need to ignore the creation of the signal dispatcher thread, as we are 
/// not interested in it. Thus we store a flag on whether we have seen it.
static std::atomic<bool> has_seen_signal_dispatcher {false};

/// Used as an intermediate value to time the amount of time taken between
/// calls to MonitorContendedEnter and MonitorContendedEntered in the same
/// thread (and consequently in the same monitor).
static thread_local uint64_t thread_cs_start_time {0};

/// Same use as above, but for timing MonitorWait and MonitorWaited.
static thread_local uint64_t thread_wait_start_time {0};

/// An index for this thread in the per-thread arrays in the
/// AtomicPhase structure. Note that all untracked threads have
/// its indice equal 0. Tracked threads have indices greater than 0.
static thread_local int thread_id = 0;

/// We are detouring sun.misc.Unsafe.park in order to monitor parking.
/// This stores the original method target (before our detour).
static void (*original_Unsafe_Park)(JNIEnv *env, jobject unsafe,
                                    jboolean is_absolute, jlong time);


/// This is a detour on sun.misc.Unsafe.park native method.
static void JNICALL
detoured_Unsafe_Park(JNIEnv *env, jobject unsafe,
                     jboolean is_absolute, jlong time)
{
    {
    auto phase_ptr = get_phase();
    phase_ptr->record_cpu(::thread_id);
    phase_ptr->phase_thread_state_change[thread_id].store(
            AtomicPhase::THREAD_STATE_PARKING, memory_order_relaxed);
    }

    const auto thread_park_start_time = get_time();
    original_Unsafe_Park(env, unsafe, is_absolute, time);
    const auto park_time = get_time() - thread_park_start_time;

    {
    auto phase_ptr = get_phase();
    phase_ptr->phase_park_time.fetch_add(park_time, memory_order_relaxed);
    phase_ptr->record_cpu(::thread_id);
    phase_ptr->phase_thread_state_change[thread_id].store(
            AtomicPhase::THREAD_STATE_RUNNING, memory_order_relaxed);
    }
}

/// Called when a thread is about to wait on a monitor object.
static void JNICALL
MonitorWait(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread,
            jobject object,
            jlong timeout)
{
    const auto curr_time = get_time();
    phase_checkpoint(curr_time);

    thread_wait_start_time = curr_time;

    auto phase_ptr = get_phase();
    phase_ptr->record_cpu(::thread_id);
    phase_ptr->phase_thread_state_change[thread_id].store(
            AtomicPhase::THREAD_STATE_WAITING, memory_order_relaxed);
}

/// Called when a thread finishes waiting on a monitor object.
static void JNICALL
MonitorWaited(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread,
            jobject object,
            jboolean timed_out)
{
    // The JVM may report waited for something that it did not report wait.
    // https://bugs.openjdk.java.net/browse/JDK-8075259
    if(thread_wait_start_time == 0)
        return;

    const auto curr_time = get_time();
    phase_checkpoint(curr_time);

    const auto wait_time = curr_time - thread_wait_start_time;
    thread_wait_start_time = 0;

    auto phase_ptr = get_phase();
    phase_ptr->phase_wait_time.fetch_add(wait_time, memory_order_relaxed);
    phase_ptr->record_cpu(::thread_id);
    phase_ptr->phase_thread_state_change[thread_id].store(
            AtomicPhase::THREAD_STATE_RUNNING, memory_order_relaxed);
}

/// Called when a thread attempts to enter a monitor already acquired
/// by another thread.
static void JNICALL
MonitorContendedEnter(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread,
            jobject object)
{
    const auto curr_time = get_time();
    phase_checkpoint(curr_time);

    thread_cs_start_time = curr_time;

    auto phase_ptr = get_phase();
    phase_ptr->record_cpu(::thread_id);
    phase_ptr->phase_thread_state_change[thread_id].store(
            AtomicPhase::THREAD_STATE_CONTENDED, memory_order_relaxed);
}

/// Called when a thread enters a monitor after waiting for it to be
/// released by another thread. 
static void JNICALL
MonitorContendedEntered(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread,
            jobject object)
{
    assert(thread_cs_start_time != 0);

    const auto curr_time = get_time();
    phase_checkpoint(curr_time);

    const auto cs_time = curr_time - thread_cs_start_time;
    thread_cs_start_time = 0;

    auto phase_ptr = get_phase();
    phase_ptr->phase_cs_time.fetch_add(cs_time, memory_order_relaxed);
    phase_ptr->record_cpu(::thread_id);
    phase_ptr->phase_thread_state_change[thread_id].store(
            AtomicPhase::THREAD_STATE_RUNNING, memory_order_relaxed);
}

/// Called when a Java Thread starts.
static void JNICALL
ThreadStart(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread)
{
    // We do not want to count the internal VM threads. These are launched
    // before boot. Note that they do not have a corresponding ThreadEnd,
    // so we do not have to worry about it there.
    if(!is_vm_alive)
        return;

    /// Another internal thread that we do not want to count is the signal
    /// dispatcher thread. This one also do not have a corresponding ThreadEnd.
    if(!has_seen_signal_dispatcher)
    {
        jvmtiError err;
        jvmtiThreadInfo info;

        if((err = jvmti_env->GetThreadInfo(thread, &info)))
        {
            fprintf(stderr, "sync_jvmti: Failed to GetThreadInfo\n");
            return;
        }

        if(info.name && !strcmp(info.name, "Signal Dispatcher"))
        {
            has_seen_signal_dispatcher.store(true);
            return;
        }
    }

    const auto curr_time = get_time();
    phase_checkpoint(curr_time);

    auto phase_ptr = get_phase();
    ::thread_id = phase_alloc_thread();
    phase_ptr->phase_thread_change_count.fetch_add(1, memory_order_relaxed);
    phase_ptr->record_cpu(::thread_id);
    phase_ptr->phase_thread_state_change[thread_id].store(
            AtomicPhase::THREAD_STATE_RUNNING, memory_order_relaxed);
}

/// Called when a Java Thread ends.
static void JNICALL
ThreadEnd(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread)
{
    const auto curr_time = get_time();
    phase_checkpoint(curr_time);

    auto phase_ptr = get_phase();
    phase_ptr->phase_thread_change_count.fetch_sub(1, memory_order_relaxed);
    phase_ptr->record_cpu(::thread_id);
    phase_ptr->phase_thread_state_change[thread_id].store(
            AtomicPhase::THREAD_STATE_DIED, memory_order_relaxed);
}




/// Called once the VM is ready.
static void JNICALL
VMInit(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread)
{
    phase_vm_init();
    is_vm_alive.store(true);
}

/// Called once the VM shutdowns.
static void JNICALL
VMDeath(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env)
{
    phase_vm_die();
    is_vm_alive.store(false);
}

/// Called whenever a native method is bound to an address.
///
/// We use this to detour sun.misc.Unsafe.park into our own native method.
static void JNICALL
NativeMethodBind(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread,
            jmethodID method,
            void* address,
            void** new_address_ptr)
{
    jvmtiError err = JVMTI_ERROR_NONE;
    char *method_name = 0;

    // Usually when one wants to detour a method by using NativeMethodBind
    // it should get the name of the declaring class as well as the method
    // name being bound, compare, and detour if it matches what you are
    // looking for.
    //
    // The problem is, to get the name of the declaring class we need to
    // execute reflection code, which is Java code. However, all the methods
    // from sun.misc.Unsafe (unlike other classes) are bound before the VM
    // starts. This means we cannot execute Java code, as such we cannot get
    // the name of the declaring class.
    //
    // As a workaround, we only inspect the name of the methods bound before
    // the VM boots. If we find a `park` during this process, we know it is
    // from sun.misc.Unsafe and not other classes.

    if(is_vm_alive)
        return;

    if(original_Unsafe_Park)
        return;

    if((err = jvmti_env->GetMethodName(method, &method_name, NULL, NULL)))
        goto cleanup;

    if(!strcmp(method_name, "park"))
    {
        fprintf(stderr, "sync_jvmti: Detouring sun.misc.Unsafe.park.\n");
        original_Unsafe_Park = (decltype(original_Unsafe_Park)) address;
        *new_address_ptr = (void*) detoured_Unsafe_Park;
    }

cleanup:
    if(method_name)
        jvmti_env->Deallocate((unsigned char*)method_name);
    if(err)
        fprintf(stderr, "sync_jvmti: Failed on NativeMethodBind (%d)\n", err);
}


/// Called by the virtual machine to configure the agent.
JNIEXPORT jint JNICALL 
Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
    jvmtiError err;

    jvmtiEnv *jvmti;
    if(vm->GetEnv((void **)&jvmti, JVMTI_VERSION_1_0) != JNI_OK)
    {
        fprintf(stderr, "sync_jvmti: Failed to get environment\n");
        return 1;
    }

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_monitor_events = true;
    caps.can_generate_native_method_bind_events = true;
    if((err = jvmti->AddCapabilities(&caps)))
    {
        fprintf(stderr, "sync_jvmti: Failed to add capabilities (%d)\n", err);
        return 1;
    }

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    callbacks.MonitorContendedEnter = MonitorContendedEnter;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_MONITOR_CONTENDED_ENTER,
                                    NULL);

    callbacks.MonitorContendedEntered = MonitorContendedEntered;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_MONITOR_CONTENDED_ENTERED,
                                    NULL);

    callbacks.MonitorWait = MonitorWait;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_MONITOR_WAIT,
                                    NULL);

    callbacks.MonitorWaited = MonitorWaited;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_MONITOR_WAITED,
                                    NULL);

    callbacks.ThreadStart = ThreadStart;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_THREAD_START,
                                    NULL);

    callbacks.ThreadEnd = ThreadEnd;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_THREAD_END,
                                    NULL);

    callbacks.NativeMethodBind = NativeMethodBind;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_NATIVE_METHOD_BIND,
                                    NULL);

    callbacks.VMInit = VMInit;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_VM_INIT,
                                    NULL);

    callbacks.VMDeath = VMDeath;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_VM_DEATH,
                                    NULL);

    if((err = jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks))))
    {
        fprintf(stderr, "sync_jvmti: Failed to set callbacks (%d)\n", err);
        return 1;
    }

    std::atomic<uint64_t> u64_atomic;
    if(!u64_atomic.is_lock_free())
        fprintf(stderr, "sync_jvmti: aligned atomic_uint64_t is not lock free!!!\n");

    phase_init();
    has_seen_signal_dispatcher.store(false);
    original_Unsafe_Park = nullptr;

    fprintf(stderr, "sync_jvmti: Agent has been loaded.\n");
    return 0;
}

/// Called by the virtual machine once the agent is about to unload.
JNIEXPORT void JNICALL 
Agent_OnUnload(JavaVM *vm)
{
    phase_shutdown();
    fprintf(stderr, "sync_jvmti: Agent has been unloaded\n");
}
