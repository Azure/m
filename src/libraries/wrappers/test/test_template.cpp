// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <compare>

#include <m/wrappers/wrapper_template.h>

namespace
{
    // Unique tag types for test instantiations
    struct tag_nonscalar {};
    struct tag_scalar {};

    using test_nonscalar = m::nonscalar_wrapper<int, tag_nonscalar>;
    using test_scalar    = m::scalar_wrapper<int, tag_scalar>;
} // namespace

// ── nonscalar_wrapper ────────────────────────────────────────────────────────

TEST(NonScalarWrapper, ValueInit)
{
    test_nonscalar x{};
    EXPECT_EQ(static_cast<int>(x), 0);
}

TEST(NonScalarWrapper, ExplicitConstruct)
{
    test_nonscalar x(42);
    EXPECT_EQ(static_cast<int>(x), 42);
}

TEST(NonScalarWrapper, EqualityTrue)
{
    EXPECT_EQ(test_nonscalar(7), test_nonscalar(7));
}

TEST(NonScalarWrapper, EqualityFalse)
{
    EXPECT_NE(test_nonscalar(1), test_nonscalar(2));
}

TEST(NonScalarWrapper, CopyConstruct)
{
    test_nonscalar a(10);
    test_nonscalar b(a); // NOLINT
    EXPECT_EQ(a, b);
}

TEST(NonScalarWrapper, CopyAssign)
{
    test_nonscalar a(10);
    test_nonscalar b(99);
    b = a;
    EXPECT_EQ(static_cast<int>(b), 10);
}

TEST(NonScalarWrapper, MoveConstruct)
{
    test_nonscalar a(10);
    test_nonscalar b(std::move(a));
    EXPECT_EQ(static_cast<int>(b), 10);
}

TEST(NonScalarWrapper, MoveAssign)
{
    test_nonscalar a(10);
    test_nonscalar b(0);
    b = std::move(a);
    EXPECT_EQ(static_cast<int>(b), 10);
}

TEST(NonScalarWrapper, Swap)
{
    test_nonscalar a(1);
    test_nonscalar b(2);
    a.swap(b);
    EXPECT_EQ(static_cast<int>(a), 2);
    EXPECT_EQ(static_cast<int>(b), 1);
}

TEST(NonScalarWrapper, ConstexprUsage)
{
    constexpr test_nonscalar x(99);
    static_assert(static_cast<int>(x) == 99);
    EXPECT_EQ(static_cast<int>(x), 99);
}

// ── scalar_wrapper ───────────────────────────────────────────────────────────

TEST(ScalarWrapper, ThreeWayCompare)
{
    test_scalar a(1);
    test_scalar b(2);
    EXPECT_LT(static_cast<int>(a), static_cast<int>(b));
    EXPECT_TRUE((a <=> b) < 0);
    EXPECT_TRUE((b <=> a) > 0);
    EXPECT_TRUE((a <=> a) == 0);
}
