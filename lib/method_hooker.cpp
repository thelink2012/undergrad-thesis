#include <jvmtiprof/jvmtiprof.h>
#include <jnif.hpp>
#include <cassert>
#include "method_hooker.hpp"
#include "env.hpp"
#include "native/JVMTIPROF.h"

// FIXME lots of race conditions

namespace
{
const uint8_t helper_classfile[] {
0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x34, 0x00, 0x10, 0x0a, 0x00, 0x03, 0x00, 0x0d, 0x07,
0x00, 0x0e, 0x07, 0x00, 0x0f, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e, 0x69, 0x74, 0x3e, 0x01, 0x00,
0x03, 0x28, 0x29, 0x56, 0x01, 0x00, 0x04, 0x43, 0x6f, 0x64, 0x65, 0x01, 0x00, 0x0f, 0x4c, 0x69,
0x6e, 0x65, 0x4e, 0x75, 0x6d, 0x62, 0x65, 0x72, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x01, 0x00, 0x0d,
0x6f, 0x6e, 0x4d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x45, 0x6e, 0x74, 0x72, 0x79, 0x01, 0x00, 0x04,
0x28, 0x4a, 0x29, 0x56, 0x01, 0x00, 0x0c, 0x6f, 0x6e, 0x4d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x45,
0x78, 0x69, 0x74, 0x01, 0x00, 0x0a, 0x53, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x46, 0x69, 0x6c, 0x65,
0x01, 0x00, 0x0e, 0x4a, 0x56, 0x4d, 0x54, 0x49, 0x50, 0x52, 0x4f, 0x46, 0x2e, 0x6a, 0x61, 0x76,
0x61, 0x0c, 0x00, 0x04, 0x00, 0x05, 0x01, 0x00, 0x09, 0x4a, 0x56, 0x4d, 0x54, 0x49, 0x50, 0x52,
0x4f, 0x46, 0x01, 0x00, 0x10, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f,
0x62, 0x6a, 0x65, 0x63, 0x74, 0x00, 0x21, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
0x03, 0x00, 0x01, 0x00, 0x04, 0x00, 0x05, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1d, 0x00,
0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x2a, 0xb7, 0x00, 0x01, 0xb1, 0x00, 0x00, 0x00, 0x01,
0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x09, 0x00, 0x08,
0x00, 0x09, 0x00, 0x00, 0x01, 0x09, 0x00, 0x0a, 0x00, 0x09, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0b,
0x00, 0x00, 0x00, 0x02, 0x00, 0x0c
};
}

extern "C"
{
JNIEXPORT
void JNICALL JavaCritical_JVMTIPROF_onMethodEntry
  (jlong hook_ptr)
{
    auto& hook = *reinterpret_cast<jvmtiprof::MethodHooker::HookInfo*>(hook_ptr);
    hook.jvmtiprof_env->post_method_entry(hook.hook_id);
}

JNIEXPORT
void JNICALL JavaCritical_JVMTIPROF_onMethodExit
  (jlong hook_ptr)
{
    auto& hook = *reinterpret_cast<jvmtiprof::MethodHooker::HookInfo*>(hook_ptr);
    hook.jvmtiprof_env->post_method_exit(hook.hook_id);
}

JNIEXPORT
void JNICALL Java_JVMTIPROF_onMethodEntry
  (JNIEnv *, jclass, jlong hook_ptr)
{
    JavaCritical_JVMTIPROF_onMethodEntry(hook_ptr);
}

JNIEXPORT
void JNICALL Java_JVMTIPROF_onMethodExit
  (JNIEnv *, jclass, jlong hook_ptr)
{
    JavaCritical_JVMTIPROF_onMethodExit(hook_ptr);
}
}

namespace jvmtiprof
{
/*
auto MethodHooker::add_capability() -> bool
{
    return false;
}

void MethodHooker::relinquish_capability()
{
}

auto MethodHooker::has_capability() -> bool
{
    return false;
}
*/

MethodHooker::MethodHooker(JvmtiProfEnv& jvmtiprof_env)
    : m_jvmtiprof_env(&jvmtiprof_env)
{
}

MethodHooker::~MethodHooker()
{
    // TODO
}

bool MethodHooker::define_helper_class(JNIEnv& jni_env)
{
    if(m_helper_class)
        return true;

    m_helper_class = jni_env.DefineClass(
            "JVMTIPROF", NULL,
            reinterpret_cast<const jbyte*>(helper_classfile),
            sizeof(helper_classfile));
    if(!m_helper_class)
    {
        // TODO check and clear exception
        return false;
    }
    return true;
}

auto MethodHooker::set_method_event_flag(
        const char* class_name,
        const char* method_name,
        const char* method_signature,
        jvmtiProfMethodEventFlag flags,
        jboolean enable,
        jint* hook_id_ptr) -> jvmtiProfError
{
    // TODO how to avoid race conditions with the hook code?

    auto* hook = this->create_or_find_hook(
                  class_name, method_name, method_signature);
    if(!hook)
        return JVMTIPROF_ERROR_INTERNAL;

    hook->flags = flags;
    hook->enabled = enable;

    *hook_id_ptr = hook->hook_id;
    return JVMTIPROF_ERROR_NONE;
}

auto MethodHooker::find_hook(
        const char* class_name,
        const char* method_name,
        const char* method_signature)
    -> HookInfo*
{
    for(auto& hook : m_hooks)
    {
        if(hook.method_name == method_name
            && hook.class_name == class_name
            && hook.method_signature == method_signature)
        {
            return &hook;
        }
    }
    return nullptr;
}

auto MethodHooker::create_or_find_hook(
        const char* class_name,
        const char* method_name,
        const char* method_signature)
    -> HookInfo*
{
    if(HookInfo* hook = this->find_hook(
               class_name, method_name, method_signature))
        return hook;

    const auto new_hook_id = m_hooks.size();
    m_hooks.push_back(HookInfo {
        m_jvmtiprof_env,
        class_name,
        method_name,
        method_signature,
        (jint)new_hook_id
    });
    auto& hook = m_hooks[new_hook_id];

    EachHookedClass* registered_class{};
    for(auto& xclass : m_hook_hierarchy)
    {
        assert(xclass.class_name != nullptr);
        if(*xclass.class_name == class_name) 
        {
            registered_class = &xclass;
            break;
        }
    }

    if(!registered_class)
    {
        m_hook_hierarchy.emplace_back();
        registered_class = &m_hook_hierarchy.back();
        registered_class->class_name = &hook.class_name;
    }

    EachHookedMethod* registered_method{};
    for(auto& xmethod : registered_class->methods)
    {
        assert(xmethod.method_name != nullptr);
        if(*xmethod.method_name == method_name) 
        {
            registered_method = &xmethod;
            break;
        }
    }

    if(!registered_method)
    {
        registered_class->methods.emplace_back();
        registered_method = &registered_class->methods.back();
        registered_method->method_name = &hook.method_name;
    }

    EachHookedSignature* registered_signature{};
    for(auto& xsignature : registered_method->signatures)
    {
        assert(xsignature.method_signature != nullptr);
        if(*xsignature.method_signature == method_signature) 
        {
            registered_signature = &xsignature;
            break;
        }
    }

    if(!registered_signature)
    {
        registered_method->signatures.emplace_back();
        registered_signature = &registered_method->signatures.back();
        registered_signature->method_signature = &hook.method_signature;
    }

    registered_signature->hook = &hook;
    return &hook;
}

void MethodHooker::class_file_load_hook(
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
    // TODO missing lots of error handling

    if(!name)
        return;

    EachHookedClass* hooked_class_info{};
    for(auto& xclass : m_hook_hierarchy)
    {
        assert(xclass.class_name != nullptr);
        if(*xclass.class_name == name)
        {
            hooked_class_info = &xclass;
            break;
        }
    }

    if(!hooked_class_info)
        return;

    // TODO could additionaly make sure that any method hook
    // under this class is enabled before further processing 

    jnif::parser::ClassFileParser cf(class_data, class_data_len);

    const auto helper_class_ref = cf.addClass("JVMTIPROF");
    const auto helper_entry_ref = cf.addMethodRef(
            helper_class_ref, "onMethodEntry", "(J)V");
    const auto helper_exit_ref = cf.addMethodRef(
            helper_class_ref, "onMethodExit", "(J)V");

    for(auto& method : cf.methods)
    {
        EachHookedMethod* hooked_method_info{};
        const char* method_name = method.getName();
        for(auto& xmethod : hooked_class_info->methods)
        {
            assert(xmethod.method_name != nullptr);
            if(*xmethod.method_name == method_name)
            {
                hooked_method_info = &xmethod;
                break;
            }
        }

        if(!hooked_method_info)
            continue;

        EachHookedSignature* hooked_signature_info{};
        const char* method_signature = method.getDesc();
        for(auto& xsignature : hooked_method_info->signatures)
        {
            assert(xsignature.method_signature != nullptr);
            if(*xsignature.method_signature == method_signature)
            {
                hooked_signature_info = &xsignature;
                break;
            }
        }

        if(!hooked_signature_info)
            continue;

        auto& hook = *hooked_signature_info->hook;

        // TODO check if pointer fits in long
        const auto hook_ptr_ref = cf.addLong(
                reinterpret_cast<long>(&hook));

        // TODO respect entry/exit flags
        // TODO respect enabled

        // TODO log interface
        fprintf(stderr, "instrumenting %s %s%s\n",
                name, method_name, method_signature);

        auto& il = method.instList();
        il.addInvoke(jnif::Opcode::invokestatic,
                     helper_entry_ref, *il.begin());
        il.addLdc(jnif::Opcode::ldc2_w,
                     hook_ptr_ref, *il.begin());
        
        for(auto* instruction : il)
        {
            if(instruction->isExit())
            {
                il.addLdc(jnif::Opcode::ldc2_w,
                             hook_ptr_ref, instruction);
                il.addInvoke(jnif::Opcode::invokestatic,
                             helper_exit_ref, instruction);
            }
        }
    }

    *new_class_data_len = cf.computeSize();
    if(m_jvmtiprof_env->allocate(
                *new_class_data_len,
                *new_class_data) != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to allocate new class data\n");
        *new_class_data_len = 0;
        *new_class_data = nullptr;
        return;
    }

    cf.write(*new_class_data, *new_class_data_len);

    /*
    FILE* f = fopen("x.class", "wb");
    fwrite(*new_class_data, *new_class_data_len, 1, f);
    fclose(f);
    */
}
}

