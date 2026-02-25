// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/windows_wrappers/win32_dword_ms.h>

TEST(Win32DWORDMs, ValueInit)
{
    // Value-initialization must zero-initialize the underlying DWORD.
    m::win32_dword_ms x{};
    EXPECT_EQ(static_cast<DWORD>(x), DWORD{0});
}

TEST(Win32DWORDMs, ExplicitConstruct)
{
    m::win32_dword_ms x(DWORD{100});
    EXPECT_EQ(static_cast<DWORD>(x), DWORD{100});
}

TEST(Win32DWORDMs, EqualityTrue)
{
    m::win32_dword_ms a(DWORD{42});
    m::win32_dword_ms b(DWORD{42});
    EXPECT_EQ(a, b);
}

TEST(Win32DWORDMs, EqualityFalse)
{
    m::win32_dword_ms a(DWORD{1});
    m::win32_dword_ms b(DWORD{2});
    EXPECT_NE(a, b);
}

TEST(Win32DWORDMs, CopyConstruct)
{
    m::win32_dword_ms a(DWORD{7});
    m::win32_dword_ms b(a); // NOLINT(performance-unnecessary-copy-initialization)
    EXPECT_EQ(a, b);
    EXPECT_EQ(static_cast<DWORD>(b), DWORD{7});
}

TEST(Win32DWORDMs, CopyAssign)
{
    m::win32_dword_ms a(DWORD{7});
    m::win32_dword_ms b(DWORD{99});
    b = a;
    EXPECT_EQ(static_cast<DWORD>(b), DWORD{7});
}

TEST(Win32DWORDMs, MoveConstruct)
{
    m::win32_dword_ms a(DWORD{55});
    m::win32_dword_ms b(std::move(a));
    EXPECT_EQ(static_cast<DWORD>(b), DWORD{55});
}

TEST(Win32DWORDMs, MoveAssign)
{
    m::win32_dword_ms a(DWORD{55});
    m::win32_dword_ms b(DWORD{0});
    b = std::move(a);
    EXPECT_EQ(static_cast<DWORD>(b), DWORD{55});
}

TEST(Win32DWORDMs, Swap)
{
    m::win32_dword_ms a(DWORD{10});
    m::win32_dword_ms b(DWORD{20});
    a.swap(b);
    EXPECT_EQ(static_cast<DWORD>(a), DWORD{20});
    EXPECT_EQ(static_cast<DWORD>(b), DWORD{10});
}

TEST(Win32DWORDMs, ConstexprUsage)
{
    constexpr m::win32_dword_ms x(DWORD{5000});
    static_assert(static_cast<DWORD>(x) == DWORD{5000});
    EXPECT_EQ(static_cast<DWORD>(x), DWORD{5000});
}
