// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <type_traits>
#include <utility>

#include <m/exception/exception.h>
#include <m/tracing/tracing.h>

#define M_FAIL_FAST_NO_TEXT()                                                                      \
    do                                                                                             \
    {                                                                                              \
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
        bool const m_internal_v = !!(e);                                                           \
        if (!m_internal_v)                                                                         \
            M_FAIL_FAST_NO_TEXT();                                                                 \
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
        M_FAIL_FAST_NO_TEXT();                                                                     \
    } while (false)

#define M_NOT_IMPLEMENTED(text)                                                                    \
    do                                                                                             \
    {                                                                                              \
        throw m::not_implemented(text);                                                            \
    } while (false)

#define M_VALIDATE_PARAMETER(pname, expr)                                                          \
    do                                                                                             \
    {                                                                                              \
        auto const m_internal_value = !!(expr);                                                    \
        if (!m_internal_value)                                                                     \
        {                                                                                          \
            m::trace_error("Parameter '{}' failed validation expression: '{}'", #pname, #expr);    \
            throw m::invalid_parameter(#pname);                                                    \
        }                                                                                          \
    } while (false)

#define M_API_PARAMETER_MUST_BE_ZERO(api, p)                                                       \
    do                                                                                             \
    {                                                                                              \
        auto const m_internal_parameter_value           = (p);                                     \
        auto const m_internal_parameter_reference_value = decltype(m_internal_parameter_value){};  \
        if (m_internal_parameter_value != m_internal_parameter_reference_value)                    \
        {                                                                                          \
            m::trace_error("Parameter '{}' failed MBZ validation in api '{}'", #p, #api);          \
            throw m::invalid_parameter(api "." #p);                                                \
        }                                                                                          \
    } while (false)

