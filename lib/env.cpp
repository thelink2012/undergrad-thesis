#include "env.hpp"
#include "bit.hpp"
#include <cassert>
#include <cstring>

// TODO check thread safety of impl functions
// TODO should unattached thread checking happen in every env enter?

namespace
{
using jvmtiprof::JvmtiProfEnv;

void JNICALL vm_start(jvmtiEnv* jvmti_env, JNIEnv* jni_env)
{
    fprintf(stderr, "VMSTART\n");

    return JvmtiProfEnv::from_jvmti_env(*jvmti_env).vm_start(jni_env);
}

void JNICALL vm_init(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jthread thread)
{
    fprintf(stderr, "VMINIT\n");

    return JvmtiProfEnv::from_jvmti_env(*jvmti_env).vm_init(jni_env, thread);
}

void JNICALL vm_death(jvmtiEnv* jvmti_env, JNIEnv* jni_env)
{
    fprintf(stderr, "VMDEATH\n");

    return JvmtiProfEnv::from_jvmti_env(*jvmti_env).vm_death(jni_env);
}
}

namespace
{
struct EnvSingleton
{
    jvmtiEnv* jvmti_env;
    JvmtiProfEnv* jvmtiprof_env;
};
EnvSingleton env_singleton{};
}

namespace jvmtiprof
{
const jvmtiProfCapabilities JvmtiProfEnv::onload_capabilities
        = JvmtiProfEnv::compute_onload_capabilities();

const jvmtiProfCapabilities JvmtiProfEnv::always_capabilities
        = JvmtiProfEnv::compute_always_capabilities();

const jvmtiProfCapabilities JvmtiProfEnv::always_solo_capabilities
        = JvmtiProfEnv::compute_always_solo_capabilities();

JvmtiProfEnv::JvmtiProfEnv(JavaVM& vm, jvmtiEnv& jvmti)
{
    static_assert(sizeof(*jvmti.functions) == sizeof(m_patched_jvmti_interface),
                  "Incompatible JVMTI interfaces");

    // FIXME
    assert(env_singleton.jvmti_env == nullptr);
    env_singleton.jvmti_env = &jvmti;
    env_singleton.jvmtiprof_env = this;

    jvmtiError jvmti_err;

    m_external.functions = &JvmtiProfEnv::interface_1;

    m_vm = &vm;

    m_jvmti_env = &jvmti;
    m_phase = JVMTI_PHASE_ONLOAD;

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

JvmtiProfEnv::~JvmtiProfEnv()
{
    // TODO
    // note that this may be called after death
    // see death for shutdown procedures otherwise
}

auto JvmtiProfEnv::is_valid() const -> bool
{
    // TODO handle unaligned structure
    return m_magic == jvmtiprof_magic;
}

auto JvmtiProfEnv::allocate(jlong size, unsigned char*& mem_ptr)
        -> jvmtiProfError
{
    assert(size >= 0);

    jvmtiError jvmti_err = m_jvmti_env->Allocate(size, &mem_ptr);
    if(jvmti_err != JVMTI_ERROR_NONE)
        return to_jvmtiprof_error(jvmti_err);

    return JVMTIPROF_ERROR_NONE;
}

auto JvmtiProfEnv::deallocate(unsigned char* mem) -> jvmtiProfError
{
    jvmtiError jvmti_err = m_jvmti_env->Deallocate(mem);
    if(jvmti_err != JVMTI_ERROR_NONE)
        return to_jvmtiprof_error(jvmti_err);

    return JVMTIPROF_ERROR_NONE;
}

auto JvmtiProfEnv::to_jvmtiprof_error(jvmtiError jvmti_err) -> jvmtiProfError
{
    // TODO(thelink2012):
    return JVMTIPROF_ERROR_INTERNAL;
}

auto JvmtiProfEnv::from_jvmti_env(jvmtiEnv& jvmti_env) -> JvmtiProfEnv&
{
    JvmtiProfEnv* result{};
    jvmtiError jvmti_err = from_jvmti_env(jvmti_env, result);
    assert(jvmti_err == JVMTI_ERROR_NONE);
    return *result;
}

auto JvmtiProfEnv::from_jvmti_env(jvmtiEnv& jvmti_env, JvmtiProfEnv*& result)
        -> jvmtiError
{
    // TODO use a hash table with pointer hashing to allow multiple instances
    assert(env_singleton.jvmti_env == &jvmti_env);
    result = env_singleton.jvmtiprof_env;
    // may return JVMTI_ERROR_INVALID_ENVIRONMENT
    return JVMTI_ERROR_NONE;
}

auto JvmtiProfEnv::intercepts_event(jvmtiEvent event_type) const -> bool
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

auto JvmtiProfEnv::set_event_notification_mode(jvmtiEventMode mode,
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

auto JvmtiProfEnv::set_event_callbacks(const jvmtiEventCallbacks* callbacks,
                                       jint size_of_callbacks) -> jvmtiError
{
    if(size_of_callbacks < 0)
        return JVMTI_ERROR_ILLEGAL_ARGUMENT;

    // TODO explain this check (see code below it)
    if(static_cast<size_t>(size_of_callbacks) > sizeof(jvmtiEventCallbacks))
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

auto JvmtiProfEnv::set_event_notification_mode(jvmtiEventMode mode,
                                               jvmtiProfEvent event_type,
                                               jthread event_thread)
        -> jvmtiProfError
{
    // TODO check phase?

    if(mode != JVMTI_ENABLE && mode != JVMTI_DISABLE)
        return JVMTIPROF_ERROR_ILLEGAL_ARGUMENT;

    if(event_thread != nullptr)
        return JVMTIPROF_ERROR_ILLEGAL_ARGUMENT;

    const bool mode_as_bool = (mode == JVMTI_ENABLE);

    switch(event_type)
    {
        case JVMTIPROF_EVENT_SAMPLE_APPLICATION_STATE:
            m_event_modes.sample_all_enabled_globally = mode_as_bool;
            break;
        default:
            return JVMTIPROF_ERROR_ILLEGAL_ARGUMENT;
    }

    return JVMTIPROF_ERROR_NONE;
}

auto JvmtiProfEnv::set_event_callbacks(const jvmtiProfEventCallbacks* callbacks,
                                       jint size_of_callbacks) -> jvmtiProfError
{
    // TODO check phase?

    if(size_of_callbacks < 0)
        return JVMTIPROF_ERROR_ILLEGAL_ARGUMENT;

    if(callbacks == nullptr)
    {
        m_callbacks.sample_all = nullptr;
    }
    else
    {
        if(size_of_callbacks != sizeof(jvmtiProfEventCallbacks))
            return JVMTIPROF_ERROR_ILLEGAL_ARGUMENT;

        m_callbacks.sample_all = callbacks->SampleApplicationState;
    }

    return JVMTIPROF_ERROR_NONE;
}

auto JvmtiProfEnv::compute_onload_capabilities() -> jvmtiProfCapabilities
{
    jvmtiProfCapabilities capabilities{};

    // Enabling the get of a stack trace in an synchronous manner requires
    // giving the VM certain capabilities (e.g. -XX:+DebugNonsafepoints)
    // during the OnLoad phase.
    /*capabilities.can_get_stack_trace_asynchronously = true;*/

    // The SpecificMethodEntry/Exit events can only be enabled during OnLoad
    // because it requires intercepting the class loading event (in addition
    // to the native method bind event). This would be a problem in anything
    // but OnLoad for two reasons: The first being that we'd need to retransform
    // classes in further stages, and the second is that since interception
    // of these JVMTI events will be necessary we need to keep track of
    // calls to event management API of the JVMTI environment, which
    // is complicated in further stages.
    // TODO(thelink2012): Implement retransformation on other stages?
    // TODO(thelink2012): Intercept in a more intelligent way?
    /*capabilities.can_generate_specific_method_entry_events = true;
    capabilities.can_generate_specific_method_exit_events = true;*/

    // The following samples can only be enabled during OnLoad because they
    // require interception of the monitor events. Since interception of
    // these JVMTI events will be necessary we need to keep track of calls to
    // the event management API of the JVMTI environment, which is complicated
    // in further stages.
    // TODO(thelink2012): Intercept in a more intelligent way?
    /*capabilities.can_sample_critical_section_pressure = true;
    capabilities.can_sample_thread_state = true;
    capabilities.can_sample_thread_processor = true;*/

    return capabilities;
}

auto JvmtiProfEnv::compute_always_capabilities() -> jvmtiProfCapabilities
{
    jvmtiProfCapabilities capabilities{};

    // Sampling the application state is always available since this capability
    // is nothing but the spawning of a thread. It's related capabilities (e.g.
    // critical section pressure) however are an entirely diferent beast.
    capabilities.can_generate_sample_application_state_events = true;

    // Sampling of hardware and software counters can be enabled/disabled at
    // any moment since those are unrelated to the VM but the OS/CPU.
    /*capabilities.can_sample_hardware_counters = true;
    capabilities.can_sample_software_counters = true;*/

    return capabilities;
}

auto JvmtiProfEnv::compute_always_solo_capabilities() -> jvmtiProfCapabilities
{
    jvmtiProfCapabilities capabilities{};

    // Sampling execution uses OS functionality (timers and signals) therefore
    // it can be enabled/disabled at any point in time. Although, it can only
    // be enabled in a single agent a time.
    /*capabilities.can_generate_sample_execution_events = true;*/

    return capabilities;
}

auto JvmtiProfEnv::get_capabilities(jvmtiProfCapabilities& capabilities) const
        -> jvmtiProfError
{
    // NOTE: This may be called from any phase, operations here must avoid
    // touching the VM.
    capabilities = m_capabilities;
    return JVMTIPROF_ERROR_NONE;
}

auto JvmtiProfEnv::get_potential_capabilities(
        jvmtiProfCapabilities& capabilities) const -> jvmtiProfError
{
    // The set of currently possessed capabilities is certainly available
    // and is our starting point for the potential capabilities.
    jvmtiProfError jvmtiprof_err = get_capabilities(capabilities);
    assert(jvmtiprof_err == JVMTIPROF_ERROR_NONE);

    capabilities = bitwise_or(capabilities, always_capabilities);

    if(m_phase == JVMTI_PHASE_ONLOAD)
        capabilities = bitwise_or(capabilities, onload_capabilities);

    // TODO(thelink2012): always_solo_capabilities must be checked manually

    return JVMTIPROF_ERROR_NONE;
}

auto JvmtiProfEnv::add_capabilities(const jvmtiProfCapabilities& capabilities)
        -> jvmtiProfError
{
    // TODO(thelink2012): There is a possibility of a race condition here (OS
    // related, we cannot mutex) where the potential capabilities tell us a
    // solo capability (e.g. sample execution events) is available, but in the
    // mean time we go to enable and refresh, the capability is no longer
    // available. What should we do in such a case?

    JNIEnv* jni_env{};
    if(m_phase == JVMTI_PHASE_LIVE)
    {
        if((jni_env = this->jni_env()) == nullptr)
            return JVMTIPROF_ERROR_UNATTACHED_THREAD;
    }

    jvmtiProfCapabilities potential_capabilities;

    jvmtiProfError jvmtiprof_err = get_potential_capabilities(
            potential_capabilities);
    assert(jvmtiprof_err == JVMTIPROF_ERROR_NONE);
    potential_capabilities = bitwise_and(potential_capabilities, capabilities);

    // Ensure that prohibited capabilities are not being set.
    // Careful! XORing only produces the expected result because we have
    // ANDed `potential_capabilities` with `capabilities` above.
    if(has_any_bit(bitwise_xor(capabilities, potential_capabilities)))
        return JVMTIPROF_ERROR_NOT_AVAILABLE;

    m_capabilities = bitwise_or(m_capabilities, capabilities);
    refresh_capabilities(jni_env);

    return JVMTIPROF_ERROR_NONE;
}

auto JvmtiProfEnv::relinquish_capabilities(
        const jvmtiProfCapabilities& capabilities) -> jvmtiProfError
{
    JNIEnv* jni_env{};
    if(m_phase == JVMTI_PHASE_LIVE)
    {
        if((jni_env = this->jni_env()) == nullptr)
            return JVMTIPROF_ERROR_UNATTACHED_THREAD;
    }

    // Can only relinquish capabilities that are always present.
    jvmtiProfCapabilities relinquishable_capabilities = bitwise_or(
            always_capabilities, always_solo_capabilities);

    // Can only relinquish possessed capabilities.
    relinquishable_capabilities = bitwise_and(relinquishable_capabilities,
                                              m_capabilities);

    // Careful! XORing only produces the expected result because
    // `relinquishable_capabilities` has been ANDed with `m_capabilities` above.
    m_capabilities = bitwise_xor(m_capabilities, relinquishable_capabilities);
    refresh_capabilities(jni_env);

    return JVMTIPROF_ERROR_NONE;
}

void JvmtiProfEnv::refresh_capabilities(JNIEnv* jni_env)
{
    const auto is_vm_live = (m_phase == JVMTI_PHASE_LIVE);

    if(is_vm_live)
        assert(jni_env != nullptr);

    if(m_capabilities.can_generate_sample_application_state_events)
    {
        if(m_sampling_thread == nullptr)
        {
            m_sampling_thread = std::make_unique<SamplingThread>(*m_jvmti_env,
                                                                 *this);
            // When the VM is not yet live, the `vm_init` event is responsible
            // for starting the sampling thread. Otherwise, we must do so.
            if(is_vm_live)
            {
                assert(jni_env != nullptr);
                m_sampling_thread->start(*jni_env);
            }
        }
    }
    else
    {
        if(m_sampling_thread != nullptr)
        {
            if(is_vm_live)
                m_sampling_thread->stop_and_join(*jni_env);
            m_sampling_thread.reset();
        }
    }
}

auto JvmtiProfEnv::jni_env() -> JNIEnv*
{
    JNIEnv* env;

    // TODO(thelink2012): Provide a faster implementation that caches this.

    jint jni_err = m_vm->GetEnv(reinterpret_cast<void**>(&env),
                                JNI_VERSION_1_6);
    if(jni_err == JNI_OK)
        return env;
    else if(jni_err == JNI_EDETACHED)
        return nullptr;
    else
    {
        // TODO(thelink2012): Log failure, should not happen. The only other
        // error case is JNI_EVERSION. We should probably test this during
        // construction?
        return nullptr;
    }
}

auto JvmtiProfEnv::set_application_state_sampling_interval(jlong nanos_interval)
        -> jvmtiProfError
{
    assert(nanos_interval >= 0);

    if(!m_capabilities.can_generate_sample_application_state_events)
        return JVMTIPROF_ERROR_MUST_POSSESS_CAPABILITY;

    assert(m_sampling_thread != nullptr);
    m_sampling_thread->set_sampling_interval(nanos_interval);

    return JVMTIPROF_ERROR_NONE;
}

void JvmtiProfEnv::post_application_state_sample()
{
    // TODO(thelink2012):

    if(m_capabilities.can_generate_sample_application_state_events
       && m_callbacks.sample_all)
    {
        // TODO(thelink2012): JNIEnv
        // TODO(thelink2012): sampling data
        m_callbacks.sample_all(&external(), m_jvmti_env, nullptr, nullptr);
    }
}

void JvmtiProfEnv::vm_start(JNIEnv* jni_env)
{
    m_phase = JVMTI_PHASE_START;

    if(m_event_modes.vm_start_enabled_globally && m_callbacks.vm_start)
        m_callbacks.vm_start(m_jvmti_env, jni_env);
}

void JvmtiProfEnv::vm_init(JNIEnv* jni_env, jthread thread)
{
    m_phase = JVMTI_PHASE_LIVE;

    if(m_capabilities.can_generate_sample_application_state_events)
    {
        assert(m_sampling_thread != nullptr);
        m_sampling_thread->start(*jni_env);
    }
    else
    {
        assert(m_sampling_thread == nullptr);
    }

    if(m_event_modes.vm_init_enabled_globally && m_callbacks.vm_init)
        m_callbacks.vm_init(m_jvmti_env, jni_env, thread);
}

void JvmtiProfEnv::vm_death(JNIEnv* jni_env)
{
    if(m_capabilities.can_generate_sample_application_state_events)
    {
        assert(m_sampling_thread != nullptr);
        m_sampling_thread->stop_and_join(*jni_env);
        // TODO(thelink2012): Should reset the unique_ptr?
    }
    else
    {
        assert(m_sampling_thread == nullptr);
    }

    if(m_event_modes.vm_death_enabled_globally && m_callbacks.vm_death)
        m_callbacks.vm_death(m_jvmti_env, jni_env);

    // TODO(thelink2012): RAII wrapper for this
    m_phase = JVMTI_PHASE_DEAD;

    // TODO(thelink2012): Maybe we should dettach from the JVMTI env here?
}

// TODO do not spawn sampling thead if does not have capability
}
