// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <span>
#include <string>
#include <string_view>

#include <m/strings/convert.h>

#include "journaling.h"

namespace m::pil::impl::journaling
{
    void
    journal::save(pugi::xml_node& journal_node) const
    {
        auto l = std::unique_lock(m_mutex);
        for (auto const& e: m_deque)
            e->save(journal_node);
    }
} // namespace m::pil::impl::journaling
