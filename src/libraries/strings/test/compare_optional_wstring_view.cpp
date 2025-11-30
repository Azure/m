// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <m/strings/compare.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace
{
    auto const lt_ws = m::case_insensitive_less<std::wstring>{};
}

TEST(Strings_Compare_optional_wstring_view, Basic1) { EXPECT_TRUE(lt_ws(L"a"s, L"b"s)); }

TEST(Strings_Compare_optional_wstring_view, Basic2) { EXPECT_TRUE(lt_ws(L"a"s, L"B"s)); }

TEST(Strings_Compare_optional_wstring_view, Basic3) { EXPECT_TRUE(lt_ws(L"a"s, L"banana"s)); }

TEST(Strings_Compare_optional_wstring_view, Basic4) { EXPECT_TRUE(lt_ws(L"apple"s, L"b"s)); }

TEST(Strings_Compare_optional_wstring_view, Basic5) { EXPECT_TRUE(lt_ws(L"apple"s, L"banana"s)); }

TEST(Strings_Compare_optional_wstring_view, Basic6) { EXPECT_TRUE(lt_ws(L"apple"s, L"BANANA"s)); }

TEST(Strings_Compare_optional_wstring_view, NonReflexive1) { EXPECT_FALSE(lt_ws(L"a"s, L"a"s)); }

TEST(Strings_Compare_optional_wstring_view, NonReflexive2) { EXPECT_FALSE(lt_ws(L"A"s, L"a"s)); }

TEST(Strings_Compare_optional_wstring_view, NonReflexive3) { EXPECT_FALSE(lt_ws(L"a"s, L"A"s)); }

TEST(Strings_Compare_optional_wstring_view, NonReflexive4) { EXPECT_FALSE(lt_ws(L"A"s, L"A"s)); }

TEST(Strings_Compare_optional_wstring_view, Gt_B_A) { EXPECT_FALSE(lt_ws(L"B"s, L"A"s)); }

TEST(Strings_Compare_optional_wstring_view, Gt_B_a) { EXPECT_FALSE(lt_ws(L"B"s, L"a"s)); }

TEST(Strings_Compare_optional_wstring_view, Gt_Banana_Apple)
{
    EXPECT_FALSE(lt_ws(L"Banana"s, L"Apple"s));
}

TEST(Strings_Compare_optional_wstring_view, Gt_BANANA_apple)
{
    EXPECT_FALSE(lt_ws(L"BANANA"s, L"apple"s));
}

TEST(Strings_Compare_optional_wstring_view, Views1) { EXPECT_TRUE(lt_ws(L"apple"s, L"BANANA"s)); }

TEST(Strings_Compare_optional_wstring_view, Views2) { EXPECT_TRUE(lt_ws(L"apple"sv, L"BANANA"sv)); }

TEST(Strings_Compare_optional_wstring_view, Views3) { EXPECT_TRUE(lt_ws(L"apple"sv, L"BANANA"sv)); }

TEST(Strings_Compare_optional_wstring_view, Views4) { EXPECT_TRUE(lt_ws(L"apple"sv, L"BANANA"sv)); }

//
// matrix testing:
//
//

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_s_nullopt_sv)
{
    auto const l = L"Banana"s;
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_s_Apple_sv)
{
    auto const l = L"Banana"s;
    auto const r = L"Apple"sv;

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_s_Banana_sv)
{
    auto const l = L"Banana"s;
    auto const r = L"Banana"sv;

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_s_Cherry_sv)
{
    auto const l = L"Banana"s;
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_sv_nullopt_sv)
{
    auto const l = L"Banana"sv;
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_sv_Apple_sv)
{
    auto const l = L"Banana"sv;
    auto const r = L"Apple"sv;

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_sv_Banana_sv)
{
    auto const l = L"Banana"sv;
    auto const r = L"Banana"sv;

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_sv_Cherry_sv)
{
    auto const l = L"Banana"sv;
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_osv_nullopt_sv)
{
    auto const l = std::optional<std::wstring_view>(L"Banana"sv);
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_osv_Apple_sv)
{
    auto const l = std::optional<std::wstring_view>(L"Banana"sv);
    auto const r = L"Apple"sv;

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_osv_Banana_sv)
{
    auto const l = std::optional<std::wstring_view>(L"Banana"sv);
    auto const r = L"Banana"sv;

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_osv_Cherry_sv)
{
    auto const l = std::optional<std::wstring_view>(L"Banana"sv);
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_osv_nullopt_sv)
{
    auto const l = std::optional<std::wstring_view>(std::nullopt);
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_osv_Apple_sv)
{
    auto const l = std::optional<std::wstring_view>(std::nullopt);
    auto const r = L"Apple"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_osv_Banana_sv)
{
    auto const l = std::optional<std::wstring_view>(std::nullopt);
    auto const r = L"Banana"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_osv_Cherry_sv)
{
    auto const l = std::optional<std::wstring_view>(std::nullopt);
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_os_nullopt_sv)
{
    auto const l = std::optional<std::wstring>(L"Banana"s);
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_os_Apple_sv)
{
    auto const l = std::optional<std::wstring>(L"Banana"s);
    auto const r = L"Apple"sv;

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_os_Banana_sv)
{
    auto const l = std::optional<std::wstring>(L"Banana"s);
    auto const r = L"Banana"sv;

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_Banana_os_Cherry_sv)
{
    auto const l = std::optional<std::wstring>(L"Banana"s);
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_os_nullopt_sv)
{
    auto const l = std::optional<std::wstring>(std::nullopt);
    auto const r = std::optional<std::wstring_view>(std::nullopt);

    EXPECT_FALSE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_os_Apple_sv)
{
    auto const l = std::optional<std::wstring>(std::nullopt);
    auto const r = L"Apple"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_os_Banana_sv)
{
    auto const l = std::optional<std::wstring>(std::nullopt);
    auto const r = L"Banana"sv;

    EXPECT_TRUE(lt_ws(l, r));
}

TEST(Strings_Compare_optional_wstring_view, cmp_nullopt_os_Cherry_sv)
{
    auto const l = std::optional<std::wstring>(std::nullopt);
    auto const r = L"Cherry"sv;

    EXPECT_TRUE(lt_ws(l, r));
}
