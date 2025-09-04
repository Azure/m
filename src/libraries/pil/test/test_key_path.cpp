// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <m/pil/pil.h>
#include <m/pil/registry.h>

using m::pil::key_path;

using namespace std::string_view_literals;

TEST(TestKeyPath, CreateHklmPath1)
{
    auto p = key_path(u"HKLM\\Software"sv);

    EXPECT_EQ(p.root_key().has_value(), true);
    EXPECT_EQ(p.root_key().value(), m::pil::predefined_key::local_machine);
    EXPECT_EQ(p.native(), u"HKLM\\Software"sv);
    EXPECT_EQ(p.root(), key_path(u"HKLM"sv));
    EXPECT_EQ(p.relative_path(), u"Software"sv);
}

TEST(TestKeyPath, CreateHklmFromPredefinedKey)
{
    auto p = key_path(m::pil::predefined_key::local_machine);

    EXPECT_EQ(p.root_key().has_value(), true);
    EXPECT_EQ(p.root_key().value(), m::pil::predefined_key::local_machine);
    EXPECT_EQ(p.native(), u"HKLM"sv);
    EXPECT_EQ(p.root(), key_path(u"HKLM"sv));
    EXPECT_EQ(p.relative_path(), u""sv);
}

TEST(TestKeyPath, TestAppend)
{
    auto p = key_path(m::pil::predefined_key::local_machine);

    EXPECT_EQ(p.root_key().has_value(), true);
    EXPECT_EQ(p.root_key().value(), m::pil::predefined_key::local_machine);
    EXPECT_EQ(p.native(), u"HKLM"sv);
    EXPECT_EQ(p.root(), key_path(u"HKLM"sv));
    EXPECT_EQ(p.relative_path(), u""sv);

    auto p2 = p + u"Software"sv;
    EXPECT_EQ(p2.native(), u"HKLM\\Software"sv);

    auto p_microsoft = key_path(u"Microsoft"sv);
    auto p3          = p2 + p_microsoft;

    EXPECT_EQ(p3.native(), u"HKLM\\Software\\Microsoft"sv);
}

TEST(TestKeyPath, TestRelativePath)
{
    auto p = key_path(u"foo"sv);

    EXPECT_EQ(p.root_key().has_value(), false);
    EXPECT_EQ(p.native(), u"foo"sv);
    EXPECT_EQ(p.root(), key_path(u"foo"sv));
    EXPECT_EQ(p.relative_path(), u""sv);
    auto p2 = p + u"Software"sv;
    EXPECT_EQ(p2.native(), u"foo\\Software"sv);
    EXPECT_EQ(p2.root_key().has_value(), false);

    auto p_microsoft = key_path(u"Microsoft"sv);
    auto p3          = p2 + p_microsoft;

    EXPECT_EQ(p3.native(), u"foo\\Software\\Microsoft"sv);
    EXPECT_EQ(p3.root_key().has_value(), false);
}
