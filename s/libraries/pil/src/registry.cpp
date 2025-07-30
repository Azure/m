// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "platform.h"

namespace m::pil
{
    registry::registry(std::shared_ptr<iregistry>&& sp) noexcept: m_registry(std::move(sp)) {}

    key
    registry::open_predefined_key(predefined_key pk)
    {
        return key(m_registry->open_predefined_key(pk));
    }

} // namespace m::pil
