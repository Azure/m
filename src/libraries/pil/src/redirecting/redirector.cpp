// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string>
#include <string_view>

#include <m/strings/compare.h>
#include <m/strings/convert.h>

#include <m/pil/file_path.h>
#include <m/pil/registry.h>

#include "redirecting.h"

namespace m::pil::impl::redirecting
{
    redirector::redirector(std::span<std::pair<view_type, view_type> const> redirections)
    {
        for (auto const& e: redirections)
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

    //
    // fs_redirector: same prefix-mapping machinery as `redirector`, producing
    // file_path values. file_path and key_path share the same string_type /
    // view_type and the same '\' separator, so the algorithm is identical.
    //

    fs_redirector::fs_redirector(std::span<std::pair<view_type, view_type> const> redirections)
    {
        for (auto const& e: redirections)
        {
            m_public_to_private.emplace(e);
            m_private_to_public.emplace(std::make_pair(e.second, e.first));
        }
    }

    file_path
    fs_redirector::map_public_to_private(file_path const& p) const
    {
        return try_map(m_public_to_private, p);
    }

    file_path
    fs_redirector::map_private_to_public(file_path const& p) const
    {
        return try_map(m_private_to_public, p);
    }

    file_path
    fs_redirector::try_map(ci_map<string_type> const& rmap, file_path const& p)
    {
        auto const& native     = p.native();
        auto const  v          = native.view();
        auto        search_key = native.view();
        std::size_t remainder_size{};

        // First, try matching the full path (existing prefix-trimming behavior).
        for (;;)
        {
            auto const it = rmap.find(search_key);
            if (it != rmap.end())
            {
                auto const remainder_start      = v.size() - remainder_size;
                auto const remainder_view       = v.substr(remainder_start);
                auto const combined_path_string = string_type{{it->second.view(), remainder_view}};
                return file_path{combined_path_string.view()};
            }

            auto const sep_pos = search_key.find_last_of(m::pil::file_preferred_separator);
            if (sep_pos == npos)
                break;

            search_key     = search_key.substr(0, sep_pos);
            remainder_size = v.size() - sep_pos;
        }

        // If no match and the path has a root, try suffix-matching on the relative
        // portion. This supports redirection tables that use relative keys (e.g.,
        // "Public\Documents") when the watch path is rooted (e.g.,
        // "C:\Users\Test\Public\Documents"). The relative key may appear anywhere
        // in the path, so we strip directory components from the beginning until
        // we find a match or exhaust the path.
        if (p.root_kind() != file_root_kind::none)
        {
            auto const  root_text = p.root().text();
            auto const  rel       = p.relative_path();
            auto const  rel_view  = rel.view();
            std::size_t prefix_start{};

            // Outer loop: strip components from the beginning of the relative path.
            while (prefix_start < rel_view.size())
            {
                auto        rel_key            = rel_view.substr(prefix_start);
                std::size_t rel_remainder_size{};

                // Inner loop: prefix-trimming on this suffix (strip from end).
                for (;;)
                {
                    auto const it = rmap.find(rel_key);
                    if (it != rmap.end())
                    {
                        // Found a match. Build the result: root + stripped prefix +
                        // mapped value + remainder. The stripped prefix is everything
                        // before prefix_start in the relative path.
                        auto const prefix_view     = rel_view.substr(0, prefix_start);
                        auto const remainder_start = rel_view.size() - rel_remainder_size;
                        auto const remainder_view  = rel_view.substr(remainder_start);
                        auto const combined_path_string =
                            string_type{{root_text, prefix_view, it->second.view(),
                                         remainder_view}};
                        return file_path{combined_path_string.view()};
                    }

                    auto const sep_pos =
                        rel_key.find_last_of(m::pil::file_preferred_separator);
                    if (sep_pos == npos)
                        break;

                    rel_key            = rel_key.substr(0, sep_pos);
                    rel_remainder_size = rel_view.size() - prefix_start - sep_pos;
                }

                // Move to the next component: find the first separator after
                // prefix_start and skip past it.
                auto const sep_pos =
                    rel_view.find_first_of(m::pil::file_preferred_separator, prefix_start);
                if (sep_pos == npos)
                    break;

                prefix_start = sep_pos + 1;
            }
        }

        return p;
    }

} // namespace m::pil::impl::redirecting
