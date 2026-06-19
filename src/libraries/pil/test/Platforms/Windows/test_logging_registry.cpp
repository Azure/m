// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <m/pil/pil.h>
#include <m/pil/registry.h>
#include <m/print/print.h>

using namespace std::string_view_literals;

namespace
{
    std::string
    read_file_text(std::filesystem::path const& p)
    {
        std::ifstream      in(p, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
} // namespace

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

// M-LOG-OUT-2 (D6): the persisted <Platform> must not carry the diagnostic log,
// and the requested-vs-done trace must still be obtainable from a separate side
// artifact written by save_diagnostic_log.
TEST(TestLoggingRegistry, DiagnosticLogIsSideArtifactNotInPersistedPlatform)
{
    auto const      platform_out = std::filesystem::temp_directory_path() / "mlogout2_platform.xml";
    auto const      diag_out     = std::filesystem::temp_directory_path() / "mlogout2_diag.xml";
    std::error_code ec;
    std::filesystem::remove(platform_out, ec);
    std::filesystem::remove(diag_out, ec);

    {
        auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates |
                                       m::pil::make_platform_flags::record_modifications);
        auto r  = p.get_registry();
        auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k2 = k1.create_key(L"MLOGOUT2_App"sv);
        k2.set_string_value(L"name", L"Joe");
        k2.set_value(L"age", 24u);

        p.save(platform_out);
        p.save_diagnostic_log(diag_out);
    }

    auto const platform_text = read_file_text(platform_out);
    auto const diag_text     = read_file_text(diag_out);

    // The persisted platform carries the registry snapshot but no log.
    EXPECT_NE(platform_text.find("<Platform"), std::string::npos);
    EXPECT_EQ(platform_text.find("<Log"), std::string::npos);
    EXPECT_EQ(platform_text.find("Registry.CreateKey"), std::string::npos);

    // The side diagnostic artifact carries the requested-vs-done trace.
    EXPECT_NE(diag_text.find("<DiagnosticLog"), std::string::npos);
    EXPECT_NE(diag_text.find("<Log"), std::string::npos);
    EXPECT_NE(diag_text.find("Registry.CreateKey"), std::string::npos);

    std::filesystem::remove(platform_out, ec);
    std::filesystem::remove(diag_out, ec);
}
