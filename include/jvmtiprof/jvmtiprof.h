/*
 * Copyright (c) 2020 Denilson das Mercês Amorim
 *
 * Distributed under the Boost Software License, Version 1.0. (See accompanying
 * file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)
 */
#pragma once
#ifndef JVMTIPROF_JVMTIPROF_H
#define JVMTIPROF_JVMTIPROF_H
#include <jvmti.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Since the library is statically linked there's no distinction between
 * importing and exporting.
 *
 * Another important factor of static linking in the design of this interface
 * is that reserving struct fields for ABI compatibility is unecessary. */
#define JVMTIPROF_IMPORT_OR_EXPORT

/* Forward declaration of the environment */

struct jvmtiProfEnv_;
struct jvmtiProfInterface_;

#ifdef __cplusplus
typedef jvmtiProfEnv_ jvmtiProfEnv;
#else
typedef const struct jvmtiProfInterface_* jvmtiProfEnv;
#endif

/* Enumerations */

typedef enum
{
    JVMTIPROF_VERSION_1 = 0x00010000,
    JVMTIPROF_VERSION_1_0 = 0x00010000,
    JVMTIPROF_VERSION = JVMTIPROF_VERSION_1_0
} jvmtiProfVersion;

typedef enum
{
    /* Errors with error code similar to that of the jvmti */
    JVMTIPROF_ERROR_NONE = JVMTI_ERROR_NONE,
    JVMTIPROF_ERROR_NULL_POINTER = JVMTI_ERROR_NULL_POINTER,
    JVMTIPROF_ERROR_INVALID_ENVIRONMENT = JVMTI_ERROR_INVALID_ENVIRONMENT,
    JVMTIPROF_ERROR_WRONG_PHASE = JVMTI_ERROR_WRONG_PHASE,
    JVMTIPROF_ERROR_INTERNAL = JVMTI_ERROR_INTERNAL,
    JVMTIPROF_ERROR_ILLEGAL_ARGUMENT = JVMTI_ERROR_ILLEGAL_ARGUMENT,
    JVMTIPROF_ERROR_UNSUPPORTED_VERSION = JVMTI_ERROR_UNSUPPORTED_VERSION,
    JVMTIPROF_ERROR_NOT_AVAILABLE = JVMTI_ERROR_NOT_AVAILABLE,
    JVMTIPROF_ERROR_MUST_POSSESS_CAPABILITY = JVMTI_ERROR_MUST_POSSESS_CAPABILITY,
    JVMTIPROF_ERROR_UNATTACHED_THREAD = JVMTI_ERROR_UNATTACHED_THREAD,

    /* jvmti-prof specific errors */
    JVMTIPROF_SPECIFIC_ERROR_MIN = 2000,
    JVMTIPROF_SPECIFIC_ERROR_MAX,
    JVMTIPROF_ERROR_MAX = JVMTIPROF_SPECIFIC_ERROR_MAX,
} jvmtiProfError;

#ifdef __cplusplus
/* ISO C++ forbids empty unnamed enum [-Wpedantic] */
typedef enum jvmtiProfVerboseFlag
#else
typedef enum
#endif
{
} jvmtiProfVerboseFlag;

typedef enum
{
    JVMTIPROF_METHOD_EVENT_ENTRY = 1,
    JVMTIPROF_METHOD_EVENT_EXIT = 2,
} jvmtiProfMethodEventFlag;

typedef enum
{
    JVMTIPROF_EVENT_SAMPLE_EXECUTION,
    JVMTIPROF_EVENT_SAMPLE_APPLICATION_STATE,
    JVMTIPROF_EVENT_SAMPLE_CRITICAL_SECTION_PRESSURE,
    JVMTIPROF_EVENT_SAMPLE_THREAD_STATE,
    JVMTIPROF_EVENT_SAMPLE_THREAD_PROCESSOR,
    JVMTIPROF_EVENT_SAMPLE_HARDWARE_COUNTER,
    JVMTIPROF_EVENT_SAMPLE_SOFTWARE_COUNTER,
    JVMTIPROF_EVENT_SPECIFIC_METHOD_ENTRY,
    JVMTIPROF_EVENT_SPECIFIC_METHOD_EXIT,
    JVMTIPROF_EVENT_MAX,
} jvmtiProfEvent;

/* Structures */

typedef struct
{
    unsigned int can_generate_sample_execution_events : 1;
    unsigned int can_generate_sample_application_state_events : 1;
    unsigned int can_generate_specific_method_entry_events : 1;
    unsigned int can_generate_specific_method_exit_events : 1;
    unsigned int can_get_stack_trace_asynchronously : 1;
    unsigned int can_sample_critical_section_pressure : 1;
    unsigned int can_sample_thread_state : 1;
    unsigned int can_sample_thread_processor : 1;
    unsigned int can_sample_hardware_counters : 1;
    unsigned int can_sample_software_counters : 1;
    unsigned int : 6; /* ensure `std::has_unique_object_representation` since
                         bitwise operations will be applied to this struct */

} jvmtiProfCapabilities;

typedef struct
{
    jthread thread;
    jint thread_state;
    jint processor_id;
} jvmtiProfSampledThreadState;

typedef struct
{
    jint offset; /* JVMTI_JLOCATION_JVMBCI */
    jmethodID method;
} jvmtiProfFrameInfo;

struct jvmtiProfApplicationState_;
typedef struct jvmtiProfApplicationState_ jvmtiProfApplicationState;

/* Event Callbacks */

typedef void (*JNICALL jvmtiProfEventSampleExecution)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jthread thread);

typedef void (*JNICALL jvmtiProfEventSampleApplicationState)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jvmtiProfApplicationState* sample_data);

typedef void (*JNICALL jvmtiProfEventSampleCriticalSectionPressure)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jdouble critical_section_pressure);

typedef void (*JNICALL jvmtiProfEventSampleThreadState)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jthread thread, jint thread_state);

typedef void (*JNICALL jvmtiProfEventSampleThreadProcessor)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jthread thread, jint processor_id);

typedef void (*JNICALL jvmtiProfEventSampleHardwareCounter)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jint processor_id, jint counter_id, uint64_t counter_value);

typedef void (*JNICALL jvmtiProfEventSampleSoftwareCounter)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jint counter_id, uint64_t counter_value);

typedef void (*JNICALL jvmtiProfEventSpecificMethodEntry)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jthread thread, jint hook_id);

typedef void (*JNICALL jvmtiProfEventSpecificMethodExit)(
        jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env, JNIEnv* jni_env,
        jthread thread, jint hook_id);

typedef struct
{
    jvmtiProfEventSampleExecution SampleExecution;
    jvmtiProfEventSampleApplicationState SampleApplicationState;
    jvmtiProfEventSampleCriticalSectionPressure SampleCriticalSectionPressure;
    jvmtiProfEventSampleThreadState SampleThreadState;
    jvmtiProfEventSampleThreadProcessor SampleThreadProcessor;
    jvmtiProfEventSampleHardwareCounter SampleHardwareCounter;
    jvmtiProfEventSampleSoftwareCounter SampleSoftwareCounter;
    jvmtiProfEventSpecificMethodEntry SpecificMethodEntry;
    jvmtiProfEventSpecificMethodExit SpecificMethodExit;

} jvmtiProfEventCallbacks;

/* Interface */

struct jvmtiProfInterface_
{
    /*  General */

    jvmtiProfError(JNICALL* DisposeEnvironment)(jvmtiProfEnv* jvmtiprof_env);

    jvmtiProfError(JNICALL* GetJvmtiEnv)(jvmtiProfEnv* jvmtiprof_env,
                                         jvmtiEnv** jvmti_env_ptr);

    jvmtiProfError(JNICALL* GetVersionNumber)(jvmtiProfEnv* jvmtiprof_env,
                                              jint* version_ptr);

    jvmtiProfError(JNICALL* GetErrorName)(jvmtiProfEnv* jvmtiprof_env,
                                          jvmtiProfError error,
                                          char** name_ptr);

    jvmtiProfError(JNICALL* SetVerboseFlag)(jvmtiProfEnv* jvmtiprof_env,
                                            jvmtiProfVerboseFlag flag,
                                            jboolean value);

    /* Event Management */

    jvmtiProfError(JNICALL* SetEventNotificationMode)(
            jvmtiProfEnv* jvmtiprof_env, jvmtiEventMode mode,
            jvmtiProfEvent event_type, jthread event_thread, ...);

    jvmtiProfError(JNICALL* SetEventCallbacks)(
            jvmtiProfEnv* jvmtiprof_env,
            const jvmtiProfEventCallbacks* callbacks, jint size_of_callbacks);

    jvmtiProfError(JNICALL* GenerateEvents)(jvmtiProfEnv* jvmtiprof_env,
                                            jvmtiProfEvent event_type);

    /* Capability */

    jvmtiProfError(JNICALL* GetPotentialCapabilities)(
            jvmtiProfEnv* jvmtiprof_env,
            jvmtiProfCapabilities* capabilities_ptr);

    jvmtiProfError(JNICALL* AddCapabilities)(
            jvmtiProfEnv* jvmtiprof_env,
            const jvmtiProfCapabilities* capabilities_ptr);

    jvmtiProfError(JNICALL* RelinquishCapabilities)(
            jvmtiProfEnv* jvmtiprof_env,
            const jvmtiProfCapabilities* capabilities_ptr);

    jvmtiProfError(JNICALL* GetCapabilities)(
            jvmtiProfEnv* jvmtiprof_env,
            jvmtiProfCapabilities* capabilities_ptr);

    /* Application Execution Sampling */

    jvmtiProfError(JNICALL* SetExecutionSamplingInterval)(
            jvmtiProfEnv* jvmtiprof_env, jlong nanos_interval);

    jvmtiProfError(JNICALL* GetStackTraceAsync)(
            jvmtiProfEnv* jvmtiprof_env, jint depth, jint max_frame_count,
            jvmtiProfFrameInfo* frame_buffer, jint* count_ptr);

    /* Application State Sampling */

    jvmtiProfError(JNICALL* GetProcessorCount)(jvmtiProfEnv* jvmtiprof_env_ptr,
                                               jint* processor_count_ptr);

    jvmtiProfError(JNICALL* SetApplicationStateSamplingInterval)(
            jvmtiProfEnv* jvmtiprof_env, jlong nanos_interval);

    jvmtiProfError(JNICALL* GetSampledCriticalSectionPressure)(
            jvmtiProfEnv* jvmtiprof_env_ptr,
            jvmtiProfApplicationState* sample_data, jdouble* csp_ptr);

    jvmtiProfError(JNICALL* GetSampledThreadCount)(
            jvmtiProfEnv* jvmtiprof_env_ptr,
            jvmtiProfApplicationState* sample_data, jint* thread_count_ptr);

    jvmtiProfError(JNICALL* GetSampledThreadsData)(
            jvmtiProfEnv* jvmtiprof_env_ptr,
            jvmtiProfApplicationState* sample_data, jint max_thread_count,
            jvmtiProfSampledThreadState* threads_data_ptr, jint* count_ptr);

    jvmtiProfError(JNICALL* GetSampledHardwareCounters)(
            jvmtiProfEnv* jvmtiprof_env, jvmtiProfApplicationState* sample_data,
            jint processor_id, jint max_counters, uint64_t* counter_buffer_ptr,
            jint* count_ptr);

    jvmtiProfError(JNICALL* GetSampledSoftwareCounters)(
            jvmtiProfEnv* jvmtiprof_env, jvmtiProfApplicationState* sample_data,
            jint max_counters, uint64_t* counter_buffer_ptr, jint* count_ptr);

    /* Method Call Interception */

    jvmtiProfError(JNICALL* SetMethodEventFlag)(jvmtiProfEnv* jvmtiprof_env,
                                                const char* class_name,
                                                const char* method_name,
                                                const char* method_signature,
                                                jvmtiProfMethodEventFlag flags,
                                                jboolean enable,
                                                jint* hook_id_ptr);
};

struct jvmtiProfEnv_
{
    const struct jvmtiProfInterface_* functions;

#ifdef __cplusplus
    jvmtiProfError DisposeEnvironment()
    {
        return functions->DisposeEnvironment(this);
    }

    jvmtiProfError GetJvmtiEnv(jvmtiEnv** jvmti_env_ptr)
    {
        return functions->GetJvmtiEnv(this, jvmti_env_ptr);
    }

    jvmtiProfError GetVersionNumber(jint* version_ptr)
    {
        return functions->GetVersionNumber(this, version_ptr);
    }

    jvmtiProfError GetErrorName(jvmtiProfError error, char** name_ptr)
    {
        return functions->GetErrorName(this, error, name_ptr);
    }

    jvmtiProfError SetVerboseFlag(jvmtiProfVerboseFlag flag, jboolean value)
    {
        return functions->SetVerboseFlag(this, flag, value);
    }

    jvmtiProfError SetEventNotificationMode(jvmtiEventMode mode,
                                            jvmtiProfEvent event_type,
                                            jthread event_thread)
    {
        return functions->SetEventNotificationMode(this, mode, event_type,
                                                   event_thread);
    }

    jvmtiProfError SetEventCallbacks(const jvmtiProfEventCallbacks* callbacks,
                                     jint size_of_callbacks)
    {
        return functions->SetEventCallbacks(this, callbacks, size_of_callbacks);
    }

    jvmtiProfError GenerateEvents(jvmtiProfEvent event_type)
    {
        return functions->GenerateEvents(this, event_type);
    }

    jvmtiProfError
    GetPotentialCapabilities(jvmtiProfCapabilities* capabilities_ptr)
    {
        return functions->GetPotentialCapabilities(this, capabilities_ptr);
    }

    jvmtiProfError
    AddCapabilities(const jvmtiProfCapabilities* capabilities_ptr)
    {
        return functions->AddCapabilities(this, capabilities_ptr);
    }

    jvmtiProfError
    RelinquishCapabilities(const jvmtiProfCapabilities* capabilities_ptr)
    {
        return functions->RelinquishCapabilities(this, capabilities_ptr);
    }

    jvmtiProfError GetCapabilities(jvmtiProfCapabilities* capabilities_ptr)
    {
        return functions->GetCapabilities(this, capabilities_ptr);
    }

    jvmtiProfError SetExecutionSamplingInterval(jlong nanos_interval)
    {
        return functions->SetExecutionSamplingInterval(this, nanos_interval);
    }

    jvmtiProfError GetStackTraceAsync(jint depth, jint max_frame_count,
                                      jvmtiProfFrameInfo* frame_buffer,
                                      jint* count_ptr)
    {
        return functions->GetStackTraceAsync(this, depth, max_frame_count,
                                             frame_buffer, count_ptr);
    }

    jvmtiProfError GetProcessorCount(jint* processor_count_ptr)
    {
        return functions->GetProcessorCount(this, processor_count_ptr);
    }

    jvmtiProfError SetApplicationStateSamplingInterval(jlong nanos_interval)
    {
        return functions->SetApplicationStateSamplingInterval(this,
                                                              nanos_interval);
    }

    jvmtiProfError
    GetSampledCriticalSectionPressure(jvmtiProfApplicationState* sample_data,
                                      jdouble* csp_ptr)
    {
        return functions->GetSampledCriticalSectionPressure(this, sample_data,
                                                            csp_ptr);
    }

    jvmtiProfError GetSampledThreadCount(jvmtiProfApplicationState* sample_data,
                                         jint* thread_count_ptr)
    {
        return functions->GetSampledThreadCount(this, sample_data,
                                                thread_count_ptr);
    }

    jvmtiProfError GetSampledThreadsData(
            jvmtiProfApplicationState* sample_data, jint max_thread_count,
            jvmtiProfSampledThreadState* threads_data_ptr, jint* count_ptr)
    {
        return functions->GetSampledThreadsData(this, sample_data,
                                                max_thread_count,
                                                threads_data_ptr, count_ptr);
    }

    jvmtiProfError
    GetSampledHardwareCounters(jvmtiProfApplicationState* sample_data,
                               jint processor_id, jint max_counters,
                               uint64_t* counter_buffer_ptr, jint* count_ptr)
    {
        return functions->GetSampledHardwareCounters(
                this, sample_data, processor_id, max_counters,
                counter_buffer_ptr, count_ptr);
    }

    jvmtiProfError
    GetSampledSoftwareCounters(jvmtiProfApplicationState* sample_data,
                               jint max_counters, uint64_t* counter_buffer_ptr,
                               jint* count_ptr)
    {
        return functions->GetSampledSoftwareCounters(
                this, sample_data, max_counters, counter_buffer_ptr, count_ptr);
    }

    jvmtiProfError SetMethodEventFlag(const char* class_name,
                                      const char* method_name,
                                      const char* method_signature,
                                      jvmtiProfMethodEventFlag flags,
                                      jboolean enable,
                                      jint* hook_id_ptr)
    {
        return functions->SetMethodEventFlag(
                this, class_name, method_name, method_signature, 
                flags, enable, hook_id_ptr);
    }
#endif
};

/* Extension Injection Functions */

JVMTIPROF_IMPORT_OR_EXPORT
jvmtiProfError JNICALL jvmtiProf_Create(JavaVM* vm, jvmtiEnv* jvmti_env,
                                        jvmtiProfEnv** jvmtiprof_env_ptr,
                                        jvmtiProfVersion version);

JVMTIPROF_IMPORT_OR_EXPORT
jvmtiProfError JNICALL jvmtiProf_GetEnv(jvmtiEnv* jvmti_env,
                                        jvmtiProfEnv** jvmtiprof_env_ptr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* JVMTIPROF_JVMTIPROF_H */
