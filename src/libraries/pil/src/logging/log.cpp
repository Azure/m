// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
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

#include "logging.h"

#include <pugixml.hpp>

namespace m::pil::impl::logging
{
    void
    log::save(pugi::xml_node& log_node) const
    {
        auto l = std::unique_lock(m_mutex);
        for (auto const& e: m_deque)
        {
            e->save(log_node);
        }
    }
} // namespace m::pil::impl::logging
