// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <m/pil/pil.h>
#include <m/pil/registry.h>
#include <m/print/print.h>

using namespace std::string_view_literals;

TEST(TestLoggingRegistry, TryCreatingKey)
{
    auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates |
                                   m::pil::make_platform_flags::record_modifications);
    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k2 = k1.open_key(L"Software\\Microsoft"sv);

    auto k3 = k2.create_key(u"test_key");

    k3.set_string_value(L"name", L"Joe Amazing");
    k3.set_value(L"age", 24);

    p.save(L"log1.xml"sv);
}
