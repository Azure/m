// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

#include <m/filesystem/filesystem.h>
#include <m/memory/memory.h>
#include <m/pe/loader_context.h>
#include <m/pe/pe_decoder.h>
#include <m/strings/convert.h>

namespace
{
    std::wstring
    downcase(std::wstring_view v)
    {
        std::wstring result;
        auto         it = std::back_inserter(result);

        std::ranges::for_each(v, [&](wchar_t wch) {
            if (wch <= std::numeric_limits<unsigned char>::max())
            {
                *it = std::tolower(static_cast<unsigned char>(wch));
                ++it;
            }
            else
            {
                *it = wch;
                ++it;
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
                *it = std::tolower(static_cast<unsigned char>(ch));
                ++it;
            }
            else
            {
                if (ch >= 0)
                {
                    *it = std::tolower(static_cast<unsigned char>(ch));
                    ++it;
                }
                else
                {
                    *it = static_cast<wchar_t>(ch);
                    ++it;
                }
            }
        });

        return result;
    }

} // namespace

m::pe::loader_context::loader_context():
    m_known_pes(known_apiset_dlls.begin(), known_apiset_dlls.end())
{
    //
}

std::size_t
m::pe::loader_context::unresolved_count() const
{
    return m_unresolved_count;
}

std::optional<std::filesystem::path>
m::pe::loader_context::try_resolve(std::wstring_view name)
{
    for (auto&& e: m_search_path)
    {
        auto path = e / name;

        if (std::filesystem::exists(path))
            return path;
    }

    return std::nullopt;
}

void
m::pe::loader_context::resolve(std::filesystem::path const& path)
{
    auto parent_path = path.parent_path();

    // Our only element on the search path for now: the path
    // where the initial thing came from.
    m_search_path.push_back(parent_path);

    auto path_name               = path.filename();
    auto path_name_str           = path_name.c_str();
    auto downcased_path_name_str = downcase(path_name_str);
    auto as_wstring              = m::to_wstring(downcased_path_name_str);

    m_resolved.emplace(std::make_pair(as_wstring, m::pe::loader_context::pe_record(path, this)));

    while (!m_pending.empty())
    {
        auto name = m_pending.front();

        auto it = m_resolved.find(name);
        if (it != m_resolved.end())
        {
            m_pending.pop();
            continue;
        }

        if (m_known_pes.find(name) != m_known_pes.end())
        {
            m_resolved.emplace(std::make_pair(name, pe_record(std::wstring_view(name))));
            m_pending.pop();
            continue;
        }

        auto maybe_path = try_resolve(std::wstring_view(name));
        if (maybe_path.has_value())
            m_resolved.emplace(std::make_pair(name, pe_record(maybe_path.value(), this)));
        else
        {
            m_resolved.emplace(
                std::make_pair(name, pe_record(std::wstring_view(name), pe_record::pe_not_found)));
            m_unresolved_count++;
        }

        m_pending.pop();
    }
}

m::pe::loader_context::pe_record::pe_record(std::filesystem::path const&          path,
                                            gsl::not_null<m::pe::loader_context*> loader):
    m_path(path),
    m_name(m::to_wstring(downcase(path.filename().c_str()))),
    m_not_found(!std::filesystem::exists(path)),
    m_decoder(m_not_found ? nullptr : std::make_unique<m::pe::decoder>(m::filesystem::open(path)))
{
    if (!m_decoder)
        return;

    for (auto&& e: m_decoder->m_image_import_descriptors)
    {
        if (e.m_name_string.size() != 0)
            loader->m_pending.push(downcase(e.m_name_string));
    }
}

m::pe::loader_context::pe_record::pe_record(std::wstring_view view):
    m_path(view), m_name(view), m_not_found(false), m_decoder(nullptr)
{}

m::pe::loader_context::pe_record::pe_record(std::wstring_view view,
                                            m::pe::loader_context::pe_record::pe_not_found_t):
    m_path(view), m_name(view), m_not_found(true), m_decoder(nullptr)
{}

bool
m::pe::loader_context::pe_record::not_found() const
{
    return m_not_found;
}

std::wstring
m::pe::loader_context::pe_record::name() const
{
    return m_name;
}
