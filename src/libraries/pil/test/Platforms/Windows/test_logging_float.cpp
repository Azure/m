// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>

#include "buffered/buffered.h"
#include "logging/logging.h"
#include "passthrough/passthrough.h"

using namespace std::string_view_literals;

#ifdef WIN32

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

    // Build a sealed snapshot fixture file holding HKCU with one seed value, so
    // the floating-tap stacks below have a deterministic, win32-free leaf to
    // wrap. Returns the path written.
    void
    write_snapshot_fixture(std::filesystem::path const& p)
    {
        std::error_code ec;
        std::filesystem::remove(p, ec);

        auto pf  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r   = pf.get_registry();
        auto k1  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto app = k1.create_key(L"MLOGFLOAT_Seed"sv);
        app.set_value(L"seed"sv, 1u);

        pf.save(p);
    }

    // Apply an identical set of mutations to the snapshot through whatever stack
    // `top` represents, and return the value read back so callers can prove the
    // tap depth did not alter behavior.
    std::uint32_t
    exercise(std::shared_ptr<m::pil::iplatform> top)
    {
        m::pil::platform p(std::move(top));

        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

        // The seed from the snapshot is served identically regardless of depth.
        auto seed = hkcu.open_key(L"MLOGFLOAT_Seed"sv);
        EXPECT_EQ(seed.get_uint32_value(L"seed"sv), 1u);

        // Mutations the tap should observe.
        auto app = hkcu.create_key(L"FloatApp"sv);
        app.set_value(L"v"sv, 11u);

        return app.get_uint32_value(L"v"sv);
    }
} // namespace

// M-LOG-FLOAT-2: the logging tap can float at any depth. The same operations
// are issued against the same sealed snapshot through two stacks that differ
// only in where the logging layer sits: directly above the leaf, and beneath a
// transparent passthrough layer. The requested-vs-done trace is captured at
// both depths and remains reachable from the top, and the observable behavior
// is identical.
TEST(LoggingFloat, TapCapturesAtAnyDepthWithoutAlteringBehavior)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mlogfloat_snapshot.xml";
    auto const diag_top = std::filesystem::temp_directory_path() / "mlogfloat_diag_top.xml";
    auto const diag_mid = std::filesystem::temp_directory_path() / "mlogfloat_diag_mid.xml";

    std::error_code ec;
    std::filesystem::remove(diag_top, ec);
    std::filesystem::remove(diag_mid, ec);

    write_snapshot_fixture(snapshot);

    // Stack A: logging tap directly above the leaf snapshot.
    std::uint32_t value_a = 0;
    {
        auto leaf = m::pil::impl::buffered::create_platform_from_persisted_xml(snapshot);
        auto tap  = std::make_shared<m::pil::impl::logging::platform>(leaf);

        // Keep a handle to call save_diagnostic_log after exercise() consumes
        // the platform wrapper.
        std::shared_ptr<m::pil::iplatform> top = tap;
        value_a                                = exercise(top);

        m::pil::platform p(std::move(top));
        p.save_diagnostic_log(diag_top);
    }

    // Stack B: logging tap beneath a transparent passthrough layer.
    std::uint32_t value_b = 0;
    {
        auto leaf = m::pil::impl::buffered::create_platform_from_persisted_xml(snapshot);
        auto tap  = std::make_shared<m::pil::impl::logging::platform>(leaf);
        auto outer = std::make_shared<m::pil::impl::passthrough::platform>(
            std::static_pointer_cast<m::pil::iplatform>(tap));

        std::shared_ptr<m::pil::iplatform> top = outer;
        value_b                                = exercise(top);

        m::pil::platform p(std::move(top));
        p.save_diagnostic_log(diag_mid);
    }

    // Behavior is unaltered by tap depth.
    EXPECT_EQ(value_a, 11u);
    EXPECT_EQ(value_b, 11u);

    auto const top_text = read_file_text(diag_top);
    auto const mid_text = read_file_text(diag_mid);

    // The tap captured the requested-vs-done trace at both depths.
    for (auto const* text: {&top_text, &mid_text})
    {
        EXPECT_NE(text->find("<DiagnosticLog"), std::string::npos);
        EXPECT_NE(text->find("Registry.CreateKey"), std::string::npos);
        EXPECT_NE(text->find("Registry.SetValue"), std::string::npos);
    }

    std::filesystem::remove(snapshot, ec);
    std::filesystem::remove(diag_top, ec);
    std::filesystem::remove(diag_mid, ec);
}

#endif // WIN32
