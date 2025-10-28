// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <type_traits>
#include <utility>

#include <m/utility/to_underlying.h>

#define M_INTEGER_RELATIONAL_OPERATORS(T)                                                          \
    constexpr bool                 operator!(T v) { return m::to_underlying(v) == 0; }             \
    constexpr std::strong_ordering operator<=>(T l, T r)                                           \
    {                                                                                              \
        if (m::to_underlying(l) < m::to_underlying(r))                                             \
            return std::strong_ordering::less;                                                     \
                                                                                                   \
        if (m::to_underlying(l) > m::to_underlying(r))                                             \
            return std::strong_ordering::greater;                                                  \
                                                                                                   \
        return std::strong_ordering::equivalent;                                                   \
    }                                                                                              \
                                                                                                   \
    constexpr bool operator==(T l, T r) { return (m::to_underlying(l) == m::to_underlying(r)); }   \
                                                                                                   \
    constexpr std::strong_ordering operator<=>(T l, std::underlying_type_t<T> r)                   \
    {                                                                                              \
        if (m::to_underlying(l) < r)                                                               \
            return std::strong_ordering::less;                                                     \
                                                                                                   \
        if (m::to_underlying(l) > r)                                                               \
            return std::strong_ordering::greater;                                                  \
                                                                                                   \
        return std::strong_ordering::equivalent;                                                   \
    }                                                                                              \
                                                                                                   \
    constexpr bool operator==(T l, std::underlying_type_t<T> r)                                    \
    {                                                                                              \
        return (m::to_underlying(l) == r);                                                         \
    }                                                                                              \
                                                                                                   \
    constexpr std::strong_ordering operator<=>(std::underlying_type_t<T> l, T r)                   \
    {                                                                                              \
        if (l < m::to_underlying(r))                                                               \
            return std::strong_ordering::less;                                                     \
                                                                                                   \
        if (l > m::to_underlying(r))                                                               \
            return std::strong_ordering::greater;                                                  \
                                                                                                   \
        return std::strong_ordering::equivalent;                                                   \
    }                                                                                              \
                                                                                                   \
    constexpr bool operator==(std::underlying_type_t<T> l, T r)                                    \
    {                                                                                              \
        return (l == m::to_underlying(r));                                                         \
    }

#define M_INTEGER_OPERATIONS_INC_DEC(T)                                                            \
    /* ++v */                                                                                      \
    constexpr T& operator++(T& v)                                                                  \
    {                                                                                              \
        v = T{m::math::add(m::to_underlying(v), 1, std::underlying_type_t<T>{})};                  \
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
        v = T{m::math::subtract(m::to_underlying(v), 1, std::underlying_type_t<T>{})};             \
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
            m::to_underlying(l), m::to_underlying(r), std::underlying_type_t<TRESULT>{})};         \
    }

#define M_INTEGER_OPERATIONS_PLUSSES_DECAYONLY(TLEFT, TRIGHT, TRESULT)                             \
    constexpr TRESULT operator+(TLEFT l, std::underlying_type_t<TRIGHT> r)                         \
    {                                                                                              \
        return TRESULT{m::math::add(m::to_underlying(l), r, std::underlying_type_t<TRESULT>{})};   \
    }                                                                                              \
    constexpr TRESULT operator+(std::underlying_type_t<TLEFT> l, TRIGHT r)                         \
    {                                                                                              \
        return TRESULT{m::math::add(l, m::to_underlying(r), std::underlying_type_t<TRESULT>{})};   \
    }

#define M_INTEGER_OPERATIONS_PLUSSES(TLEFT, TRIGHT, TRESULT)                                       \
    M_INTEGER_OPERATIONS_PLUSSES_NODECAY(TLEFT, TRIGHT, TRESULT)                                   \
    M_INTEGER_OPERATIONS_PLUSSES_DECAYONLY(TLEFT, TRIGHT, TRESULT)

#define M_INTEGER_OPERATIONS_PLUS_T_(T, TADDEND, ResultT)                                          \
    constexpr T operator+(T l, TADDEND r)                                                          \
    {                                                                                              \
        return T{m::math::add(m::to_underlying(l), r, ResultT{})};                                 \
    }                                                                                              \
    constexpr T operator+(TADDEND l, T r)                                                          \
    {                                                                                              \
        return T{m::math::add(l, m::to_underlying(r), ResultT{})};                                 \
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
            m::to_underlying(l), m::to_underlying(r), std::underlying_type_t<TRESULT>{})};         \
    }

#define M_INTEGER_OPERATIONS_MINUSES_DECAYONLY(TLEFT, TRIGHT, TRESULT)                             \
    constexpr TRESULT operator-(TLEFT l, std::underlying_type_t<TRIGHT> r)                         \
    {                                                                                              \
        return TRESULT{                                                                            \
            m::math::subtract(m::to_underlying(l), r, std::underlying_type_t<TRESULT>{})};         \
    }                                                                                              \
    constexpr TRESULT operator-(std::underlying_type_t<TLEFT> l, TRIGHT r)                         \
    {                                                                                              \
        return TRESULT{                                                                            \
            m::math::subtract(l, m::to_underlying(r), std::underlying_type_t<TRESULT>{})};         \
    }

#define M_INTEGER_OPERATIONS_MINUSES(TLEFT, TRIGHT, TRESULT)                                       \
    M_INTEGER_OPERATIONS_MINUSES_NODECAY(TLEFT, TRIGHT, TRESULT)                                   \
    M_INTEGER_OPERATIONS_MINUSES_DECAYONLY(TLEFT, TRIGHT, TRESULT)

#define M_INTEGER_OPERATIONS_MINUS_T_(T, TMINUEND, ResultT)                                        \
    constexpr T operator-(T l, TMINUEND r)                                                         \
    {                                                                                              \
        return T{m::math::subtract(m::to_underlying(l), r, ResultT{})};                            \
    }

#define M_INTEGER_OPERATIONS_MINUS_SIZE_T_(T, ResultT)                                             \
    M_INTEGER_OPERATIONS_MINUS_T_(T, std::size_t, ResultT)

#define M_INTEGER_OPERATIONS_MINUS_SIZE_T(T)                                                       \
    constexpr T operator-(T l, std::size_t r)                                                      \
    {                                                                                              \
        return T{m::math::subtract(m::to_underlying(l), r, std::underlying_type_t<T>{})};          \
    }

#define M_INTEGER_OPERATIONS_PLUSEQUALS_NOENUM(TLEFT, TRIGHT)                                      \
    constexpr TLEFT& operator+=(TLEFT& l, TRIGHT r)                                                \
    {                                                                                              \
        l = l + r;                                                                                 \
        return l;                                                                                  \
    }

#define M_INTEGER_OPERATIONS_PLUSEQUALS(TLEFT, TRIGHT)                                             \
    M_INTEGER_OPERATIONS_PLUSEQUALS_NOENUM(TLEFT, TRIGHT)                                          \
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
        return T{                                                                                  \
            m::math::add(m::to_underlying(l), m::to_underlying(r), std::underlying_type_t<T>{})};  \
    }                                                                                              \
    constexpr T operator+(T l, std::underlying_type_t<T> r)                                        \
    {                                                                                              \
        return T{m::math::add(m::to_underlying(l), r, std::underlying_type_t<T>{})};               \
    }                                                                                              \
    constexpr T operator+(std::underlying_type_t<T> l, T r)                                        \
    {                                                                                              \
        return T{m::math::add(l, m::to_underlying(r), std::underlying_type_t<T>{})};               \
    }                                                                                              \
    constexpr T operator-(T l, T r)                                                                \
    {                                                                                              \
        return T{m::math::subtract(                                                                \
            m::to_underlying(l), m::to_underlying(r), std::underlying_type_t<T>{})};               \
    }                                                                                              \
    constexpr T operator-(T l, std::underlying_type_t<T> r)                                        \
    {                                                                                              \
        return T{m::math::subtract(m::to_underlying(l), r, std::underlying_type_t<T>{})};          \
    }                                                                                              \
    constexpr T operator-(std::underlying_type_t<T> l, T r)                                        \
    {                                                                                              \
        return T{m::math::subtract(l, m::to_underlying(r), std::underlying_type_t<T>{})};          \
    }
