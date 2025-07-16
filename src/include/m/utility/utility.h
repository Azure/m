// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <type_traits>
#include <utility>

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

namespace m
{
    // std::to_underlying<>() is provided in <utility> in C++23
    template <typename Enum>
    constexpr std::underlying_type_t<Enum>
    to_underlying(Enum e)
    {
        return static_cast<std::underlying_type_t<Enum>>(e);
    }
} // namespace m

#define M_INTEGER_RELATIONAL_OPERATORS(T)                                                          \
    constexpr bool                 operator!(T v) { return std::to_underlying(v) == 0; }           \
    constexpr std::strong_ordering operator<=>(T l, T r)                                           \
    {                                                                                              \
        if (std::to_underlying(l) < std::to_underlying(r))                                         \
            return std::strong_ordering::less;                                                     \
                                                                                                   \
        if (std::to_underlying(l) > std::to_underlying(r))                                         \
            return std::strong_ordering::greater;                                                  \
                                                                                                   \
        return std::strong_ordering::equivalent;                                                   \
    }                                                                                              \
                                                                                                   \
    constexpr bool operator==(T l, T r)                                                            \
    {                                                                                              \
        return (std::to_underlying(l) == std::to_underlying(r));                                   \
    }                                                                                              \
                                                                                                   \
    constexpr std::strong_ordering operator<=>(T l, std::underlying_type_t<T> r)                   \
    {                                                                                              \
        if (std::to_underlying(l) < r)                                                             \
            return std::strong_ordering::less;                                                     \
                                                                                                   \
        if (std::to_underlying(l) > r)                                                             \
            return std::strong_ordering::greater;                                                  \
                                                                                                   \
        return std::strong_ordering::equivalent;                                                   \
    }                                                                                              \
                                                                                                   \
    constexpr bool operator==(T l, std::underlying_type_t<T> r)                                    \
    {                                                                                              \
        return (std::to_underlying(l) == r);                                                       \
    }                                                                                              \
                                                                                                   \
    constexpr std::strong_ordering operator<=>(std::underlying_type_t<T> l, T r)                   \
    {                                                                                              \
        if (l < std::to_underlying(r))                                                             \
            return std::strong_ordering::less;                                                     \
                                                                                                   \
        if (l > std::to_underlying(r))                                                             \
            return std::strong_ordering::greater;                                                  \
                                                                                                   \
        return std::strong_ordering::equivalent;                                                   \
    }                                                                                              \
                                                                                                   \
    constexpr bool operator==(std::underlying_type_t<T> l, T r)                                    \
    {                                                                                              \
        return (l == std::to_underlying(r));                                                       \
    }

#define M_INTEGER_OPERATIONS_INC_DEC(T)                                                            \
    /* ++v */                                                                                      \
    constexpr T& operator++(T& v)                                                                  \
    {                                                                                              \
        v = T{m::math::add(std::to_underlying(v), 1, std::underlying_type_t<T>{})};                \
        return v;                                                                                  \
    }                                                                                              \
                                                                                                   \
    /* v++ */                                                                                      \
    constexpr T operator++(T& v, int)                                                              \
    {                                                                                              \
        T old = v;                                                                                 \
        ++v;                                                                                       \
        return old;                                                                                \
    }                                                                                              \
                                                                                                   \
    /* --v */                                                                                      \
    constexpr T& operator--(T& v)                                                                  \
    {                                                                                              \
        v = T{m::math::subtract(std::to_underlying(v), 1, std::underlying_type_t<T>{})};           \
        return v;                                                                                  \
    }                                                                                              \
                                                                                                   \
    /* v-- */                                                                                      \
    constexpr T operator--(T& v, int)                                                              \
    {                                                                                              \
        T old = v;                                                                                 \
        --v;                                                                                       \
        return old;                                                                                \
    }

#define M_INTEGER_OPERATIONS_PLUSSES_NODECAY(TLEFT, TRIGHT, TRESULT)                               \
    constexpr TRESULT operator+(TLEFT l, TRIGHT r)                                                 \
    {                                                                                              \
        return TRESULT{m::math::add(                                                               \
            std::to_underlying(l), std::to_underlying(r), std::underlying_type_t<TRESULT>{})};     \
    }

#define M_INTEGER_OPERATIONS_PLUSSES_DECAYONLY(TLEFT, TRIGHT, TRESULT)                             \
    constexpr TRESULT operator+(TLEFT l, std::underlying_type_t<TRIGHT> r)                         \
    {                                                                                              \
        return TRESULT{m::math::add(std::to_underlying(l), r, std::underlying_type_t<TRESULT>{})}; \
    }                                                                                              \
    constexpr TRESULT operator+(std::underlying_type_t<TLEFT> l, TRIGHT r)                         \
    {                                                                                              \
        return TRESULT{m::math::add(l, std::to_underlying(r), std::underlying_type_t<TRESULT>{})}; \
    }

#define M_INTEGER_OPERATIONS_PLUSSES(TLEFT, TRIGHT, TRESULT)                                       \
    M_INTEGER_OPERATIONS_PLUSSES_NODECAY(TLEFT, TRIGHT, TRESULT)                                   \
    M_INTEGER_OPERATIONS_PLUSSES_DECAYONLY(TLEFT, TRIGHT, TRESULT)

#define M_INTEGER_OPERATIONS_PLUS_T_(T, TADDEND, ResultT)                                          \
    constexpr T operator+(T l, TADDEND r)                                                          \
    {                                                                                              \
        return T{m::math::add(std::to_underlying(l), r, ResultT{})};                               \
    }                                                                                              \
    constexpr T operator+(TADDEND l, T r)                                                          \
    {                                                                                              \
        return T{m::math::add(l, std::to_underlying(r), ResultT{})};                               \
    }

#define M_INTEGER_OPERATIONS_PLUS_SIZE_T_(T, ResultT)                                              \
    M_INTEGER_OPERATIONS_PLUS_T_(T, std::size_t, ResultT)

#define M_INTEGER_OPERATIONS_PLUS_T(T, TADDEND)                                                    \
    M_INTEGER_OPERATIONS_PLUS_T_(T, TADDEND, std::underlying_type_t<T>)

#define M_INTEGER_OPERATIONS_PLUS_SIZE_T(T)                                                        \
    M_INTEGER_OPERATIONS_PLUS_SIZE_T_(T, std::underlying_type_t<T>)

#define M_INTEGER_OPERATIONS_MINUSES_NODECAY(TLEFT, TRIGHT, TRESULT)                               \
    constexpr TRESULT operator-(TLEFT l, TRIGHT r)                                                 \
    {                                                                                              \
        return TRESULT{m::math::subtract(                                                          \
            std::to_underlying(l), std::to_underlying(r), std::underlying_type_t<TRESULT>{})};     \
    }

#define M_INTEGER_OPERATIONS_MINUSES_DECAYONLY(TLEFT, TRIGHT, TRESULT)                             \
    constexpr TRESULT operator-(TLEFT l, std::underlying_type_t<TRIGHT> r)                         \
    {                                                                                              \
        return TRESULT{                                                                            \
            m::math::subtract(std::to_underlying(l), r, std::underlying_type_t<TRESULT>{})};       \
    }                                                                                              \
    constexpr TRESULT operator-(std::underlying_type_t<TLEFT> l, TRIGHT r)                         \
    {                                                                                              \
        return TRESULT{                                                                            \
            m::math::subtract(l, std::to_underlying(r), std::underlying_type_t<TRESULT>{})};       \
    }

#define M_INTEGER_OPERATIONS_MINUSES(TLEFT, TRIGHT, TRESULT)                                       \
    M_INTEGER_OPERATIONS_MINUSES_NODECAY(TLEFT, TRIGHT, TRESULT)                                   \
    M_INTEGER_OPERATIONS_MINUSES_DECAYONLY(TLEFT, TRIGHT, TRESULT)

#define M_INTEGER_OPERATIONS_MINUS_T_(T, TMINUEND, ResultT)                                        \
    constexpr T operator-(T l, TMINUEND r)                                                         \
    {                                                                                              \
        return T{m::math::subtract(std::to_underlying(l), r, ResultT{})};                          \
    }

#define M_INTEGER_OPERATIONS_MINUS_SIZE_T_(T, ResultT)                                             \
    M_INTEGER_OPERATIONS_MINUS_T_(T, std::size_t, ResultT)

#define M_INTEGER_OPERATIONS_MINUS_SIZE_T(T)                                                       \
    constexpr T operator-(T l, std::size_t r)                                                      \
    {                                                                                              \
        return T{m::math::subtract(std::to_underlying(l), r, std::underlying_type_t<T>{})};        \
    }

#define M_INTEGER_OPERATIONS_PLUSEQUALS(TLEFT, TRIGHT)                                             \
    constexpr TLEFT& operator+=(TLEFT& l, TRIGHT r)                                                \
    {                                                                                              \
        l = l + r;                                                                                 \
        return l;                                                                                  \
    }                                                                                              \
                                                                                                   \
    constexpr TLEFT& operator+=(TLEFT& l, std::underlying_type_t<TRIGHT> r)                        \
    {                                                                                              \
        l = l + r;                                                                                 \
        return l;                                                                                  \
    }

#define M_INTEGER_OPERATIONS_MINUSEQUALS_(TLEFT, TRIGHT)                                           \
    constexpr TLEFT& operator-=(TLEFT& l, TRIGHT r)                                                \
    {                                                                                              \
        l = l - r;                                                                                 \
        return l;                                                                                  \
    }

#define M_INTEGER_OPERATIONS_MINUSEQUALS(TLEFT, TRIGHT)                                            \
    M_INTEGER_OPERATIONS_MINUSEQUALS_(TLEFT, TRIGHT)                                               \
    M_INTEGER_OPERATIONS_MINUSEQUALS_(TLEFT, std::underlying_type_t<TRIGHT>)

#define M_INTEGER_OPERATIONS_PLUS_MINUS(T)                                                         \
    M_INTEGER_OPERATIONS_MINUSES(T, T, T)                                                          \
    M_INTEGER_OPERATIONS_PLUSSES(T, T, T)                                                          \
    M_INTEGER_OPERATIONS_MINUSEQUALS(T, T)                                                         \
    M_INTEGER_OPERATIONS_PLUSEQUALS(T, T)

#define M_INTEGER_OPERATIONS_PLUS_MINUS___OLD(T)                                                   \
    constexpr T operator+(T l, T r)                                                                \
    {                                                                                              \
        return T{m::math::add(                                                                     \
            std::to_underlying(l), std::to_underlying(r), std::underlying_type_t<T>{})};           \
    }                                                                                              \
    constexpr T operator+(T l, std::underlying_type_t<T> r)                                        \
    {                                                                                              \
        return T{m::math::add(std::to_underlying(l), r, std::underlying_type_t<T>{})};             \
    }                                                                                              \
    constexpr T operator+(std::underlying_type_t<T> l, T r)                                        \
    {                                                                                              \
        return T{m::math::add(l, std::to_underlying(r), std::underlying_type_t<T>{})};             \
    }                                                                                              \
    constexpr T operator-(T l, T r)                                                                \
    {                                                                                              \
        return T{m::math::subtract(                                                                \
            std::to_underlying(l), std::to_underlying(r), std::underlying_type_t<T>{})};           \
    }                                                                                              \
    constexpr T operator-(T l, std::underlying_type_t<T> r)                                        \
    {                                                                                              \
        return T{m::math::subtract(std::to_underlying(l), r, std::underlying_type_t<T>{})};        \
    }                                                                                              \
    constexpr T operator-(std::underlying_type_t<T> l, T r)                                        \
    {                                                                                              \
        return T{m::math::subtract(l, std::to_underlying(r), std::underlying_type_t<T>{})};        \
    }
