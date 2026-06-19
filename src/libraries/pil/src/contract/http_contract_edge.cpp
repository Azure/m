// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/pil/http_contract_edge.h>

#include <memory>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "contract_errors.h"
#include "contract_validating_facet.h"

namespace m::pil
{
    namespace
    {
        //
        // In-process contract edge (D-HWC-10). Each attached validate-mode
        // document becomes a validating facet (surfacing on, so a violation
        // returns a contract_error the edge interprets for the tally and then
        // swallows — the engine's response is never altered). The facet also
        // traces every violation (D6).
        //
        class contract_edge final : public ihttp_contract_edge
        {
        public:
            explicit contract_edge(engine_submit engine): m_engine(std::move(engine))
            {
            }

            captured_contract_response
            submit(synthesized_request const& request) override
            {
                std::span<http_header const>  const req_headers(request.headers);
                std::span<std::uint8_t const> const req_body(request.body);

                ++m_tally.requests;
                for (auto& facet: m_facets)
                {
                    std::error_code const ec =
                        facet.on_request(request.method, request.path, req_headers, req_body);
                    if (ec == contract_error::request_violation)
                        ++m_tally.request_violations;
                }

                captured_contract_response response =
                    m_engine ? m_engine(request) : captured_contract_response{};
                ++m_tally.responses;

                std::span<http_header const>  const resp_headers(response.headers);
                std::span<std::uint8_t const> const resp_body(response.body);
                for (auto& facet: m_facets)
                {
                    std::error_code const ec = facet.on_response(
                        request.method, request.path, response.status, resp_headers, resp_body);
                    if (ec == contract_error::response_violation)
                        ++m_tally.response_violations;
                }

                return response;
            }

            void
            attach_validation(std::shared_ptr<ihttp_contract_document> document) override
            {
                m_facets.emplace_back(std::move(document),
                                      contract_validating_facet::flags::surface_violations);
            }

            contract_edge_tally
            tally() const override
            {
                return m_tally;
            }

        private:
            engine_submit                          m_engine;
            std::vector<contract_validating_facet> m_facets;
            contract_edge_tally                    m_tally;
        };
    } // namespace

    std::shared_ptr<ihttp_contract_edge>
    make_contract_edge(engine_submit engine)
    {
        return std::make_shared<contract_edge>(std::move(engine));
    }
} // namespace m::pil
