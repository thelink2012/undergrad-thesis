jvmtiProfEnv *jvmtiprof;

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void*)
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

    jvmtiprof->SetExecutionSamplingInterval(1000000000L); // 1 second

    return 0;
}
