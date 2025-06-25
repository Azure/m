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


m::pe::loader::reference::reference(m::not_null<m::pe::loader::pe_record*> source,
                                  std::filesystem::path const& target_path):
    m_source(source), m_target{}, m_target_path(downcase(target_path.native()))
{}

m::not_null<m::pe::loader::pe_record*>
m::pe::loader::reference::source() const
{
    return m_source;
}

std::wstring
m::pe::loader::reference::target_name() const
{
    return m::to_wstring(m_target_path.native());
}

m::pe::loader::pe_record*
m::pe::loader::reference::target() const
{
    return m_target;
}

void
m::pe::loader::reference::target(m::not_null<m::pe::loader::pe_record*> pe) const
{
    m_target = pe;
}

