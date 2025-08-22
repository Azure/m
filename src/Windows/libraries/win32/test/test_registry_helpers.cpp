// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <Windows.h>

#include <m/win32/registry.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(Win32RegistryHelpers, PositivePredefinedKeyLookup)
{
    auto pk1 = m::win32::registry::try_map_string_to_predefined_key(L"HKLM"sv);
    EXPECT_EQ(pk1, m::win32::registry::predefined_key::local_machine);

    auto pk2 = m::win32::registry::try_map_string_to_predefined_key(L"hklm"sv);
    EXPECT_EQ(pk2, m::win32::registry::predefined_key::local_machine);

    auto pk3 = m::win32::registry::try_map_string_to_predefined_key(L"HkLm"sv);
    EXPECT_EQ(pk3, m::win32::registry::predefined_key::local_machine);

}

TEST(Win32RegistryHelpers, NegativePredefinedKeyLookup)
{
    auto pk1 = m::win32::registry::try_map_string_to_predefined_key(L"xHKLM"sv);
    EXPECT_FALSE(pk1.has_value());

    auto pk2 = m::win32::registry::try_map_string_to_predefined_key(L"hxlm"sv);
    EXPECT_FALSE(pk2.has_value());

    auto pk3 = m::win32::registry::try_map_string_to_predefined_key(L"HkLmxxxxa"sv);
    EXPECT_FALSE(pk3.has_value());
}
