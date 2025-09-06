// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string>
#include <string_view>

#include <m/strings/compare.h>
#include <m/strings/convert.h>

#include <m/pil/registry.h>

#include "redirecting.h"

namespace m::pil::impl::redirecting
{
    redirector::redirector(std::initializer_list<std::pair<view_type, view_type>>* il)
    {
        for (auto const& e: *il)
        {
            m_public_to_private.emplace(e);
            m_private_to_public.emplace(std::make_pair(e.second, e.first));
        }
    }

    path
    redirector::map_public_to_private(path const& p) const
    {
        return try_map(m_public_to_private, p);
    }

    path
    redirector::map_private_to_public(path const& p) const
    {
        return try_map(m_private_to_public, p);
    }

    path
    redirector::try_map(ci_map<string_type> const& rmap, path const& p)
    {
        auto const& native     = p.native();
        auto const  v          = native.view();
        auto        search_key = native.view();
        std::size_t remainder_size{};

        // keep trying to map the public path to a private path:
        for (;;)
        {
            auto const it = rmap.find(search_key);
            if (it != rmap.end())
            {
                // just another name that makes more sense in context
                auto const remainder_start      = v.size() - remainder_size;
                auto const remainder_view       = v.substr(remainder_start);
                auto const combined_path_string = string_type{{it->second.view(), remainder_view}};
                return path{combined_path_string};
            }

            auto const sep_pos = search_key.find_last_of(registry_path_separator);
            if (sep_pos == npos)
                break;

            search_key     = search_key.substr(0, sep_pos);
            remainder_size = v.size() - sep_pos;
        }

        return p;
    }

} // namespace m::pil::impl::redirecting
