#include "env.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>

//
// Statically checked assertions for the API
//

static_assert(static_cast<jint>(JVMTIPROF_SPECIFIC_ERROR_MIN)
                      > static_cast<jint>(JVMTI_ERROR_MAX),
              "jvmtiprof-specific error codes overlap with jvmti error codes.");

//
// Macros for making the API enter more declarative
//

#define JVMTIPROF_ERROR_NOT_IMPLEMENTED JVMTIPROF_ERROR_INTERNAL

/// Checks whether `arg` isn't `nullptr` as a pre-condition. If `nullptr`,
/// returns `error_code`.
#define NULL_CHECK(arg, error_code)                                            \
    do                                                                         \
    {                                                                          \
        if((arg) == nullptr)                                                   \
            return (error_code);                                               \
    } while(0)

/// Checks for `condition` as a pre-condition. If not satisfied, returns
/// `error_code
#define CHECK(condition, error_code)                                           \
    do                                                                         \
    {                                                                          \
        if(!(condition))                                                       \
            return (error_code);                                               \
    } while(0)

/// Checks whether `jvmti_env` is attached to an jvmtiprof environment (as a
/// pre-condition) and returns its implementation in `env_impl_ptr`. Otherwise,
/// returns an error code.
#define ENV_FROM_JVMTI(jvmti_env, env_impl_ptr)                                \
    do                                                                         \
    {                                                                          \
        jvmtiError jvmti_err = JvmtiProfEnv::from_jvmti_env((jvmti_env),       \
                                                            (env_impl_ptr));   \
        if(jvmti_err != JVMTI_ERROR_NONE)                                      \
            return jvmti_err;                                                  \
    } while(0)

/// Checks whether `current_phase` is any of `...` as a pre-condition. If not,
/// returns `error_value`.
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
            return (error_value);                                              \
    } while(0)

/// Checks whether the current phase (as registered by `env_impl` is any of
/// `...` as a pre-condition. If not, returns `JVMTIPROF_ERROR_WRONG_PHASE`.
#define PHASE_CHECK(env_impl, ...)                                             \
    PHASE_CHECK_IMPL(JVMTIPROF_ERROR_WRONG_PHASE,                              \
                     (env_impl).phase(), __VA_ARGS__)

/// Checks whether the current phase (as registered by `env_impl` is any of
/// `...` as a pre-condition. If not, returns `JVMTI_ERROR_WRONG_PHASE`.
#define PHASE_CHECK_JVMTI(env_impl, ...)                                       \
    PHASE_CHECK_IMPL(JVMTI_ERROR_WRONG_PHASE, (env_impl).phase(), __VA_ARGS__)

/// Declares that the API function can be called from any phase.
#define PHASE_ANY() ((void)0)

/// Helper for converting an error code to its string equivalent.
#define CASE_ERROR(error_code)                                                 \
    case error_code:                                                           \
    {                                                                          \
        result = #error_code;                                                  \
        len = sizeof(#error_code) - 1;                                         \
        break;                                                                 \
    }

//
// Helper functions
//

namespace
{
/// Converts `jvmtiProfError` to `jvmtiError` if possible, otherwise returns
/// `JVMTI_ERROR_INTERNAL`.
auto to_jvmti_error(jvmtiProfError jvmtiprof_err) -> jvmtiError
{
    jint err = static_cast<jint>(jvmtiprof_err);
    if(err >= JVMTI_ERROR_NONE && err <= JVMTI_ERROR_MAX)
        return static_cast<jvmtiError>(err);
    return JVMTI_ERROR_INTERNAL;
}

/// Converts `jvmtiProfError` to a null-terminated C string.
auto to_zstring(jvmtiProfError jvmtiprof_err, const char*& result, size_t& len)
        -> jvmtiProfError
{
    result = nullptr;
    len = 0;

    switch(jvmtiprof_err)
    {
        CASE_ERROR(JVMTIPROF_ERROR_NONE)
        CASE_ERROR(JVMTIPROF_ERROR_NULL_POINTER)
        CASE_ERROR(JVMTIPROF_ERROR_INVALID_ENVIRONMENT)
        CASE_ERROR(JVMTIPROF_ERROR_WRONG_PHASE)
        CASE_ERROR(JVMTIPROF_ERROR_INTERNAL)
        CASE_ERROR(JVMTIPROF_ERROR_ILLEGAL_ARGUMENT)
        CASE_ERROR(JVMTIPROF_ERROR_UNSUPPORTED_VERSION)
        CASE_ERROR(JVMTIPROF_ERROR_NOT_AVAILABLE)
        CASE_ERROR(JVMTIPROF_ERROR_MUST_POSSESS_CAPABILITY)
        CASE_ERROR(JVMTIPROF_ERROR_UNATTACHED_THREAD)

        // for satisfying -Wswitch
        case JVMTIPROF_SPECIFIC_ERROR_MIN:
        case JVMTIPROF_ERROR_MAX:
            break;
    }

    // Guard for default values (i.e. not part of jvmtiProfError).
    // Do not put the default check in the switch because it would suppress
    // exhaustive check of -Wswitch.
    if(result == nullptr)
        return JVMTIPROF_ERROR_ILLEGAL_ARGUMENT;

    return JVMTIPROF_ERROR_NONE;
}
}

// end of helper functions

extern "C" {
using jvmtiprof::JvmtiProfEnv;

//
// jvmtiProf interface
//

jvmtiProfError JNICALL jvmtiProf_Create(JavaVM* vm, jvmtiEnv* jvmti_env,
                                        jvmtiProfEnv** jvmtiprof_env_ptr,
                                        jvmtiProfVersion version)
{
    NULL_CHECK(vm, JVMTIPROF_ERROR_NULL_POINTER);
    NULL_CHECK(jvmti_env, JVMTIPROF_ERROR_NULL_POINTER);
    NULL_CHECK(jvmtiprof_env_ptr, JVMTIPROF_ERROR_NULL_POINTER);

    if(version != JVMTIPROF_VERSION_1_0)
        return JVMTIPROF_ERROR_UNSUPPORTED_VERSION;

    // TODO(issues/1): This is not actually an effective way to test for
    // breaking changes in the JVMTI API since the major version is the same
    // between JVMTI and JDK since JDK 9.
    jint jvmti_version;
    jvmtiError jvmti_err = jvmti_env->GetVersionNumber(&jvmti_version);
    assert(jvmti_err == JVMTI_ERROR_NONE);
    const auto jvmti_major_version = (jvmti_version & JVMTI_VERSION_MASK_MAJOR)
                                     >> JVMTI_VERSION_SHIFT_MAJOR;
    if(jvmti_major_version != 1)
    {
        *jvmtiprof_env_ptr = nullptr;
        return JVMTIPROF_ERROR_UNSUPPORTED_VERSION;
    }

    // It's only possible to attach to a JVMTI environment during OnLoad
    // because the API client has likely invoked other JVMTI functions before
    // attaching which is prohibited by us but not enforceable. Additionally,
    // our internals require the capture of e.g. VMStart which happens just
    // after OnLoad.
    jvmtiPhase current_phase;
    jvmti_err = jvmti_env->GetPhase(&current_phase);
    assert(jvmti_err == JVMTI_ERROR_NONE);
    if(current_phase != JVMTI_PHASE_ONLOAD)
    {
        *jvmtiprof_env_ptr = nullptr;
        return JVMTIPROF_ERROR_WRONG_PHASE;
    }

    auto impl = new JvmtiProfEnv(*vm, *jvmti_env);
    *jvmtiprof_env_ptr = &impl->external();
    return JVMTIPROF_ERROR_NONE;
}

jvmtiProfError JNICALL jvmtiProf_GetEnv(jvmtiEnv* jvmti_env,
                                        jvmtiProfEnv** jvmtiprof_env_ptr)
{
    NULL_CHECK(jvmti_env, JVMTIPROF_ERROR_NULL_POINTER);
    NULL_CHECK(jvmtiprof_env_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    PHASE_ANY();

    JvmtiProfEnv* impl;
    if(JvmtiProfEnv::from_jvmti_env(*jvmti_env, impl) != JVMTI_ERROR_NONE)
        return JVMTIPROF_ERROR_INVALID_ENVIRONMENT;

    *jvmtiprof_env_ptr = &impl->external();
    return JVMTIPROF_ERROR_NONE;
}

//
// Intercepted jvmti interface
//

static jvmtiError JNICALL jvmti_DisposeEnvironment(jvmtiEnv* jvmti_env)
{
    JvmtiProfEnv* impl;
    NULL_CHECK(jvmti_env, JVMTI_ERROR_INVALID_ENVIRONMENT);
    ENV_FROM_JVMTI(*jvmti_env, impl);
    PHASE_ANY(); // TODO(thelink2012): Is this actually true?
                 // (see jvmtiProfEnv_DisposeEnvironment)

    // Dispose of the jvmtiprof environment associated with the jvmti
    // environment. This will restore the jvmti function table, therefore
    // calling jvmti->DisposeEnvironment again invokes the upstream (probably
    // original) environment disposal function.
    jvmtiProfError jvmtiprof_err = impl->external().DisposeEnvironment();
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
        return to_jvmti_error(jvmtiprof_err);
    else
        impl = nullptr;

    return jvmti_env->DisposeEnvironment();
}

static jvmtiError JNICALL jvmti_SetEventNotificationMode(jvmtiEnv* jvmti_env,
                                                         jvmtiEventMode mode,
                                                         jvmtiEvent event_type,
                                                         jthread event_thread,
                                                         ...)
{
    JvmtiProfEnv* impl;
    NULL_CHECK(jvmti_env, JVMTI_ERROR_INVALID_ENVIRONMENT);
    ENV_FROM_JVMTI(*jvmti_env, impl);
    PHASE_CHECK_JVMTI(*impl, JVMTI_PHASE_ONLOAD, JVMTI_PHASE_LIVE);

    return impl->set_event_notification_mode(mode, event_type, event_thread);
}

static jvmtiError JNICALL jvmti_SetEventCallbacks(
        jvmtiEnv* jvmti_env, const jvmtiEventCallbacks* callbacks,
        jint size_of_callbacks)
{
    JvmtiProfEnv* impl;
    NULL_CHECK(jvmti_env, JVMTI_ERROR_INVALID_ENVIRONMENT);
    ENV_FROM_JVMTI(*jvmti_env, impl);
    PHASE_CHECK_JVMTI(*impl, JVMTI_PHASE_ONLOAD, JVMTI_PHASE_LIVE);

    return impl->set_event_callbacks(callbacks, size_of_callbacks);
}

//
// jvmtiProfEnv General interface
//

static jvmtiProfError JNICALL
jvmtiProfEnv_DisposeEnvironment(jvmtiProfEnv* jvmtiprof_env)
{
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    PHASE_ANY(); // TODO(thelink2012): Is this actually true?

    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*jvmtiprof_env);
    delete &impl;

    return JVMTIPROF_ERROR_NONE;
}

static jvmtiProfError JNICALL
jvmtiProfEnv_GetJvmtiEnv(jvmtiProfEnv* jvmtiprof_env, jvmtiEnv** jvmti_env_ptr)
{
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(jvmti_env_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    PHASE_ANY();

    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*jvmtiprof_env);
    *jvmti_env_ptr = &impl.jvmti_env();

    return JVMTIPROF_ERROR_NONE;
}

static jvmtiProfError JNICALL
jvmtiProfEnv_GetVersionNumber(jvmtiProfEnv* jvmtiprof_env, jint* version_ptr)
{
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(version_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    PHASE_ANY();

    *version_ptr = JVMTIPROF_VERSION;

    return JVMTIPROF_ERROR_NONE;
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetErrorName(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfError error, char** name_ptr)
{
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(name_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    PHASE_ANY();

    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*jvmtiprof_env);

    const char* error_string;
    size_t error_len;
    jvmtiProfError jvmtiprof_err = to_zstring(error, error_string, error_len);
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
        return jvmtiprof_err;

    jvmtiprof_err = impl.allocate(error_len + 1,
                                  *reinterpret_cast<unsigned char**>(name_ptr));
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
        return jvmtiprof_err;

    memcpy(*name_ptr, error_string, error_len);
    (*name_ptr)[error_len] = '\0';

    return JVMTIPROF_ERROR_NONE;
}

static jvmtiProfError JNICALL jvmtiProfEnv_SetVerboseFlag(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfVerboseFlag flag, jboolean value)
{
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    PHASE_ANY();

    // Since there's no constant in the jvmtiProfVerboseFlag any call to this
    // method necessarily contains an invalid value for `flag`.
    return JVMTIPROF_ERROR_ILLEGAL_ARGUMENT;
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
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(capabilities_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*jvmtiprof_env);
    PHASE_CHECK(impl, JVMTI_PHASE_ONLOAD, JVMTI_PHASE_LIVE);
    return impl.get_potential_capabilities(*capabilities_ptr);
}

static jvmtiProfError JNICALL
jvmtiProfEnv_AddCapabilities(jvmtiProfEnv* jvmtiprof_env,
                             const jvmtiProfCapabilities* capabilities_ptr)
{
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(capabilities_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*jvmtiprof_env);
    PHASE_CHECK(impl, JVMTI_PHASE_ONLOAD, JVMTI_PHASE_LIVE);
    return impl.add_capabilities(*capabilities_ptr);
}

static jvmtiProfError JNICALL jvmtiProfEnv_RelinquishCapabilities(
        jvmtiProfEnv* jvmtiprof_env,
        const jvmtiProfCapabilities* capabilities_ptr)
{
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(capabilities_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*jvmtiprof_env);
    PHASE_CHECK(impl, JVMTI_PHASE_ONLOAD, JVMTI_PHASE_LIVE);
    return impl.relinquish_capabilities(*capabilities_ptr);
}

static jvmtiProfError JNICALL jvmtiProfEnv_GetCapabilities(
        jvmtiProfEnv* jvmtiprof_env, jvmtiProfCapabilities* capabilities_ptr)
{
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(capabilities_ptr, JVMTIPROF_ERROR_NULL_POINTER);
    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*jvmtiprof_env);
    PHASE_ANY();
    return impl.get_capabilities(*capabilities_ptr);
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
    NULL_CHECK(jvmtiprof_env, JVMTIPROF_ERROR_INVALID_ENVIRONMENT);
    CHECK(nanos_interval >= 0, JVMTIPROF_ERROR_ILLEGAL_ARGUMENT);

    JvmtiProfEnv& impl = JvmtiProfEnv::from_external(*jvmtiprof_env);
    return impl.set_application_state_sampling_interval(nanos_interval);
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
    // Get Potential Capabilities
    // Add Capabilities
    // Relinquish Capabilities
    // Get Capabilities
}
}

// TODO(thelink2012): JvmtiProfEnv's phase check doesn't contain the primordial
// phase. Look into the implications of this and if necessary fix it somehow.
