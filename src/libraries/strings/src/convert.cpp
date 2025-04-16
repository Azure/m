// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>

#include <m/cast/to.h>
#include <m/strings/convert.h>
#include <m/utf/utf_decode.h>
#include <m/utf/utf_encode.h>

//
// World's worst conversions
//
// Please fix
//
// at least they throw for "extended" cases except for surrogate pairs char16_t to char32_t.
// 
// Updated: some are fixed, please read before rewriting.
//

wchar_t
m::byte_to_wchar(std::byte b)
{
    return static_cast<wchar_t>(b);
}

std::wstring
m::to_wstring(std::string_view v)
{
    std::wstring str;
    to_wstring(v, str);
    return str;
}

void
m::to_wstring(std::string_view v, std::wstring& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<wchar_t>(ch));
    }
}

std::wstring
m::to_wstring(std::wstring_view v)
{
    return std::wstring(v);
}

void
m::to_wstring(std::wstring_view v, std::wstring& str)
{
    //
    // This could be simply 'str = v;' but that would seem to form a
    // new wstring and then copy whereas this could avoid a new allocation.
    //
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<wchar_t>(ch));
    }
}

std::wstring
m::to_wstring(std::u8string_view v)
{
    std::wstring str;
    to_wstring(v, str);
    return str;
}

void
m::to_wstring(std::u8string_view v, std::wstring& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<wchar_t>(ch));
    }
}

std::wstring
m::to_wstring(std::u16string_view v)
{
    std::wstring str;
    to_wstring(v, str);
    return str;
}

void
m::to_wstring(std::u16string_view v, std::wstring& str)
{
    str.erase();
    str.reserve(v.size());

    for (char ch: v)
    {
        str.push_back(m::to<wchar_t>(ch));
    }
}

std::wstring
m::to_wstring(std::u32string_view v)
{
    std::wstring str;
    to_wstring(v, str);
    return str;
}

void
m::to_wstring(std::u32string_view v, std::wstring& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<wchar_t>(ch));
    }
}

std::string
m::to_string(std::string_view v)
{
    return std::string(v);
}

void
m::to_string(std::string_view v, std::string& str)
{
    //
    // This could be simply 'str = v;' but that would seem to form a
    // new wstring and then copy whereas this could avoid a new allocation.
    //
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(ch);
    }
}

std::string
m::to_string(std::wstring_view v)
{
    std::string str;
    to_string(v, str);
    return str;
}

void
m::to_string(std::wstring_view v, std::string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char>(ch));
    }
}

std::string
m::to_string(std::u8string_view v)
{
    std::string str;
    to_string(v, str);
    return str;
}

void
m::to_string(std::u8string_view v, std::string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char>(ch));
    }
}

std::string
m::to_string(std::u16string_view v)
{
    std::string str;
    to_string(v, str);
    return str;
}

void
m::to_string(std::u16string_view v, std::string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char>(ch));
    }
}

std::string
m::to_string(std::u32string_view v)
{
    std::string str;
    to_string(v, str);
    return str;
}

void
m::to_string(std::u32string_view v, std::string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char>(ch));
    }
}

//
// to_u8string
//

std::u8string
m::to_u8string(std::string_view v)
{
    std::u8string str;
    to_u8string(v, str);
    return str;
}

void
m::to_u8string(std::string_view v, std::u8string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char8_t>(ch));
    }
}

std::u8string
m::to_u8string(std::wstring_view v)
{
    std::u8string str;
    to_u8string(v, str);
    return str;
}

void
m::to_u8string(std::wstring_view v, std::u8string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char8_t>(ch));
    }
}

std::u8string
m::to_u8string(std::u8string_view v)
{
    return std::u8string(v);
}

void
m::to_u8string(std::u8string_view v, std::u8string& str)
{
    //
    // This could be simply 'str = v;' but that would seem to form a
    // new wstring and then copy whereas this could avoid a new allocation.
    //
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(ch);
    }
}

std::u8string
m::to_u8string(std::u16string_view v)
{
    std::u8string str;
    to_u8string(v, str);
    return str;
}

void
m::to_u8string(std::u16string_view v, std::u8string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char8_t>(ch));
    }
}

std::u8string
m::to_u8string(std::u32string_view v)
{
    std::u8string str;
    to_u8string(v, str);
    return str;
}

void
m::to_u8string(std::u32string_view v, std::u8string& str)
{
    std::size_t s{};

    for (auto const ch : v)
        s += utf::compute_encoded_utf8_size(ch);

    str.erase();
    str.reserve(s);

    auto it = std::back_inserter(str);

    for (auto const ch: v)
        utf::encode_utf8(ch, it);
}

//
// to_u16string
//

std::u16string
m::to_u16string(std::string_view v)
{
    std::u16string str;
    to_u16string(v, str);
    return str;
}

void
m::to_u16string(std::string_view v, std::u16string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char16_t>(ch));
    }
}

std::u16string
m::to_u16string(std::wstring_view v)
{
    std::u16string str;
    to_u16string(v, str);
    return str;
}

void
m::to_u16string(std::wstring_view v, std::u16string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char16_t>(ch));
    }
}

std::u16string
m::to_u16string(std::u8string_view v)
{
    std::u16string str;
    to_u16string(v, str);
    return str;
}

void
m::to_u16string(std::u8string_view v, std::u16string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char16_t>(ch));
    }
}

std::u16string
m::to_u16string(std::u16string_view v)
{
    return std::u16string(v);
}

void
m::to_u16string(std::u16string_view v, std::u16string& str)
{
    //
    // This could be simply 'str = v;' but that would seem to form a
    // new wstring and then copy whereas this could avoid a new allocation.
    //
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(ch);
    }
}

std::u16string
m::to_u16string(std::u32string_view v)
{
    std::u16string str;
    to_u16string(v, str);
    return str;
}

void
m::to_u16string(std::u32string_view v, std::u16string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char16_t>(ch));
    }
}

//
// to_u32string
//

std::u32string
m::to_u32string(std::string_view v)
{
    std::u32string str;
    to_u32string(v, str);
    return str;
}

void
m::to_u32string(std::string_view v, std::u32string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char32_t>(ch));
    }
}

std::u32string
m::to_u32string(std::wstring_view v)
{
    std::u32string str;
    to_u32string(v, str);
    return str;
}

void
m::to_u32string(std::wstring_view v, std::u32string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char32_t>(ch));
    }
}

std::u32string
m::to_u32string(std::u8string_view v)
{
    std::u32string str;
    to_u32string(v, str);
    return str;
}

void
m::to_u32string(std::u8string_view v, std::u32string& str)
{
    std::size_t s{};

    auto it = v.begin();
    auto const end = v.end();

    while (it != end)
    {
        auto x = utf::decode_utf8(it, end);
        s += utf::compute_encoded_utf32_size(x.ch);
        it = x.it;
    }

    str.erase();
    str.reserve(s);

    it = v.begin();
    auto out_it = std::back_inserter(str);

    while (it != end)
    {
        auto x = utf::decode_utf8(it, end);
        m::utf::encode_utf32(x.ch, out_it);
        it = x.it;
    }
}

std::u32string
m::to_u32string(std::u16string_view v)
{
    std::u32string str;
    to_u32string(v, str);
    return str;
}

void
m::to_u32string(std::u16string_view v, std::u32string& str)
{
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(m::to<char32_t>(ch));
    }
}

std::u32string
m::to_u32string(std::u32string_view v)
{
    return std::u32string(v);
}

void
m::to_u32string(std::u32string_view v, std::u32string& str)
{
    //
    // This could be simply 'str = v;' but that would seem to form a
    // new wstring and then copy whereas this could avoid a new allocation.
    //
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(ch);
    }
}
