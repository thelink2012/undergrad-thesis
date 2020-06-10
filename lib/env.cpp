#include "env.hpp"
#include <cstring>
#include <cassert>

// TODO check thread safety of impl functions

namespace
{
using profagent::ProfAgentEnvImpl;

void JNICALL vm_start(jvmtiEnv* jvmti_env, JNIEnv* jni_env)
{
    fprintf(stderr, "VMSTART\n");

    return ProfAgentEnvImpl::from_jvmti_env(*jvmti_env).vm_start(jni_env);
}

void JNICALL vm_init(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jthread thread)
{
    fprintf(stderr, "VMINIT\n");

    return ProfAgentEnvImpl::from_jvmti_env(*jvmti_env)
            .vm_init(jni_env, thread);
}

void JNICALL vm_death(jvmtiEnv* jvmti_env, JNIEnv* jni_env)
{
    fprintf(stderr, "VMDEATH\n");

    return ProfAgentEnvImpl::from_jvmti_env(*jvmti_env).vm_death(jni_env);
}

void JNICALL sample_consumer_thread(jvmtiEnv* jvmti_env, JNIEnv* jni_env, void* self)
{
    return static_cast<ProfAgentEnvImpl*>(self)->sample_consumer_thread();
}
}

namespace
{
struct EnvSingleton
{
    jvmtiEnv* jvmti_env;
    ProfAgentEnvImpl* jvmtiprof_env;
};
EnvSingleton env_singleton{};
}

namespace profagent
{
ProfAgentEnvImpl::ProfAgentEnvImpl(JavaVM& vm, jvmtiEnv& jvmti)
{
    static_assert(sizeof(*jvmti.functions) == sizeof(m_patched_jvmti_interface),
                  "Incompatible JVMTI interfaces");

    // FIXME
    assert(env_singleton.jvmti_env == nullptr);
    env_singleton.jvmti_env = &jvmti;
    env_singleton.jvmtiprof_env = this;

    jvmtiError jvmti_err;

    m_external.functions = &ProfAgentEnvImpl::interface_1;

    m_jvmti_env = &jvmti;

    m_original_jvmti_interface = jvmti.functions;
    m_patched_jvmti_interface = *m_original_jvmti_interface;
    patch_jvmti_interface(m_patched_jvmti_interface);
    jvmti.functions = &m_patched_jvmti_interface;

    jvmti_err = m_original_jvmti_interface->SetEventNotificationMode(
            &jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, nullptr);
    assert(jvmti_err == JVMTI_ERROR_NONE);

    jvmti_err = m_original_jvmti_interface->SetEventNotificationMode(
            &jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_START, nullptr);
    assert(jvmti_err == JVMTI_ERROR_NONE);

    jvmti_err = m_original_jvmti_interface->SetEventNotificationMode(
            &jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, nullptr);
    assert(jvmti_err == JVMTI_ERROR_NONE);

    jvmti_err = this->set_event_callbacks(
            static_cast<jvmtiEventCallbacks*>(nullptr), 0);
    assert(jvmti_err == JVMTI_ERROR_NONE);
}

ProfAgentEnvImpl::~ProfAgentEnvImpl()
{
    // TODO
}

auto ProfAgentEnvImpl::is_valid() const -> bool
{
    // TODO handle unaligned structure
    return m_magic == prof_agent_magic;
}

auto ProfAgentEnvImpl::from_jvmti_env(jvmtiEnv& jvmti_env) -> ProfAgentEnvImpl&
{
    ProfAgentEnvImpl* result{};
    jvmtiError jvmti_err = from_jvmti_env(jvmti_env, result);
    assert(jvmti_err == JVMTI_ERROR_NONE);
    return *result;
}

auto ProfAgentEnvImpl::from_jvmti_env(jvmtiEnv& jvmti_env,
                                      ProfAgentEnvImpl*& result) -> jvmtiError
{
    // TODO use a hash table with pointer hashing to allow multiple instances
    assert(env_singleton.jvmti_env == &jvmti_env);
    result = env_singleton.jvmtiprof_env;
    // may return JVMTI_ERROR_INVALID_ENVIRONMENT
    return JVMTI_ERROR_NONE;
}

auto ProfAgentEnvImpl::intercepts_event(jvmtiEvent event_type) const -> bool
{
    switch(event_type)
    {
        case JVMTI_EVENT_VM_START:
        case JVMTI_EVENT_VM_INIT:
        case JVMTI_EVENT_VM_DEATH:
            return true;
        default:
            return false;
    }
}

auto ProfAgentEnvImpl::set_event_notification_mode(jvmtiEventMode mode,
                                                   jvmtiEvent event_type,
                                                   jthread event_thread)
        -> jvmtiError
{
    if(!intercepts_event(event_type))
    {
        return m_original_jvmti_interface->SetEventNotificationMode(
                m_jvmti_env, mode, event_type, event_thread);
    }

    if(mode != JVMTI_ENABLE && mode != JVMTI_DISABLE)
        return JVMTI_ERROR_ILLEGAL_ARGUMENT;

    if(event_thread != nullptr)
        return JVMTI_ERROR_ILLEGAL_ARGUMENT;
    
    // TODO check JVMTI_ERROR_INVALID_THREAD and JVMTI_ERROR_THREAD_NOT_ALIVE
    // TODO check JVMTI_ERROR_MUST_POSSESS_CAPABILITY

    switch(event_type)
    {
        case JVMTI_EVENT_VM_START:
            m_event_modes.vm_start_enabled_globally = static_cast<bool>(mode);
            break;
        case JVMTI_EVENT_VM_INIT:
            m_event_modes.vm_init_enabled_globally = static_cast<bool>(mode);
            break;
        case JVMTI_EVENT_VM_DEATH:
            m_event_modes.vm_death_enabled_globally = static_cast<bool>(mode);
            break;
        default:
            assert(false);
            return JVMTI_ERROR_INTERNAL;
    }

    return JVMTI_ERROR_NONE;
}

auto ProfAgentEnvImpl::set_event_callbacks(const jvmtiEventCallbacks* callbacks,
                                           jint size_of_callbacks) -> jvmtiError
{
    if(size_of_callbacks < 0)
        return JVMTI_ERROR_ILLEGAL_ARGUMENT;

    // TODO explain this check (see code below it)
    if(size_of_callbacks > sizeof(jvmtiEventCallbacks))
        return JVMTI_ERROR_ILLEGAL_ARGUMENT;

    jvmtiEventCallbacks mut_callbacks;
    memset(&mut_callbacks, 0, sizeof(mut_callbacks));

    if(callbacks == nullptr)
    {
        m_callbacks.vm_init = nullptr;
        m_callbacks.vm_start = nullptr;
        m_callbacks.vm_death = nullptr;
    }
    else
    {
        memcpy(&mut_callbacks, callbacks, size_of_callbacks);

        m_callbacks.vm_start = mut_callbacks.VMStart;
        m_callbacks.vm_init = mut_callbacks.VMInit;
        m_callbacks.vm_death = mut_callbacks.VMDeath;
    }

    mut_callbacks.VMStart = ::vm_start;
    mut_callbacks.VMInit = ::vm_init;
    mut_callbacks.VMDeath = ::vm_death;

    return m_original_jvmti_interface->SetEventCallbacks(
            m_jvmti_env, &mut_callbacks, sizeof(mut_callbacks));
}

auto ProfAgentEnvImpl::set_event_notification_mode(ProfAgentEventMode mode,
                                                   ProfAgentEvent event_type,
                                                   jthread event_thread)
        -> ProfAgentError
{
    // TODO check phase?

    if(mode != PROFAGENT_ENABLE && mode != PROFAGENT_DISABLE)
        return PROFAGENT_ERROR_ILLEGAL_ARGUMENT;

    if(event_thread != nullptr)
        return PROFAGENT_ERROR_ILLEGAL_ARGUMENT;

    const bool mode_as_bool = (mode == PROFAGENT_ENABLE);

    switch(event_type)
    {
        case PROFAGENT_EVENT_TICK:
            m_event_modes.sample_all_enabled_globally = mode_as_bool;
            break;
        default:
            return PROFAGENT_ERROR_ILLEGAL_ARGUMENT;
    }

    return PROFAGENT_ERROR_NONE;
}

auto ProfAgentEnvImpl::set_event_callbacks(
        const ProfAgentEventCallbacks* callbacks, jint size_of_callbacks)
        -> ProfAgentError
{
    // TODO check phase?

    if(size_of_callbacks < 0)
        return PROFAGENT_ERROR_ILLEGAL_ARGUMENT;

    if(callbacks == nullptr)
    {
        m_callbacks.sample_all = nullptr;
    }
    else
    {
        if(size_of_callbacks != sizeof(ProfAgentEventCallbacks))
            return PROFAGENT_ERROR_ILLEGAL_ARGUMENT;

        m_callbacks.sample_all = callbacks->Tick;
    }

    return PROFAGENT_ERROR_NONE;
}

auto ProfAgentEnvImpl::add_capabilities(
        const ProfAgentCapabilities& capabilities) -> ProfAgentError
{
    // TODO check phase

    if(capabilities.can_generate_tick_events)
        m_capabilities.can_generate_tick_events = true;

    return PROFAGENT_ERROR_NONE;
}

void ProfAgentEnvImpl::vm_start(JNIEnv* jni_env)
{

    if(m_event_modes.vm_start_enabled_globally && m_callbacks.vm_start)
        return m_callbacks.vm_start(m_jvmti_env, jni_env);
}

void ProfAgentEnvImpl::vm_init(JNIEnv* jni_env, jthread thread)
{
    auto thread_class = jni_env->FindClass("java/lang/Thread");
    if(!thread_class)
    {
        // TODO error
    }

    // TODO actually give a name to the thread
    auto init_method_id = jni_env->GetMethodID(thread_class, "<init>", "()V");
    if(!init_method_id)
    {
        // TODO error
    }

    auto result = jni_env->NewObject(thread_class, init_method_id);
    if(!result)
    {
        // TODO error
    }

    // TODO which priority do I actually want?
    jvmtiError jvmti_err = m_jvmti_env->RunAgentThread(
            result, ::sample_consumer_thread, this, JVMTI_THREAD_MAX_PRIORITY);
    assert(jvmti_err == JVMTI_ERROR_NONE);

    if(m_event_modes.vm_init_enabled_globally && m_callbacks.vm_init)
        return m_callbacks.vm_init(m_jvmti_env, jni_env, thread);
}

void ProfAgentEnvImpl::vm_death(JNIEnv* jni_env)
{

    if(m_event_modes.vm_death_enabled_globally && m_callbacks.vm_death)
        return m_callbacks.vm_death(m_jvmti_env, jni_env);
}

void ProfAgentEnvImpl::sample_consumer_thread()
{
    puts("OA");
}
}
