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
#include <m/print/print.h>

using namespace std::string_view_literals;

#ifdef WIN32

TEST(BufferedOverDirectRegistry, TryEnumeratingSoftwareMicrosoft)
{
    auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r    = p.get_registry();
    auto k1   = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k2   = k1.open_key(L"Software\\Microsoft"sv);
    auto keys = k2.list_subkey_names();

    for (auto&& e: keys)
    {
        m::println("key: {}", m::to_string(e.native().view()));
    }

    auto values = k2.list_value_names_and_types();

    for (auto&& e: values)
    {
        m::println("value: {{ name: {}, type: {} }}",
                   m::to_string(e.m_value_name),
                   std::to_underlying(e.m_reg_value_type));
    }

    EXPECT_EQ(1, 1);
    //
}

TEST(BufferedOverDirectRegistry, TryEnumeratingSoftwareMicrosoftWindiff)
{
    auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
    try
    {
        auto k2   = k1.open_key(L"Software\\Microsoft\\Windiff"sv);
        auto keys = k2.list_subkey_names();

        for (auto&& e: keys)
        {
            m::println("key: {}", m::to_string(e.native().view()));
        }

        auto values = k2.list_value_names_and_types();

        for (auto&& e: values)
        {
            m::println("value: {{ name: {}, type: {} }}",
                       m::to_string(e.m_value_name),
                       std::to_underlying(e.m_reg_value_type));
        }
    }
    catch (std::exception const&)
    {}

    EXPECT_EQ(1, 1);
    //
}

TEST(BufferedOverDirectRegistry, TrySettingStringValue)
{
    auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k2 = k1.open_key(L"Software\\Microsoft"sv);

    k2.set_string_value(L"Value1"sv, L"First"sv);
    k2.set_string_value(L"Value1"sv, L"Second"sv);

    auto v = k2.get_string_value(L"Value1"sv);

    m::println("Value was: {}", m::to_string(v));

    EXPECT_EQ(1, 1);
}

TEST(BufferedOverDirectRegistry, TrySettingStringValuesBreakingEmplaceWithHint)
{
    auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k2 = k1.open_key(L"Software\\Microsoft"sv);

    k2.set_string_value(L"apple"sv, L"First"sv);
    k2.set_string_value(L"cherry"sv, L"Second"sv);
    k2.set_string_value(L"banana"sv, L"whatever"sv);

    auto v = k2.get_string_value(L"Cherry"sv);

    m::println("Value was: {}", m::to_string(v));

    EXPECT_EQ(1, 1);
}

TEST(BufferedOverDirectRegistry, TrySettingStringValuesBreakingEmplaceWithHint2)
{
    auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k2 = k1.open_key(L"Software\\Microsoft"sv);

    k2.set_string_value(L"apple"sv, L"First"sv);
    k2.set_string_value(L"cherry"sv, L"Second"sv);
    k2.set_string_value(L"banana"sv, L"whatever"sv);
    k2.set_string_value(L"cherry"sv, L"3"sv);
    k2.set_string_value(L"cherry"sv, L"4"sv);
    k2.set_string_value(L"cherry"sv, L"5"sv);
    k2.set_string_value(L"cherry"sv, L"6"sv);

    auto v = k2.get_string_value(L"Cherry"sv);

    m::println("Value was: {}", m::to_string(v));

    EXPECT_EQ(1, 1);
}

TEST(BufferedOverDirectRegistry, TryOpenKeyFindsExistingKey)
{
    auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);

    auto k2 = k1.try_open_key(L"Software"sv);

    EXPECT_TRUE(k2.has_value());
}

TEST(BufferedOverDirectRegistry, TryOpenKeyReturnsNulloptForMissingKey)
{
    auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);

    // A key that is exceedingly unlikely to exist: a tentative open of it
    // must report absence as std::nullopt rather than throwing.
    auto k2 = k1.try_open_key(
        L"Software\\m-pil-test-this-key-does-not-exist-7b3f1c9e"sv);

    EXPECT_FALSE(k2.has_value());
}

TEST(BufferedOverDirectRegistry, OpenAfterDeleteFails)
{
    // All buffered mutations stay in the in-memory overlay; the underlying
    // registry is never touched.
    auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k2 = k1.open_key(L"Software"sv);

    constexpr auto child_name = L"m-pil-test-open-after-delete-2e9a4d61"sv;

    // Create the child so there is something concrete to delete.
    k2.create_key(child_name);

    // It exists now: both open flavors must succeed.
    EXPECT_TRUE(k2.try_open_key(child_name).has_value());
    EXPECT_NO_THROW(std::ignore = k2.open_key(child_name));

    // Delete it (leaving a tombstone in the overlay).
    k2.delete_key(child_name);

    // Tentative open must now report absence as std::nullopt.
    EXPECT_FALSE(k2.try_open_key(child_name).has_value());

    // The throwing open flavor must fail rather than resurrect the key.
    EXPECT_THROW(std::ignore = k2.open_key(child_name), std::exception);
}

#endif
