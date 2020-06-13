#include "env.hpp"
#include <cassert>
#include <initializer_list>

static_assert(static_cast<jint>(JVMTIPROF_SPECIFIC_ERROR_MIN)
                      > static_cast<jint>(JVMTI_ERROR_MAX),
              "jvmtiprof-specific error codes overlap with jvmti error codes.");

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
    PHASE_CHECK_IMPL(PROFAGENT_ERROR_WRONG_PHASE,                              \
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
using profagent::ProfAgentEnvImpl;

ProfAgentError JNICALL ProfAgent_Create(JavaVM* vm, jvmtiEnv* jvmti,
                                        ProfAgentEnv** env,
                                        ProfAgentVersion version)
{
    NULL_CHECK(vm, PROFAGENT_ERROR_NULL_POINTER);
    NULL_CHECK(jvmti, PROFAGENT_ERROR_NULL_POINTER);
    NULL_CHECK(env, PROFAGENT_ERROR_NULL_POINTER);

    if(version != PROFAGENT_VERSION_1_0)
        return PROFAGENT_ERROR_UNSUPPORTED_VERSION;

    jint jvmti_version;
    jvmtiError jvmti_err = jvmti->GetVersionNumber(&jvmti_version);
    assert(jvmti_err == JVMTI_ERROR_NONE);

    const auto jvmti_major_version = (jvmti_version & JVMTI_VERSION_MASK_MAJOR)
                                     >> JVMTI_VERSION_SHIFT_MAJOR;
    if(jvmti_major_version != 1)
    {
        *env = nullptr;
        return PROFAGENT_ERROR_UNSUPPORTED_VERSION;
    }

    jvmtiPhase current_phase;
    jvmti_err = jvmti->GetPhase(&current_phase);
    assert(jvmti_err == JVMTI_ERROR_NONE);
    if(current_phase != JVMTI_PHASE_ONLOAD)
    {
        *env = nullptr;
        return PROFAGENT_ERROR_WRONG_PHASE;
    }

    auto impl = new ProfAgentEnvImpl(*vm, *jvmti);
    *env = &impl->external();
    return PROFAGENT_ERROR_NONE;
}

void JNICALL ProfAgent_Destroy(ProfAgentEnv* env)
{
    assert(env != nullptr);
    ProfAgentEnvImpl& impl = ProfAgentEnvImpl::from_external(*env);
    PHASE_ANY(); // TODO is this actually true?
    delete &impl;
}

ProfAgentError JNICALL ProfAgent_GetEnv(jvmtiEnv* jvmti, ProfAgentEnv** env)
{
    NULL_CHECK(jvmti, PROFAGENT_ERROR_NULL_POINTER);
    NULL_CHECK(env, PROFAGENT_ERROR_NULL_POINTER);
    PHASE_ANY();

    ProfAgentEnvImpl* impl;
    if(ProfAgentEnvImpl::from_jvmti_env(*jvmti, impl) != JVMTI_ERROR_NONE)
        return PROFAGENT_ERROR_INVALID_ENVIRONMENT;

    *env = &impl->external();
    return PROFAGENT_ERROR_NONE;
}

static ProfAgentError JNICALL ProfAgentEnv_SetEventNotificationMode(
        ProfAgentEnv* env, ProfAgentEventMode mode, ProfAgentEvent event_type,
        jthread event_thread, ...)
{
    NULL_CHECK(env, PROFAGENT_ERROR_INVALID_ENVIRONMENT);
    // TODO phase check

    ProfAgentEnvImpl& impl = ProfAgentEnvImpl::from_external(*env);
    return impl.set_event_notification_mode(mode, event_type, event_thread);
}

static ProfAgentError JNICALL ProfAgentEnv_SetEventCallbacks(
        ProfAgentEnv* env, const ProfAgentEventCallbacks* callbacks,
        jint size_of_callbacks)
{
    NULL_CHECK(env, PROFAGENT_ERROR_INVALID_ENVIRONMENT);
    // TODO phase check

    ProfAgentEnvImpl& impl = ProfAgentEnvImpl::from_external(*env);
    return impl.set_event_callbacks(callbacks, size_of_callbacks);
}

static ProfAgentError JNICALL ProfAgentEnv_AddCapabilities(
        ProfAgentEnv* env, const ProfAgentCapabilities* capabilities_ptr)
{
    NULL_CHECK(env, PROFAGENT_ERROR_INVALID_ENVIRONMENT);
    NULL_CHECK(capabilities_ptr, PROFAGENT_ERROR_NULL_POINTER);
    // TODO phase check

    ProfAgentEnvImpl& impl = ProfAgentEnvImpl::from_external(*env);
    return impl.add_capabilities(*capabilities_ptr);
}

jvmtiError JNICALL jvmti_SetEventNotificationMode(jvmtiEnv* jvmti,
                                                  jvmtiEventMode mode,
                                                  jvmtiEvent event_type,
                                                  jthread event_thread, ...)
{
    NULL_CHECK(jvmti, JVMTI_ERROR_INVALID_ENVIRONMENT);
    // TODO efficient phase check

    ProfAgentEnvImpl* impl;
    jvmtiError jvmti_err = ProfAgentEnvImpl::from_jvmti_env(*jvmti, impl);
    if(jvmti_err != JVMTI_ERROR_NONE)
        return jvmti_err;

    return impl->set_event_notification_mode(mode, event_type, event_thread);
}

jvmtiError JNICALL jvmti_SetEventCallbacks(jvmtiEnv* jvmti,
                                           const jvmtiEventCallbacks* callbacks,
                                           jint size_of_callbacks)
{
    NULL_CHECK(jvmti, JVMTI_ERROR_INVALID_ENVIRONMENT);
    // TODO efficient phase check

    ProfAgentEnvImpl* impl;
    jvmtiError jvmti_err = ProfAgentEnvImpl::from_jvmti_env(*jvmti, impl);
    if(jvmti_err != JVMTI_ERROR_NONE)
        return jvmti_err;

    return impl->set_event_callbacks(callbacks, size_of_callbacks);
}
}

namespace profagent
{
const ProfAgentEnvInterface ProfAgentEnvImpl::interface_1 = {
        nullptr, /* DisposeEnvironment */
        nullptr, /* GetJvmtiEnv */
        nullptr, /* GetVersionNumber */
        nullptr, /* GetErrorName */
        nullptr, /* SetVerboseFlag */
        nullptr, /* SetEventNotificationMode */
        nullptr, /* SetEventCallbacks */
        nullptr, /* GenerateEvents */
        nullptr, /* GetPotentialCapabilities */
        nullptr, /* AddCapabilities */
        nullptr, /* RelinquishCapabilities */
        nullptr, /* GetCapabilities */
        nullptr, /* SetExecutionSamplingInterval */
        nullptr, /* GetStackTraceAsync */
        nullptr, /* GetProcessorCount */
        nullptr, /* SetApplicationStateSamplingInterval */
        nullptr, /* GetSampledCriticalSectionPressure */
        nullptr, /* GetSampledThreadCount */
        nullptr, /* GetSampledThreadsData */
        nullptr, /* GetSampledHardwareCounters */
        nullptr, /* GetSampledSoftwareCounters */
        nullptr, /* SetMethodEventFlag */
};

void ProfAgentEnvImpl::patch_jvmti_interface(jvmtiInterface_1& jvmti_interface)
{
    jvmti_interface.SetEventCallbacks = jvmti_SetEventCallbacks;
    jvmti_interface.SetEventNotificationMode = jvmti_SetEventNotificationMode;

    // TODO patch
    //
    // DisposeEnvironment
    //
    // Generate Events
    //
    // Get Potential Capabilities
    // Add Capabilities
    // Relinquish Capabilities
    // Get Capabilities
}
}
