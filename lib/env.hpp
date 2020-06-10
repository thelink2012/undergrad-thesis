#pragma once
#include <profagent/profagent.h>
#include <cstddef>

namespace profagent
{
class ProfAgentEnvImpl
{
public:
    ProfAgentEnvImpl(JavaVM& vm, jvmtiEnv& jvmti);
    ~ProfAgentEnvImpl();
    
    ProfAgentEnvImpl(const ProfAgentEnvImpl&) = delete;
    ProfAgentEnvImpl& operator=(const ProfAgentEnvImpl&) = delete;

    ProfAgentEnvImpl(ProfAgentEnvImpl&&) = delete;
    ProfAgentEnvImpl& operator=(ProfAgentEnvImpl&&) = delete;

    auto external() -> ProfAgentEnv& { return m_external; }

    static auto from_external(ProfAgentEnv& external) -> ProfAgentEnvImpl&
    {
        return *reinterpret_cast<ProfAgentEnvImpl*>(
                reinterpret_cast<uintptr_t>(&external)
                - offsetof(ProfAgentEnvImpl, m_external));
    }

    static auto from_jvmti_env(jvmtiEnv& jvmti_env) -> ProfAgentEnvImpl&;

    static auto from_jvmti_env(jvmtiEnv& jvmti_env, ProfAgentEnvImpl*& result)
            -> jvmtiError;

    auto is_valid() const -> bool;

    auto environment_local_storage() -> const void*
    {
        return m_jvmti_local_storage;
    }

    void set_environment_local_storage(const void* data)
    {
        m_jvmti_local_storage = data;
    }

    void vm_start(JNIEnv* jni_env);
    void vm_init(JNIEnv* jni_env, jthread thread);
    void vm_death(JNIEnv* jni_env);

    auto set_event_notification_mode(jvmtiEventMode mode, jvmtiEvent event_type,
                                     jthread event_thread) -> jvmtiError;

    auto set_event_callbacks(const jvmtiEventCallbacks* callbacks,
                             jint size_of_callbacks) -> jvmtiError;

    auto set_event_notification_mode(ProfAgentEventMode mode, ProfAgentEvent event_type,
                                     jthread event_thread) -> ProfAgentError;

    auto set_event_callbacks(const ProfAgentEventCallbacks* callbacks,
                             jint size_of_callbacks) -> ProfAgentError;

    auto add_capabilities(const ProfAgentCapabilities& capabilities)
            -> ProfAgentError;
    
    // TODO private
    void sample_consumer_thread();

private:
    static void patch_jvmti_interface(jvmtiInterface_1&);
    auto intercepts_event(jvmtiEvent event_type) const -> bool;

private:
    static const ProfAgentEnvInterface interface_1;
    static constexpr jint prof_agent_magic = 0x71EF;
    static constexpr jint dispose_magic = 0xDEFC;
    static constexpr jint bad_magic = 0xDEAD;

    struct EventModes
    {
        bool vm_start_enabled_globally;
        bool vm_init_enabled_globally;
        bool vm_death_enabled_globally;
        bool sample_all_enabled_globally;
    };
    
    struct EventCallbacks
    {
        jvmtiEventVMStart vm_start;
        jvmtiEventVMInit vm_init;
        jvmtiEventVMDeath vm_death;
        ProfAgentEventTick sample_all;
    };

    ProfAgentEnv m_external;
    jint m_magic = prof_agent_magic;

    jvmtiEnv* m_jvmti_env;
    const jvmtiInterface_1* m_original_jvmti_interface;
    jvmtiInterface_1 m_patched_jvmti_interface;

    const void* m_jvmti_local_storage{};

    ProfAgentCapabilities m_capabilities{};

    EventCallbacks m_callbacks{};
    EventModes m_event_modes{};
};
}
