#include <jvmti.h>

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm,
             char *options,
             void *reserved)
{
    jvmtiEnv *jvmti;

    vm->GetEnv((void **)&jvmti, JVMTI_VERSION);

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_method_entry_events = true;
    jvmti->AddCapabilities(&caps);

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.MethodEntry = OnMethodEntry;

    jvmti->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTI_EVENT_METHOD_ENTRY, NULL);
    jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

    return 0;
}

void JNICALL
OnMethodEntry(jvmtiEnv *jvmti_env,
              JNIEnv *jni_env,
              jthread thread,
              jmethodID method)
{
    printf("method %p called by thread %p\n",
            (void*) method, (void*) thread);
}

