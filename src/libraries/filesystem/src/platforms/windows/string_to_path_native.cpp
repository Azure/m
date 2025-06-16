// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/filesystem/filesystem.h>
#include <m/strings/convert.h>

//
// Implementations of Windows conversions to the std::filesystem::path native
// string type.
// 
// For the MSVC STL this is std::wstring.
//

void
m::filesystem::to_native(std::string_view v, std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_wstring(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::string_view v)
{
    return m::to_wstring(v);
}

void
m::filesystem::to_native(std::wstring_view                                     v,
    std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_wstring(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::wstring_view v)
{
    return m::to_wstring(v);
}

void
m::filesystem::to_native(std::u8string_view                                    v,
    std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_wstring(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::u8string_view v)
{
    return m::to_wstring(v);
}

void
m::filesystem::to_native(std::u16string_view                                   v,
    std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_wstring(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::u16string_view v)
{
    return m::to_wstring(v);
}

void
m::filesystem::to_native(std::u32string_view                                   v,
    std::basic_string<std::filesystem::path::value_type>& str)
{
    m::to_wstring(v, str);
}

std::basic_string<std::filesystem::path::value_type>
m::filesystem::to_native(std::u32string_view v)
{
    return m::to_wstring(v);
}
