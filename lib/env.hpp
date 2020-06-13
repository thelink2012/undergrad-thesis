#pragma once
#include <jvmtiprof/jvmtiprof.h>
#include <cstddef>

namespace jvmtiprof
{
class JvmtiProfEnv
{
public:
    JvmtiProfEnv(JavaVM& vm, jvmtiEnv& jvmti);
    ~JvmtiProfEnv();
    
    JvmtiProfEnv(const JvmtiProfEnv&) = delete;
    JvmtiProfEnv& operator=(const JvmtiProfEnv&) = delete;

    JvmtiProfEnv(JvmtiProfEnv&&) = delete;
    JvmtiProfEnv& operator=(JvmtiProfEnv&&) = delete;

    auto external() -> jvmtiProfEnv& { return m_external; }

    static auto from_external(jvmtiProfEnv& external) -> JvmtiProfEnv&
    {
        return *reinterpret_cast<JvmtiProfEnv*>(
                reinterpret_cast<uintptr_t>(&external)
                - offsetof(JvmtiProfEnv, m_external));
    }

    static auto from_jvmti_env(jvmtiEnv& jvmti_env) -> JvmtiProfEnv&;

    static auto from_jvmti_env(jvmtiEnv& jvmti_env, JvmtiProfEnv*& result)
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

    auto set_event_notification_mode(jvmtiEventMode mode, jvmtiProfEvent event_type,
                                     jthread event_thread) -> jvmtiProfError;

    auto set_event_callbacks(const jvmtiProfEventCallbacks* callbacks,
                             jint size_of_callbacks) -> jvmtiProfError;

    auto add_capabilities(const jvmtiProfCapabilities& capabilities)
            -> jvmtiProfError;
    
    // TODO private
    void sample_consumer_thread();

private:
    static void patch_jvmti_interface(jvmtiInterface_1&);
    auto intercepts_event(jvmtiEvent event_type) const -> bool;

private:
    static const jvmtiProfInterface_ interface_1;
    static constexpr jint jvmtiprof_magic = 0x71EF;
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
        jvmtiProfEventSampleApplicationState sample_all;
    };

    jvmtiProfEnv m_external;
    jint m_magic = jvmtiprof_magic;

    jvmtiEnv* m_jvmti_env;
    const jvmtiInterface_1* m_original_jvmti_interface;
    jvmtiInterface_1 m_patched_jvmti_interface;

    const void* m_jvmti_local_storage{};

    jvmtiProfCapabilities m_capabilities{};

    EventCallbacks m_callbacks{};
    EventModes m_event_modes{};
};
}
