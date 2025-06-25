// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <m/filesystem/filesystem.h>
#include <m/memory/memory.h>
#include <m/pe/loader.h>
#include <m/pe/pe_decoder.h>
#include <m/strings/convert.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

namespace
{
    std::wstring
    downcase(std::wstring_view v)
    {
        std::wstring result;
        auto         it = std::back_inserter(result);

        std::ranges::for_each(v, [&](wchar_t wch) {
            if (wch <= (std::numeric_limits<unsigned char>::max)())
            {
                *it++ = static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(wch)));
            }
            else
            {
                *it++ = wch;
            }
        });

        return result;
    }

    std::string
    downcase(std::string_view v)
    {
        std::string result;
        auto        it = std::back_inserter(result);

        std::ranges::for_each(v, [&](char ch) {
            if constexpr (std::is_unsigned_v<char>)
            {
                *it++ = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            else
            {
                if (ch >= 0)
                {
                    *it++ = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                else
                {
                    *it++ = ch;
                }
            }
        });

        return result;
    }

} // namespace

m::pe::loader::loader::loader(std::initializer_list<path_resolver> path_resolvers):
    m_path_resolvers(path_resolvers.begin(), path_resolvers.end()), m_unresolved_count{}
{
    //
}

std::size_t
m::pe::loader::loader::unresolved_count() const
{
    return m_unresolved_count;
}

m::pe::loader::path_resolver_result
m::pe::loader::loader::resolve_path(std::filesystem::path const& name)
{
    path_resolver_result result = path_resolver_result(m::pe::loader::pe_not_found);

    for (auto&& r: m_path_resolvers)
    {
        result = r(name);

        if (!std::holds_alternative<pe_not_found_t>(result))
            break;
    }

    return result;
}

void
m::pe::loader::loader::resolve(std::filesystem::path const& path)
{
    auto parent_path = path.parent_path();

    auto path_name               = path.filename();
    auto path_name_str           = filesystem::path_to_string(path_name);
    auto downcased_path_name_str = downcase(path_name_str);
    auto as_wstring              = m::to_wstring(downcased_path_name_str);

    m_resolved.emplace(std::make_pair(as_wstring, m::pe::loader::pe_record(path, this)));

    while (!m_pending_references.empty())
    {
        auto pending_reference = m_pending_references.front();
        auto name         = pending_reference->target_name();
        auto target_path  = std::filesystem::path(name);

        auto path_resolution_result = resolve_path(target_path);

        std::visit(
            [this, pending_reference, &target_path](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, pe_not_found_t>)
                {
                    auto [it, insertted] = m_resolved.emplace(
                        std::make_pair(target_path, pe_record(target_path, pe_not_found)));
                    if (insertted)
                    {
                        it->second.add_reference(pending_reference);
                        pending_reference->target(&it->second);
                    }

                    m_unresolved_count++;
                }
                else if constexpr (std::is_same_v<T, trim_graph_t>)
                {
                    auto [it, insertted] = m_resolved.emplace(
                        std::make_pair(target_path, pe_record(target_path)));
                    if (insertted)
                    {
                        it->second.add_reference(pending_reference);
                        pending_reference->target(&it->second);
                    }
                }
                else if constexpr (std::is_same_v<T, std::filesystem::path>)
                {
                    auto [it, insertted] = m_resolved.emplace(
                        std::make_pair(target_path, pe_record(target_path, this)));
                    if (insertted)
                    {
                        it->second.add_reference(pending_reference);
                        pending_reference->target(&it->second);
                    }
                }
                else
                    throw std::runtime_error("invalid variant type");
            },
            path_resolution_result);

        m_pending_references.pop();
    }
}
