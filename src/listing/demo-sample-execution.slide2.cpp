void JNICALL
OnSampleExecution(jvmtiProfEnv *jvmtiprof, jvmtiEnv *jvmti, JNIEnv *jni, jthread thread)
{
    char printbuf[256];
    jvmtiProfError err;

    const jint depth = 1;
    const jint max_frame_count = 1;
    jvmtiProfFrameInfo frames[max_frame_count];
    jint frame_count;

    // fprintf isn't async-signal-safe, thus we use write.
    snprintf(printbuf, sizeof(printbuf), "sampling...\n");
    write(STDERR_FILENO, printbuf, strlen(printbuf));

    err = jvmtiprof->GetStackTraceAsync(depth, max_frame_count, frames, &frame_count);
    if(err != JVMTIPROF_ERROR_NONE)
        return;

    for(int i = 0; i < frame_count; ++i)
    {
        snprintf(printbuf, sizeof(printbuf),
                 "\tframe %d; bci offset %d; method %p\n",
                 i, frames[i].offset, (void *)frames[i].method);
        write(STDERR_FILENO, printbuf, strlen(printbuf));
    }
}
