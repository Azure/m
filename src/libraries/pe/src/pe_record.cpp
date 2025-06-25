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

m::pe::loader::pe_record::pe_record(std::filesystem::path const&        path,
                                    m::not_null<m::pe::loader::loader*> loader):
    m_path(m::filesystem::make_path(downcase(path.native()))),
    m_not_found(!std::filesystem::exists(path)),
    m_decoder(m_not_found ?
                  nullptr :
                  std::make_unique<m::pe::decoder>(m::filesystem::open_seekable_input_file(path)))
{
    if (!m_decoder)
        return;

    for (auto&& e: m_decoder->m_image_import_descriptors)
    {
        if (e.m_name_string.size() != 0)
        {
            auto const p         = m_outbound_references.emplace(this, downcase(e.m_name_string));
            auto const it        = p.first;
            auto const insertted = p.second;

            if (insertted)
            {
                loader->m_pending_references.push(&(*it));
            }
        }
    }
}

m::pe::loader::pe_record::pe_record(std::filesystem::path const& path):
    m_path(m::filesystem::make_path(downcase(path.native()))),
    m_not_found{false},
    m_decoder{nullptr}
{}

m::pe::loader::pe_record::pe_record(std::filesystem::path const& path,
                                    m::pe::loader::pe_not_found_t):
    m_path(m::filesystem::make_path(downcase(path.native()))),
    m_not_found{true},
    m_decoder{nullptr}
{}

void
m::pe::loader::pe_record::add_reference(
    m::not_null<m::pe::loader::reference const*> reference)
{
    m_inbound_references.insert(reference);
}

bool
m::pe::loader::pe_record::not_found() const
{
    return m_not_found;
}

std::wstring
m::pe::loader::pe_record::name() const
{
    return m::to_wstring(m_path.native());
}

std::set<m::not_null<m::pe::loader::reference const*>> const&
m::pe::loader::pe_record::inbound_references_ref() const
{
    return m_inbound_references;
}
