// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <m/pil/http_contract_interfaces.h>

#include "openapi_model.h"

//
// Live `ihttp_contract` provider for the HWC HTTP contract surface
// (M-HWC-CONTRACT-VALIDATE, D-HWC-8, D-HWC-9).
//
// This provider parses an OpenAPI/Swagger spec into the normalized
// `openapi_model` (the M-HWC-CONTRACT-MODEL loader + matcher) and produces a
// document that contract-checks requests and responses crossing the synthetic
// HTTP edge.
//
// PIL owns the contract behavior (Design Autonomy): operation selection (method
// + path + query discriminator), parameter presence, response status / declared
// headers, and media-type awareness are PIL code. Only the JSON body-schema
// check is delegated — to `nlohmann-json-schema-validator`, chosen because its
// behavior matches our specification for JSON bodies. One validator is built per
// JSON body schema at `load` time.
//
// Media-type awareness (D-HWC-9): body *value* validation engages only for JSON
// content types. Non-JSON bodies (e.g. `text/xml`) get method/path/status,
// parameter, and declared-header checks but no body-value check — that is a
// scoped follow-on, not done here.
//
// This header is internal to m_pil (lives under src/, not include/).
//

namespace m::pil
{
    //
    // Build a live contract provider. The resolver (D-HWC-9, caller-owns-I/O) is
    // captured here and used by `load` to splice `$ref` bundle documents; a spec
    // with no external refs never invokes it.
    //
    std::unique_ptr<ihttp_contract>
    make_http_contract_provider(ref_resolver resolver = {});
}
