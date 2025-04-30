// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <string>
#include <string_view>

#include <m/cast/cast.h>
#include <m/filesystem/filesystem.h>
#include <m/strings/convert.h>
#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

//
// Unfortunately this source module diverges significantly for Windows and
// Linux.
// 
// On Windows using the MSVC STL, the std::filesystem uses a wchar_t based
// string to store the path (which is natural for Windows) encoded in UTF-16.
// This is perfectly natural and aligns with good Windows practices.
// 
// On Linux, using the STLs that appear to be in use in the CIs that I have
// seen, std::filesystem stores the path in a std::string which is char based
// and there is some level of tacit assumption that the paths are encoded in
// UTF-8.
// 
// We will try to use "the right" headers so that the code can, for the most
// part, be common, but there may be significantly different modes.
//

std::string
m::filesystem::path_to_string(std::filesystem::path const& p)
{
    std::string str;
    path_to_string(p, str);
    return str;
}

void
m::filesystem::path_to_string(std::filesystem::path const& p, std::string& str)
{
    auto s = p.c_str();
    auto v = std::basic_string_view<std::filesystem::path::value_type>(s);
    m::to_string(v, str);
}

std::wstring
m::filesystem::path_to_wstring(std::filesystem::path const& p)
{
    std::wstring str;
    path_to_wstring(p, str);
    return str;
}

void
m::filesystem::path_to_wstring(std::filesystem::path const& p, std::wstring& str)
{
    auto s = p.c_str();
    auto v = std::basic_string_view<std::filesystem::path::value_type>(s);
    m::to_wstring(v, str);
}

std::u8string
m::filesystem::path_to_u8string(std::filesystem::path const& p)
{
    std::u8string str;
    path_to_u8string(p, str);
    return str;
}

void
m::filesystem::path_to_u8string(std::filesystem::path const& p, std::u8string& str)
{
    auto s = p.c_str();
    auto v = std::basic_string_view<std::filesystem::path::value_type>(s);
    m::to_u8string(v, str);
}

std::u16string
m::filesystem::path_to_u16string(std::filesystem::path const& p)
{
    std::u16string str;
    path_to_u16string(p, str);
    return str;
}

void
m::filesystem::path_to_u16string(std::filesystem::path const& p, std::u16string& str)
{
    auto s = p.c_str();
    auto v = std::basic_string_view<std::filesystem::path::value_type>(s);
    m::to_u16string(v, str);
}

std::u32string
m::filesystem::path_to_u32string(std::filesystem::path const& p)
{
    std::u32string str;
    path_to_u32string(p, str);
    return str;
}

void
m::filesystem::path_to_u32string(std::filesystem::path const& p, std::u32string& str)
{
    auto s = p.c_str();
    auto v = std::basic_string_view<std::filesystem::path::value_type>(s);
    m::to_u32string(v, str);
}
