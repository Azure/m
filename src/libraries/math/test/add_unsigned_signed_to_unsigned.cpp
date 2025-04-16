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

namespace
{
    //
    // Testing harness to enable testing addition of two types with an expected
    // sum type and whether the sum type is expected to
    // be able to hold all the sums.
    //
    // If, for example, the types are uint32_t, uint32_t, uint64_t then we
    // expect that no overflows will happen.
    //
    template <typename LeftType, typename RightType, typename SumType>
    void
    exercise_add()
    {
        constexpr auto l_zero     = static_cast<LeftType>(0);
        constexpr auto l_one      = static_cast<LeftType>(1);
        constexpr auto l_greatest = std::numeric_limits<LeftType>::max();

        constexpr auto r_least    = std::numeric_limits<RightType>::min();
        constexpr auto r_min1     = static_cast<RightType>(-1);
        constexpr auto r_zero     = static_cast<RightType>(0);
        constexpr auto r_one      = static_cast<RightType>(1);
        constexpr auto r_greatest = std::numeric_limits<RightType>::max();

        EXPECT_EQ(m::math::add(l_zero, r_zero, SumType{}), 0);

        EXPECT_EQ(m::math::add(l_zero, r_one, SumType{}), 1);

        EXPECT_EQ(m::math::add(l_one, r_zero, SumType{}), 1);

        EXPECT_EQ(m::math::add(l_one, r_min1, SumType{}), 0);

        // This could be debated how this addition would be performed in
        // abstract but in our theoretical system, the math is performed
        // in the full integer set, Z, and then cast back into the
        // SumType. There is no debate about whether the -1 was cast
        // to unsigned first or any such nonsense. It's zero plus
        // minus one which is minus one and since SumType is unsigned,
        // that cannot be represented => overflow_error.
        EXPECT_THROW(m::math::add(l_zero, r_min1, SumType{}), std::overflow_error)
            << " with LeftType: " << typeid(LeftType).name()
            << ", RightType: " << typeid(RightType).name()
            << ", and SumType: " << typeid(SumType).name();

        //
        // We will consider this inductive proof that addition works
        // up to the limits.
        //
        EXPECT_EQ(m::math::add(l_one, r_one, SumType{}), 2);

        //
        // Originally the logic here was in terms of numeric_limits<T>::max() but
        // those values are of type T and then you can get weird results comparing
        // them.
        //
        // It's safer to compare numeric_limits<T>::digits which is always a
        // small unsigned number. It can be more confusing when dealing with
        // signed / unsigned but in this case it's trivial.
        //
        constexpr auto ldigits = std::numeric_limits<LeftType>::digits;
        constexpr auto rdigits = std::numeric_limits<RightType>::digits + 1; // CHEATING!! :-)
        constexpr auto sdigits = std::numeric_limits<SumType>::digits;

#if 0
        std::cout << std::format(
            "LeftType: {}, ldigits = {}, RightType: {} rdigits = {}, SumType: {}, sdigits = {}\n", typeid(LeftType).name(), ldigits, typeid(RightType).name(), rdigits, typeid(SumType).name(), sdigits);
#endif

        if (ldigits >= sdigits)
        {
            if (ldigits > sdigits)
            {
                //
            }
        }

#if 0
        //
        // REFACTOR THIS!
        //
        // Literally!
        //
        // I kept trying to make the well factored version and kept messing it up
        // so instead I have constructed the horrible to the leaf version. there
        // are obvious repetitions but I fear making another factoring mistake
        // which is less valuable than getting it right.
        //
        if (ldigits >= sdigits)
        {
            if (ldigits > sdigits)
            {
                // The left type is outright larger than the sum type. Even adding
                // zero to the largest left will throw an exception.
                EXPECT_THROW(m::math::add(l_greatest, r_zero, SumType{}), std::overflow_error)
                    << " with LeftType: " << typeid(LeftType).name()
                    << ", RightType: " << typeid(RightType).name()
                    << ", and SumType: " << typeid(SumType).name();

                EXPECT_THROW(m::math::add(l_greatest, r_one, SumType{}), std::overflow_error)
                    << " with LeftType: " << typeid(LeftType).name()
                    << ", RightType: " << typeid(RightType).name()
                    << ", and SumType: " << typeid(SumType).name();

                EXPECT_THROW(m::math::add(l_greatest, r_greatest, SumType{}), std::overflow_error)
                    << " with LeftType: " << typeid(LeftType).name()
                    << ", RightType: " << typeid(RightType).name()
                    << ", and SumType: " << typeid(SumType).name();

                if (rdigits >= sdigits)
                {
                    //
                    // (ldigits > sdigits) && (rdigits >= sdigits)
                    //
                    if (rdigits > sdigits)
                    {
                        //
                        // (ldigits > sdigits) && (rdigits > sdigits)
                        //
                        EXPECT_THROW(m::math::add(l_zero, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();

                        EXPECT_THROW(m::math::add(l_one, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();

                        EXPECT_THROW(m::math::add(l_greatest, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();
                    }
                    else
                    {
                        //
                        // (ldigits > sdigits) && (rdigits == sdigits)
                        //
                        EXPECT_EQ(m::math::add(l_zero, r_greatest, SumType{}),
                                  static_cast<SumType>(l_zero) + static_cast<SumType>(r_greatest))
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();

                        EXPECT_THROW(m::math::add(l_one, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();

                        EXPECT_THROW(m::math::add(l_greatest, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();
                    }
                }
                else
                {
                    //
                    // (ldigits > sdigits) && (rdigits < sdigits)
                    //
                    // l_greatest operations throwing has already been validated.
                    //
                    // r_greatest will not throw.
                    //
                    EXPECT_EQ(m::math::add(l_zero, r_greatest, SumType{}),
                              static_cast<SumType>(l_zero) + static_cast<SumType>(r_greatest))
                        << " with LeftType: " << typeid(LeftType).name()
                        << ", RightType: " << typeid(RightType).name()
                        << ", and SumType: " << typeid(SumType).name();

                    EXPECT_EQ(m::math::add(l_one, r_greatest, SumType{}),
                              static_cast<SumType>(l_one) + static_cast<SumType>(r_greatest))
                        << " with LeftType: " << typeid(LeftType).name()
                        << ", RightType: " << typeid(RightType).name()
                        << ", and SumType: " << typeid(SumType).name();
                }
            }
            else
            {
                //
                // (ldigits == sdigits)
                //
                EXPECT_EQ(m::math::add(l_greatest, r_zero, SumType{}),
                          static_cast<SumType>(l_greatest) + static_cast<SumType>(r_zero))
                    << " with LeftType: " << typeid(LeftType).name()
                    << ", RightType: " << typeid(RightType).name()
                    << ", and SumType: " << typeid(SumType).name();

                EXPECT_THROW(m::math::add(l_greatest, r_one, SumType{}), std::overflow_error)
                    << " with LeftType: " << typeid(LeftType).name()
                    << ", RightType: " << typeid(RightType).name()
                    << ", and SumType: " << typeid(SumType).name();

                EXPECT_THROW(m::math::add(l_greatest, r_greatest, SumType{}), std::overflow_error)
                    << " with LeftType: " << typeid(LeftType).name()
                    << ", RightType: " << typeid(RightType).name()
                    << ", and SumType: " << typeid(SumType).name();

                if (rdigits >= sdigits)
                {
                    if (rdigits > sdigits)
                    {
                        EXPECT_THROW(m::math::add(l_zero, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();

                        EXPECT_THROW(m::math::add(l_one, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();

                        EXPECT_THROW(m::math::add(l_greatest, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();
                    }
                    else
                    {
                        // (ldigits == sdigits) && (rdigits == sdigits)
                        EXPECT_EQ(m::math::add(l_zero, r_greatest, SumType{}),
                                  static_cast<SumType>(l_zero) + static_cast<SumType>(r_greatest))
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();

                        EXPECT_THROW(m::math::add(l_one, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();

                        EXPECT_THROW(m::math::add(l_greatest, r_greatest, SumType{}),
                                     std::overflow_error)
                            << " with LeftType: " << typeid(LeftType).name()
                            << ", RightType: " << typeid(RightType).name()
                            << ", and SumType: " << typeid(SumType).name();
                    }
                }
                else
                {
                    // (ldigits == sdigits) && (rdigits < sdigits)
                }
            }
        }
        else
        {
            // (ldigits < sdigits)
            EXPECT_EQ(m::math::add(l_greatest, r_zero, SumType{}),
                      static_cast<SumType>(l_greatest) + static_cast<SumType>(r_zero))
                << " with LeftType: " << typeid(LeftType).name()
                << ", RightType: " << typeid(RightType).name()
                << ", and SumType: " << typeid(SumType).name();

            EXPECT_EQ(m::math::add(l_greatest, r_one, SumType{}),
                      static_cast<SumType>(l_greatest) + static_cast<SumType>(r_one))
                << " with LeftType: " << typeid(LeftType).name()
                << ", RightType: " << typeid(RightType).name()
                << ", and SumType: " << typeid(SumType).name();

            if (rdigits >= sdigits)
            {
                if (rdigits > sdigits)
                {
                    EXPECT_THROW(m::math::add(l_zero, r_greatest, SumType{}), std::overflow_error)
                        << " with LeftType: " << typeid(LeftType).name()
                        << ", RightType: " << typeid(RightType).name()
                        << ", and SumType: " << typeid(SumType).name();

                    EXPECT_THROW(m::math::add(l_one, r_greatest, SumType{}), std::overflow_error)
                        << " with LeftType: " << typeid(LeftType).name()
                        << ", RightType: " << typeid(RightType).name()
                        << ", and SumType: " << typeid(SumType).name();

                    EXPECT_THROW(m::math::add(l_greatest, r_greatest, SumType{}),
                                 std::overflow_error)
                        << " with LeftType: " << typeid(LeftType).name()
                        << ", RightType: " << typeid(RightType).name()
                        << ", and SumType: " << typeid(SumType).name();
                }
                else
                {
                    // (ldigits < sdigits) && (rdigits == sdigits)
                    EXPECT_EQ(m::math::add(l_zero, r_greatest, SumType{}),
                              static_cast<SumType>(l_zero) + static_cast<SumType>(r_greatest))
                        << " with LeftType: " << typeid(LeftType).name()
                        << ", RightType: " << typeid(RightType).name()
                        << ", and SumType: " << typeid(SumType).name();

                    EXPECT_THROW(m::math::add(l_one, r_greatest, SumType{}), std::overflow_error)
                        << " with LeftType: " << typeid(LeftType).name()
                        << ", RightType: " << typeid(RightType).name()
                        << ", and SumType: " << typeid(SumType).name();

                    EXPECT_THROW(m::math::add(l_greatest, r_greatest, SumType{}),
                                 std::overflow_error)
                        << " with LeftType: " << typeid(LeftType).name()
                        << ", RightType: " << typeid(RightType).name()
                        << ", and SumType: " << typeid(SumType).name();
                }
            }
            else
            {
                // (ldigits < sdigits) && (rdigits < sdigits)
                //
                // The one time that summing the greatest values should not throw.
                //
                EXPECT_EQ(m::math::add(l_greatest, r_greatest, SumType{}),
                          static_cast<SumType>(l_greatest) + static_cast<SumType>(r_greatest))
                    << " with LeftType: " << typeid(LeftType).name()
                    << ", RightType: " << typeid(RightType).name()
                    << ", and SumType: " << typeid(SumType).name();
            }
        }
#endif
    }
} // namespace

TEST(ExerciseUnsignedAdd, Add_uint64_int64_to_uint64)
{
    exercise_add<uint64_t, int64_t, uint64_t>();
}

TEST(ExerciseUnsignedAdd, Add_uint32_int32_to_uint32)
{
    exercise_add<uint32_t, int32_t, uint32_t>();
}

TEST(ExerciseUnsignedAdd, Add_uint32_int32_to_uint64)
{
    exercise_add<uint32_t, int32_t, uint64_t>();
}

TEST(ExerciseUnsignedAdd, Add_uint32_int64_to_uint32)
{
    exercise_add<uint32_t, int64_t, uint32_t>();
}

TEST(ExerciseUnsignedAdd, Add_uint64_int32_to_uint32)
{
    exercise_add<uint64_t, int32_t, uint32_t>();
}

TEST(ExerciseUnsignedAdd, Add_uint32_int64_to_uint64)
{
    exercise_add<uint32_t, int64_t, uint64_t>();
}

TEST(ExerciseUnsignedAdd, Add_uint64_int32_to_uint64)
{
    exercise_add<uint64_t, int32_t, uint64_t>();
}
