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

static void JNICALL OnSampleExecution(jvmtiProfEnv *jvmtiprof, jvmtiEnv *jvmti,
                                      JNIEnv *jni, jthread thread)
{
    char printbuf[256];
    jvmtiProfError err;

    const jint depth = 1;
    const jint max_frame_count = 1;
    jvmtiProfFrameInfo frames[max_frame_count];
    jint frame_count;

    // fprintf isn't async-signal-safe, thus we use `write(2)`
    snprintf(printbuf, sizeof(printbuf), "sampling...\n");
    write(STDERR_FILENO, printbuf, strlen(printbuf));

    err = jvmtiprof->GetStackTraceAsync(depth, max_frame_count, frames,
                                        &frame_count);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        snprintf(printbuf, sizeof(printbuf),
                 "failed to GetStackTraceAsync (error %d)\n", err);
        write(STDERR_FILENO, printbuf, strlen(printbuf));
        return;
    }

    // Since there's not much you can do in an async-signal-safe manner, the
    // best approach you can do is to save the frame data (or other useful
    // metric derived from it) into a produce-consumer data structure to be
    // consumed elsewhere. But since this is a simple demo, we'll print the
    // frame data contents right away.
    for(int i = 0; i < frame_count; ++i)
    {
        snprintf(printbuf, sizeof(printbuf),
                 "\tframe %d; bci offset %d; method %p\n", i, frames[i].offset,
                 (void *)frames[i].method);
        write(STDERR_FILENO, printbuf, strlen(printbuf));
    }
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
    caps.can_generate_sample_execution_events = true;
    err = jvmtiprof->AddCapabilities(&caps);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err, "failed to add sample execution capability");
        goto dispose;
    }

    jvmtiProfEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.SampleExecution = OnSampleExecution;

    err = jvmtiprof->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTIPROF_EVENT_SAMPLE_EXECUTION, NULL);
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err,
                    "failed to enable sample execution event notifications");
        goto dispose;
    }

    err = jvmtiprof->SetEventCallbacks(&callbacks, sizeof(callbacks));
    if(err != JVMTIPROF_ERROR_NONE)
    {
        print_error(err, "failed to set event callbacks for sample execution");
        goto dispose;
    }

    jvmtiprof->SetExecutionSamplingInterval(interval_ms * ms_to_ns);

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
