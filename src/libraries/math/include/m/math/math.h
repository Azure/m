// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include <m/cast/cast.h>
#include <m/cast/to.h>
#include <m/cast/try_cast.h>
#include <m/utility/type_traits.h>

//
// This library gives a framework for "safe" mathematics.
//
// The core intention is to provide for the built-in integer types;
// at least initially the operations make a half-hearted effort to
// prevent use in other cases since they are not tested.
//
// The fundamental model is that the operations are performed in
// the space of the integer set, Z. Not in any particular "two's
// complement" or such. The addition, subtraction, or what have
// you are performed per their usual accustomed mathematical
// definitions and then brought into some result type. If the
// result type cannot represent the value, an overflow exception
// is thrown.
//
// Other errors may occur such as division by zero or the like,
// but fundamentally the range is always determined by the output,
// never by the inputs.
//
// As such, you will find a pattern where the template functions
// usually have a third parameter which is not used at runtime but
// which is used to "set" the return value type for the operation.
//
// so for example, a client might specify:
//
// auto x = m::math::add(a, b, uint64_t{});
//
// in order to add a and b with a desired uint64_t value result.
//
// This seems odd at first but once you realize that it's actually
// the defaulting and wrapping of the values that's so odd in
// normal programming, it's quite natural and the places where
// there are missing overflow checks throughout your code base
// will stick out at you like sore thumbs.
//
// There is a natural adjunct to this which is to have integral
// types which support the "normal" operators, +, -, *, etc but
// which perform the overflow checking operations naturally. You
// will find those in the m/wrapped/integer_functor_macros.h
// header which uses this library to achieve its goals.
//
// This library is incomplete and can use fleshing out. Safe
// math has been done, repeatedly, in various contexts, and is
// not for the feint of heart to get right. The pivot towards
// a non-inferencing type based model here is novel.
//

namespace m
{
    namespace math
    {
        template <typename LeftT, typename RightT, typename ResultT>
        struct safe_math_helper;

        //
        // Handle (unsigned [op] unsigned) -> unsigned
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_unsigned_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_unsigned_v<ResultT>
        struct safe_math_helper<LeftT, RightT, ResultT>
        {
            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                auto       lmax = uintmax_t{l};
                auto       rmax = uintmax_t{r};
                auto const rv   = uintmax_t{lmax + rmax};

                if ((rv < lmax) || (rv < rmax))
                    throw std::overflow_error(std::format(
                        "m::math::add overflow: {} + {} = {} exceeds maximum representable value",
                        lmax, rmax, rv));

                return m::try_cast<ResultT>(rv);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                if (r > l)
                    throw std::overflow_error(std::format(
                        "m::math::subtract overflow: {} - {} would be negative (unsigned types cannot represent negative values)",
                        m::cast<uintmax_t>(l), m::cast<uintmax_t>(r)));

                auto       lmax = uintmax_t{l};
                auto       rmax = uintmax_t{r};
                auto const rv   = uintmax_t{lmax - rmax};

                if ((rv < lmax) || (rv < rmax))
                    throw std::overflow_error(std::format(
                        "m::math::subtract overflow: result does not fit in target type"));

                return m::try_cast<ResultT>(rv);
            }

            // There are obvious optimizations for multiplications of smaller
            // domains to larger codomains (e.g. uint8 x uint8 -> uint32 which
            // cannot overflow) but these do not seem common enough to code
            // for at least in the initial implementations. Nothing wrong with
            // providing them over time.
            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // There is certainly a better implementation?

                // Are these micro-optimizations worth it? I would think so
                // given the checks after but who knows.
                if (r == 0 || l == 0)
                    return 0;

                if (l == 1)
                    return m::to<ResultT>(r);

                if (r == 1)
                    return m::to<ResultT>(l);

                            auto lmax = uintmax_t{l};
                            auto rmax = uintmax_t{r};

                            auto prod = lmax * rmax;

                            if ((prod / lmax) != r || (prod / rmax) != l)
                            {
                                throw std::overflow_error(std::format(
                                    "m::math::multiply overflow: {} × {} exceeds maximum representable value",
                                    lmax, rmax));
                            }

                            return m::to<ResultT>(prod);
                        }

                        static constexpr ResultT
                        divide(LeftT l, RightT r)
                        {
                            if (r == 0)
                                throw std::overflow_error(std::format(
                                    "m::math::divide overflow: division by zero ({} / 0)",
                                    m::cast<uintmax_t>(l)));

                            // For unsigned division, overflow can only occur if the result
                            // doesn't fit in ResultT. Division by non-zero always produces
                            // a result <= l, so we just need to check if l fits in ResultT.
                            auto lmax = uintmax_t{l};
                            auto rmax = uintmax_t{r};

                            auto quot = lmax / rmax;

                            return m::try_cast<ResultT>(quot);
                        }
                };

        //
        // Handle (unsigned [op] unsigned) -> signed
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_unsigned_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_signed_v<ResultT>
        struct safe_math_helper<LeftT, RightT, ResultT>
        {
            using ResultTAsUnsigned = std::make_unsigned_t<ResultT>;

            using common_type_t = std::common_type_t<LeftT, RightT, ResultTAsUnsigned>;

            // Should go without saying, but...
            static_assert(std::is_unsigned_v<common_type_t>);

            using Doppelganger = safe_math_helper<LeftT, RightT, ResultTAsUnsigned>;

            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                return m::try_cast<ResultT>(Doppelganger::add(l, r));
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                //
                // An interesting case: it is tempting to cast the numbers to
                // the signed type and subtract but the signed type may not
                // have the range of the unsigned type. It *probably works*
                // but those don't seem like good words to use in this kind
                // of setting.
                //
                // So instead we will perform the subtraction in the unsigned
                // space, safely, (meaning that we will always subtract the
                // smaller from the larger and keep track of which "direction"
                // it was done in thus the sign of the result) and then
                // apply the result and the sign after.
                //

                auto promoted_l = m::cast<common_type_t>(l);
                auto promoted_r = m::cast<common_type_t>(r);

                if (promoted_l >= promoted_r)
                {
                    // This case is "easy". The result should be positive, so
                    // perform the subtraction in the normal way, and then
                    // let try_cast<> be the final arbiter of whether the
                    // value fits in ResultT.
                    //
                    return m::try_cast<ResultT>(promoted_l - promoted_r);
                }
                else
                {
                    auto diff = promoted_r - promoted_l;

                    return try_negate(diff);
                }
            }

            //
            // try_negate() is an "internal helper function" which only
            // operates on the negotiated common type between LeftT and
            // RightT. It's possible there should be a public primitive
            // that does the same, but the need is to factor this code
            // out from (possibly?) several operations that have this
            // pattern of working in large unsigned spaces and then need
            // to invert and restrict down to the negative side of the
            // result type.
            //
            static constexpr ResultT
            try_negate(common_type_t v)
            {
                constexpr common_type_t biggest_negative_as_positive =
                    m::try_cast<common_type_t>(-((std::numeric_limits<ResultT>::min)() + 1));

                if (v >= biggest_negative_as_positive)
                    throw std::overflow_error(std::format(
                        "m::math overflow: negative value magnitude exceeds maximum representable in target type"));

                return -m::try_cast<ResultT>(v);
            }

            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Multiplication of two unsigned values with signed result.
                // Use the unsigned/unsigned multiply and cast to signed.
                return m::try_cast<ResultT>(Doppelganger::multiply(l, r));
            }

                        static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Division of two unsigned values with signed result.
                // Use the unsigned/unsigned divide and cast to signed.
                return m::try_cast<ResultT>(Doppelganger::divide(l, r));
            }
        };

        //
        // Handle (unsigned [op] signed) -> unsigned
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_unsigned_v<LeftT> &&
                     std::is_signed_v<RightT> && std::is_unsigned_v<ResultT>
        struct safe_math_helper<LeftT, RightT, ResultT>
        {
            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                //
                // There are, perhaps, useful specializations which do not
                // use full uintmax_t and intmax_t values here. If LeftT and
                // RightT are uint8_t and int8_t respectively, what we are
                // doing here is downright wasteful. But the first use case
                // for this code is adding pointer sized offsets, so this
                // kind of optimization can come later in the form of explicit
                // specializations when there is demonstrated need.
                //
                // It shouldn't be difficult, and implementers can
                // delegate the remainder of the implementation to the
                // generic implementation for the other operations.
                //
                auto promoted_l = m::cast<uintmax_t>(l);
                auto promoted_r = m::cast<intmax_t>(r);

                //
                // There are efficient ways to approach this but it's more
                // important to be correct right now.
                //

                if (promoted_r >= 0)
                {
                    // Seems obvious but best to validate at compile time,
                    // stranger things have happened.
                    static_assert(std::numeric_limits<uintmax_t>::digits >=
                                  std::numeric_limits<intmax_t>::digits);

                    auto const r_as_unsigned = m::cast<uintmax_t>(promoted_r);

                    // Explicit type here because we want to be super tight on typing
                    uintmax_t const sum = promoted_l + r_as_unsigned;

                    if ((sum < promoted_l) || (sum < r_as_unsigned))
                        throw std::overflow_error("integer overflow");

                    return m::try_cast<ResultT>(sum);
                }

                //
                // This "loop" ensures that promoted_r is in the range of
                // -std::numeric_limits<intmax_t>::max() .. -1.
                //
                // In practice, it will only execute at most once since the
                // only value lower than that value is the negative power of
                // two just below that value. It's written as a loop so as
                // to avoid encoding 2's complement assumptions in the code
                // when possible; it should not make the code less efficient.
                //
                while (promoted_r < -(std::numeric_limits<intmax_t>::max)())
                {
                    // We're adding a negative, thus it's a subtrahend

                    // Widening constant to uintmax_t for computation
                    constexpr uintmax_t subtrahend =
                        m::cast<uintmax_t>((std::numeric_limits<intmax_t>::max)());
                    if (subtrahend > promoted_l)
                        throw std::overflow_error("integer overflow");

                    promoted_l -= subtrahend;
                    promoted_r += (std::numeric_limits<intmax_t>::max)();
                }

                // Now we know that promoted_r is in the range such that
                // applying the unary minus to it will work as intended,
                // that is, it will yield a positive integer in the range
                // of 0 .. std::numeric_limits<intmax_t>::max()
                //
                uintmax_t that_which_remains = m::cast<uintmax_t>(-promoted_r);

                if (that_which_remains > promoted_l)
                    throw std::overflow_error("integer overflow");

                promoted_l -= that_which_remains;

                return m::to<ResultT>(promoted_l);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                if (r < 0)
                {
                    if (r == (std::numeric_limits<intmax_t>::min)())
                    {
                        // We can't overcome this case just throw
                        throw std::overflow_error(std::format(
                            "m::math::add overflow: cannot add most negative signed value"));
                    }

                    // since r is not the most negative number, and we know since we're
                    // C++20 and later that this is 2's complement arithmetic, we can
                    // simply negate r to get its absolute value if it's negative.
                    auto r_as_unsigned = m::cast<uintmax_t>(-r);

                    // Let's kind of statically verify this somewhat obtusely
                    static_assert(((-(std::numeric_limits<intmax_t>::max)()) - 1) ==
                                  (std::numeric_limits<intmax_t>::min)());

                    return safe_math_helper<LeftT, uintmax_t, ResultT>::add(l, r_as_unsigned);
                }

                // Plain old subtraction. r is positive, l is positive, and we require a
                // positive answer, so if r > l, overflow.

                if (m::cast<uintmax_t>(r) > m::cast<uintmax_t>(l))
                    throw std::overflow_error("integer overflow");

                return m::to<ResultT>(m::cast<uintmax_t>(l) - m::cast<uintmax_t>(r));
            }

            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Unsigned × signed with unsigned result.
                // If r is negative, result is negative (can't represent in unsigned).

                if (r == 0 || l == 0)
                    return 0;

                if (r < 0)


                {


                    // Multiplying by negative gives negative result


                    throw std::overflow_error(std::format(


                        "m::math overflow: operation with negative value cannot be represented in unsigned result type"));
                }

                // r is positive, safe to cast to unsigned
                auto l_promoted = m::cast<uintmax_t>(l);
                auto r_as_unsigned = m::cast<uintmax_t>(r);

                auto prod = l_promoted * r_as_unsigned;

                // Check for overflow using division
                if (prod / l_promoted != r_as_unsigned || prod / r_as_unsigned != l_promoted)
                {
                    throw std::overflow_error("integer overflow");
                }

                return m::try_cast<ResultT>(prod);
            }

            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Unsigned / signed with unsigned result.

                if (r == 0)
                    throw std::overflow_error(std::format(
                    "m::math::divide overflow: division by zero"));

                if (r < 0)


                {


                    // Dividing by negative gives negative result


                    throw std::overflow_error(std::format(


                        "m::math overflow: operation with negative value cannot be represented in unsigned result type"));
                }

                // r is positive, safe to cast to unsigned
                auto l_promoted = m::cast<uintmax_t>(l);
                auto r_as_unsigned = m::cast<uintmax_t>(r);

                auto quot = l_promoted / r_as_unsigned;

                return m::try_cast<ResultT>(quot);
            }
        };

        //
        // Handle (signed [op] unsigned) -> signed
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_signed_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_signed_v<ResultT>
        struct safe_math_helper<LeftT, RightT, ResultT>
        {
            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                auto promoted_l = m::cast<intmax_t>(l);
                auto promoted_r = m::cast<uintmax_t>(r);

                //
                // There are efficient ways to approach this but it's more
                // important to be correct right now.
                //

                if (promoted_l >= 0)
                {
                    // Seems obvious but best to validate at compile time,
                    // stranger things have happened.
                    static_assert(std::numeric_limits<uintmax_t>::digits >=
                                  std::numeric_limits<intmax_t>::digits);

                    auto const l_as_unsigned = m::cast<uintmax_t>(promoted_l);

                    // Explicit type here because we want to be super tight on typing
                    uintmax_t const sum = promoted_r + l_as_unsigned;

                    if ((sum < promoted_r) || (sum < l_as_unsigned))
                        throw std::overflow_error("integer overflow");

                    return m::try_cast<ResultT>(sum);
                }

                //
                // This "loop" ensures that promoted_r is in the range of
                // -std::numeric_limits<intmax_t>::max() .. -1.
                //
                // In practice, it will only execute at most once since the
                // only value lower than that value is the negative power of
                // two just below that value. It's written as a loop so as
                // to avoid encoding 2's complement assumptions in the code
                // when possible; it should not make the code less efficient.
                //
                while (promoted_l < -(std::numeric_limits<intmax_t>::max)())
                {
                    // We're adding a negative, thus it's a subtrahend
                    constexpr uintmax_t subtrahend = (std::numeric_limits<intmax_t>::max)();
                    if (subtrahend > promoted_r)
                        throw std::overflow_error("integer overflow");

                    promoted_r -= subtrahend;
                    promoted_l += (std::numeric_limits<intmax_t>::max)();
                }

                uintmax_t that_which_remains = m::cast<uintmax_t>(-promoted_l);

                if (that_which_remains > promoted_r)
                    throw std::overflow_error("integer overflow");

                promoted_r -= that_which_remains;

                return m::try_cast<ResultT>(promoted_r);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                if (l == (std::numeric_limits<intmax_t>::min)())
                {
                    // We can't overcome this case just throw
                    throw std::overflow_error("integer overflow");
                }

                // since r is not the most negative number, and we know since we're
                // C++20 and later that this is 2's complement arithmetic, we can
                // simply negate r to get its absolute value if it's negative.
                auto l_as_unsigned = m::cast<uintmax_t>((l < 0) ? (-l) : l);

                // Let's kind of statically verify this somewhat obtusely
                static_assert(((-(std::numeric_limits<intmax_t>::max)()) - 1) ==
                              (std::numeric_limits<intmax_t>::min)());

                return safe_math_helper<RightT, uintmax_t, ResultT>::add(r, l_as_unsigned);
            }
        };

        //
        // Handle (unsigned [op] signed) -> signed
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_unsigned_v<LeftT> &&
                     std::is_signed_v<RightT> && std::is_signed_v<ResultT>
        struct safe_math_helper<LeftT, RightT, ResultT>
        {
            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                //
                // There are, perhaps, useful specializations which do not
                // use full uintmax_t and intmax_t values here. If LeftT and
                // RightT are uint8_t and int8_t respectively, what we are
                // doing here is downright wasteful. But the first use case
                // for this code is adding pointer sized offsets, so this
                // kind of optimization can come later in the form of explicit
                // specializations when there is demonstrated need.
                //
                // It shouldn't be difficult, and implementers can
                // delegate the remainder of the implementation to the
                // generic implementation for the other operations.
                //
                auto promoted_l = m::cast<uintmax_t>(l);
                auto promoted_r = m::cast<intmax_t>(r);

                //
                // There are efficient ways to approach this but it's more
                // important to be correct right now.
                //

                if (promoted_r >= 0)
                {
                    // Seems obvious but best to validate at compile time,
                    // stranger things have happened.
                    static_assert(std::numeric_limits<uintmax_t>::digits >=
                                  std::numeric_limits<intmax_t>::digits);

                    auto const r_as_unsigned = m::cast<uintmax_t>(promoted_r);

                    // Explicit type here because we want to be super tight on typing
                    uintmax_t const sum = promoted_l + r_as_unsigned;

                    if ((sum < promoted_l) || (sum < r_as_unsigned))
                        throw std::overflow_error("integer overflow");

                    return m::try_cast<ResultT>(sum);
                }

                //
                // This "loop" ensures that promoted_r is in the range of
                // -std::numeric_limits<intmax_t>::max() .. -1.
                //
                // In practice, it will only execute at most once since the
                // only value lower than that value is the negative power of
                // two just below that value. It's written as a loop so as
                // to avoid encoding 2's complement assumptions in the code
                // when possible; it should not make the code less efficient.
                //
                while (promoted_r < -(std::numeric_limits<intmax_t>::max)())
                {
                    // We're adding a negative, thus it's a subtrahend
                    constexpr uintmax_t subtrahend = (std::numeric_limits<intmax_t>::max)();
                    if (subtrahend > promoted_l)
                        throw std::overflow_error("integer overflow");

                    promoted_l -= subtrahend;
                    promoted_r += (std::numeric_limits<intmax_t>::max)();
                }

                // Now we know that promoted_r is in the range such that
                // applying the unary minus to it will work as intended,
                // that is, it will yield a positive integer in the range
                // of 0 .. std::numeric_limits<intmax_t>::max()
                //
                uintmax_t that_which_remains = m::cast<uintmax_t>(-promoted_r);

                if (that_which_remains > promoted_l)
                    throw std::overflow_error("integer overflow");

                promoted_l -= that_which_remains;

                return m::try_cast<ResultT>(promoted_l);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                if (r == (std::numeric_limits<intmax_t>::min)())
                {
                    // We can't overcome this case just throw
                    throw std::overflow_error("integer overflow");
                }

                // since r is not the most negative number, and we know since we're
                // C++20 and later that this is 2's complement arithmetic, we can
                // simply negate r to get its absolute value if it's negative.
                auto r_as_unsigned = m::cast<uintmax_t>((r < 0) ? (-r) : r);

                // Let's kind of statically verify this somewhat obtusely
                static_assert(((-(std::numeric_limits<intmax_t>::max)()) - 1) ==
                              (std::numeric_limits<intmax_t>::min)());

                return add(l, r_as_unsigned);
            }

        static constexpr ResultT
        multiply(LeftT l, RightT r)
        {
            // Unsigned × signed with signed result.

            if (l == 0 || r == 0)
                return 0;

            auto promoted_l = m::cast<uintmax_t>(l);
            auto promoted_r = m::cast<intmax_t>(r);

            if (promoted_r > 0)
            {
                // Unsigned × positive: treat as unsigned multiplication
                auto r_as_unsigned = m::cast<uintmax_t>(promoted_r);
                auto prod = promoted_l * r_as_unsigned;

                // Check overflow
                if (prod / promoted_l != r_as_unsigned || prod / r_as_unsigned != promoted_l)
                {
                    throw std::overflow_error("integer overflow");
                }

                return m::try_cast<ResultT>(prod);
            }
            else
            {
                // Unsigned × negative: result is negative

                // Handle INT_MIN specially
                if (promoted_r == (std::numeric_limits<intmax_t>::min)())
                {
                    constexpr uintmax_t abs_min =
                        m::cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;

                    auto prod = promoted_l * abs_min;

                    // Check overflow
                    if (prod / promoted_l != abs_min || prod / abs_min != promoted_l)
                    {
                        throw std::overflow_error("integer overflow");
                    }

                    if (prod > abs_min)
                {
                    throw std::overflow_error(std::format(
                        "m::math::multiply overflow: result magnitude exceeds target type limits"));
                    }

                    if (prod == abs_min)
                    {
                        return (std::numeric_limits<ResultT>::min)();
                    }

                    return m::try_cast<ResultT>(-m::cast<intmax_t>(prod));
                }

                auto abs_r = m::cast<uintmax_t>(-promoted_r);
                auto prod = promoted_l * abs_r;

                // Check overflow
                if (prod / promoted_l != abs_r || prod / abs_r != promoted_l)
                {
                    throw std::overflow_error("integer overflow");
                }

                // Negate and check it fits
                constexpr uintmax_t max_negative =
                    m::cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;

                if (prod > max_negative)
                {
                    throw std::overflow_error(std::format(
                        "m::math::multiply overflow: result magnitude exceeds target type limits"));
                }

                if (prod == max_negative)
                {
                    return m::try_cast<ResultT>((std::numeric_limits<intmax_t>::min)());
                }

                return m::try_cast<ResultT>(-m::cast<intmax_t>(prod));
            }
        }

        static constexpr ResultT
        divide(LeftT l, RightT r)
        {
            // Unsigned / signed with signed result.
            
            if (r == 0)
                throw std::overflow_error(std::format(
                    "m::math::divide overflow: division by zero"));
            
            if (r < 0)
            {
                // Unsigned / negative = negative or zero
                // Result is -(l / |r|)
                
                if (r == (std::numeric_limits<RightT>::min)())
                {
                    // Handle most negative value specially
                    // Intentional: Computing abs(INT_MIN) = INT_MAX + 1 as constexpr
                    constexpr uintmax_t abs_min =
                        static_cast<uintmax_t>(-(m::cast<intmax_t>(
                            (std::numeric_limits<RightT>::min)()) + 1)) + 1;
                    auto l_promoted = m::cast<uintmax_t>(l);
                    auto quot = l_promoted / abs_min;
                    
                    if (quot > m::cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()))
                    {
                        throw std::overflow_error("integer overflow");
                    }
                    return m::try_cast<ResultT>(-m::cast<intmax_t>(quot));
                }
                
                auto l_promoted = m::cast<uintmax_t>(l);
                auto abs_r = m::cast<uintmax_t>(-m::cast<intmax_t>(r));
                auto quot = l_promoted / abs_r;
                
                if (quot > m::cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()))
                {
                    throw std::overflow_error("integer overflow");
                }
                return m::try_cast<ResultT>(-m::cast<intmax_t>(quot));
            }
            else
            {
                // Unsigned / positive signed = positive
                auto l_promoted = m::cast<uintmax_t>(l);
                auto r_as_unsigned = m::cast<uintmax_t>(r);
                auto quot = l_promoted / r_as_unsigned;
                return m::try_cast<ResultT>(quot);
            }
        }
        
        };

        //
        // Handle (signed [op] unsigned) -> unsigned
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_signed_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_unsigned_v<ResultT>
        struct safe_math_helper<LeftT, RightT, ResultT>
        {
            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                //
                // Adding signed + unsigned with unsigned result.
                // If l is negative, the mathematical result is negative or zero,
                // which can only be represented in unsigned if the result is exactly zero.
                //
                // If l is non-negative, we can safely cast it to unsigned and perform
                // unsigned + unsigned addition with overflow checking.
                //
                
                if (l < 0)
                {
                    // l is negative, r is unsigned.
                    // The mathematical result is r + l where l < 0.
                    // This is effectively r - |l|.
                    //
                    // Handle the special case where l is the most negative value
                    if (l == (std::numeric_limits<LeftT>::min)())
                    {
                        // We can't safely negate this value, so we need special handling
                        // Result = r - |min|
                        // This can only succeed if r >= |min|
                        // Intentional: Computing abs(INT_MIN) = INT_MAX + 1 as constexpr
                        constexpr uintmax_t abs_min = 
                            static_cast<uintmax_t>(-(m::cast<intmax_t>(
                                (std::numeric_limits<LeftT>::min)()) + 1)) + 1;
                        
                        auto promoted_r = m::cast<uintmax_t>(r);
                        
                        if (promoted_r < abs_min)
                        {
                            throw std::overflow_error("integer overflow");
                        }
                        
                        return m::try_cast<ResultT>(promoted_r - abs_min);
                    }
                    
                    // l is negative but not the most negative value, so we can negate it
                    auto abs_l = m::cast<uintmax_t>(-m::cast<intmax_t>(l));
                    auto promoted_r = m::cast<uintmax_t>(r);
                    
                    if (promoted_r < abs_l)
                    {
                        // Result would be negative
                        throw std::overflow_error("integer overflow");
                    }
                    
                    return m::try_cast<ResultT>(promoted_r - abs_l);
                }
                
                // l is non-negative, so we can treat this as unsigned + unsigned
                auto l_as_unsigned = m::cast<uintmax_t>(l);
                auto promoted_r = m::cast<uintmax_t>(r);
                
                auto sum = l_as_unsigned + promoted_r;
                
                // Check for unsigned overflow
                if (sum < l_as_unsigned || sum < promoted_r)
                {
                    throw std::overflow_error("integer overflow");
                }
                
                return m::try_cast<ResultT>(sum);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                //
                // Subtracting unsigned from signed with unsigned result.
                // Mathematical result is l - r.
                // This must be non-negative to fit in an unsigned type.
                //
                
                if (l < 0)
                {
                    // l is negative, so l - r is definitely negative
                    throw std::overflow_error("integer overflow");
                }
                
                // l is non-negative
                auto l_as_unsigned = m::cast<uintmax_t>(l);
                auto promoted_r = m::cast<uintmax_t>(r);
                
                if (l_as_unsigned < promoted_r)
                {
                    // Result would be negative
                    throw std::overflow_error("integer overflow");
                }
                
                auto diff = l_as_unsigned - promoted_r;
                
                return m::try_cast<ResultT>(diff);
            }

            static constexpr ResultT
            multiply(LeftT l, RightT r)
            {
                // Signed × unsigned with unsigned result.
                // Result must be non-negative, so l must be non-negative.

                if (l == 0 || r == 0)
                    return 0;

                if (l < 0)


                {


                    // Negative × positive = negative (cannot represent in unsigned)


                    throw std::overflow_error(std::format(


                        "m::math::multiply overflow: negative value cannot be represented in unsigned result type"));
                }

                // Both effectively unsigned now
                auto l_as_unsigned = m::cast<uintmax_t>(l);
                auto r_promoted = m::cast<uintmax_t>(r);

                auto prod = l_as_unsigned * r_promoted;

                // Check overflow
                if (prod / l_as_unsigned != r_promoted || prod / r_promoted != l_as_unsigned)
                {
                    throw std::overflow_error("integer overflow");
                }

                return m::try_cast<ResultT>(prod);
            }

            static constexpr ResultT
            divide(LeftT l, RightT r)
            {
                // Signed / unsigned with unsigned result.
                // Result must be non-negative, so l must be non-negative.
            
            if (r == 0)
                throw std::overflow_error(std::format(
                    "m::math::divide overflow: division by zero"));
            
            if (l < 0)
            {
                // Negative / positive = negative (can't represent in unsigned)
                throw std::overflow_error("integer overflow");
            }
            
            // Both effectively unsigned now
            auto l_as_unsigned = m::cast<uintmax_t>(l);
            auto r_promoted = m::cast<uintmax_t>(r);
            
            auto quot = l_as_unsigned / r_promoted;
            
            return m::try_cast<ResultT>(quot);
        }
        
        };

        //
        // Handle (signed [op] signed) -> signed
        //
        template <typename LeftT, typename RightT, typename ResultT>
            requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_signed_v<LeftT> &&
                     std::is_signed_v<RightT> && std::is_signed_v<ResultT>
        struct safe_math_helper<LeftT, RightT, ResultT>
        {
            using common_type_t = std::common_type_t<LeftT, RightT>;

            // Should go without saying, but...
            static_assert(std::is_signed_v<common_type_t>);

            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                // Intentional: common_type_t determined by type traits
                auto promoted_l = static_cast<common_type_t>(l);
                auto promoted_r = static_cast<common_type_t>(r);  // common_type_t varies by context

                //
                // Detect overflow before performing the addition in common_type_t
                // This handles the case where common_type_t might overflow.
                //
                // Positive overflow: both operands positive and sum would exceed max
                // Check: r > 0 && l > max - r
                //
                // Negative overflow: both operands negative and sum would go below min  
                // Check: r < 0 && l < min - r
                //
                constexpr auto max_common = (std::numeric_limits<common_type_t>::max)();
                constexpr auto min_common = (std::numeric_limits<common_type_t>::min)();

                if (promoted_r > 0 && promoted_l > max_common - promoted_r)
                {
                    throw std::overflow_error("integer overflow");
                }

                if (promoted_r < 0 && promoted_l < min_common - promoted_r)
                {
                    throw std::overflow_error("integer overflow");
                }

                common_type_t const rv = promoted_l + promoted_r;

                return m::try_cast<ResultT>(rv);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                // Intentional: common_type_t determined by type traits
                auto promoted_l = static_cast<common_type_t>(l);
                auto promoted_r = static_cast<common_type_t>(r);  // common_type_t varies by context

                //
                // Detect overflow before performing the subtraction in common_type_t
                //
                // Positive overflow: subtracting a negative from a positive
                // Check: r < 0 && l > max - (-r) which is l > max + r
                //
                // Negative overflow: subtracting a positive from a negative
                // Check: r > 0 && l < min + r
                //
                constexpr auto max_common = (std::numeric_limits<common_type_t>::max)();
                constexpr auto min_common = (std::numeric_limits<common_type_t>::min)();

                if (promoted_r < 0 && promoted_l > max_common + promoted_r)
                {
                    throw std::overflow_error("integer overflow");
                }

                if (promoted_r > 0 && promoted_l < min_common + promoted_r)
                {
                    throw std::overflow_error("integer overflow");
                }

                auto const rv = promoted_l - promoted_r;

                return m::try_cast<ResultT>(rv);
            }

        static constexpr ResultT
        multiply(LeftT l, RightT r)
        {
            // Signed × signed with signed result.

            if (l == 0 || r == 0)
                return 0;

            auto promoted_l = m::cast<intmax_t>(l);
            auto promoted_r = m::cast<intmax_t>(r);

            // Determine the sign of the result
            bool result_negative = (promoted_l < 0) != (promoted_r < 0);

            // Work with absolute values
            uintmax_t abs_l, abs_r;

            if (promoted_l == (std::numeric_limits<intmax_t>::min)())
            {
                abs_l = m::cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
            }
            else
            {
                abs_l = m::cast<uintmax_t>(promoted_l < 0 ? -promoted_l : promoted_l);
            }

            if (promoted_r == (std::numeric_limits<intmax_t>::min)())
            {
                abs_r = m::cast<uintmax_t>((std::numeric_limits<intmax_t>::max)()) + 1;
            }
            else
            {
                abs_r = m::cast<uintmax_t>(promoted_r < 0 ? -promoted_r : promoted_r);
            }

            auto prod = abs_l * abs_r;

            // Check for overflow in the multiplication itself
            if (prod / abs_l != abs_r || prod / abs_r != abs_l)
            {
                throw std::overflow_error("integer overflow");
            }

            // Check if the result fits in the signed range
            constexpr uintmax_t max_positive = m::cast<uintmax_t>((std::numeric_limits<intmax_t>::max)());
            constexpr uintmax_t max_negative = max_positive + 1;

            if (result_negative)
            {
                if (prod > max_negative)
                {
                    throw std::overflow_error(std::format(
                        "m::math::multiply overflow: result magnitude exceeds target type limits"));
                }

                if (prod == max_negative)
                {
                    return m::try_cast<ResultT>((std::numeric_limits<intmax_t>::min)());
                }

                return m::try_cast<ResultT>(-m::cast<intmax_t>(prod));
            }
            else
            {
                if (prod > max_positive)
                {
                    throw std::overflow_error(std::format(
                        "m::math::multiply overflow: result magnitude exceeds target type limits"));
                }

                return m::try_cast<ResultT>(m::cast<intmax_t>(prod));
            }
        }

        static constexpr ResultT
        divide(LeftT l, RightT r)
        {
            // Signed / signed with signed result.
            // Special case: INT_MIN / -1 = overflow (result would be INT_MAX + 1)
            
            if (r == 0)
                throw std::overflow_error(std::format(
                    "m::math::divide overflow: division by zero"));
            
            auto promoted_l = m::cast<intmax_t>(l);
            auto promoted_r = m::cast<intmax_t>(r);
            
            // Check for INT_MIN / -1
            if (promoted_l == (std::numeric_limits<intmax_t>::min)() && promoted_r == -1)
            {
                throw std::overflow_error("integer overflow");
            }
            
            auto quot = promoted_l / promoted_r;
            
            return m::try_cast<ResultT>(quot);
        }
        
        };

        template <typename InputT, typename ResultT, typename Enable = void>
        struct unary_safe_math_helper;

        // Unary ops, signed -> signed
        template <typename InputT, typename ResultT>
            requires m::is_integral_non_bool_v<InputT> && m::is_integral_non_bool_v<ResultT> &&
                     std::is_signed_v<InputT> && std::is_signed_v<ResultT>
        struct unary_safe_math_helper<InputT, ResultT>
        {
            static constexpr ResultT
            negate(InputT v)
            {
                if ((v == (std::numeric_limits<InputT>::min)()) &&
                    (std::numeric_limits<InputT>::digits == std::numeric_limits<intmax_t>::digits))
                {
                    // There is no way to negate the most negative intmax_t
                    throw std::overflow_error(std::format(
                    "m::math::negate overflow: value cannot be negated in target type"));
                }

                // lazy implementation for other cases
                //
                auto vmax = m::cast<intmax_t>(v);
                return m::try_cast<ResultT>(-vmax);
            }
        };

        // Unary ops, signed -> unsigned
        template <typename InputT, typename ResultT>
            requires m::is_integral_non_bool_v<InputT> && m::is_integral_non_bool_v<ResultT> &&
                     std::is_signed_v<InputT> && std::is_unsigned_v<ResultT>
        struct unary_safe_math_helper<InputT, ResultT>
        {
            static constexpr ResultT
            negate(InputT v)
            {
                //
                // Negating a signed value to produce an unsigned result.
                // This can only succeed if the input is negative (since -negative = positive).
                // Special case: the most negative value may not be representable.
                //
                if ((v == (std::numeric_limits<InputT>::min)()) &&
                    (std::numeric_limits<InputT>::digits == std::numeric_limits<intmax_t>::digits))
                {
                    // There is no way to negate the most negative intmax_t
                    throw std::overflow_error(std::format(
                    "m::math::negate overflow: value cannot be negated in target type"));
                }

                // lazy implementation for other cases
                //
                auto vmax = m::cast<intmax_t>(v);
                return m::try_cast<ResultT>(-vmax);
            }
        };

        // Unary ops, unsigned -> signed
        template <typename InputT, typename ResultT>
            requires m::is_integral_non_bool_v<InputT> && m::is_integral_non_bool_v<ResultT> &&
                     std::is_unsigned_v<InputT> && std::is_signed_v<ResultT>
        struct unary_safe_math_helper<InputT, ResultT>
        {
            static constexpr ResultT
            negate(InputT v)
            {
                auto vmax = m::cast<uintmax_t>(v);

                // negmax is the absolute value of the most negative ResultT, as a uintmax_t.
                // Intentional: Complex constexpr computation of abs(min) value
                constexpr auto negmax = static_cast<uintmax_t>(static_cast<intmax_t>(
                                            -((std::numeric_limits<ResultT>::min)() + 1))) +
                                        1;

                if (vmax > negmax)
                    throw std::overflow_error(std::format(
                    "m::math::negate overflow: value cannot be negated in target type"));

                if (vmax == negmax)
                    return (std::numeric_limits<ResultT>::min)();

                // Intentional: Final result narrowing checked by caller
                return -static_cast<ResultT>(v);
            }
        };

        // Unary ops, unsigned -> unsigned
        template <typename InputT, typename ResultT>
            requires m::is_integral_non_bool_v<InputT> && m::is_integral_non_bool_v<ResultT> &&
                     std::is_unsigned_v<InputT> && std::is_unsigned_v<ResultT>
        struct unary_safe_math_helper<InputT, ResultT>
        {
            static constexpr ResultT
            negate(InputT v)
            {
                if (v == 0)
                    return 0;

                throw std::overflow_error(std::format(
                    "m::math::negate overflow: value cannot be negated in target type"));
            }
        };

        //
        // overflow and underflow safe arithmetic
        //
        // If the operation overflows, an exception is thrown.
        //

        template <typename LeftType, typename RightType, typename SumType>
            requires m::is_integral_non_bool_v<LeftType> && m::is_integral_non_bool_v<RightType> &&
                     m::is_integral_non_bool_v<SumType>
        constexpr SumType
        add(LeftType l, RightType r, SumType = {})
        {
            return safe_math_helper<LeftType, RightType, SumType>::add(l, r);
        } // namespace math

        template <typename LeftType, typename RightType, typename DifferenceType>
            requires m::is_integral_non_bool_v<LeftType> && m::is_integral_non_bool_v<RightType> &&
                     m::is_integral_non_bool_v<DifferenceType>
        constexpr DifferenceType
        subtract(LeftType l, RightType r, DifferenceType = {})

        {
            return safe_math_helper<LeftType, RightType, DifferenceType>::subtract(l, r);
        }

        template <typename LeftType, typename RightType, typename ProductType>
            requires m::is_integral_non_bool_v<LeftType> && m::is_integral_non_bool_v<RightType> &&
                     m::is_integral_non_bool_v<ProductType>
        constexpr ProductType
        multiply(LeftType l, RightType r, ProductType = {})

        {
            return safe_math_helper<LeftType, RightType, ProductType>::multiply(l, r);
        }

        template <typename LeftType, typename RightType, typename QuotientType>
            requires m::is_integral_non_bool_v<LeftType> && m::is_integral_non_bool_v<RightType> &&
                     m::is_integral_non_bool_v<QuotientType>
        constexpr QuotientType
        divide(LeftType l, RightType r, QuotientType = {})

        {
            return safe_math_helper<LeftType, RightType, QuotientType>::divide(l, r);
        }

        template <typename T, typename ResultType>
            requires m::is_integral_non_bool_v<T> && m::is_integral_non_bool_v<ResultType>
        constexpr ResultType
        negate(T v, ResultType = {})
        {
            return unary_safe_math_helper<T, ResultType>::negate(v);
        }
    } // namespace math
} // namespace m
