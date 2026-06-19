// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Integration coverage for the `.pilcfg` `webcore.contracts` binding
// (M-HWC-CONTRACTCFG-5, D-HWC-8). A configuration references a small OpenAPI
// spec on disk; `load_webcore_contracts` reads and binds it through the live
// PIL contract provider, and the bound document is exercised end to end against
// a fake engine via the public drive surface (`m::pil::drive_contract`). The
// fake engine plays the role of the synthetic edge: it turns each synthesized
// request into a response, and the contract validates that response.
//

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <Windows.h>

#include <gtest/gtest.h>

#include <m/pil/http_contract_interfaces.h>
#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>

#include "pilcfg.h"
#include "webcore_config_platform.h"

using namespace std::string_view_literals;

namespace
{
    // A minimal OpenAPI document declaring a single GET /ping operation that
    // returns 200. Loading it yields exactly one synthesizable request.
    constexpr std::string_view k_ping_spec = R"(
openapi: 3.0.0
info:
  title: contractcfg-integration
  version: "1.0"
paths:
  /ping:
    get:
      responses:
        '200':
          description: ok
)"sv;

    // RAII temp file that holds the spec for the duration of a test and removes
    // it on destruction, so the test leaves no artifacts behind.
    class scoped_spec_file
    {
    public:
        explicit scoped_spec_file(std::string_view contents)
        {
            auto dir = std::filesystem::temp_directory_path();
            m_path   = dir / ("m_contractcfg_" +
                            std::to_string(::GetCurrentProcessId()) + "_" +
                            std::to_string(reinterpret_cast<std::uintptr_t>(this)) +
                            ".yaml");
            std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
            out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }

        ~scoped_spec_file()
        {
            std::error_code ec;
            std::filesystem::remove(m_path, ec);
        }

        scoped_spec_file(scoped_spec_file const&)            = delete;
        scoped_spec_file& operator=(scoped_spec_file const&) = delete;

        std::filesystem::path const& path() const { return m_path; }

    private:
        std::filesystem::path m_path;
    };

    using m::mwin32_impl::load_webcore_contracts;
    using m::mwin32_impl::pilcfg;

    pilcfg::webcore_config
    make_contract_config(std::filesystem::path const&             spec_path,
                         pilcfg::webcore_config::contract_mode     mode)
    {
        pilcfg::webcore_config cfg;
        pilcfg::webcore_config::contract_binding binding;
        binding.spec     = spec_path.u16string();
        binding.endpoint = u"ping";
        binding.mode     = mode;
        cfg.contracts.push_back(std::move(binding));
        return cfg;
    }
} // namespace

//
// A configured contract binds through the live provider and validates a
// conforming fake engine, then flags a non-conforming response as a violation.
//
TEST(ContractCfgIntegration, BindsAndDrivesFakeEngine)
{
    scoped_spec_file const spec(k_ping_spec);

    auto platform = m::pil::make_platform_interface();
    ASSERT_NE(platform, nullptr);

    auto const cfg   = make_contract_config(spec.path(),
                                          pilcfg::webcore_config::contract_mode::drive);
    auto const bound = load_webcore_contracts(*platform, cfg);

    ASSERT_EQ(bound.size(), 1u);
    EXPECT_EQ(bound[0].endpoint, u"ping");
    EXPECT_EQ(bound[0].mode, pilcfg::webcore_config::contract_mode::drive);
    ASSERT_NE(bound[0].document, nullptr);

    // The spec's single operation yields one synthesized request.
    auto const requests = bound[0].document->synthesize_requests();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].method, "GET");
    EXPECT_EQ(requests[0].path, "/ping");

    // Conforming fake engine: returns the declared 200.
    auto const conforming = m::pil::drive_contract(
        *bound[0].document,
        [](m::pil::synthesized_request const&) -> m::pil::captured_contract_response {
            return {200, {}, {}};
        });
    EXPECT_EQ(conforming.requests, 1u);
    EXPECT_EQ(conforming.responses_validated, 1u);
    EXPECT_EQ(conforming.conforming, 1u);
    EXPECT_EQ(conforming.violating, 0u);

    // Non-conforming fake engine: returns an undeclared 500 -> a violation.
    auto const violating = m::pil::drive_contract(
        *bound[0].document,
        [](m::pil::synthesized_request const&) -> m::pil::captured_contract_response {
            return {500, {}, {}};
        });
    EXPECT_EQ(violating.requests, 1u);
    EXPECT_EQ(violating.responses_validated, 1u);
    EXPECT_EQ(violating.conforming, 0u);
    EXPECT_EQ(violating.violating, 1u);
}

//
// A binding whose spec path does not exist is tolerated (best-effort, D5/D7):
// the host stays up with that contract simply unbound.
//
TEST(ContractCfgIntegration, MissingSpecIsToleratedAndUnbound)
{
    auto platform = m::pil::make_platform_interface();
    ASSERT_NE(platform, nullptr);

    pilcfg::webcore_config cfg;
    pilcfg::webcore_config::contract_binding binding;
    binding.spec     = u"Z:\\does-not-exist\\nope.yaml";
    binding.endpoint = u"ping";
    binding.mode     = pilcfg::webcore_config::contract_mode::validate;
    cfg.contracts.push_back(std::move(binding));

    auto const bound = load_webcore_contracts(*platform, cfg);
    EXPECT_TRUE(bound.empty());
}

//
// Multiple bindings preserve configuration order; each is bound independently.
//
TEST(ContractCfgIntegration, MultipleBindingsPreserveOrder)
{
    scoped_spec_file const spec_a(k_ping_spec);
    scoped_spec_file const spec_b(k_ping_spec);

    auto platform = m::pil::make_platform_interface();
    ASSERT_NE(platform, nullptr);

    pilcfg::webcore_config cfg;
    {
        pilcfg::webcore_config::contract_binding b;
        b.spec     = spec_a.path().u16string();
        b.endpoint = u"first";
        b.mode     = pilcfg::webcore_config::contract_mode::validate;
        cfg.contracts.push_back(std::move(b));
    }
    {
        pilcfg::webcore_config::contract_binding b;
        b.spec     = spec_b.path().u16string();
        b.endpoint = u"second";
        b.mode     = pilcfg::webcore_config::contract_mode::drive;
        cfg.contracts.push_back(std::move(b));
    }

    auto const bound = load_webcore_contracts(*platform, cfg);
    ASSERT_EQ(bound.size(), 2u);
    EXPECT_EQ(bound[0].endpoint, u"first");
    EXPECT_EQ(bound[0].mode, pilcfg::webcore_config::contract_mode::validate);
    EXPECT_EQ(bound[1].endpoint, u"second");
    EXPECT_EQ(bound[1].mode, pilcfg::webcore_config::contract_mode::drive);
}

//
// CONTRACTCFG-6: the bound contracts are wired onto a PIL contract edge backed
// by a fake engine. The validate-mode document is attached and observes every
// crossing; the drive-mode document generates traffic through the same edge. A
// deliberately non-conforming (undeclared 500) engine response is reported both
// in the drive tally and by the attached validate document.
//
TEST(ContractCfgIntegration, WiresBoundContractsToEdge)
{
    scoped_spec_file const spec_validate(k_ping_spec);
    scoped_spec_file const spec_drive(k_ping_spec);

    auto platform = m::pil::make_platform_interface();
    ASSERT_NE(platform, nullptr);

    pilcfg::webcore_config cfg;
    {
        pilcfg::webcore_config::contract_binding b;
        b.spec     = spec_validate.path().u16string();
        b.endpoint = u"watch";
        b.mode     = pilcfg::webcore_config::contract_mode::validate;
        cfg.contracts.push_back(std::move(b));
    }
    {
        pilcfg::webcore_config::contract_binding b;
        b.spec     = spec_drive.path().u16string();
        b.endpoint = u"ping";
        b.mode     = pilcfg::webcore_config::contract_mode::drive;
        cfg.contracts.push_back(std::move(b));
    }

    auto const bound = load_webcore_contracts(*platform, cfg);
    ASSERT_EQ(bound.size(), 2u);

    // Fake engine playing the role behind the synthetic edge: it returns an
    // undeclared 500 for every request, so each crossing violates the contract.
    auto edge = m::pil::make_contract_edge(
        [](m::pil::synthesized_request const&) -> m::pil::captured_contract_response {
            return {500, {}, {}};
        });
    ASSERT_NE(edge, nullptr);

    auto const summary = m::mwin32_impl::wire_contracts_to_edge(bound, *edge);

    EXPECT_EQ(summary.validate_bindings, 1u);
    EXPECT_EQ(summary.drive_bindings, 1u);

    // Drive: the single GET /ping was synthesized and submitted; its undeclared
    // 500 response is a violation.
    EXPECT_EQ(summary.drive.requests, 1u);
    EXPECT_EQ(summary.drive.responses_validated, 1u);
    EXPECT_EQ(summary.drive.conforming, 0u);
    EXPECT_EQ(summary.drive.violating, 1u);

    // The drive request crossed the edge; the attached validate document saw the
    // same crossing and flagged the undeclared 500 (a side diagnostic, D6).
    auto const tally = edge->tally();
    EXPECT_EQ(tally.requests, 1u);
    EXPECT_EQ(tally.responses, 1u);
    EXPECT_EQ(tally.response_violations, 1u);
}

//
// CONTRACTCFG-6: a conforming engine produces no violations on either path.
//
TEST(ContractCfgIntegration, WiredEdgeConformingEngineHasNoViolations)
{
    scoped_spec_file const spec_validate(k_ping_spec);
    scoped_spec_file const spec_drive(k_ping_spec);

    auto platform = m::pil::make_platform_interface();
    ASSERT_NE(platform, nullptr);

    pilcfg::webcore_config cfg;
    {
        pilcfg::webcore_config::contract_binding b;
        b.spec     = spec_validate.path().u16string();
        b.endpoint = u"watch";
        b.mode     = pilcfg::webcore_config::contract_mode::validate;
        cfg.contracts.push_back(std::move(b));
    }
    {
        pilcfg::webcore_config::contract_binding b;
        b.spec     = spec_drive.path().u16string();
        b.endpoint = u"ping";
        b.mode     = pilcfg::webcore_config::contract_mode::drive;
        cfg.contracts.push_back(std::move(b));
    }

    auto const bound = load_webcore_contracts(*platform, cfg);
    ASSERT_EQ(bound.size(), 2u);

    // Conforming fake engine: returns the declared 200.
    auto edge = m::pil::make_contract_edge(
        [](m::pil::synthesized_request const&) -> m::pil::captured_contract_response {
            return {200, {}, {}};
        });

    auto const summary = m::mwin32_impl::wire_contracts_to_edge(bound, *edge);

    EXPECT_EQ(summary.drive.requests, 1u);
    EXPECT_EQ(summary.drive.conforming, 1u);
    EXPECT_EQ(summary.drive.violating, 0u);

    auto const tally = edge->tally();
    EXPECT_EQ(tally.requests, 1u);
    EXPECT_EQ(tally.request_violations, 0u);
    EXPECT_EQ(tally.response_violations, 0u);
}

//
// CONTRACTCFG-6: an empty binding set wires nothing and leaves the edge idle.
//
TEST(ContractCfgIntegration, WiringEmptyContractsIsNoOp)
{
    auto edge = m::pil::make_contract_edge(
        [](m::pil::synthesized_request const&) -> m::pil::captured_contract_response {
            return {200, {}, {}};
        });

    auto const summary = m::mwin32_impl::wire_contracts_to_edge({}, *edge);

    EXPECT_EQ(summary.validate_bindings, 0u);
    EXPECT_EQ(summary.drive_bindings, 0u);
    EXPECT_EQ(summary.drive.requests, 0u);
    EXPECT_EQ(edge->tally().requests, 0u);
}
