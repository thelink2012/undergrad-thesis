#pragma once
#include <jvmtiprof/jvmtiprof.h>
#include <deque>
#include <vector>
#include <string>

namespace jvmtiprof
{
class JvmtiProfEnv;

class MethodHooker
{
public:
    static auto add_capability() -> bool;

    static void relinquish_capability();

    static auto has_capability() -> bool;

    explicit MethodHooker(JvmtiProfEnv& jvmtiprof_env);

    MethodHooker(const MethodHooker&) = delete;
    MethodHooker& operator=(const MethodHooker&) = delete;

    MethodHooker(MethodHooker&&) = delete;
    MethodHooker& operator=(MethodHooker&&) = delete;

    ~MethodHooker();

    auto set_method_event_flag(
            const char* class_name,
            const char* method_name,
            const char* method_signature,
            jvmtiProfMethodEventFlag flags,
            jboolean enable,
            jint* hook_id_ptr) -> jvmtiProfError;

protected:
    friend class JvmtiProfEnv;

    void class_file_load_hook(
            JNIEnv* jni_env,
            jclass class_being_redefined,
            jobject loader,
            const char* name,
            jobject protection_domain,
            jint class_data_len,
            const unsigned char* class_data,
            jint* new_class_data_len,
            unsigned char** new_class_data);

    bool define_helper_class(JNIEnv& jni_env);

public: // TODO protected and friend'ned
    struct HookInfo
    {
        JvmtiProfEnv* jvmtiprof_env;
        // TODO can be optimized to use an arena of strings.
        std::string class_name;
        std::string method_name;
        std::string method_signature;
        jint hook_id;
        jvmtiProfMethodEventFlag flags{};
        jboolean enabled{};
    };

protected:
    struct EachHookedSignature
    {
        std::string* method_signature;
        HookInfo* hook;
    };

    struct EachHookedMethod
    {
        std::string* method_name;
        std::vector<EachHookedSignature> signatures;
    };

    struct EachHookedClass
    {
        std::string* class_name;
        std::vector<EachHookedMethod> methods;
    };


    auto find_hook(
            const char* class_name,
            const char* method_name,
            const char* method_signature)
        -> HookInfo*;

    auto create_or_find_hook(
            const char* class_name,
            const char* method_name,
            const char* method_signature)
        -> HookInfo*;



private:
    JvmtiProfEnv* m_jvmtiprof_env;
    // using deque to avoid pointer invalidation
    std::deque<HookInfo> m_hooks;
    std::vector<EachHookedClass> m_hook_hierarchy;

    jclass m_helper_class{};
};
}
