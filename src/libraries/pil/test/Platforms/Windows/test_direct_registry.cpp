// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <iostream>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include <m/pil/pil.h>
#include <m/pil/registry.h>
#include <m/print/print.h>

using namespace std::string_view_literals;

TEST(DirectRegistry, TryEnumeratingSoftwareMicrosoft)
{
    auto p = m::pil::make_platform(m::pil::platform_type::direct);
    auto r = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k2 = k1.open_key(L"Software\\Microsoft"sv);
    auto keys = k2.list_subkey_names();

    for (auto&& e : keys)
    {
        m::println("key: {}", m::to_string(e.native().view()));
    }

    auto values = k2.list_value_names_and_types();

    for (auto&& e : values)
    {
        m::println("value: {{ name: {}, type: {} }}",
                     m::to_string(e.m_value_name),
                     std::to_underlying(e.m_reg_value_type));
    }

    EXPECT_EQ(1, 1);
//
}
