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

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/exception.h>

#include "buffered/buffered.h"
#include "fault/fault.h"

using namespace std::string_view_literals;

#ifdef WIN32

namespace
{
    namespace fault = m::pil::impl::fault;

    // Build a sealed snapshot fixture file holding HKCU with one seed value so
    // the fault decorator runs over a deterministic, win32-free base world.
    void
    write_snapshot_fixture(std::filesystem::path const& p)
    {
        std::error_code ec;
        std::filesystem::remove(p, ec);

        auto pf  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r   = pf.get_registry();
        auto k1  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto app = k1.create_key(L"MFAULT_Seed"sv);
        app.set_value(L"seed"sv, 1u);

        pf.save(p);
    }

    // Wrap a fresh copy of the base world with the fault-injecting layer driven
    // by script, returning the friendly platform over it.
    m::pil::platform
    make_fault_platform(std::filesystem::path const&              snapshot,
                        std::shared_ptr<fault::fault_script> const& script)
    {
        auto fault_plat = std::make_shared<fault::platform>(
            m::pil::impl::buffered::create_platform_from_persisted_xml(snapshot), script);
        return m::pil::platform{std::shared_ptr<m::pil::iplatform>(fault_plat)};
    }
} // namespace

// M-FAULT-3: a counted rule fires on exactly the Nth matching occurrence, not
// before, and (being one-shot) not again afterward.
TEST(Fault, CountedMatchFiresOnNthOccurrenceAndNotBefore)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mfault_counted.xml";
    write_snapshot_fixture(snapshot);

    auto script = std::make_shared<fault::fault_script>();
    auto p      = make_fault_platform(snapshot, script);

    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    // Build the base structure before any rule is installed, so these creates
    // pass through, then install a rule using the target's real path.
    auto app    = hkcu.create_key(L"FaultApp"sv);
    auto target = app.create_key(L"Target"sv);

    script->add_rule(fault::fault_rule(fault::fault_operation::open_key,
                                       target.get_path(),
                                       std::nullopt,
                                       3,
                                       fault::fault_action::out_of_resources));

    // 1st and 2nd opens succeed (not before the Nth).
    EXPECT_NO_THROW(static_cast<void>(app.open_key(L"Target"sv)));
    EXPECT_NO_THROW(static_cast<void>(app.open_key(L"Target"sv)));

    // 3rd open fires.
    EXPECT_THROW(static_cast<void>(app.open_key(L"Target"sv)), m::out_of_resources);

    // 4th open succeeds: the rule is one-shot on the Nth occurrence.
    EXPECT_NO_THROW(static_cast<void>(app.open_key(L"Target"sv)));

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

// M-FAULT-3: multiple rules compose; each counts its own matching operations
// independently, and one rule firing does not consume another's counter.
TEST(Fault, MultipleRulesComposeIndependently)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mfault_compose.xml";
    write_snapshot_fixture(snapshot);

    auto script = std::make_shared<fault::fault_script>();
    auto p      = make_fault_platform(snapshot, script);

    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    auto app  = hkcu.create_key(L"FaultApp"sv);
    auto keyA = app.create_key(L"KeyA"sv);
    auto keyB = app.create_key(L"KeyB"sv);

    script->add_rule(fault::fault_rule(fault::fault_operation::open_key,
                                       keyA.get_path(),
                                       std::nullopt,
                                       1,
                                       fault::fault_action::access_denied));
    script->add_rule(fault::fault_rule(fault::fault_operation::open_key,
                                       keyB.get_path(),
                                       std::nullopt,
                                       2,
                                       fault::fault_action::out_of_resources));

    // Rule A fires on its first open of KeyA.
    EXPECT_THROW(static_cast<void>(app.open_key(L"KeyA"sv)), m::access_denied);

    // Rule B is unaffected by A: its first open of KeyB succeeds, its second fires.
    EXPECT_NO_THROW(static_cast<void>(app.open_key(L"KeyB"sv)));
    EXPECT_THROW(static_cast<void>(app.open_key(L"KeyB"sv)), m::out_of_resources);

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

// M-FAULT-3: operations that match no rule pass through unchanged.
TEST(Fault, NonMatchingOperationsPassThroughUnchanged)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mfault_passthrough.xml";
    write_snapshot_fixture(snapshot);

    auto script = std::make_shared<fault::fault_script>();
    // A rule targeting a path the test never touches.
    script->add_rule(fault::fault_rule(
        fault::fault_operation::open_key,
        m::pil::key_path(u"HKEY_CURRENT_USER"sv) + m::pil::key_path(u"Ghost"sv),
        std::nullopt,
        1,
        fault::fault_action::out_of_resources));

    auto p = make_fault_platform(snapshot, script);

    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    // None of these match the Ghost rule, so all forward transparently.
    auto app = hkcu.create_key(L"FaultApp"sv);
    app.set_value(L"v"sv, 42u);
    EXPECT_EQ(app.get_uint32_value(L"v"sv), 42u);

    auto sub = app.create_key(L"Sub"sv);
    EXPECT_NO_THROW(static_cast<void>(app.open_key(L"Sub"sv)));
    EXPECT_TRUE(app.try_open_key(L"Sub"sv).has_value());

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

// M-FAULT-3: a script parsed from the <FaultScript> grammar fires against the
// matching operation, exercising parse_fault_script and full-path matching end
// to end.
TEST(Fault, ParsedScriptFiresOnMatchingCreate)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mfault_parsed.xml";
    write_snapshot_fixture(snapshot);

    // Discover the real absolute path the decorator will compute for the target
    // key, so the parsed rule's path matches exactly. (The probe platform's
    // in-memory mutations are never persisted, so the snapshot stays clean.)
    m::pil::key_path parsed_path;
    {
        auto probe_script = std::make_shared<fault::fault_script>();
        auto probe        = make_fault_platform(snapshot, probe_script);
        auto pr           = probe.get_registry();
        auto phkcu        = pr.open_predefined_key(m::pil::predefined_key::current_user);
        parsed_path       = phkcu.create_key(L"ParsedApp"sv).get_path();
    }

    pugi::xml_document doc;
    auto               root = doc.append_child(L"FaultScript");
    auto               rule = root.append_child(L"Rule");
    rule.append_attribute(L"operation").set_value(L"create_key");
    rule.append_attribute(L"path").set_value(m::to_wstring(parsed_path.native().view()).c_str());
    rule.append_attribute(L"occurrence").set_value(L"1");
    rule.append_attribute(L"action").set_value(L"access_denied");

    auto script = fault::parse_fault_script(root);
    auto p      = make_fault_platform(snapshot, script);

    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    EXPECT_THROW(static_cast<void>(hkcu.create_key(L"ParsedApp"sv)), m::access_denied);

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

#endif // WIN32
