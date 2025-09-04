// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <m/pil/pil.h>
#include <m/pil/registry.h>

#include "redirecting/redirecting.h"

using m::pil::key_path;

using namespace std::string_view_literals;

using P = std::pair<std::u16string_view, std::u16string_view>;

auto r_il_1 = std::initializer_list<P>{
    P{u"HKLM\\Software"sv, u"HKCU\\FooTemp1234\\Software"sv},
    P{u"HKLM\\Software\\Microsoft\\Xyz"sv, u"HKCU\\Temp987\\Pdq\\MjgWasHere"sv},
    P{u"HKEY_CLASSES_ROOT\\CLSID"sv, u"HKCU\\FooTemp1234\\HKCR"sv},
};

TEST(TestRedirectingRedirector, ValidateHKLMNotMapped)
{
    auto r = std::make_shared<m::pil::impl::redirecting::redirector>(r_il_1);

    // HKLM itself is not mapped
    EXPECT_EQ(r->map_public_to_private(key_path(u"HKLM"sv)), key_path(u"HKLM"sv));
}

TEST(TestRedirectingRedirector, ValidateHKLMSoftwareMicrosoftMapped)
{
    auto r = std::make_shared<m::pil::impl::redirecting::redirector>(r_il_1);

    EXPECT_EQ(r->map_public_to_private(key_path(u"HKLM\\Software\\Microsoft"sv)),
              key_path(u"HKCU\\FooTemp1234\\Software\\Microsoft"sv));

    EXPECT_EQ(r->map_public_to_private(key_path(u"HKLM\\Software\\Microsoft\\Access"sv)),
              key_path(u"HKCU\\FooTemp1234\\Software\\Microsoft\\Access"sv));

    EXPECT_EQ(r->map_public_to_private(key_path(u"HKLM\\Software\\Microsoft\\Xyz"sv)),
              key_path(u"HKCU\\Temp987\\Pdq\\MjgWasHere"sv));
}
