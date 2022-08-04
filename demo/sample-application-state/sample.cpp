#include <cstdio>
#include <cstring>
#include <jvmti.h>
#include <jvmtiprof/jvmtiprof.h>
#include <unistd.h>

static jvmtiProfEnv *jvmtiprof;

static void print_error(jvmtiProfError err, const char *message)
{
    fprintf(stderr, "%s (error %d)\n", message, err);
}

static void JNICALL
OnSampleApplicationState(jvmtiProfEnv *jvmtiprof, jvmtiEnv *jvmti, JNIEnv *jni,
                         jvmtiProfApplicationState *sample_data)
{
    fprintf(stderr, "sampling...\n");
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
    jvmtiProfError err;
    jvmtiEnv *jvmti;

    // TODO would be nice to have interval_ms passed through options
    const jlong ms_to_ns = 1000000;
    jlong interval_ms = 1000;

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
    caps.can_generate_sample_application_state_events = true;
    err = jvmtiprof->AddCapabilities(&caps);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err, "failed to add sample application state capability");
        goto dispose;
    }

    jvmtiProfEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.SampleApplicationState = OnSampleApplicationState;

    err = jvmtiprof->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTIPROF_EVENT_SAMPLE_APPLICATION_STATE, NULL);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err, "failed to enable sample application state event "
                         "notifications");
        goto dispose;
    }

    err = jvmtiprof->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(
                err,
                "failed to set event callbacks for sample application state");
        goto dispose;
    }

    jvmtiprof->SetApplicationStateSamplingInterval(interval_ms * ms_to_ns);

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
