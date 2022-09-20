#include <cstdio>
#include <cstring>
#include <jvmti.h>
#include <jvmtiprof/jvmtiprof.h>
#include <unistd.h>

static jvmtiProfEnv *jvmtiprof;
static jint hook_id;

static void print_error(jvmtiProfError err, const char *message)
{
    fprintf(stderr, "%s (error %d)\n", message, err);
}

void JNICALL OnMethodEntry(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env,
        JNIEnv* jni_env, jthread thread, jint hook_id)
{
    fprintf(stderr, "OnMethodEntry %d\n", hook_id);
}

void JNICALL OnMethodExit(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env,
        JNIEnv* jni_env, jthread thread, jint hook_id)
{
    fprintf(stderr, "OnMethodExit %d\n", hook_id);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
    jvmtiProfError err;
    jvmtiEnv *jvmti;

    fprintf(stderr, "loading jvmti agent...\n");

    if(vm->GetEnv((void **)&jvmti, JVMTI_VERSION) != JNI_OK)
    {
        fprintf(stderr, "failed to create JVMTI environment\n");
        return 1;
    }

    err = jvmtiProf_Create(vm, jvmti, &jvmtiprof, JVMTIPROF_VERSION);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to create JVMTIPROF environment (error %d)\n",
                err);
        return 1;
    }

    jvmtiProfCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_specific_method_entry_events = true;
    caps.can_generate_specific_method_exit_events = true;
    err = jvmtiprof->AddCapabilities(&caps);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err, "failed to add hooking capability");
        goto dispose;
    }

    jvmtiProfEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.SpecificMethodEntry = OnMethodEntry;
    callbacks.SpecificMethodExit = OnMethodExit;

    err = jvmtiprof->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTIPROF_EVENT_SPECIFIC_METHOD_ENTRY, NULL);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err,
                    "failed to enable method entry event notifications");
        goto dispose;
    }

    err = jvmtiprof->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTIPROF_EVENT_SPECIFIC_METHOD_EXIT, NULL);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err,
                    "failed to enable method exit event notifications");
        goto dispose;
    }

    err = jvmtiprof->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err, "failed to set event callbacks for method hook");
        goto dispose;
    }

    err = jvmtiprof->SetMethodEventFlag(
            "Target", "printGreetings", "()V",
            (jvmtiProfMethodEventFlag)((int)JVMTIPROF_METHOD_EVENT_ENTRY | (int)JVMTIPROF_METHOD_EVENT_EXIT),
            JVMTI_ENABLE,
            &hook_id);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err, "failed to set method event flag for method hook");
        goto dispose;
    }

    fprintf(stderr, "jvmti agent has been loaded.\n");
    return 0;

dispose:
    jvmtiprof->DisposeEnvironment();
    return 1;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm)
{
    fprintf(stderr, "unloading jvmti agent...\n");

    jvmtiprof->DisposeEnvironment();

    fprintf(stderr, "jvmti agent has been unloaded...\n");
}
