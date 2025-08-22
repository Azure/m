// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <source_location>
#include <type_traits>
#include <utility>

#include <m/exception/exception.h>
#include <m/tracing/tracing.h>

#define M_FAIL_FAST_NO_TEXT()                                                                      \
    do                                                                                             \
    {                                                                                              \
        m::tracing::monitor->close(m::tracing::close_flush_option::expedite);                      \
        std::abort();                                                                              \
    } while (false)

//
// M_INTERNAL_ERROR_CHECK is a lot like assert() except that it's always on
// even in retail code. It is meant to afford swift justice to offenders.
//
// It is much like the proposed contract_assert() in C++26 although the
// effects of contract_assert() violations are not yet specified. Chances
// are that it will be specified to call std::abort() which is effectively
// if not literally what M_INTERNAL_ERROR_CHECK() does when the expression
// evaluates falsy.
//

#define M_INTERNAL_ERROR_CHECK(e)                                                                  \
    do                                                                                             \
    {                                                                                              \
        bool const m_m_internal_v = !!(e);                                                         \
        if (!m_m_internal_v)                                                                       \
        {                                                                                          \
            m::trace_internal_error_check_failure(std::source_location::current(), #e);            \
            M_FAIL_FAST_NO_TEXT();                                                                 \
        }                                                                                          \
    } while (false)

//
// M_DEBUG_INTERNAL_ERROR_CHECK() is M_INTERNAL_ERROR_CHECK but only
// when NDEBUG is not defined, much like assert(). "So what's the difference
// between it and assert?" assert() may do other things other than just
// terminate the program.
//
// Probably most asserts coming from standard library implementations
// are fine but in practice is seems like everyone defines their own
// assert() and it can do things like pop up windowed user interface
// dialogs which is not an acceptable behavior if the application is
// a TCP/IP service.
//

#ifdef NDEBUG

#define M_DEBUG_INTERNAL_ERROR_CHECK(e)

#else

#define M_DEBUG_INTERNAL_ERROR_CHECK(e) M_INTERNAL_ERROR_CHECK((e))

#endif

#define M_UNREACHABLE_CODE()                                                                       \
    do                                                                                             \
    {                                                                                              \
        M_INTERNAL_ERROR_CHECK(!"this code should not be reachable");                              \
    } while (false)

#define M_NOT_IMPLEMENTED(text)                                                                    \
    do                                                                                             \
    {                                                                                              \
        m::trace_error("Not implemented: '{}'", text);                                             \
        throw m::not_implemented(text);                                                            \
    } while (false)

#define M_CHECK_OR_NOT_IMPLEMENTED(expr, text)                                                     \
    do                                                                                             \
    {                                                                                              \
        auto const m_m_internal_value = !!(expr);                                                  \
        if (!m_m_internal_value)                                                                   \
        {                                                                                          \
            m::trace_error("Test failed: {}; not implementetd. {}", #expr, text);                  \
            throw m::not_implemented(text);                                                        \
        }                                                                                          \
    } while (false)

#define M_VALIDATE_PARAMETER(pname, expr)                                                          \
    do                                                                                             \
    {                                                                                              \
        auto const m_m_internal_value = !!(expr);                                                  \
        if (!m_m_internal_value)                                                                   \
        {                                                                                          \
            m::trace_error("Parameter '{}' failed validation expression: '{}'", #pname, #expr);    \
            throw m::invalid_parameter(#pname);                                                    \
        }                                                                                          \
    } while (false)

#define M_VALIDATE_PARAMETER_NOT_NULLPTR(pname)                                                    \
    do                                                                                             \
    {                                                                                              \
        auto const m_m_internal_value = (pname);                                                   \
        if (pname == nullptr)                                                                      \
        {                                                                                          \
            m::trace_error("Parameter '{}' failed validation. Must not be nullptr.", #pname);      \
            throw m::invalid_parameter(#pname);                                                    \
        }                                                                                          \
    } while (false)

namespace m::macros_impl
{
    /// <summary>
    /// m::macros_impl::integral_type_for_t<T> yields either the underlying type for the
    /// enumeration type T, or T if it is an integral type.
    /// </summary>
    /// <typeparam name="T"></typeparam>
    template <typename T>
        requires(std::integral<T> || std::is_enum_v<T>)
    using integral_type_for_t = std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>;
} // namespace m::macros_impl

#define M_VALIDATE_FLAGS_PARAMETER(pname, valid_flags)                                             \
    do                                                                                             \
    {                                                                                              \
        auto m_m_internal_value       = (pname);                                                   \
        using m_m_internal_value_type = decltype(m_m_internal_value);                              \
        using m_m_integral_type =                                                                  \
            m::macros_impl::integral_type_for_t<decltype(m_m_internal_value)>;                     \
        static_assert(std::integral<m_m_internal_value_type> ||                                    \
                      std::is_enum_v<m_m_internal_value_type>);                                    \
        m_m_internal_value_type m_m_internal_valid_flags = (valid_flags);                          \
        m_m_internal_value_type m_m_internal_excess_flags =                                        \
            m_m_internal_value & ~m_m_internal_valid_flags;                                        \
        if (static_cast<bool>(m_m_internal_excess_flags))                                          \
        {                                                                                          \
            m::trace_error("Flags parameter '{}' has excess flags set: {:#x}",                     \
                           #pname,                                                                 \
                           static_cast<m_m_integral_type>(m_m_internal_excess_flags));             \
            throw m::invalid_parameter(#pname);                                                    \
        }                                                                                          \
    } while (false)

#define M_API_PARAMETER_MUST_BE_ZERO(api, p)                                                       \
    do                                                                                             \
    {                                                                                              \
        auto const m_m_internal_parameter_value = (p);                                             \
        auto const m_m_internal_parameter_reference_value =                                        \
            decltype(m_m_internal_parameter_value){};                                              \
        if (m_m_internal_parameter_value != m_m_internal_parameter_reference_value)                \
        {                                                                                          \
            m::trace_error("Parameter '{}' failed MBZ validation in api '{}'", #p, #api);          \
            throw m::invalid_parameter(api "." #p);                                                \
        }                                                                                          \
    } while (false)
