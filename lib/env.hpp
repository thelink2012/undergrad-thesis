#pragma once
#include "sampling_thread.hpp"
#include "execution_sampler.hpp"
#include "method_hooker.hpp"
#include <cstddef>
#include <jvmtiprof/jvmtiprof.h>
#include <memory>

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

    auto jvmti_env() -> jvmtiEnv& { return *m_jvmti_env; }

    auto is_valid() const -> bool;

    static auto to_jvmtiprof_error(jvmtiError jvmti_err) -> jvmtiProfError;

    auto phase() const -> jvmtiPhase { return m_phase; }

    auto allocate(jlong size, unsigned char*& mem) -> jvmtiProfError;

    auto deallocate(unsigned char* mem) -> jvmtiProfError;

    auto environment_local_storage() -> const void*
    {
        return m_jvmti_local_storage;
    }

    void set_environment_local_storage(const void* data)
    {
        m_jvmti_local_storage = data;
    }

    auto jni_env() -> JNIEnv*;

    void vm_start(JNIEnv* jni_env);
    void vm_init(JNIEnv* jni_env, jthread thread);
    void vm_death(JNIEnv* jni_env);
    void class_file_load_hook(JNIEnv* jni_env,
                              jclass class_being_redefined,
                              jobject loader,
                              const char* name,
                              jobject protection_domain,
                              jint class_data_len,
                              const unsigned char* class_data,
                              jint* new_class_data_len,
                              unsigned char** new_class_data);

    auto set_event_notification_mode(jvmtiEventMode mode, jvmtiEvent event_type,
                                     jthread event_thread) -> jvmtiError;

    auto set_event_callbacks(const jvmtiEventCallbacks* callbacks,
                             jint size_of_callbacks) -> jvmtiError;

    auto set_event_notification_mode(jvmtiEventMode mode,
                                     jvmtiProfEvent event_type,
                                     jthread event_thread) -> jvmtiProfError;

    auto set_event_callbacks(const jvmtiProfEventCallbacks* callbacks,
                             jint size_of_callbacks) -> jvmtiProfError;

    auto get_potential_capabilities(jvmtiProfCapabilities& capabilities) const
            -> jvmtiProfError;

    auto add_capabilities(const jvmtiProfCapabilities& capabilities)
            -> jvmtiProfError;

    auto relinquish_capabilities(const jvmtiProfCapabilities& capabilities)
            -> jvmtiProfError;

    auto get_capabilities(jvmtiProfCapabilities& capabilities) const
            -> jvmtiProfError;

    void refresh_capabilities(JNIEnv* jni_env);

    auto set_application_state_sampling_interval(jlong nanos_interval)
            -> jvmtiProfError;

    auto set_execution_sampling_interval(jlong nanos_interval)
            -> jvmtiProfError;

    auto set_method_event_flag(
            const char* class_name,
            const char* method_name,
            const char* method_signature,
            jvmtiProfMethodEventFlag flags,
            jboolean enable,
            jint* hook_id_ptr) -> jvmtiProfError;

    void post_application_state_sample();

    void post_execution_sample();

    void post_method_entry(jint hook_id);

    void post_method_exit(jint hook_id);

private:
    static void patch_jvmti_interface(jvmtiInterface_1&);
    auto intercepts_event(jvmtiEvent event_type) const -> bool;
    static auto compute_onload_capabilities() -> jvmtiProfCapabilities;
    static auto compute_always_capabilities() -> jvmtiProfCapabilities;

private:
    static const jvmtiProfInterface_ interface_1;
    static constexpr jint jvmtiprof_magic = 0x71EF;
    static constexpr jint dispose_magic = 0xDEFC;
    static constexpr jint bad_magic = 0xDEAD;

    /// Capabilities which are only available during the OnLoad phase.
    static const jvmtiProfCapabilities onload_capabilities;

    /// Capabilities which are available during any phase and can be enabled in
    /// multiple environments at once.
    static const jvmtiProfCapabilities always_capabilities;

    struct EventModes
    {
        bool vm_start_enabled_globally;
        bool vm_init_enabled_globally;
        bool vm_death_enabled_globally;
        bool class_file_load_hook_enabled_globally;
        bool sample_all_enabled_globally;
        bool sample_execution_enabled_globally;
        bool method_hook_enabled_globally;
    };

    struct EventCallbacks
    {
        jvmtiEventVMStart vm_start;
        jvmtiEventVMInit vm_init;
        jvmtiEventVMDeath vm_death;
        jvmtiEventClassFileLoadHook class_file_load_hook;
        jvmtiProfEventSampleApplicationState sample_all;
        jvmtiProfEventSampleExecution sample_execution;
        jvmtiProfEventSpecificMethodEntry method_entry;
        jvmtiProfEventSpecificMethodExit method_exit;
    };

    jvmtiProfEnv m_external;
    jint m_magic = jvmtiprof_magic;

    JavaVM* m_vm;

    jvmtiEnv* m_jvmti_env;
    const jvmtiInterface_1* m_original_jvmti_interface;
    jvmtiInterface_1 m_patched_jvmti_interface;

    const void* m_jvmti_local_storage{};

    jvmtiProfCapabilities m_capabilities{};

    EventCallbacks m_callbacks{};
    EventModes m_event_modes{};

    jvmtiPhase m_phase;

    std::unique_ptr<SamplingThread> m_sampling_thread;
    std::unique_ptr<ExecutionSampler> m_execution_sampler;
    std::unique_ptr<MethodHooker> m_method_hooker;

    // once enabled cannot be disabled
    bool m_has_control_of_class_file_load_hook_mode{false};
};
}
