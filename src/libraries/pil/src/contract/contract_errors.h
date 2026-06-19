// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <system_error>

//
// Contract-violation error codes for the validating facet
// (M-HWC-CONTRACT-VALIDATE, D-HWC-8, D6).
//
// A contract violation detected at the synthetic edge is a *side diagnostic*:
// it always traces, and — only when the facet is opted in to surface
// violations — is reported as one of these error codes so a test can assert it.
// PIL owns these values; they are not a wire contract. The facet maps a
// request-side disposition onto `request_violation` and a response-side
// disposition onto `response_violation`.
//
// This header is internal to m_pil (lives under src/, not include/).
//

namespace m::pil
{
    enum class contract_error
    {
        request_violation  = 1,
        response_violation = 2,
    };

    std::error_code
    make_error_code(contract_error e) noexcept;

    std::error_category const&
    contract_error_category() noexcept;
}

namespace std
{
    template <>
    struct is_error_code_enum<m::pil::contract_error> : true_type
    {
    };
}
