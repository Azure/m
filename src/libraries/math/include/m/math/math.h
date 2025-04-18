// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include <m/cast/cast.h>
#include <m/cast/try_cast.h>
#include <m/type_traits/type_traits.h>

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
        struct safe_math_helper<
            LeftT,
            RightT,
            ResultT>
        {
            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                auto       lmax = uintmax_t{l};
                auto       rmax = uintmax_t{r};
                auto const rv   = uintmax_t{lmax + rmax};

                if ((rv < lmax) || (rv < rmax))
                    throw std::overflow_error("integer overflow");

                return m::try_cast<ResultT>(rv);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                if (r > l)
                    throw std::overflow_error("integer overflow");

                auto       lmax = uintmax_t{l};
                auto       rmax = uintmax_t{r};
                auto const rv   = uintmax_t{lmax - rmax};

                if ((rv < lmax) || (rv < rmax))
                    throw std::overflow_error("integer overflow");

                return m::try_cast<ResultT>(rv);
            }
        };

        //
        // Handle (unsigned [op] unsigned) -> signed
        //
        template <typename LeftT, typename RightT, typename ResultT>
        requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_unsigned_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_signed_v<ResultT>
        struct safe_math_helper<
            LeftT,
            RightT,
            ResultT>
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
                    m::try_cast<common_type_t>(-(std::numeric_limits<ResultT>::min() + 1));

                if (v >= biggest_negative_as_positive)
                    throw std::overflow_error("value too negative");

                return -m::try_cast<ResultT>(v);
            }
        };

        //
        // Handle (unsigned [op] signed) -> unsigned
        //
        template <typename LeftT, typename RightT, typename ResultT>
        requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_unsigned_v<LeftT> &&
                     std::is_signed_v<RightT> && std::is_unsigned_v<ResultT>
        struct safe_math_helper<
            LeftT,
            RightT,
            ResultT>
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

                    auto const r_as_unsigned = static_cast<uintmax_t>(promoted_r);

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
                // to avoid encoding 2s complement assumptions in the code
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
                uintmax_t that_which_remains = static_cast<uintmax_t>(-promoted_r);

                if (that_which_remains > promoted_l)
                    throw std::overflow_error("integer overflow");

                promoted_l -= that_which_remains;

                return m::try_cast<ResultT>(promoted_l);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                // Again to make the math easy even if small (16 bit) math ends
                // up taking 64 bits, we're just going to promote everything to
                // uintmax_t and bluster along.
                //
                auto promoted_l = static_cast<uintmax_t>(l);
                auto promoted_r = static_cast<intmax_t>(r);

                if (r == std::numeric_limits<intmax_t>::min())
                {
                    // We can't overcome this case just throw
                    throw std::overflow_error("integer overflow");
                }

                // since r is not the most negative number, and we know since we're
                // C++20 and later that this is 2s compliment arithmetic, we can
                // simply negate r to get its absolute value if it's negative.
                auto r_as_unsigned = static_cast<uintmax_t>((r < 0) ? (-r) : r);

                // Let's kind of statically verify this somewhat obtusely
                static_assert(((-std::numeric_limits<intmax_t>::max()) - 1) ==
                              std::numeric_limits<intmax_t>::min());

                return add(l, r_as_unsigned);
            }
        };

        //
        // Handle (signed [op] unsigned) -> signed
        //
        template <typename LeftT, typename RightT, typename ResultT>
        requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_signed_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_signed_v<ResultT>
        struct safe_math_helper<
            LeftT,
            RightT,
            ResultT>
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

                    auto const l_as_unsigned = static_cast<uintmax_t>(promoted_l);

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
                // to avoid encoding 2s complement assumptions in the code
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

                uintmax_t that_which_remains = static_cast<uintmax_t>(-promoted_l);

                if (that_which_remains > promoted_r)
                    throw std::overflow_error("integer overflow");

                promoted_r -= that_which_remains;

                return m::try_cast<ResultT>(promoted_r);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                auto promoted_l = static_cast<intmax_t>(l);
                auto promoted_r = static_cast<uintmax_t>(r);

                if (l == std::numeric_limits<intmax_t>::min())
                {
                    // We can't overcome this case just throw
                    throw std::overflow_error("integer overflow");
                }

                // since r is not the most negative number, and we know since we're
                // C++20 and later that this is 2s compliment arithmetic, we can
                // simply negate r to get its absolute value if it's negative.
                auto l_as_unsigned = static_cast<uintmax_t>((l < 0) ? (-l) : l);

                // Let's kind of statically verify this somewhat obtusely
                static_assert(((-std::numeric_limits<intmax_t>::max()) - 1) ==
                              std::numeric_limits<intmax_t>::min());

                return add(r, l_as_unsigned);
            }
        };

        //
        // Handle (unsigned [op] signed) -> signed
        //
        template <typename LeftT, typename RightT, typename ResultT>
        requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_unsigned_v<LeftT> &&
                     std::is_signed_v<RightT> && std::is_signed_v<ResultT>
        struct safe_math_helper<
            LeftT,
            RightT,
            ResultT>
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

                    auto const r_as_unsigned = static_cast<uintmax_t>(promoted_r);

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
                // to avoid encoding 2s complement assumptions in the code
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
                uintmax_t that_which_remains = static_cast<uintmax_t>(-promoted_r);

                if (that_which_remains > promoted_l)
                    throw std::overflow_error("integer overflow");

                promoted_l -= that_which_remains;

                return m::try_cast<ResultT>(promoted_l);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                // Again to make the math easy even if small (16 bit) math ends
                // up taking 64 bits, we're just going to promote everything to
                // uintmax_t and bluster along.
                //
                auto promoted_l = static_cast<uintmax_t>(l);
                auto promoted_r = static_cast<intmax_t>(r);

                if (r == std::numeric_limits<intmax_t>::min())
                {
                    // We can't overcome this case just throw
                    throw std::overflow_error("integer overflow");
                }

                // since r is not the most negative number, and we know since we're
                // C++20 and later that this is 2s compliment arithmetic, we can
                // simply negate r to get its absolute value if it's negative.
                auto r_as_unsigned = static_cast<uintmax_t>((r < 0) ? (-r) : r);

                // Let's kind of statically verify this somewhat obtusely
                static_assert(((-std::numeric_limits<intmax_t>::max()) - 1) ==
                              std::numeric_limits<intmax_t>::min());

                return add(l, r_as_unsigned);
            }
        };

        //
        // Handle (signed [op] unsigned) -> unsigned
        //
        template <typename LeftT, typename RightT, typename ResultT>
        requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_signed_v<LeftT> &&
                     std::is_unsigned_v<RightT> && std::is_unsigned_v<ResultT>
        struct safe_math_helper<
            LeftT,
            RightT,
            ResultT>
        {
            using common_type_t = std::common_type_t<LeftT, RightT>;

            // Should go without saying, but...
            static_assert(std::is_signed_v<common_type_t>);

            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                common_type_t const rv =
                    static_cast<common_type_t>(l) + static_cast<common_type_t>(r);

                // BUGGY

                if ((rv < l) || (rv < r) || (rv > (std::numeric_limits<ResultT>::max)()))
                    throw std::overflow_error("integer overflow");

                return m::try_cast<ResultT>(rv);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                if (r > l)
                    throw std::overflow_error("integer overflow");

                // BUGGY

                auto const rv = static_cast<common_type_t>(l) - static_cast<common_type_t>(r);

                return m::try_cast<ResultT>(rv);
            }
        };

        //
        // Handle (signed [op] signed) -> signed
        //
        template <typename LeftT, typename RightT, typename ResultT>
        requires m::is_integral_non_bool_v<LeftT> && m::is_integral_non_bool_v<RightT> &&
                     m::is_integral_non_bool_v<ResultT> && std::is_signed_v<LeftT> &&
                     std::is_signed_v<RightT> && std::is_signed_v<ResultT>
        struct safe_math_helper<
            LeftT,
            RightT,
            ResultT>
        {
            using common_type_t = std::common_type_t<LeftT, RightT>;

            // Should go without saying, but...
            static_assert(std::is_signed_v<common_type_t>);

            static constexpr ResultT
            add(LeftT l, RightT r)
            {
                common_type_t const rv =
                    static_cast<common_type_t>(l) + static_cast<common_type_t>(r);

                // BUGGY

                if ((rv < l) || (rv < r) || (rv > (std::numeric_limits<ResultT>::max)()))
                    throw std::overflow_error("integer overflow");

                return m::try_cast<ResultT>(rv);
            }

            static constexpr ResultT
            subtract(LeftT l, RightT r)
            {
                if (r > l)
                    throw std::overflow_error("integer overflow");

                // BUGGY

                auto const rv = static_cast<common_type_t>(l) - static_cast<common_type_t>(r);

                return m::try_cast<ResultT>(rv);
            }
        };

        template <typename InputT, typename ResultT, typename Enable = void>
        struct unary_safe_math_helper;

        // Unary ops, signed -> signed
        template <typename InputT, typename ResultT>
        requires m::is_integral_non_bool_v<InputT> && m::is_integral_non_bool_v<ResultT> &&
                     std::is_signed_v<InputT> && std::is_signed_v<ResultT>
        struct unary_safe_math_helper<
            InputT,
            ResultT>
        {
            static constexpr ResultT
            negate(InputT v)
            {
                if ((v == std::numeric_limits<InputT>::min()) &&
                    (std::numeric_limits<InputT>::digits == std::numeric_limits<intmax_t>::digits))
                {
                    // There is no way to negate the most negative intmax_t
                    throw std::overflow_error("v");
                }

                // lazy implementation for other cases
                //
                auto vmax = static_cast<intmax_t>(v);
                return m::try_cast<ResultT>(-vmax);
            }
        };

        // Unary ops, signed -> unsigned
        template <typename InputT, typename ResultT>
        requires m::is_integral_non_bool_v<InputT> && m::is_integral_non_bool_v<ResultT> &&
                     std::is_signed_v<InputT> && std::is_unsigned_v<ResultT>
        struct unary_safe_math_helper<
            InputT,
            ResultT>
        {
            static constexpr ResultT
            negate(InputT v)
            {
                throw std::overflow_error("v");
                if ((v == std::numeric_limits<InputT>::min()) &&
                    (std::numeric_limits<InputT>::digits == std::numeric_limits<intmax_t>::digits))
                {
                    // There is no way to negate the most negative intmax_t
                    throw std::overflow_error("v");
                }

                // lazy implementation for other cases
                //
                auto vmax = static_cast<intmax_t>(v);
                return m::try_cast<ResultT>(-vmax);
            }
        };

        // Unary ops, unsigned -> signed
        template <typename InputT, typename ResultT>
        requires m::is_integral_non_bool_v<InputT> && m::is_integral_non_bool_v<ResultT> &&
                     std::is_unsigned_v<InputT> && std::is_signed_v<ResultT>
        struct unary_safe_math_helper<
            InputT,
            ResultT>
        {
            static constexpr ResultT
            negate(InputT v)
            {
                auto vmax = static_cast<uintmax_t>(v);

                // negmax is the absolute value of the most negative ResultT, as a uintmax_t.
                constexpr auto negmax = static_cast<uintmax_t>(static_cast<intmax_t>(
                                            -(std::numeric_limits<ResultT>::min() + 1))) +
                                        1;

                if (vmax > negmax)
                    throw std::overflow_error("v");

                if (vmax == negmax)
                    return std::numeric_limits<ResultT>::min();

                return -static_cast<ResultT>(v);
            }
        };

        // Unary ops, unsigned -> unsigned
        template <typename InputT, typename ResultT>
        requires m::is_integral_non_bool_v<InputT> && m::is_integral_non_bool_v<ResultT> &&
                     std::is_unsigned_v<InputT> && std::is_unsigned_v<ResultT>
        struct unary_safe_math_helper<
            InputT,
            ResultT>
        {
            static constexpr ResultT
            negate(InputT v)
            {
                if (v == 0)
                    return 0;

                throw std::overflow_error("v");
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
