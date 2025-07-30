// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <m/strings/compare.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(Strings_Compare_optional_wstring_view, Basic1)
{
    auto x = m::case_insensitive_less<std::optional<std::wstring_view>>{};

    EXPECT_TRUE(x(L"a"s, L"b"s));
}

TEST(Strings_Compare_optional_wstring_view, Basic2)
{
    auto x = m::case_insensitive_less<std::optional<std::wstring_view>>{};

    EXPECT_TRUE(x(L"a"s, L"B"s));
}
TEST(Strings_Compare_optional_wstring_view, Basic3)
{
    auto x = m::case_insensitive_less<std::optional<std::wstring_view>>{};

    EXPECT_TRUE(x(L"a"s, L"banana"s));
}
TEST(Strings_Compare_optional_wstring_view, Basic4)
{
    auto x = m::case_insensitive_less<std::optional<std::wstring_view>>{};

    EXPECT_TRUE(x(L"apple"s, L"b"s));
}
TEST(Strings_Compare_optional_wstring_view, Basic5)
{
    auto x = m::case_insensitive_less<std::optional<std::wstring_view>>{};

    EXPECT_TRUE(x(L"apple"s, L"banana"s));
}

TEST(Strings_Compare_optional_wstring_view, Basic6)
{
    auto x = m::case_insensitive_less<std::optional<std::wstring_view>>{};

    EXPECT_TRUE(x(L"apple"s, L"BANANA"s));
}

TEST(Strings_Compare_optional_wstring_view, NonReflexive1)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    EXPECT_FALSE(x(L"a"s, L"a"s));
}

TEST(Strings_Compare_optional_wstring_view, NonReflexive2)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    EXPECT_FALSE(x(L"A"s, L"a"s));
}

TEST(Strings_Compare_optional_wstring_view, NonReflexive3)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    EXPECT_FALSE(x(L"a"s, L"A"s));
}

TEST(Strings_Compare_optional_wstring_view, NonReflexive4)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    EXPECT_FALSE(x(L"A"s, L"A"s));
}

TEST(Strings_Compare_optional_wstring_view, Gt_B_A)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>();
    EXPECT_FALSE(x(L"B"s, L"A"s));
}

TEST(Strings_Compare_optional_wstring_view, Gt_B_a)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>();
    EXPECT_FALSE(x(L"B"s, L"a"s));
}

TEST(Strings_Compare_optional_wstring_view, Gt_Banana_Apple)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>();
    EXPECT_FALSE(x(L"Banana"s, L"Apple"s));
}

TEST(Strings_Compare_optional_wstring_view, Gt_BANANA_apple)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>();
    EXPECT_FALSE(x(L"BANANA"s, L"apple"s));
}

TEST(Strings_Compare_optional_wstring_view, Views1)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    EXPECT_TRUE(x(L"apple"s, L"BANANA"s));
}

TEST(Strings_Compare_optional_wstring_view, Views2)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    EXPECT_TRUE(x(L"apple"sv, L"BANANA"sv));
}

TEST(Strings_Compare_optional_wstring_view, Views3)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    EXPECT_TRUE(x(L"apple"sv, L"BANANA"sv));
}

TEST(Strings_Compare_optional_wstring_view, Views4)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    EXPECT_TRUE(x(L"apple"sv, L"BANANA"sv));
}

//
// matrix testing:
// 
//

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_s_nullopt_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = L"Banana"s;
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_s_Apple_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = L"Banana"s;
    auto const r = L"Apple"sv;

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_s_Banana_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = L"Banana"s;
    auto const r = L"Banana"sv;

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_s_Cherry_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = L"Banana"s;
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(x(l, r));
}




TEST(Strings_Compare_optional_wstring_view, cmp_Banana_sv_nullopt_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = L"Banana"sv;
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_sv_Apple_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = L"Banana"sv;
    auto const r = L"Apple"sv;

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_sv_Banana_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = L"Banana"sv;
    auto const r = L"Banana"sv;

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_sv_Cherry_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = L"Banana"sv;
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(x(l, r));
}




TEST(Strings_Compare_optional_wstring_view, cmp_Banana_osv_nullopt_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring_view>(L"Banana"sv);
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_osv_Apple_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring_view>(L"Banana"sv);
    auto const r = L"Apple"sv;

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_osv_Banana_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring_view>(L"Banana"sv);
    auto const r = L"Banana"sv;

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_osv_Cherry_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring_view>(L"Banana"sv);
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(x(l, r));
}


TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_osv_nullopt_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring_view>(std::nullopt);
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_osv_Apple_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring_view>(std::nullopt);
    auto const r = L"Apple"sv;

    EXPECT_TRUE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_osv_Banana_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring_view>(std::nullopt);
    auto const r = L"Banana"sv;

    EXPECT_TRUE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_osv_Cherry_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring_view>(std::nullopt);
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(x(l, r));
}










TEST(Strings_Compare_optional_wstring_view, cmp_Banana_os_nullopt_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring>(L"Banana"s);
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_os_Apple_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring>(L"Banana"s);
    auto const r = L"Apple"sv;

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_os_Banana_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring>(L"Banana"s);
    auto const r = L"Banana"sv;

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_os_Cherry_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring>(L"Banana"s);
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_os_nullopt_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring>(std::nullopt);
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_os_Apple_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring>(std::nullopt);
    auto const r = L"Apple"sv;

    EXPECT_TRUE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_os_Banana_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring>(std::nullopt);
    auto const r = L"Banana"sv;

    EXPECT_TRUE(x(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_os_Cherry_sv)
{
    auto const x = m::case_insensitive_less<std::optional<std::wstring_view>>{};
    auto const l = std::optional<std::wstring>(std::nullopt);
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(x(l, r));
}
