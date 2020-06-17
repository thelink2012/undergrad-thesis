#pragma once
#include <cassert>
#include <exception> // for std::terminate
#include <jni.h>

namespace jvmtiprof
{
namespace detail
{
/// Provides RAII ownership of a JNI reference.
///
/// Please use `jvmtiprof::JNILocalRef`, `jvmtiprof::JNIGlobalRef` or
/// `jvmtiprof::JNIWeakGlobalRef` instead.
template<typename T, typename Traits>
class JNIBasicRef
{
public:
    /// Constructs a wrapper that owns no JNI reference.
    JNIBasicRef() = default;

    /// Constructs a wrapper that owns the same object as `other_ref`.
    template<typename OtherTraits>
    JNIBasicRef(JNIEnv& jni_env, const JNIBasicRef<T, OtherTraits>& other_ref);

    JNIBasicRef(const JNIBasicRef&) = delete;
    JNIBasicRef& operator=(const JNIBasicRef&) = delete;

    JNIBasicRef(JNIBasicRef&&) noexcept;
    JNIBasicRef& operator=(JNIBasicRef&&) noexcept;

    /// Terminates the program if the wrapper still owns a JNI reference.
    ~JNIBasicRef();

    /// Deletes the JNI reference.
    void reset(JNIEnv& jni_env) noexcept;

    /// Unwraps the JNI reference from this wrapper.
    auto release() noexcept -> T;

    /// Gets the wrapped value.
    auto get() const noexcept -> T;

    /// Checks whether any reference is being managed by this wrapper.
    explicit operator bool() const noexcept;

    auto operator==(std::nullptr_t) const noexcept -> bool;
    auto operator!=(std::nullptr_t) const noexcept -> bool;

protected:
    explicit JNIBasicRef(T obj);

private:
    T m_handle{};
};

/// Provides JNI local reference semantics for `JNIBasicRef`.
struct JNILocalRefTraits
{
    static constexpr bool is_weak_ref = false;

    static auto new_ref(JNIEnv& jni_env, jobject obj) -> jobject
    {
        return jni_env.NewLocalRef(obj);
    }

    static void delete_ref(JNIEnv& jni_env, jobject obj)
    {
        return jni_env.DeleteLocalRef(obj);
    }
};

/// Provides JNI global reference semantics for `JNIBasicRef`.
struct JNIGlobalRefTraits
{
    static constexpr bool is_weak_ref = false;

    static auto new_ref(JNIEnv& jni_env, jobject obj) -> jobject
    {
        return jni_env.NewGlobalRef(obj);
    }

    static void delete_ref(JNIEnv& jni_env, jobject obj)
    {
        return jni_env.DeleteGlobalRef(obj);
    }
};

/// Provides JNI weak global reference semantics for `JNIBasicRef`.
struct JNIWeakGlobalRefTraits
{
    static constexpr bool is_weak_ref = true;

    static auto new_ref(JNIEnv& jni_env, jweak obj) -> jweak
    {
        return jni_env.NewWeakGlobalRef(obj);
    }

    static void delete_ref(JNIEnv& jni_env, jweak obj)
    {
        return jni_env.DeleteWeakGlobalRef(obj);
    }
};

template<typename T, typename Traits>
template<typename OtherTraits>
inline JNIBasicRef<T, Traits>::JNIBasicRef(
        JNIEnv& jni_env, const JNIBasicRef<T, OtherTraits>& other_ref)
{
    if(other_ref)
    {
        m_handle = static_cast<T>(Traits::new_ref(jni_env, other_ref.get()));

        // In case the other reference is weak, it may have been collected,
        // as such the new reference is `nullptr`. In other cases, the returned
        // reference cannot be null.
        if(!OtherTraits::is_weak_ref)
            assert(m_handle != nullptr);
    }
}

template<typename T, typename Traits>
inline JNIBasicRef<T, Traits>::JNIBasicRef(T obj) : m_handle(obj)
{}

template<typename T, typename Traits>
inline JNIBasicRef<T, Traits>::JNIBasicRef(JNIBasicRef&& rhs) noexcept :
    m_handle(rhs.m_handle)
{
    rhs.m_handle = nullptr;
}

template<typename T, typename Traits>
inline auto JNIBasicRef<T, Traits>::operator=(JNIBasicRef&& rhs) noexcept
        -> JNIBasicRef&
{
    m_handle = rhs.m_handle;
    rhs.m_handle = nullptr;
    return *this;
}

template<typename T, typename Traits>
inline JNIBasicRef<T, Traits>::~JNIBasicRef()
{
    if(m_handle != nullptr)
    {
        // TODO(thelink2012): log error
        std::terminate();
    }
}

template<typename T, typename Traits>
inline void JNIBasicRef<T, Traits>::reset(JNIEnv& jni_env) noexcept
{
    if(m_handle != nullptr)
    {
        Traits::delete_ref(jni_env, m_handle);
        m_handle = nullptr;
    }
}

template<typename T, typename Traits>
inline auto JNIBasicRef<T, Traits>::release() noexcept -> T
{
    const auto old_handle = m_handle;
    m_handle = nullptr;
    return old_handle;
}

template<typename T, typename Traits>
inline auto JNIBasicRef<T, Traits>::get() const noexcept -> T
{
    return m_handle;
}

template<typename T, typename Traits>
inline JNIBasicRef<T, Traits>::operator bool() const noexcept
{
    return m_handle != nullptr;
}

template<typename T, typename Traits>
auto JNIBasicRef<T, Traits>::operator==(std::nullptr_t) const noexcept -> bool
{
    return m_handle == nullptr;
}

template<typename T, typename Traits>
auto JNIBasicRef<T, Traits>::operator!=(std::nullptr_t) const noexcept -> bool
{
    return m_handle != nullptr;
}
}

/// Provides RAII ownership of a [JNI local reference][1].
///
/// This can be used to keep the number of JNI reference low since this
/// references will be deleted with C++ scopes  instead of the Java native call
/// boundaries.
///
/// [1]:
/// https://docs.oracle.com/en/java/javase/11/docs/specs/jni/design.html#referencing-java-objects
template<typename T>
class JNILocalRef : public detail::JNIBasicRef<T, detail::JNILocalRefTraits>
{
private:
    using super_type = detail::JNIBasicRef<T, detail::JNILocalRefTraits>;

public:
    using super_type::operator bool;
    using super_type::get;
    using super_type::release;
    using super_type::reset;

    JNILocalRef() = default;

    /// Constructs a wrapper for the given local JNI reference.
    JNILocalRef(JNIEnv& jni_env, T local_ref) :
        super_type(local_ref), m_jni_env(&jni_env)
    {}

    /// Constructs a local reference from another reference.
    template<typename OtherTraits>
    JNILocalRef(JNIEnv& jni_env,
                const detail::JNIBasicRef<T, OtherTraits>& other_ref) :
        super_type(jni_env, other_ref), m_jni_env(&jni_env)
    {}

    JNILocalRef(const JNILocalRef&) = delete;
    JNILocalRef& operator=(const JNILocalRef&) = delete;

    JNILocalRef(JNILocalRef&& rhs) noexcept = default;
    JNILocalRef& operator=(JNILocalRef&& rhs) noexcept = default;

    /// Deletes the owned reference.
    ~JNILocalRef() { reset(); }

    /// Deletes the owned reference.
    void reset() { super_type::reset(*m_jni_env); }

private:
    JNIEnv* m_jni_env;
};

/// Provides RAII ownership of a [JNI global reference][1].
///
/// [1]:
/// https://docs.oracle.com/en/java/javase/11/docs/specs/jni/design.html#referencing-java-objects
template<typename T>
using JNIGlobalRef = detail::JNIBasicRef<T, detail::JNIGlobalRefTraits>;

/// Provides RAII ownership of a [JNI weak reference][1].
///
/// [1]:
/// https://docs.oracle.com/en/java/javase/11/docs/specs/jni/functions.html#weak-global-references
template<typename T>
using JNIWeakGlobalRef = detail::JNIBasicRef<T, detail::JNIWeakGlobalRefTraits>;
}
