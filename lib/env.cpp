#include "env.hpp"
#include "bit.hpp"
#include <cassert>
#include <cstring>

// TODO check thread safety of impl functions
// TODO should unattached thread checking happen in every env enter?
// TODO take care of event callback invoking DisposeEnvironment (defer delete)

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

void JNICALL class_file_load_hook(jvmtiEnv *jvmti_env,
                                  JNIEnv* jni_env,
                                  jclass class_being_redefined,
                                  jobject loader,
                                  const char* name,
                                  jobject protection_domain,
                                  jint class_data_len,
                                  const unsigned char* class_data,
                                  jint* new_class_data_len,
                                  unsigned char** new_class_data)
{

    return JvmtiProfEnv::from_jvmti_env(*jvmti_env)
        .class_file_load_hook(jni_env, class_being_redefined, loader,
                              name, protection_domain, class_data_len,
                              class_data, new_class_data_len, new_class_data);
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
        case JVMTI_EVENT_CLASS_FILE_LOAD_HOOK:
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
        case JVMTI_EVENT_CLASS_FILE_LOAD_HOOK:
            // TODO per-thread
            m_event_modes.class_file_load_hook_enabled_globally = static_cast<bool>(mode);
            if(!m_has_control_of_class_file_load_hook_mode)
                return m_original_jvmti_interface->SetEventNotificationMode(
                        m_jvmti_env, mode, event_type, event_thread);
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

    // TODO callback setting must be an atomic operation! data races ahead.

    jvmtiEventCallbacks mut_callbacks;
    memset(&mut_callbacks, 0, sizeof(mut_callbacks));

    if(callbacks == nullptr)
    {
        m_callbacks.vm_init = nullptr;
        m_callbacks.vm_start = nullptr;
        m_callbacks.vm_death = nullptr;
        m_callbacks.class_file_load_hook = nullptr;
    }
    else
    {
        memcpy(&mut_callbacks, callbacks, size_of_callbacks);

        m_callbacks.vm_start = mut_callbacks.VMStart;
        m_callbacks.vm_init = mut_callbacks.VMInit;
        m_callbacks.vm_death = mut_callbacks.VMDeath;
        m_callbacks.class_file_load_hook = mut_callbacks.ClassFileLoadHook;
    }

    mut_callbacks.VMStart = ::vm_start;
    mut_callbacks.VMInit = ::vm_init;
    mut_callbacks.VMDeath = ::vm_death;
    mut_callbacks.ClassFileLoadHook = ::class_file_load_hook;

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

    // TODO(thelink2012): Per-thread sample_execution

    const bool mode_as_bool = (mode == JVMTI_ENABLE);

    switch(event_type)
    {
        case JVMTIPROF_EVENT_SAMPLE_APPLICATION_STATE:
        {
            if(!m_capabilities.can_generate_sample_application_state_events)
                return JVMTIPROF_ERROR_MUST_POSSESS_CAPABILITY;
            m_event_modes.sample_all_enabled_globally = mode_as_bool;
            break;
        }
        case JVMTIPROF_EVENT_SAMPLE_EXECUTION:
        {
            if(!m_capabilities.can_generate_sample_execution_events)
                return JVMTIPROF_ERROR_MUST_POSSESS_CAPABILITY;
            m_event_modes.sample_execution_enabled_globally = mode_as_bool;
            break;
        }
        case JVMTIPROF_EVENT_SPECIFIC_METHOD_ENTRY:
        case JVMTIPROF_EVENT_SPECIFIC_METHOD_EXIT:
        {
            if(!m_capabilities.can_generate_specific_method_entry_events
                    || !m_capabilities.can_generate_specific_method_exit_events)
                return JVMTIPROF_ERROR_MUST_POSSESS_CAPABILITY;
            m_event_modes.method_hook_enabled_globally = mode_as_bool;
            break;
        }
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
        m_callbacks.sample_execution = nullptr;
        m_callbacks.method_entry = nullptr;
        m_callbacks.method_exit = nullptr;
    }
    else
    {
        if(size_of_callbacks != sizeof(jvmtiProfEventCallbacks))
            return JVMTIPROF_ERROR_ILLEGAL_ARGUMENT;

        m_callbacks.sample_all = callbacks->SampleApplicationState;
        m_callbacks.sample_execution = callbacks->SampleExecution;
        m_callbacks.method_entry = callbacks->SpecificMethodEntry;
        m_callbacks.method_exit = callbacks->SpecificMethodExit;
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
    capabilities.can_generate_specific_method_entry_events = true;
    capabilities.can_generate_specific_method_exit_events = true;

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

    // Sampling execution uses OS functionality (timers and signals) therefore
    // it can be enabled/disabled at any point in time.
    capabilities.can_generate_sample_execution_events = true;

    // Sampling of hardware and software counters can be enabled/disabled at
    // any moment since those are unrelated to the VM but the OS/CPU.
    /*capabilities.can_sample_hardware_counters = true;
    capabilities.can_sample_software_counters = true;*/

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

    return JVMTIPROF_ERROR_NONE;
}

auto JvmtiProfEnv::add_capabilities(const jvmtiProfCapabilities& capabilities)
        -> jvmtiProfError
{
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

    // Can only relinquish capabilities that are always disableable.
    jvmtiProfCapabilities relinquishable_capabilities = always_capabilities;

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

    if(m_capabilities.can_generate_sample_execution_events)
    {
        if(m_execution_sampler == nullptr)
        {
            if(ExecutionSampler::add_capability())
            {
                m_execution_sampler = std::make_unique<ExecutionSampler>(*this);

                // When the VM is not yet live, the `vm_init` event is responsible
                // for starting the sampling timer. Otherwise, we must do so.
                if(is_vm_live)
                    m_execution_sampler->start();
            }
            else
            {
                m_capabilities.can_generate_sample_execution_events = false;
            }
        }
    }
    else
    {
        if(m_execution_sampler != nullptr)
        {
            if(is_vm_live)
                m_execution_sampler->stop();
            m_execution_sampler.reset();
            ExecutionSampler::relinquish_capability();
        }
    }

    if(m_capabilities.can_generate_specific_method_entry_events
            || m_capabilities.can_generate_specific_method_exit_events)
    {
        if(!m_has_control_of_class_file_load_hook_mode)
        {
            jvmtiCapabilities caps;
            memset(&caps, 0, sizeof(caps));
            caps.can_generate_all_class_hook_events = true;
            // TODO we don't hook AddCapabilities at the client side, as such
            // they can disable capabilities we enable. fix it!
            if(m_original_jvmti_interface->AddCapabilities(m_jvmti_env, &caps))
            {
                // TODO report failure through logging interface
                fprintf(stderr, "failed to add can_generate_all_class_hook_events\n");
            }
            else
            {
                if(m_original_jvmti_interface->SetEventNotificationMode(
                            m_jvmti_env,
                            JVMTI_ENABLE,
                            JVMTI_EVENT_CLASS_FILE_LOAD_HOOK,
                            NULL))
                {
                    // TODO report failure through logging interface
                    fprintf(stderr, "failed to set JVMTI_EVENT_CLASS_FILE_LOAD_HOOK\n");
                }
                else
                {
                    m_has_control_of_class_file_load_hook_mode = true;
                }
            }
        }

        if(m_has_control_of_class_file_load_hook_mode
            && !m_method_hooker)
        {
            m_method_hooker = std::make_unique<MethodHooker>(*this);
        }
    }
    else
    {
        // TODO
    }
}

auto JvmtiProfEnv::set_method_event_flag(
        const char* class_name,
        const char* method_name,
        const char* method_signature,
        jvmtiProfMethodEventFlag flags,
        jboolean enable,
        jint* hook_id_ptr) -> jvmtiProfError
{
    if(!m_capabilities.can_generate_specific_method_entry_events
            || !m_capabilities.can_generate_specific_method_exit_events
            || !m_method_hooker)
        return JVMTIPROF_ERROR_MUST_POSSESS_CAPABILITY;

    // TODO check validity of parameters

    return m_method_hooker->set_method_event_flag(
            class_name, method_name, method_signature,
            flags, enable, hook_id_ptr);
}

auto JvmtiProfEnv::jni_env() -> JNIEnv*
{
    JNIEnv* env;

    // TODO(thelink2012): Provide a faster implementation that caches this.
    // Possible soltuion: TLS. But it is not exactly async-signal-safe. But should it be?

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

auto JvmtiProfEnv::set_execution_sampling_interval(jlong nanos_interval)
        -> jvmtiProfError
{
    assert(nanos_interval >= 0);

    if(!m_capabilities.can_generate_sample_execution_events)
        return JVMTIPROF_ERROR_MUST_POSSESS_CAPABILITY;

    assert(m_execution_sampler != nullptr);
    m_execution_sampler->set_sampling_interval(nanos_interval);

    return JVMTIPROF_ERROR_NONE;
}

void JvmtiProfEnv::post_application_state_sample()
{
    // NOTE: This method must be simple enough to be async-signal safe.

    // TODO(thelink2012): Race condition?

    if(m_capabilities.can_generate_sample_application_state_events
       && m_event_modes.sample_all_enabled_globally && m_callbacks.sample_all)
    {
        // TODO(thelink2012): JNIEnv
        // TODO(thelink2012): sampling data
        m_callbacks.sample_all(&external(), m_jvmti_env, nullptr, nullptr);
    }
}

void JvmtiProfEnv::post_execution_sample()
{
    // TODO(thelink2012): Race condition?

    if(m_capabilities.can_generate_sample_execution_events
       && m_event_modes.sample_execution_enabled_globally
       && m_callbacks.sample_execution)
    {
        // TODO(thelink2012): JNIEnv
        // TODO(thelink2012): jthread
        m_callbacks.sample_execution(&external(), m_jvmti_env, nullptr,
                                     nullptr);
    }
}

void JvmtiProfEnv::post_method_entry(jint hook_id)
{
    // TODO not checking for capabilities and modes.
    // unhooking should happen at the MethodHooker
    // by removing the hook from the bytecode

    // TODO pass jni_env(), but first must optimize it
    // TODO pass jthread

    if(m_callbacks.method_entry)
        m_callbacks.method_entry(&external(), m_jvmti_env, nullptr, nullptr, hook_id);
}

void JvmtiProfEnv::post_method_exit(jint hook_id)
{
    // TODO see comment above

    // TODO pass jni_env(), but first must optimize it
    // TODO pass jthread

    if(m_callbacks.method_exit)
        m_callbacks.method_exit(&external(), m_jvmti_env, nullptr, nullptr, hook_id);
}

void JvmtiProfEnv::vm_start(JNIEnv* jni_env)
{
    m_phase = JVMTI_PHASE_START;

    if(m_method_hooker)
    {
        // TODO move to a MethodHooker::vm_start
        if(!m_method_hooker->define_helper_class(*jni_env))
            puts("failure!!!!");
    }

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

    if(m_capabilities.can_generate_sample_execution_events)
    {
        assert(m_execution_sampler != nullptr);
        m_execution_sampler->start();
    }
    else
    {
        assert(m_execution_sampler == nullptr);
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

    if(m_capabilities.can_generate_sample_execution_events)
    {
        assert(m_execution_sampler != nullptr);
        m_execution_sampler->stop();
        // TODO(thelink2012): Should reset the unique_ptr?
    }
    else
    {
        assert(m_execution_sampler == nullptr);
    }

    if(m_event_modes.vm_death_enabled_globally && m_callbacks.vm_death)
        m_callbacks.vm_death(m_jvmti_env, jni_env);

    // TODO(thelink2012): RAII wrapper for this
    m_phase = JVMTI_PHASE_DEAD;

    // TODO(thelink2012): Maybe we should dettach from the JVMTI env here?
}

void JvmtiProfEnv::class_file_load_hook(
        JNIEnv* jni_env,
        jclass class_being_redefined,
        jobject loader,
        const char* name,
        jobject protection_domain,
        jint class_data_len,
        const unsigned char* class_data,
        jint* new_class_data_len,
        unsigned char** new_class_data)
{
    unsigned char* hooker_class_data{nullptr};
    jint hooker_class_data_len{-1};

    if(m_method_hooker)
    {
        m_method_hooker->class_file_load_hook(
                jni_env, class_being_redefined,
                loader, name, protection_domain,
                class_data_len, class_data,
                &hooker_class_data_len, &hooker_class_data);

        if(hooker_class_data != nullptr)
        {
            class_data = hooker_class_data;
            class_data_len = hooker_class_data_len;
        }
    }

    if(m_event_modes.class_file_load_hook_enabled_globally
            && m_callbacks.class_file_load_hook)
    {
        m_callbacks.class_file_load_hook(
                m_jvmti_env, jni_env, class_being_redefined,
                loader, name, protection_domain,
                class_data_len, class_data,
                new_class_data_len, new_class_data);
    }

    if(*new_class_data == nullptr && hooker_class_data)
    {
        *new_class_data = hooker_class_data;
        *new_class_data_len = hooker_class_data_len;
    }
}

// TODO do not spawn sampling thead if does not have capability
}
