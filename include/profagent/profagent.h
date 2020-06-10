#pragma once
#ifndef PROFAGENT_PROFAGENT_H
#define PROFAGENT_PROFAGENT_H
#include <jvmti.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PROFAGENT_IMPLEMENTATION
#define PROFAGENT_IMPORT_OR_EXPORT JNIEXPORT
#else
#define PROFAGENT_IMPORT_OR_EXPORT JNIIMPORT
#endif

typedef enum
{
    PROFAGENT_VERSION_1 = 0x00010000,
    PROFAGENT_VERSION_1_0 = 0x00010000,
    PROFAGENT_VERSION = PROFAGENT_VERSION_1_0
} ProfAgentVersion;

typedef enum
{
    PROFAGENT_ERROR_NONE = JVMTI_ERROR_NONE,
    PROFAGENT_ERROR_INTERNAL = JVMTI_ERROR_INTERNAL,
    PROFAGENT_ERROR_NULL_POINTER = JVMTI_ERROR_NULL_POINTER,
    PROFAGENT_ERROR_UNSUPPORTED_VERSION = JVMTI_ERROR_UNSUPPORTED_VERSION,
    PROFAGENT_ERROR_INVALID_ENVIRONMENT = JVMTI_ERROR_INVALID_ENVIRONMENT,
    PROFAGENT_ERROR_WRONG_PHASE = JVMTI_ERROR_WRONG_PHASE,
    PROFAGENT_ERROR_ILLEGAL_ARGUMENT = JVMTI_ERROR_ILLEGAL_ARGUMENT,
    PROFAGENT_ERROR_MAX,
} ProfAgentError;

typedef enum
{
} ProfAgentVerboseFlag;

typedef enum
{
    PROFAGENT_ENABLE = JVMTI_ENABLE,
    PROFAGENT_DISABLE = JVMTI_DISABLE,
} ProfAgentEventMode;

typedef enum
{
    PROFAGENT_EVENT_MIN_EVENT_TYPE_VAL = 18000,
    PROFAGENT_EVENT_TICK = PROFAGENT_EVENT_MIN_EVENT_TYPE_VAL + 0,
    PROFAGENT_EVENT_SAMPLE = PROFAGENT_EVENT_MIN_EVENT_TYPE_VAL + 1,
    PROFAGENT_EVENT_MAX_EVENT_TYPE_VAL = 18100,
} ProfAgentEvent;

struct ProfAgent_;
struct ProfAgentInterface;

struct ProfAgentEnv_;
struct ProfAgentEnvInterface;

struct ProfAgentTickEnv_;
struct ProfAgentTickEnvInterface;

#ifdef __cplusplus
typedef ProfAgent_ ProfAgent;
typedef ProfAgentEnv_ ProfAgentEnv;
typedef ProfAgentTickEnv_ ProfAgentTickEnv;
#else
typedef const struct ProfAgentInterface* ProfAgent;
typedef const struct ProfAgentEnvInterface* ProfAgentEnv;
typedef const struct ProfAgentTickEnvInterface* ProfAgentTickEnv;
#endif

typedef struct
{
    uint16_t num_counters;
    const uint64_t* counters;

} ProfAgentPerfCounters;

typedef struct
{
    jlong nanos_interval;
} ProfAgentTickParams;


typedef void (*JNICALL ProfAgentEventReserved)(void);

typedef void (*JNICALL ProfAgentEventTick)(ProfAgentEnv* prof_env,
                                           jvmtiEnv* jvmti_env, JNIEnv* jni_env,
                                           ProfAgentTickEnv* prof_tick_env);

typedef void (*JNICALL ProfAgentEventSample)(ProfAgentEnv* prof_env,
                                             jvmtiEnv* jvmti_env,
                                             JNIEnv* jni_env, jthread thread,
                                             void* reserved);

typedef struct
{
    /* 0 */
    ProfAgentEventTick Tick;
    
    /* 1 */
    ProfAgentEventSample Sample;

} ProfAgentEventCallbacks;

typedef struct
{
    unsigned int can_generate_tick_events : 1;
    unsigned int : 15;
    unsigned int : 16;
    unsigned int : 16;
    unsigned int : 16;
    unsigned int : 16;
    unsigned int : 16;
    unsigned int : 16;
    unsigned int : 16;
} ProfAgentCapabilities;

typedef uint8_t ProfAgentThreadState;

struct ProfAgentEnvInterface
{
    /* 0 */
    ProfAgentError(JNICALL* DisposeEnvironment)(ProfAgentEnv*);

    /* 1 */
    ProfAgentError(JNICALL* GetJvmtiEnv)(ProfAgentEnv*,
                                         jvmtiEnv** jvmti_env_ptr);

    /* 2 */
    ProfAgentError(JNICALL* GetVersionNumber)(ProfAgentEnv*, jint* version_ptr);

    /* 3 */
    ProfAgentError(JNICALL* GetErrorName)(ProfAgentEnv*, ProfAgentError error,
                                          char** name_ptr);

    /* 4 */
    ProfAgentError(JNICALL* SetVerboseFlag)(ProfAgentEnv*,
                                            ProfAgentVerboseFlag flag,
                                            jboolean value);
    /* 5 */
    void* reserved5;

    /* 6 */
    void* reserved6;

    /* 7 */
    void* reserved7;

    /* 8 */
    void* reserved8;

    /* 9 */
    void* reserved9;

    /* 10 */
    ProfAgentError(JNICALL* SetEventNotificationMode)(ProfAgentEnv*,
                                                      ProfAgentEventMode mode,
                                                      ProfAgentEvent event_type,
                                                      jthread event_thread,
                                                      ...);
    /* 11 */
    ProfAgentError(JNICALL* SetEventCallbacks)(ProfAgentEnv*,
                                               const ProfAgentEventCallbacks*,
                                               jint size_of_callbacks);

    /* 12 */
    ProfAgentError(JNICALL* GenerateEvents)(ProfAgentEnv*,
                                            ProfAgentEvent event_type);

    /* 13 */
    void* reserved13;

    /* 14 */
    void* reserved14;

    /* 15 */
    void* reserved15;

    /* 16 */
    void* reserved16;

    /* 17 */
    ProfAgentError(JNICALL* GetPotentialCapabilities)(
            ProfAgentEnv*, ProfAgentCapabilities* capabilities_ptr);

    /* 18 */
    ProfAgentError(JNICALL* AddCapabilities)(
            ProfAgentEnv*, const ProfAgentCapabilities* capabilities_ptr);

    /* 19 */
    ProfAgentError(JNICALL* RelinquishCapabilities)(
            ProfAgentEnv*, const ProfAgentCapabilities* capabilities_ptr);

    /* 20 */
    ProfAgentError(JNICALL* GetCapabilities)(
            ProfAgentEnv*, ProfAgentCapabilities* capabilities_ptr);

    /* TODO reserved functions */
};

struct ProfAgentEnv_
{
    const struct ProfAgentEnvInterface* functions;

#ifdef __cplusplus
    // TODO

    ProfAgentError SetEventNotificationMode(ProfAgentEventMode mode,
                                            ProfAgentEvent event_type,
                                            jthread event_thread)
    {
        return functions->SetEventNotificationMode(this, mode, event_type,
                                                   event_thread);
    }

    ProfAgentError SetEventCallbacks(const ProfAgentEventCallbacks* callbacks,
                                     jint size_of_callbacks)
    {
        return functions->SetEventCallbacks(this, callbacks, size_of_callbacks);
    }

    ProfAgentError
    AddCapabilities(const ProfAgentCapabilities* capabilities_ptr)
    {
        return functions->AddCapabilities(this, capabilities_ptr);
    }
#endif
};

struct ProfAgentTickEnvInterface
{
    ProfAgentError(JNICALL* GetElapsedTimeMs)(ProfAgentTickEnv*,
                                              uint64_t* elapsed_time);

    ProfAgentError(JNICALL* GetCriticalSectionPressure)(ProfAgentTickEnv*,
                                                        jdouble* csp);

    ProfAgentError(JNICALL* GetThreadsCount)(ProfAgentTickEnv*,
                                             jint* threads_count);

    ProfAgentError(JNICALL* GetThreadsState)(
            ProfAgentTickEnv*, ProfAgentThreadState* state_buffer_ptr,
            jint* count_ptr);

    ProfAgentError(JNICALL* GetThreadsCpu)(ProfAgentTickEnv*,
                                           jint* threads_cpu_buffer_ptr,
                                           jint* count_ptr);

    ProfAgentError(JNICALL* GetHardwareCpuCount)(ProfAgentTickEnv*,
                                                 jint* cpu_count);

    ProfAgentError(JNICALL* GetHardwareCounters)(
            ProfAgentTickEnv*, jint cpu, uint64_t* counter_buffer_ptr,
            jint* count_ptr);

    ProfAgentError(JNICALL* GetSoftwareCounters)(
            ProfAgentTickEnv*, uint64_t* counter_buffer_ptr,
            jint* count_ptr);

    /* TODO reserved functions */
};

struct ProfAgentTickEnv_
{
    const struct ProfAgentTickEnvInterface* functions;

#ifdef __cplusplus

    ProfAgentError GetElapsedTimeMs(uint64_t* elapsed_time)
    {
        return functions->GetElapsedTimeMs(this, elapsed_time);
    }

    ProfAgentError GetCriticalSectionPressure(jdouble* csp)
    {
        return functions->GetCriticalSectionPressure(this, csp);
    }

    ProfAgentError GetThreadsCount(jint* threads_count)
    {
        return functions->GetThreadsCount(this, threads_count);
    }

    ProfAgentError
    GetThreadsState(ProfAgentThreadState* state_buffer_ptr,
                    jint* count_ptr)
    {
        return functions->GetThreadsState(this, state_buffer_ptr, count_ptr);
    }

    ProfAgentError GetThreadsCpu(jint* threads_cpu_buffer_ptr,
                                 jint* count_ptr)
    {
        return functions->GetThreadsCpu(this, threads_cpu_buffer_ptr, count_ptr);
    }

    ProfAgentError GetHardwareCpuCount(jint* cpu_count)
    {
        return functions->GetHardwareCpuCount(this, cpu_count);
    }

    ProfAgentError GetHardwareCounters(jint cpu, uint64_t* counter_buffer_ptr,
                                       jint* count_ptr)
    {
        return functions->GetHardwareCounters(this, cpu, counter_buffer_ptr,
                                              count_ptr);
    }

    ProfAgentError GetSoftwareCounters(uint64_t* counter_buffer_ptr,
                                       jint* count_ptr)
    {
        return functions->GetSoftwareCounters(this, counter_buffer_ptr,
                                              count_ptr);
    }
#endif
};

/* Extension Injection Functions */

PROFAGENT_IMPORT_OR_EXPORT
ProfAgentError JNICALL ProfAgent_Create(JavaVM* vm, jvmtiEnv* jvmti,
                                        ProfAgentEnv** env,
                                        ProfAgentVersion version);

PROFAGENT_IMPORT_OR_EXPORT
void JNICALL ProfAgent_Destroy(ProfAgentEnv* env);

/* UB if ProfAgentEnv not attached to jvmti */
PROFAGENT_IMPORT_OR_EXPORT
ProfAgentError JNICALL ProfAgent_GetEnv(jvmtiEnv* jvmti, ProfAgentEnv** env);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROFAGENT_PROFAGENT_H */
