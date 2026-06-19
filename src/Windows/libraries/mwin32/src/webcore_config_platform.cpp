// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include <m/pil/http_contract_interfaces.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/synthetic_http_edge.h>
#include <m/pil/webcore_interfaces.h>

#include "webcore_config_platform.h"

namespace m::mwin32_impl
{
    namespace
    {
        //----------------------------------------------------------------------
        // contract_wiring_webcore — live-edge wiring decorator (CONTRACTCFG-7.1)
        //----------------------------------------------------------------------
        //
        // Decorates an engine instance: on activation it wires the bound contracts
        // onto the activated instance's synthetic HTTP edge (PIL D-HWC-11).
        // `validate`-mode documents become crossing observers (auto-checking every
        // crossing as a D6 side diagnostic); `drive`-mode documents are driven
        // against the activated engine. The instance owns the wiring for its life.
        //

        class contract_wiring_webcore_instance final : public m::pil::iwebcore_instance
        {
        public:
            contract_wiring_webcore_instance(
                std::unique_ptr<m::pil::iwebcore_instance> underlying,
                std::vector<bound_contract> const&         contracts,
                std::shared_ptr<live_contract_diagnostics> diagnostics,
                std::chrono::milliseconds                  drive_timeout):
                m_underlying(std::move(underlying)),
                m_diagnostics(std::move(diagnostics)),
                m_validate_mutex(std::make_shared<std::mutex>())
            {
                wire(contracts, drive_timeout);
            }

            m::pil::isynthetic_http_edge*
            synthetic_http_edge() override
            {
                return m_underlying ? m_underlying->synthetic_http_edge() : nullptr;
            }

        private:
            void
            wire(std::vector<bound_contract> const& contracts,
                 std::chrono::milliseconds          drive_timeout)
            {
                auto* edge = m_underlying ? m_underlying->synthetic_http_edge() : nullptr;
                if (edge == nullptr)
                    return; // no synthetic edge (e.g. the null engine): tolerant no-op

                // First pass: register every validate-mode document as a crossing
                // observer, so it also sees the drive traffic generated below.
                for (auto const& contract: contracts)
                {
                    if (!contract.document)
                        continue;
                    if (contract.mode != pilcfg::webcore_config::contract_mode::validate)
                        continue;

                    auto document    = contract.document;
                    auto diagnostics = m_diagnostics;
                    auto guard_mutex = m_validate_mutex;

                    edge->add_crossing_observer(
                        [document, diagnostics, guard_mutex](
                            m::pil::synthesized_request const&        request,
                            m::pil::captured_contract_response const& response) {
                            // Serialize document access: crossing observers may
                            // fire concurrently on the engine's servicing thread(s).
                            std::lock_guard<std::mutex> doc_guard(*guard_mutex);

                            std::error_code ec;
                            auto const      request_check = document->validate_request(
                                request.method, request.path, request.headers, request.body, ec);
                            bool const request_violation = !ec && static_cast<bool>(request_check);

                            ec.clear();
                            auto const response_check = document->validate_response(
                                request.method,
                                request.path,
                                response.status,
                                response.headers,
                                response.body,
                                ec);
                            bool const response_violation = !ec && static_cast<bool>(response_check);

                            std::lock_guard<std::mutex> diag_guard(diagnostics->mutex);
                            ++diagnostics->validate_crossings;
                            if (request_violation || response_violation)
                                ++diagnostics->validate_violations;
                        });

                    std::lock_guard<std::mutex> diag_guard(m_diagnostics->mutex);
                    ++m_diagnostics->validate_bindings;
                }

                // Second pass: drive every drive-mode document against the
                // activated engine through the public submit seam.
                auto const submit = m::pil::make_engine_submit(edge, drive_timeout);
                for (auto const& contract: contracts)
                {
                    if (!contract.document)
                        continue;
                    if (contract.mode != pilcfg::webcore_config::contract_mode::drive)
                        continue;

                    m::pil::drive_tally const tally =
                        m::pil::drive_contract(*contract.document, submit);

                    std::lock_guard<std::mutex> diag_guard(m_diagnostics->mutex);
                    m_diagnostics->drive.requests += tally.requests;
                    m_diagnostics->drive.responses_validated += tally.responses_validated;
                    m_diagnostics->drive.conforming += tally.conforming;
                    m_diagnostics->drive.violating += tally.violating;
                    ++m_diagnostics->drive_bindings;
                }
            }

            std::unique_ptr<m::pil::iwebcore_instance> m_underlying;
            std::shared_ptr<live_contract_diagnostics> m_diagnostics;
            std::shared_ptr<std::mutex>                m_validate_mutex;
        };

        class contract_wiring_webcore final : public m::pil::iwebcore
        {
        public:
            contract_wiring_webcore(std::shared_ptr<m::pil::iwebcore>          underlying,
                                    std::vector<bound_contract>                contracts,
                                    std::shared_ptr<live_contract_diagnostics> diagnostics,
                                    std::chrono::milliseconds                  drive_timeout):
                m_underlying(std::move(underlying)),
                m_contracts(std::move(contracts)),
                m_diagnostics(std::move(diagnostics)),
                m_drive_timeout(drive_timeout)
            {}

            activate_disposition
            activate(activate_flags                                 flags,
                     m::pil::activation_request const&              request,
                     std::unique_ptr<m::pil::iwebcore_instance>&    returned_instance,
                     std::error_code&                               ec) override
            {
                std::unique_ptr<m::pil::iwebcore_instance> underlying_instance;
                auto const d = m_underlying->activate(flags, request, underlying_instance, ec);
                if (ec || !underlying_instance)
                {
                    returned_instance = std::move(underlying_instance);
                    return d;
                }

                returned_instance = std::make_unique<contract_wiring_webcore_instance>(
                    std::move(underlying_instance), m_contracts, m_diagnostics, m_drive_timeout);
                return d;
            }

            set_metadata_disposition
            set_metadata(set_metadata_flags  flags,
                         std::u16string_view type,
                         std::u16string_view value,
                         std::error_code&    ec) override
            {
                return m_underlying->set_metadata(flags, type, value, ec);
            }

        private:
            std::shared_ptr<m::pil::iwebcore>          m_underlying;
            std::vector<bound_contract>                m_contracts;
            std::shared_ptr<live_contract_diagnostics> m_diagnostics;
            std::chrono::milliseconds                  m_drive_timeout;
        };

        //----------------------------------------------------------------------
        // webcore_config_platform — platform decorator that applies webcore config
        //----------------------------------------------------------------------

        class webcore_config_platform final : public m::pil::iplatform
        {
            using iplatform_base = m::pil::iplatform;

        public:
            webcore_config_platform(std::shared_ptr<m::pil::iplatform>         underlying_platform,
                                    pilcfg::webcore_config                     webcore_cfg,
                                    std::shared_ptr<live_contract_diagnostics> diagnostics);

            ~webcore_config_platform() override = default;

            // iplatform interface
            iplatform_base::get_registry_disposition
            get_registry(iplatform_base::get_registry_flags          flags,
                         std::shared_ptr<m::pil::iregistry>& returned_registry) override;

            iplatform_base::get_filesystem_disposition
            get_filesystem(iplatform_base::get_filesystem_flags          flags,
                           std::shared_ptr<m::pil::ifilesystem>& returned_filesystem) override;

            iplatform_base::get_webcore_disposition
            get_webcore(iplatform_base::get_webcore_flags          flags,
                        std::shared_ptr<m::pil::iwebcore>& returned_webcore) override;

            iplatform_base::get_http_listener_disposition
            get_http_listener(iplatform_base::get_http_listener_flags          flags,
                              std::shared_ptr<m::pil::ihttp_listener>& returned_http_listener) override;

            iplatform_base::save_disposition
            save(iplatform_base::save_flags flags, iplatform_base::save_contents contents, pugi::xml_node& platform_element) override;

            iplatform_base::save_disposition
            save_diagnostic_log(iplatform_base::save_flags flags, pugi::xml_node& diagnostic_element) override;

        private:
            std::shared_ptr<m::pil::iplatform>         m_underlying_platform;
            pilcfg::webcore_config                     m_webcore_cfg;
            std::vector<bound_contract>                m_bound_contracts;
            std::shared_ptr<live_contract_diagnostics> m_contract_diagnostics;
            std::shared_ptr<m::pil::iwebcore>          m_webcore;
            std::mutex                                 m_mutex;
        };

        //----------------------------------------------------------------------
        // Implementation
        //----------------------------------------------------------------------

        webcore_config_platform::webcore_config_platform(
            std::shared_ptr<m::pil::iplatform>         underlying_platform,
            pilcfg::webcore_config                     webcore_cfg,
            std::shared_ptr<live_contract_diagnostics> diagnostics):
            m_underlying_platform(std::move(underlying_platform)),
            m_webcore_cfg(std::move(webcore_cfg)),
            m_contract_diagnostics(std::move(diagnostics))
        {
            // Bind the configured contracts up front (M-HWC-CONTRACTCFG-3).
            // Tolerant: a missing/malformed spec is skipped, never fatal.
            m_bound_contracts = load_webcore_contracts(*m_underlying_platform, m_webcore_cfg);
        }

        m::pil::iplatform::get_registry_disposition
        webcore_config_platform::get_registry(iplatform_base::get_registry_flags          flags,
                                              std::shared_ptr<m::pil::iregistry>& returned_registry)
        {
            return m_underlying_platform->get_registry(flags, returned_registry);
        }

        m::pil::iplatform::get_filesystem_disposition
        webcore_config_platform::get_filesystem(iplatform_base::get_filesystem_flags          flags,
                                                std::shared_ptr<m::pil::ifilesystem>& returned_filesystem)
        {
            return m_underlying_platform->get_filesystem(flags, returned_filesystem);
        }

        m::pil::iplatform::get_webcore_disposition
        webcore_config_platform::get_webcore(iplatform_base::get_webcore_flags          flags,
                                             std::shared_ptr<m::pil::iwebcore>& returned_webcore)
        {
            returned_webcore.reset();

            if (flags != iplatform_base::get_webcore_flags{})
                throw std::runtime_error("iplatform::get_webcore() called with invalid flags");

            std::lock_guard lock(m_mutex);

            if (!m_webcore)
            {
                // Get the underlying webcore.
                std::shared_ptr<m::pil::iwebcore> underlying_webcore;
                auto d = m_underlying_platform->get_webcore(flags, underlying_webcore);
                (void)d;

                // Wrap the configured engine with the contract-wiring decorator
                // (M-HWC-CONTRACTCFG-7.1): each activation wires the bound
                // contracts onto the activated instance's synthetic HTTP edge —
                // validate-mode documents auto-check autonomous crossings (a D6
                // side diagnostic) and drive-mode documents are driven against the
                // activated engine. When no contracts are bound, the decorator is a
                // transparent pass-through; when the engine exposes no synthetic
                // edge, wiring is a tolerant no-op.
                m_webcore = make_contract_wiring_webcore(
                    std::move(underlying_webcore), m_bound_contracts, m_contract_diagnostics);
            }

            returned_webcore = m_webcore;
            return iplatform_base::get_webcore_disposition{};
        }

        m::pil::iplatform::get_http_listener_disposition
        webcore_config_platform::get_http_listener(
            iplatform_base::get_http_listener_flags          flags,
            std::shared_ptr<m::pil::ihttp_listener>& returned_http_listener)
        {
            // Forward to underlying; future work will apply endpoint mapping.
            return m_underlying_platform->get_http_listener(flags, returned_http_listener);
        }

        m::pil::iplatform::save_disposition
        webcore_config_platform::save(iplatform_base::save_flags        flags,
                                      iplatform_base::save_contents     contents,
                                      pugi::xml_node&   platform_element)
        {
            // Webcore config is a separate input artifact, not persisted.
            return m_underlying_platform->save(flags, contents, platform_element);
        }

        m::pil::iplatform::save_disposition
        webcore_config_platform::save_diagnostic_log(iplatform_base::save_flags      flags,
                                                     pugi::xml_node& diagnostic_element)
        {
            // Forward so a logging layer below remains reachable.
            return m_underlying_platform->save_diagnostic_log(flags, diagnostic_element);
        }

    } // namespace

    //--------------------------------------------------------------------------
    // Public API
    //--------------------------------------------------------------------------

    std::vector<bound_contract>
    load_webcore_contracts(m::pil::iplatform&            platform,
                           pilcfg::webcore_config const& webcore_cfg)
    {
        std::vector<bound_contract> bound;

        if (webcore_cfg.contracts.empty())
            return bound;

        // The contract provider is reachable through the live platform stack
        // (PIL M-HWC-CONTRACT-EXPOSE-1). A null provider simply yields no
        // documents, which the tolerant loop below treats as "nothing bound".
        auto provider = platform.get_http_contract();
        if (!provider)
            return bound;

        for (auto const& binding: webcore_cfg.contracts)
        {
            try
            {
                std::filesystem::path const spec_path(binding.spec);

                std::ifstream in(spec_path, std::ios::binary);
                if (!in)
                    continue; // missing spec: skip (tolerant, D5/D7)

                std::string const spec_bytes((std::istreambuf_iterator<char>(in)),
                                             std::istreambuf_iterator<char>());

                std::error_code                                       ec;
                std::unique_ptr<m::pil::ihttp_contract_document>      document;
                provider->load(m::pil::ihttp_contract::load_flags{}, spec_bytes, document, ec);
                if (ec || !document)
                    continue; // malformed spec: skip (tolerant, D5/D7)

                bound_contract bc;
                bc.endpoint = binding.endpoint;
                bc.mode     = binding.mode;
                bc.document = std::shared_ptr<m::pil::ihttp_contract_document>(std::move(document));
                bound.push_back(std::move(bc));
            }
            catch (...)
            {
                // Any unexpected failure binding one contract leaves the host
                // running with that contract simply unbound.
            }
        }

        return bound;
    }

    contract_wiring_summary
    wire_contracts_to_edge(std::vector<bound_contract> const& contracts,
                           m::pil::ihttp_contract_edge&       edge)
    {
        contract_wiring_summary summary;

        // First pass: attach every validate-mode document so it observes every
        // crossing — including the drive traffic generated below — regardless of
        // configuration order.
        for (auto const& contract: contracts)
        {
            if (!contract.document)
                continue;
            if (contract.mode == pilcfg::webcore_config::contract_mode::validate)
            {
                // Every request/response crossing the edge is contract-checked
                // (a side diagnostic, D6 — the engine's behavior is unchanged).
                edge.attach_validation(contract.document);
                ++summary.validate_bindings;
            }
        }

        // Second pass: drive every drive-mode document through the edge,
        // synthesizing the spec's examples and validating each response against
        // this same document. Sum the per-binding tally into the aggregate.
        for (auto const& contract: contracts)
        {
            if (!contract.document)
                continue;
            if (contract.mode == pilcfg::webcore_config::contract_mode::drive)
            {
                m::pil::drive_tally const tally =
                    m::pil::drive_contract(*contract.document, edge.as_engine_submit());

                summary.drive.requests += tally.requests;
                summary.drive.responses_validated += tally.responses_validated;
                summary.drive.conforming += tally.conforming;
                summary.drive.violating += tally.violating;
                ++summary.drive_bindings;
            }
        }

        return summary;
    }

    std::shared_ptr<m::pil::iwebcore>
    make_contract_wiring_webcore(std::shared_ptr<m::pil::iwebcore>          underlying,
                                 std::vector<bound_contract>                contracts,
                                 std::shared_ptr<live_contract_diagnostics> diagnostics,
                                 std::chrono::milliseconds                  drive_timeout)
    {
        if (!diagnostics)
            diagnostics = std::make_shared<live_contract_diagnostics>();

        return std::make_shared<contract_wiring_webcore>(
            std::move(underlying), std::move(contracts), std::move(diagnostics), drive_timeout);
    }

    std::shared_ptr<m::pil::iplatform>
    apply_webcore_config(std::shared_ptr<m::pil::iplatform> const& underlying_platform,
                         pilcfg::webcore_config const&             webcore_cfg)
    {
        return std::make_shared<webcore_config_platform>(
            underlying_platform, webcore_cfg, std::make_shared<live_contract_diagnostics>());
    }

    std::shared_ptr<m::pil::iplatform>
    apply_webcore_config(std::shared_ptr<m::pil::iplatform> const&   underlying_platform,
                         pilcfg::webcore_config const&               webcore_cfg,
                         std::shared_ptr<live_contract_diagnostics>  diagnostics)
    {
        if (!diagnostics)
            diagnostics = std::make_shared<live_contract_diagnostics>();

        return std::make_shared<webcore_config_platform>(
            underlying_platform, webcore_cfg, std::move(diagnostics));
    }

} // namespace m::mwin32_impl
