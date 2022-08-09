
jvmtiProfEnv *jvmtiprof;

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm,
                                    char *options,
                                    void *reserved)
{
    jvmtiEnv *jvmti;

    vm->GetEnv((void **)&jvmti, JVMTI_VERSION);

    jvmtiProf_Create(vm, jvmti, &jvmtiprof, JVMTIPROF_VERSION);

    jvmtiProfCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_sample_execution_events = true;
    caps.can_get_stack_trace_asynchronously = true;
    jvmtiprof->AddCapabilities(&caps);

    jvmtiProfEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.SampleExecution = OnSampleExecution;
    jvmtiprof->SetEventCallbacks(&callbacks, sizeof(callbacks));

    jvmtiprof->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTIPROF_EVENT_SAMPLE_EXECUTION,
            NULL);

    const jlong ms_to_ns = 1000000;
    jvmtiprof->SetExecutionSamplingInterval(
            1000 * ms_to_ns);

    return 0;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm)
{
    jvmtiprof->DisposeEnvironment();
}

void JNICALL OnSampleExecution(jvmtiProfEnv *jvmtiprof,
                               jvmtiEnv *jvmti,
                               JNIEnv *jni,
                               jthread thread)
{
    char printbuf[256];
    jvmtiProfError err;

    const jint depth = 1;
    const jint max_frame_count = 1;
    jvmtiProfFrameInfo frames[max_frame_count];
    jint frame_count;

    // fprintf isn't async-signal-safe, thus use write.
    snprintf(printbuf, sizeof(printbuf), "sampling...\n");
    write(STDERR_FILENO, printbuf, strlen(printbuf));

    err = jvmtiprof->GetStackTraceAsync(
            depth, max_frame_count, frames, &frame_count);
    if(err != JVMTIPROF_ERROR_NONE)
        return;

    for(int i = 0; i < frame_count; ++i)
    {
        snprintf(printbuf, sizeof(printbuf),
                 "\tframe %d; bci offset %d; method %p\n",
                 i, frames[i].offset,
                 (void *)frames[i].method);
        write(STDERR_FILENO, printbuf, strlen(printbuf));
    }
}

