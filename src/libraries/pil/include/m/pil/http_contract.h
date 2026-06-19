// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>

//
// Public façade for the HTTP contract surface (D-HWC-8). This header is the
// vocabulary a public consumer needs to *name* a contract binding — chiefly the
// `contract_mode` selector parsed from `.pilcfg` (mwin32 M-HWC-CONTRACTCFG).
//
// It deliberately carries no dependency on `ihttp_contract` (the interface
// layer in http_contract_interfaces.h): a consumer that only needs to express
// "validate" vs. "drive" should not pull the provider surface. The interface
// header re-declares an equivalent `contract_facet_mode` and asserts it is
// bit-for-bit identical, then maps this public enum onto it (mirroring the
// filesystem_monitor / ifilesystem_monitor flag pattern).
//

namespace m::pil
{
    //
    // contract_mode — how a bound spec participates at the synthetic edge.
    //
    //   validate : every request/response crossing the edge is contract-checked;
    //              violations are a side diagnostic (D6), never persisted.
    //   drive    : the spec's examples are synthesized into traffic. `drive`
    //              composes on top of `validate`.
    //
    // Changing either enumerator's value is a breaking change: the interface
    // header (http_contract_interfaces.h) static_asserts these values match its
    // own `contract_facet_mode` bit-for-bit.
    //
    enum class contract_mode : std::uint32_t
    {
        validate = 0,
        drive    = 1,
    };

} // namespace m::pil
