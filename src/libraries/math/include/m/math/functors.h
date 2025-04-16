// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <concepts>
#include <type_traits>

#include <m/cast/to.h>
#include <m/math/math.h>

#include <m/type_traits/type_traits.h>

namespace m
{
    namespace math
    {
        struct functor
        {
            // empty for now
        };

        template <typename T>
        concept is_functor = std::derived_from<T, functor>;
    } // namespace math

    //
    // This header defines types to assist with creating types that
    // encapsulate integer values and provide safe integer operations
    // around them.
    //
    // Functors are used here because the general model for safe math
    // using the safe integer objects is that the operators return
    // these functor objects which /do not know/ what types they will
    // return, so they (in general) /cannot/ throw based on overflows.
    //
    // Yes, it's possible that there are cases that could overflow on
    // construction like adding numeric_limits<uintmax_t>::max() with
    // itself. There's no output type where that's not going to overflow
    // but it's also not particularly valuable to move the check to be
    // earlier for that one.
    //
    // Other checks may occur earlier. e.g. division by zero may be checked
    // at the time of creation, rather than the time of evaluation.
    //
    // so there is no prohibition on early overflow checks, but, that said,
    // it's probably also usually not a good division of labor. At the time
    // of writing this comment, the author is not aware of any two-phased
    // approach to overflow checking where some could be done trivially
    // at the time of construction of the functor and more done at the
    // time of assignment to the result type.
    //
    // The mathematical operations are always performed, conceptually, in the
    // set of integers, not in any particular set of values limited by
    // length, sign or 2s complement arithmetic.
    //
    // A good example of this is subtraction of two unsigned integers.
    // "traditionally" this may be thought of as returning another unsigned
    // integer, but in our model, the fact that the value was brought to the
    // computation engine in a type like uint8_t or uint64_t is irrelevant.
    // The value is used as an integer, and if the value is negative, it
    // is negative. If the result type is unsigned, then an integer overflow
    // is reported. If the result type is signed, if the value fits in the
    // representation, the value is returned, otherwise an integer overflow
    // is reported.
    //

    template <typename T, typename Enable = void>
    struct negation_functor;

    template <typename LeftT, typename RightT, typename Enable = void>
    struct addition_functor;

    template <typename LeftT, typename RightT, typename Enable = void>
    struct subtraction_functor;

    template <typename LeftT, typename RightT, typename Enable = void>
    struct multiplication_functor;

    template <typename LeftT, typename RightT, typename Enable = void>
    struct division_functor;

    template <typename T>
    struct negation_functor<T, void> : public math::functor
    {
        using value_t = std::underlying_type_t<T>;

        constexpr negation_functor(T v) noexcept: m_v(std::to_underlying(v)) {}

        template <typename ResultType>
        constexpr ResultType
        to() const
        {
            return ResultType{m::math::negate(m_v, std::underlying_type_t<ResultType>{})};
        }

        value_t m_v;
    };

    template <typename T>
    negation_functor(T) -> negation_functor<T>;

    template <typename LeftT, typename RightT>
    struct addition_functor<LeftT, RightT, void> : public math::functor
    {
        using left_value_t  = std::underlying_type_t<LeftT>;
        using right_value_t = std::underlying_type_t<RightT>;

        constexpr addition_functor(LeftT l, RightT r) noexcept:
            m_l(std::to_underlying(l)), m_r(std::to_underlying(r))
        {}

        template <typename SumType>
        constexpr SumType
        to() const
        {
            return SumType{m::math::add(m_l, m_r, std::underlying_type_t<SumType>{})};
        }

        left_value_t  m_l;
        right_value_t m_r;
    };

    template <typename LeftT, typename RightT>
    addition_functor(LeftT, RightT) -> addition_functor<LeftT, RightT>;

    template <typename LeftT, typename RightT>
    struct subtraction_functor<LeftT, RightT, void> : public math::functor
    {
        using left_value_t  = std::underlying_type_t<LeftT>;
        using right_value_t = std::underlying_type_t<RightT>;

        constexpr subtraction_functor(LeftT l, RightT r) noexcept:
            m_l(std::to_underlying(l)), m_r(std::to_underlying(r))
        {}

        template <typename DifferenceType>
        constexpr DifferenceType
        to() const
        {
            return DifferenceType{
                m::math::subtract(m_l, m_r, std::underlying_type_t<DifferenceType>{})};
        }

        left_value_t  m_l;
        right_value_t m_r;
    };

    template <typename LeftT, typename RightT>
    subtraction_functor(LeftT, RightT) -> subtraction_functor<LeftT, RightT>;

    template <typename LeftT, typename RightT>
    struct multiplication_functor<LeftT, RightT, void> : public math::functor
    {
        using left_value_t  = std::underlying_type_t<LeftT>;
        using right_value_t = std::underlying_type_t<RightT>;

        constexpr multiplication_functor(LeftT l, RightT r) noexcept:
            m_l(std::to_underlying(l)), m_r(std::to_underlying(r))
        {}

        template <typename ProductType>
        constexpr ProductType
        to() const
        {
            return ProductType{m::math::multiply(m_l, m_r, std::underlying_type_t<ProductType>{})};
        }

        left_value_t  m_l;
        right_value_t m_r;
    };

    template <typename LeftT, typename RightT>
    multiplication_functor(LeftT, RightT) -> multiplication_functor<LeftT, RightT>;

    template <typename LeftT, typename RightT>
    struct division_functor<LeftT, RightT, void> : public math::functor
    {
        using left_value_t  = std::underlying_type_t<LeftT>;
        using right_value_t = std::underlying_type_t<RightT>;

        constexpr division_functor(LeftT l, RightT r) noexcept:
            m_l(std::to_underlying(l)), m_r(std::to_underlying(r))
        {}

        template <typename QuotientType>
        constexpr QuotientType
        to() const
        {
            return QuotientType{m::math::divide(m_l, m_r, std::underlying_type_t<QuotientType>{})};
        }

        left_value_t  m_l;
        right_value_t m_r;
    };

    template <typename LeftT, typename RightT>
    division_functor(LeftT, RightT) -> division_functor<LeftT, RightT>;
}; // namespace m
