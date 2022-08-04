#include <cstdio>
#include <jvmti.h>
#include <jvmtiprof/jvmtiprof.h>

static jvmtiProfEnv *jvmtiprof;

extern "C"
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
    jvmtiProfError perr;
    jvmtiEnv *jvmti;

    fprintf(stderr, "loading jvmti agent...\n");

    if(vm->GetEnv((void **)&jvmti, JVMTI_VERSION) != JNI_OK)
    {
        fprintf(stderr, "failed to create JVMTI environment\n");
        return 1;
    }

    perr = jvmtiProf_Create(vm, jvmti, &jvmtiprof, JVMTIPROF_VERSION);
    if(perr != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to create JVMTIPROF environment (error %d)\n",
                perr);
        return 1;
    }

    fprintf(stderr, "jvmti agent has been loaded.\n");
    return 0;
}

extern "C"
JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm)
{
    fprintf(stderr, "unloading jvmti agent...\n");

    // Disposing the JVMTIPROF environment is necessary, because that disposal
    // isn't handled internally by the JVM (unlike it does for JVMTI).
    jvmtiprof->DisposeEnvironment();

    fprintf(stderr, "jvmti agent has been unloaded...\n");
}
