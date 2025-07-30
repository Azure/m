// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <m/math/math.h>
#include <m/type_traits/type_traits.h>

namespace
{
    template <typename TFrom, typename TTo, typename Enable = void>
    struct NegationTester;

    template <typename TFrom, typename TTo>
    struct NegationTester<
        TFrom,
        TTo,
        std::enable_if_t<m::is_integral_non_bool_v<TFrom> && m::is_integral_non_bool_v<TTo> &&
                         std::is_signed_v<TFrom> && std::is_signed_v<TTo>>>
    {
        //
    };

} // namespace

//TEST(ExerciseUnsignedAdd, Add_uint64_int64_to_uint64)
//{
//    exercise_add<uint64_t, int64_t, uint64_t>();
//}
