#include "env.hpp"
#include <cassert>
#include <initializer_list>

static_assert(static_cast<jint>(JVMTIPROF_SPECIFIC_ERROR_MIN)
                      > static_cast<jint>(JVMTI_ERROR_MAX),
              "jvmtiprof-specific error codes overlap with jvmti error codes.");

#define JVMTIPROF_ERROR_NOT_IMPLEMENTED JVMTIPROF_ERROR_INTERNAL

#define NULL_CHECK(arg, result)                                                \
    do                                                                         \
    {                                                                          \
        if((arg) == nullptr)                                                   \
            return result;                                                     \
    } while(0)

#define PHASE_CHECK_IMPL(error_value, current_phase, ...)                      \
    do                                                                         \
    {                                                                          \
        bool in_correct_phase = false;                                         \
        decltype(auto) current_phase_eval = (current_phase);                   \
        for(auto x : std::initializer_list<jvmtiPhase>{__VA_ARGS__})           \
        {                                                                      \
            if(current_phase_eval == x)                                        \
            {                                                                  \
                in_correct_phase = true;                                       \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        if(!in_correct_phase)                                                  \
            return error_value;                                                \
    } while(0)

#define PHASE_CHECK(env_impl, ...)                                             \
    PHASE_CHECK_IMPL(JVMTIPROF_ERROR_WRONG_PHASE,                              \
                     env_impl.get_phase() __VA_ARGS__)

#define PHASE_CHECK_JVMTI(jvmti_env, ...)                                      \
    do                                                                         \
    {                                                                          \
        jvmtiPhase current_phase;                                              \
        jvmtiError jvmti_err = jvmti->GetPhase(&current_phase);                \
        assert(jvmti_err == JVMTI_ERROR_NONE);                                 \
        PHASE_CHECK_IMPL(JVMTI_ERROR_WRONG_PHASE, current_phase __VA_ARGS__)   \
    } while(0)

#define PHASE_ANY() ((void)0)

extern "C" {
using jvmtiprof::JvmtiProfEnv;

//
// jvmtiProf interface
//

jvmtiProfError JNICALL jvmtiProf_Create(JavaVM* vm, jvmtiEnv* jvmti,
                                        jvmtiProfEnv** env,
                                        jvmtiProfVersion version)
{
    NULL_CHECK(vm, JVMTIPROF_ERROR_NULL_POINTER);
    NULL_CHECK(jvmti, JVMTIPROF_ERROR_NULL_POINTER);
    NULL_CHECK(env, JVMTIPROF_ERROR_NULL_POINTER);

    if(version != JVMTIPROF_VERSION_1_0)
        return JVMTIPROF_ERROR_UNSUPPORTED_VERSION;

    // TODO(issues/1): This is not actually an effective way to test for
    // breaking changes in the JVMTI API since the major version is the same
    // between JVMTI and JDK since JDK 9.
    jint jvmti_version;
    jvmtiError jvmti_err = jvmti->GetVersionNumber(&jvmti_version);
    assert(jvmti_err == JVMTI_ERROR_NONE);
    const auto jvmti_major_version = (jvmti_version & JVMTI_VERSION_MASK_MAJOR)
                                     >> JVMTI_VERSION_SHIFT_MAJOR;
    if(jvmti_major_version != 1)
    {
        *env = nullptr;
        return JVMTIPROF_ERROR_UNSUPPORTED_VERSION;
    }

    // It's only possible to attach to a JVMTI environment during OnLoad
    // because the API client has likely invoked other JVMTI functions before
    // attaching which is prohibited by us but not enforceable. Additionally,
    // our internals require the capture of e.g. VMStart which happens just
    // after OnLoad.
    jvmtiPhase current_phase;
    jvmti_err = jvmti->GetPhase(&current_phase);
    assert(jvmti_err == JVMTI_ERROR_NONE);
    if(current_phase != JVMTI_PHASE_ONLOAD)
    {
        *env = nullptr;
        return JVMTIPROF_ERROR_WRONG_PHASE;
    }

    auto impl = new JvmtiProfEnv(*vm, *jvmti);
    *env = &impl->external();
    return JVMTIPROF_ERROR_NONE;
}

void JNICALL jvmtiProf_Destroy(jvmtiProfEnv* env)
{
    assert(env != nullptr);
    PHASE_ANY(); // TODO(thelink2012): is this actually true?
    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*env);
    delete &impl;
}

jvmtiProfError JNICALL jvmtiProf_GetEnv(jvmtiEnv* jvmti, jvmtiProfEnv** env)
{
    NULL_CHECK(jvmti, JVMTIPROF_ERROR_NULL_POINTER);
    NULL_CHECK(env, JVMTIPROF_ERROR_NULL_POINTER);
    PHASE_ANY();

    JvmtiProfEnv* impl;
    if(JvmtiProfEnv::from_jvmti_env(*jvmti, impl) != JVMTI_ERROR_NONE)
        return JVMTIPROF_ERROR_INVALID_ENVIRONMENT;

    *env = &impl->external();
    return JVMTIPROF_ERROR_NONE;
}

//
// Intercepted jvmti interface
//

static jvmtiError JNICALL jvmti_DisposeEnvironment(jvmtiEnv* jvmti)
{
    NULL_CHECK(jvmti, JVMTI_ERROR_INVALID_ENVIRONMENT);
    // TODO efficient phase check

    // Dispose of the jvmtiprof environment associated with the jvmti
    // environment. This will restore the function table, therefore calling
    // jvmti->DisposeEnvironment again invokes the upstream (probably
    // original) environment disposal function.
    {
        JvmtiProfEnv* impl;
        jvmtiError jvmti_err = JvmtiProfEnv::from_jvmti_env(*jvmti, impl);
        if(jvmti_err != JVMTI_ERROR_NONE)
            return jvmti_err;

        jvmtiProf_Destroy(&impl->external());
    }

    return jvmti->DisposeEnvironment();
}

static jvmtiError JNICALL jvmti_SetEventNotificationMode(jvmtiEnv* jvmti,
                                                         jvmtiEventMode mode,
                                                         jvmtiEvent event_type,
                                                         jthread event_thread,
                                                         ...)
{
    NULL_CHECK(jvmti, JVMTI_ERROR_INVALID_ENVIRONMENT);
    // TODO efficient phase check

    JvmtiProfEnv* impl;
    jvmtiError jvmti_err = JvmtiProfEnv::from_jvmti_env(*jvmti, impl);
    if(jvmti_err != JVMTI_ERROR_NONE)
        return jvmti_err;

    return impl->set_event_notification_mode(mode, event_type, event_thread);
}

static jvmtiError JNICALL
jvmti_SetEventCallbacks(jvmtiEnv* jvmti, const jvmtiEventCallbacks* callbacks,
                        jint size_of_callbacks)
{
    NULL_CHECK(jvmti, JVMTI_ERROR_INVALID_ENVIRONMENT);
    // TODO efficient phase check

    JvmtiProfEnv* impl;
    jvmtiError jvmti_err = JvmtiProfEnv::from_jvmti_env(*jvmti, impl);
    if(jvmti_err != JVMTI_ERROR_NONE)
        return jvmti_err;

    return impl->set_event_callbacks(callbacks, size_of_callbacks);
}

//
// jvmtiProfEnv General interface
//

static jvmtiProfError JNICALL
jvmtiProfEnv_DisposeEnvironment(jvmtiProfEnv* jvmtiprof_env)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL
jvmtiProfEnv_GetJvmtiEnv(jvmtiProfEnv* jvmtiprof_env, jvmtiEnv** jvmti_env_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL
jvmtiProfEnv_GetVersionNumber(jvmtiProfEnv* jvmtiprof_env, jint* version_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetErrorName(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfError error, char** name_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_SetVerboseFlag(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfVerboseFlag flag, jboolean value)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

//
// jvmtiProfEnv Event Management interface
//

static jvmtiProfError JNICALL jvmtiProfEnv_SetEventNotificationMode(
        jvmtiProfEnv* env, jvmtiEventMode mode, jvmtiProfEvent event_type,
        jthread event_thread, ...)
{
    NULL_CHECK(env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    // TODO phase check

    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*env);
    return impl.set_event_notification_mode(mode, event_type, event_thread);
}

static jvmtiProfError JNICALL jvmtiProfEnv_SetEventCallbacks(
        jvmtiProfEnv* env, const jvmtiProfEventCallbacks* callbacks,
        jint size_of_callbacks)
{
    NULL_CHECK(env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    // TODO phase check

    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*env);
    return impl.set_event_callbacks(callbacks, size_of_callbacks);
}

static jvmtiProfError JNICALL jvmtiProfEnv_GenerateEvents(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfEvent event_type)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

//
// jvmtiProfEnv Capability interface
//

static jvmtiProfError JNICALL jvmtiProfEnv_GetPotentialCapabilities(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfCapabilities* capabilities_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_AddCapabilities(
        jvmtiProfEnv* env, const jvmtiProfCapabilities* capabilities_ptr)
{
    NULL_CHECK(env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(capabilities_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    // TODO phase check

    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*env);
    return impl.add_capabilities(*capabilities_ptr);
}

static jvmtiProfError JNICALL jvmtiProfEnv_RelinquishCapabilities(
        jvmtiProfEnv* jvmtiprof_env,
        const jvmtiProfCapabilities* capabilities_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetCapabilities(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfCapabilities* capabilities_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

//
// jvmtiProfEnv Application Execution Sampling interface
//

static jvmtiProfError JNICALL jvmtiProfEnv_SetExecutionSamplingInterval(
        jvmtiProfEnv* jvmtiprof_env, jlong nanos_interval)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetStackTraceAsync(
        jvmtiProfEnv* jvmtiprof_env, jint depth, jint max_frame_count,
        jvmtiProfFrameInfo* frame_buffer, jint* count_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

//
// jvmtiProfEnv Application State Sampling interface
//

static jvmtiProfError JNICALL jvmtiProfEnv_GetProcessorCount(
        jvmtiProfEnv* jvmtiprof_env_ptr, jint* processor_count_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_SetApplicationStateSamplingInterval(
        jvmtiProfEnv* jvmtiprof_env, jlong nanos_interval)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetSampledCriticalSectionPressure(
        jvmtiProfEnv* jvmtiprof_env_ptr, jvmtiProfApplicationState* sample_data,
        jdouble* csp_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetSampledThreadCount(
        jvmtiProfEnv* jvmtiprof_env_ptr, jvmtiProfApplicationState* sample_data,
        jint* thread_count_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetSampledThreadsData(
        jvmtiProfEnv* jvmtiprof_env_ptr, jvmtiProfApplicationState* sample_data,
        jint max_thread_count, jvmtiProfSampledThreadState* threads_data_ptr,
        jint* count_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetSampledHardwareCounters(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfApplicationState* sample_data,
        jint processor_id, jint max_counters, uint64_t* counter_buffer_ptr,
        jint* count_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetSampledSoftwareCounters(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfApplicationState* sample_data,
        jint max_counters, uint64_t* counter_buffer_ptr, jint* count_ptr)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}

static jvmtiProfError JNICALL jvmtiProfEnv_SetMethodEventFlag(
        jvmtiProfEnv* jvmtiprof_env, jmethodID method_id,
        jvmtiProfMethodEventFlag flags, jboolean enable)
{
    return JVMTIPROF_ERROR_NOT_IMPLEMENTED;
}
}

namespace jvmtiprof
{
const jvmtiProfInterface_ JvmtiProfEnv::interface_1 = {
        jvmtiProfEnv_DisposeEnvironment,
        jvmtiProfEnv_GetJvmtiEnv,
        jvmtiProfEnv_GetVersionNumber,
        jvmtiProfEnv_GetErrorName,
        jvmtiProfEnv_SetVerboseFlag,
        jvmtiProfEnv_SetEventNotificationMode,
        jvmtiProfEnv_SetEventCallbacks,
        jvmtiProfEnv_GenerateEvents,
        jvmtiProfEnv_GetPotentialCapabilities,
        jvmtiProfEnv_AddCapabilities,
        jvmtiProfEnv_RelinquishCapabilities,
        jvmtiProfEnv_GetCapabilities,
        jvmtiProfEnv_SetExecutionSamplingInterval,
        jvmtiProfEnv_GetStackTraceAsync,
        jvmtiProfEnv_GetProcessorCount,
        jvmtiProfEnv_SetApplicationStateSamplingInterval,
        jvmtiProfEnv_GetSampledCriticalSectionPressure,
        jvmtiProfEnv_GetSampledThreadCount,
        jvmtiProfEnv_GetSampledThreadsData,
        jvmtiProfEnv_GetSampledHardwareCounters,
        jvmtiProfEnv_GetSampledSoftwareCounters,
        jvmtiProfEnv_SetMethodEventFlag,
};

void JvmtiProfEnv::patch_jvmti_interface(jvmtiInterface_1& jvmti_interface)
{
    jvmti_interface.DisposeEnvironment = jvmti_DisposeEnvironment;

    jvmti_interface.SetEventCallbacks = jvmti_SetEventCallbacks;
    jvmti_interface.SetEventNotificationMode = jvmti_SetEventNotificationMode;

    // TODO patch
    //
    // Get Potential Capabilities
    // Add Capabilities
    // Relinquish Capabilities
    // Get Capabilities
}
}
