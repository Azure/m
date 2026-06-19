// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

#include <pugixml.hpp>

#include <m/pil/fault.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/exception.h>

using namespace std::string_view_literals;

#ifdef WIN32

namespace
{
    // Build a sealed snapshot fixture file holding HKCU with one seed value so
    // the public fault surface runs over a deterministic, win32-free base world.
    void
    write_snapshot_fixture(std::filesystem::path const& p)
    {
        std::error_code ec;
        std::filesystem::remove(p, ec);

        auto pf  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r   = pf.get_registry();
        auto k1  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto app = k1.create_key(L"MFAULTPUB_Seed"sv);
        app.set_value(L"seed"sv, 1u);

        pf.save(p);
    }

    // Wrap a fresh sealed copy of the base world with the fault-injecting layer
    // driven by script, using only the public surface, returning the friendly
    // value-wrapper platform over it.
    m::pil::platform
    make_public_fault_platform(std::filesystem::path const& snapshot,
                               m::pil::fault_script const&  script)
    {
        auto underlying = m::pil::load_platform_interface(snapshot);
        return m::pil::platform{m::pil::apply_fault_layer(underlying, script)};
    }
} // namespace

// M-FAULTCFG-1: a programmatically-built script applied through apply_fault_layer
// fires on exactly the Nth matching occurrence and is one-shot.
TEST(FaultPublic, ProgrammaticRuleFiresOnNthOccurrence)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mfaultpub_prog.xml";
    write_snapshot_fixture(snapshot);

    m::pil::fault_script script;
    auto                 p = make_public_fault_platform(snapshot, script);

    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto app  = hkcu.create_key(L"PubApp"sv);
    auto tgt  = app.create_key(L"Target"sv);

    script.add_rule(m::pil::fault_operation::open_key,
                    tgt.get_path(),
                    std::nullopt,
                    2,
                    m::pil::fault_action::access_denied);

    EXPECT_NO_THROW(static_cast<void>(app.open_key(L"Target"sv)));
    EXPECT_THROW(static_cast<void>(app.open_key(L"Target"sv)), m::access_denied);
    EXPECT_NO_THROW(static_cast<void>(app.open_key(L"Target"sv)));

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

// M-FAULTCFG-1: a script parsed from the <FaultScript> grammar via the public
// parse_fault_script fires against the matching operation.
TEST(FaultPublic, ParsedScriptFiresOnMatchingCreate)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mfaultpub_parsed.xml";
    write_snapshot_fixture(snapshot);

    // Discover the absolute path the decorator computes for the target key so
    // the parsed rule path matches exactly. The probe layer's in-memory
    // mutations are never persisted, so the snapshot stays clean.
    m::pil::key_path parsed_path;
    {
        m::pil::fault_script probe_script;
        auto                 probe = make_public_fault_platform(snapshot, probe_script);
        auto pr    = probe.get_registry();
        auto phkcu = pr.open_predefined_key(m::pil::predefined_key::current_user);
        parsed_path = phkcu.create_key(L"ParsedApp"sv).get_path();
    }

    pugi::xml_document doc;
    auto               root = doc.append_child(L"FaultScript");
    auto               rule = root.append_child(L"Rule");
    rule.append_attribute(L"operation").set_value(L"create_key");
    rule.append_attribute(L"path").set_value(m::to_wstring(parsed_path.native().view()).c_str());
    rule.append_attribute(L"occurrence").set_value(L"1");
    rule.append_attribute(L"action").set_value(L"access_denied");

    auto script = m::pil::parse_fault_script(root);
    auto p      = make_public_fault_platform(snapshot, script);

    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    EXPECT_THROW(static_cast<void>(hkcu.create_key(L"ParsedApp"sv)), m::access_denied);

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

// M-FAULTCFG-1: a fault script loaded from a file via load_fault_script fires
// against the matching operation, exercising the file-loading entry point.
TEST(FaultPublic, LoadedScriptFromFileFires)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mfaultpub_loaded.xml";
    write_snapshot_fixture(snapshot);

    m::pil::key_path parsed_path;
    {
        m::pil::fault_script probe_script;
        auto                 probe = make_public_fault_platform(snapshot, probe_script);
        auto pr    = probe.get_registry();
        auto phkcu = pr.open_predefined_key(m::pil::predefined_key::current_user);
        parsed_path = phkcu.create_key(L"LoadedApp"sv).get_path();
    }

    auto const script_path = std::filesystem::temp_directory_path() / "mfaultpub_script.xml";
    {
        pugi::xml_document doc;
        auto               root = doc.append_child(L"FaultScript");
        auto               rule = root.append_child(L"Rule");
        rule.append_attribute(L"operation").set_value(L"create_key");
        rule.append_attribute(L"path").set_value(
            m::to_wstring(parsed_path.native().view()).c_str());
        rule.append_attribute(L"occurrence").set_value(L"1");
        rule.append_attribute(L"action").set_value(L"out_of_resources");
        ASSERT_TRUE(doc.save_file(script_path.native().c_str()));
    }

    auto script = m::pil::load_fault_script(script_path);
    auto p      = make_public_fault_platform(snapshot, script);

    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    EXPECT_THROW(static_cast<void>(hkcu.create_key(L"LoadedApp"sv)), m::out_of_resources);

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
    std::filesystem::remove(script_path, ec);
}

// M-FAULTCFG-1: operations that match no rule pass through unchanged when the
// fault layer is applied via the public surface.
TEST(FaultPublic, NonMatchingOperationsPassThrough)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mfaultpub_pass.xml";
    write_snapshot_fixture(snapshot);

    m::pil::fault_script script;
    script.add_rule(m::pil::fault_operation::open_key,
                    m::pil::key_path(u"HKEY_CURRENT_USER"sv) + m::pil::key_path(u"Ghost"sv),
                    std::nullopt,
                    1,
                    m::pil::fault_action::out_of_resources);

    auto p = make_public_fault_platform(snapshot, script);

    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto app  = hkcu.create_key(L"PassApp"sv);
    app.set_value(L"v"sv, 42u);
    EXPECT_EQ(app.get_uint32_value(L"v"sv), 42u);
    EXPECT_NO_THROW(static_cast<void>(app.create_key(L"Sub"sv)));
    EXPECT_NO_THROW(static_cast<void>(app.open_key(L"Sub"sv)));

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

#endif // WIN32
