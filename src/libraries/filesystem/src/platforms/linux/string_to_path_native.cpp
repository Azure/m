// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/filesystem/filesystem.h>
#include <m/linux_strings/convert.h>
#include <m/strings/convert.h>

//
// Implementations of Linux conversions to the std::filesystem::path native
// string type.
//
// For the GCC STL, this is std::string.
//

static_assert(std::is_same_v<char, std::filesystem::path::value_type>);

void
m::filesystem::to_native(std::string_view                                      v,
                         std::basic_string<std::filesystem::path::value_type>& str)
{
    str.clear();
    str.reserve(v.size());
    str.assign(v.data(), v.size());
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::string_view v)
{
    std::basic_string<std::filesystem::path::value_type> str;
    to_native(v, str);
    return str;
}

void
m::filesystem::to_native(std::wstring_view                                     v,
                         std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_string(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::wstring_view v)
{
    return m::to_string(v);
}

void
m::filesystem::to_native(std::u8string_view                                    v,
                         std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_string(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::u8string_view v)
{
    return m::to_string(v);
}

void
m::filesystem::to_native(std::u16string_view                                   v,
                         std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_string(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::u16string_view v)
{
    return m::to_string(v);
}

void
m::filesystem::to_native(std::u32string_view                                   v,
                         std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_string(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::u32string_view v)
{
    return m::to_string(v);
}
