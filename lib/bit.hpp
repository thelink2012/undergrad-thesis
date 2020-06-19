#pragma once
#include "config.hpp"
#include <functional>
#include <type_traits>

namespace jvmtiprof
{
namespace detail
{
/// Returns the result of an operation applied to the bit representation of
/// `lhs` and `rhs`.
template<typename T, typename Op>
constexpr JVMTIPROF_NODISCARD auto bitwise_op(const T& lhs, const T& rhs) -> T
{
    static_assert(std::is_standard_layout<T>::value,
                  "T must be standard layout");
#if __cplusplus >= 201703L
    static_assert(std::has_unique_object_representation<T>::value,
                  "T must have a unique object representation");
#endif

    Op op;
    T out{};
    auto lhs_bits = reinterpret_cast<const unsigned char*>(&lhs);
    auto rhs_bits = reinterpret_cast<const unsigned char*>(&rhs);
    auto out_bits = reinterpret_cast<unsigned char*>(&out);
    auto out_end = out_bits + sizeof(T);
    while(out_bits != out_end)
    {
        *out_bits++ = op(*lhs_bits++, *rhs_bits++);
    }
    return out;
}
}

/// Returns the result of applying the bit AND operation to the bit
/// representation of `lhs` and `rhs`.
template<typename T>
JVMTIPROF_NODISCARD constexpr auto bitwise_and(const T& lhs, const T& rhs) -> T
{
    return detail::bitwise_op<T, std::bit_and<>>(lhs, rhs);
}

/// Returns the result of applying the bit OR operation to the bit
/// representation of `lhs` and `rhs`.
template<typename T>
JVMTIPROF_NODISCARD constexpr auto bitwise_or(const T& lhs, const T& rhs) -> T
{
    return detail::bitwise_op<T, std::bit_or<>>(lhs, rhs);
}

/// Returns the result of applying the bit XOR operation to the bit
/// representation of `lhs` and `rhs`.
template<typename T>
JVMTIPROF_NODISCARD constexpr auto bitwise_xor(const T& lhs, const T& rhs) -> T
{
    return detail::bitwise_op<T, std::bit_xor<>>(lhs, rhs);
}

/// Checks whether any bit in the bit representation of `obj` is set.
template<typename T>
JVMTIPROF_NODISCARD constexpr auto has_any_bit(const T& obj)
{
    static_assert(std::is_standard_layout<T>::value,
                  "T must be standard layout");
#if __cplusplus >= 201703L
    static_assert(std::has_unique_object_representation<T>::value,
                  "T must have a unique object representation");
#endif

    auto obj_bits = reinterpret_cast<const unsigned char*>(&obj);
    auto obj_end = obj_bits + sizeof(T);
    while(obj_bits != obj_end)
    {
        if(*obj_bits++)
            return true;
    }
    return false;
}
}
