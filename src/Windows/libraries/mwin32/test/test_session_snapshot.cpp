// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <system_error>
#include <utility>

#include <gtest/gtest.h>

#include <pugixml.hpp>

#include <m/pil/fault.h>
#include <m/pil/pil.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/exception.h>

#include "pilcfg.h"
#include "session.h"

using namespace std::string_view_literals;
using m::mwin32_impl::build_platform_from_config;
using m::mwin32_impl::pilcfg;

//
// Exercises the session's platform-selection logic (build_platform_from_config)
// directly, independent of the process-wide singleton and the host module's
// `.pilcfg` sidecar. The interesting new behavior is mode (c): when a persisted
// state file is configured the session must run against that snapshot.
//

namespace
{
    constexpr auto k_subkey  = L"M4_3_3_SessionSnapshot"sv;
    constexpr auto k_age     = 24u;
    constexpr auto k_age_name = L"age";
    constexpr auto k_name    = L"name";
    constexpr auto k_name_value = L"Joe";

    // Produce a persisted-state XML file describing a small overlay and return
    // its path. The caller owns deletion.
    std::filesystem::path
    write_snapshot_file()
    {
        auto const out = std::filesystem::temp_directory_path() /
                         "m4_3_3_session_snapshot.xml";
        std::error_code ec;
        std::filesystem::remove(out, ec);

        auto p     = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r     = p.get_registry();
        auto k1    = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k_app = k1.create_key(k_subkey);

        k_app.set_value(k_age_name, k_age);
        k_app.set_string_value(k_name, k_name_value);

        p.save(out);
        return out;
    }
} // namespace

TEST(SessionSnapshotSelection, PersistedStateConfigBuildsSnapshotPlatform)
{
    auto const snapshot = write_snapshot_file();

    pilcfg cfg;
    cfg.persisted_state = snapshot.u16string();

    auto iface = build_platform_from_config(cfg);
    ASSERT_NE(iface, nullptr);

    m::pil::platform snap(std::move(iface));
    auto             r     = snap.get_registry();
    auto             k1    = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto             k_app = k1.open_key(k_subkey);

    EXPECT_EQ(k_app.get_uint32_value(L"age"sv), k_age);
    EXPECT_EQ(k_app.get_string_value(L"name"sv), L"Joe");

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

TEST(SessionSnapshotSelection, EmptyPersistedStateBuildsLivePlatform)
{
    // With no persisted state the selection logic must build a normal layered
    // platform (here: passthrough, since no flags are set) rather than a
    // snapshot. We only assert a platform is produced; reading through it would
    // touch the live registry.
    pilcfg cfg;
    auto   iface = build_platform_from_config(cfg);
    EXPECT_NE(iface, nullptr);
}

namespace
{
    constexpr auto k_fault_subkey = L"M_FAULTCFG_App"sv;

    // Build a snapshot file holding HKCU with one materialized subkey so the
    // fault layer runs over a deterministic, win32-free base world.
    std::filesystem::path
    write_fault_snapshot_file()
    {
        auto const out =
            std::filesystem::temp_directory_path() / "m_faultcfg_session_snapshot.xml";
        std::error_code ec;
        std::filesystem::remove(out, ec);

        auto p   = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r   = p.get_registry();
        auto k1  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto app = k1.create_key(k_fault_subkey);
        app.set_value(L"seed"sv, 1u);

        p.save(out);
        return out;
    }

    // Discover the absolute path the fault decorator computes for a freshly
    // created subkey of k_fault_subkey over the snapshot, so a parsed rule path
    // matches exactly. (In snapshot mode HKCU has no rooted path, so a
    // hand-authored "HKEY_CURRENT_USER\..." rule would not match.)
    m::pil::key_path
    probe_target_path(std::filesystem::path const& snapshot, wchar_t const* leaf)
    {
        auto underlying = m::pil::load_platform_interface(snapshot);
        m::pil::fault_script empty;
        m::pil::platform probe(m::pil::apply_fault_layer(underlying, empty));
        auto             r   = probe.get_registry();
        auto             hk  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto             app = hk.open_key(k_fault_subkey);
        return app.create_key(leaf).get_path();
    }

    // Write a single-rule <FaultScript> file targeting create_key on
    // target_path with the given action, and return its path.
    std::filesystem::path
    write_fault_script_file(m::pil::key_path const& target_path, wchar_t const* action)
    {
        auto const out =
            std::filesystem::temp_directory_path() / "m_faultcfg_session_script.xml";

        pugi::xml_document doc;
        auto               root = doc.append_child(L"FaultScript");
        auto               rule = root.append_child(L"Rule");
        rule.append_attribute(L"operation").set_value(L"create_key");
        rule.append_attribute(L"path").set_value(
            m::to_wstring(target_path.native().view()).c_str());
        rule.append_attribute(L"occurrence").set_value(L"1");
        rule.append_attribute(L"action").set_value(action);
        doc.save_file(out.native().c_str());

        return out;
    }
} // namespace

// M-FAULTCFG-2: a .pilcfg that names a fault script layers the fault platform
// over the selected base stack, so a configured operation fails with the
// scripted error.
TEST(SessionFaultSelection, FaultScriptConfigLayersFaultPlatform)
{
    auto const snapshot = write_fault_snapshot_file();
    auto const target   = probe_target_path(snapshot, L"Target");
    auto const script   = write_fault_script_file(target, L"access_denied");

    pilcfg cfg;
    cfg.persisted_state = snapshot.u16string();
    cfg.fault_script    = script.u16string();

    m::pil::platform p(build_platform_from_config(cfg));
    auto             r   = p.get_registry();
    auto             hk  = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto             app = hk.open_key(k_fault_subkey);

    EXPECT_THROW(static_cast<void>(app.create_key(L"Target"sv)), m::access_denied);

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
    std::filesystem::remove(script, ec);
}

// M-FAULTCFG-2: a fault script that cannot be loaded (file absent) leaves the
// base stack unwrapped rather than breaking the host — tolerant load.
TEST(SessionFaultSelection, MissingFaultScriptLeavesBaseUnwrapped)
{
    auto const snapshot = write_fault_snapshot_file();

    pilcfg cfg;
    cfg.persisted_state = snapshot.u16string();
    cfg.fault_script =
        (std::filesystem::temp_directory_path() / "m_faultcfg_does_not_exist.xml").u16string();

    m::pil::platform p(build_platform_from_config(cfg));
    auto             r   = p.get_registry();
    auto             hk  = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto             app = hk.open_key(k_fault_subkey);

    // No fault layer was applied, so the operation succeeds normally.
    EXPECT_NO_THROW(static_cast<void>(app.create_key(L"Target"sv)));

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

// M-FAULTCFG-2: a malformed fault script (valid file, invalid grammar) is also
// tolerated — the base stack is returned unwrapped.
TEST(SessionFaultSelection, MalformedFaultScriptLeavesBaseUnwrapped)
{
    auto const snapshot = write_fault_snapshot_file();

    auto const bad_script =
        std::filesystem::temp_directory_path() / "m_faultcfg_bad_script.xml";
    {
        pugi::xml_document doc;
        auto               root = doc.append_child(L"FaultScript");
        auto               rule = root.append_child(L"Rule");
        // Missing required attributes (operation/path/occurrence/action).
        rule.append_attribute(L"bogus").set_value(L"1");
        doc.save_file(bad_script.native().c_str());
    }

    pilcfg cfg;
    cfg.persisted_state = snapshot.u16string();
    cfg.fault_script    = bad_script.u16string();

    m::pil::platform p(build_platform_from_config(cfg));
    auto             r   = p.get_registry();
    auto             hk  = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto             app = hk.open_key(k_fault_subkey);

    EXPECT_NO_THROW(static_cast<void>(app.create_key(L"Target"sv)));

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
    std::filesystem::remove(bad_script, ec);
}
