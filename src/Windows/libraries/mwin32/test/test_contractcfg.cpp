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
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>

#include <Windows.h>

#include <gtest/gtest.h>

#include <m/pil/file_path.h>
#include <m/pil/http_contract_interfaces.h>
#include <m/pil/in_process_webcore.h>
#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/synthetic_http_edge.h>
#include <m/pil/webcore_interfaces.h>

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

    // A platform that forwards every surface to the real PIL stack but serves an
    // in-process engine for get_webcore. This lets the config platform wire its
    // bound contracts onto a *live* synthetic HTTP edge (the in-process engine),
    // exactly as production wires them onto the intercepting webcore's edge — the
    // only difference being which engine sits behind the edge.
    class in_process_engine_platform final : public m::pil::iplatform
    {
    public:
        in_process_engine_platform(std::shared_ptr<m::pil::iplatform> underlying,
                                   m::pil::synthetic_request_handler  handler):
            m_underlying(std::move(underlying)),
            m_handler(std::move(handler))
        {}

        get_registry_disposition
        get_registry(get_registry_flags flags, std::shared_ptr<m::pil::iregistry>& returned) override
        {
            return m_underlying->get_registry(flags, returned);
        }

        get_filesystem_disposition
        get_filesystem(get_filesystem_flags flags, std::shared_ptr<m::pil::ifilesystem>& returned) override
        {
            return m_underlying->get_filesystem(flags, returned);
        }

        get_http_contract_disposition
        get_http_contract(get_http_contract_flags                   flags,
                          std::shared_ptr<m::pil::ihttp_contract>&  returned) override
        {
            return m_underlying->get_http_contract(flags, returned);
        }

        get_http_listener_disposition
        get_http_listener(get_http_listener_flags                   flags,
                          std::shared_ptr<m::pil::ihttp_listener>&  returned) override
        {
            return m_underlying->get_http_listener(flags, returned);
        }

        get_webcore_disposition
        get_webcore(get_webcore_flags, std::shared_ptr<m::pil::iwebcore>& returned) override
        {
            returned = m::pil::make_in_process_webcore(m_handler);
            return {};
        }

        save_disposition
        save(save_flags flags, save_contents contents, pugi::xml_node& platform_element) override
        {
            return m_underlying->save(flags, contents, platform_element);
        }

    private:
        std::shared_ptr<m::pil::iplatform> m_underlying;
        m::pil::synthetic_request_handler  m_handler;
    };

    // The activation request used to bring up an in-process engine instance.
    m::pil::activation_request
    make_activation_request()
    {
        m::pil::activation_request request;
        request.app_host_config = m::pil::file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"InProcess";
        return request;
    }

    constexpr std::chrono::milliseconds k_live_timeout{4000};
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

//
// CONTRACTCFG-7.2 (integration): the config platform wires its bound contracts
// onto a *live* engine's synthetic HTTP edge. We build the config platform over
// an underlying platform whose get_webcore yields an in-process engine, and the
// engine's handler returns a deliberately non-conforming (undeclared 500)
// response. After activation the config platform's decorator (a) drove the
// drive-mode contract's synthesized traffic against the engine, (b) tallied the
// non-conforming response as a violation, and (c) registered the validate-mode
// document as a live crossing observer — which we confirm by driving the engine
// with an independent autonomous request and watching the observer validate it.
//
// The production real-hwebcore path is the same decorator with the intercepting
// webcore (synthetic mode) over a real hwebcore.dll as the config-selected
// engine; the only element not exercised in CI is IIS itself (D-HWC-11).
//
TEST(ContractCfgIntegration, LiveEdgeWiresValidateAndDriveOverInProcessEngine)
{
    scoped_spec_file const spec_validate(k_ping_spec);
    scoped_spec_file const spec_drive(k_ping_spec);

    auto real = m::pil::make_platform_interface();
    ASSERT_NE(real, nullptr);

    // Underlying platform: forwards to the real PIL stack, but serves an
    // in-process engine whose handler returns an undeclared 500 for every
    // request (deliberately non-conforming).
    auto underlying = std::make_shared<in_process_engine_platform>(
        real,
        [](m::pil::synthesized_request const&) -> m::pil::captured_contract_response {
            return {500, {}, {}};
        });

    // One validate binding and one drive binding, both over the GET /ping spec.
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

    auto diag            = std::make_shared<m::mwin32_impl::live_contract_diagnostics>();
    auto config_platform = m::mwin32_impl::apply_webcore_config(underlying, cfg, diag);
    ASSERT_NE(config_platform, nullptr);

    auto webcore = config_platform->get_webcore();
    ASSERT_NE(webcore, nullptr);

    // Activate the engine through the config platform. The decorator wires the
    // bound contracts onto the activated instance's synthetic edge: it registers
    // the validate document as a crossing observer, then drives the drive
    // document's synthesized traffic against the live engine.
    std::unique_ptr<m::pil::iwebcore_instance> instance;
    std::error_code                            ec;
    auto                                       request = make_activation_request();
    webcore->activate(m::pil::iwebcore::activate_flags{}, request, instance, ec);
    ASSERT_FALSE(ec);
    ASSERT_NE(instance, nullptr);

    // (a) + (b): the drive contract synthesized its single GET /ping and
    // submitted it to the engine synchronously during activation; the undeclared
    // 500 is tallied as a violation. (These counts are settled by the time
    // activate returns — drive_contract submits and waits inline.)
    {
        std::lock_guard<std::mutex> lock(diag->mutex);
        EXPECT_EQ(diag->validate_bindings, 1u);
        EXPECT_EQ(diag->drive_bindings, 1u);
        EXPECT_EQ(diag->drive.requests, 1u);
        EXPECT_EQ(diag->drive.responses_validated, 1u);
        EXPECT_EQ(diag->drive.conforming, 0u);
        EXPECT_EQ(diag->drive.violating, 1u);
    }

    // (c): drive the live engine with an INDEPENDENT autonomous request and
    // confirm the registered validate observer validated that crossing. We
    // register our own observer as the synchronization point: the wiring's
    // validate observer was registered first, so once ours fires for a crossing
    // the validate observer has already tallied it (observers run in order).
    auto* edge = instance->synthetic_http_edge();
    ASSERT_NE(edge, nullptr);

    std::mutex              obs_mutex;
    std::condition_variable obs_cv;
    std::size_t             observed = 0;
    edge->add_crossing_observer(
        [&](m::pil::synthesized_request const&, m::pil::captured_contract_response const&) {
            std::lock_guard<std::mutex> lock(obs_mutex);
            ++observed;
            obs_cv.notify_all();
        });

    m::pil::synthesized_request autonomous;
    autonomous.method = "GET";
    autonomous.path   = "/ping";
    edge->submit(autonomous, k_live_timeout);

    {
        std::unique_lock<std::mutex> lock(obs_mutex);
        ASSERT_TRUE(obs_cv.wait_for(lock, k_live_timeout, [&] { return observed >= 1; }));
    }

    // The validate observer saw the autonomous crossing (and the earlier drive
    // crossing); each undeclared 500 is a contract violation (a side diagnostic,
    // D6 — the engine's behavior is never altered).
    std::lock_guard<std::mutex> lock(diag->mutex);
    EXPECT_GE(diag->validate_crossings, 1u);
    EXPECT_GE(diag->validate_violations, 1u);
}

