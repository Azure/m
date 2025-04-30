// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/strings/convert.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>

#if 0

// boneyard

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
m::to_wstring(std::u8string_view v)
{
    std::wstring str;
    to_wstring(v, str);
    return str;
}

void
m::to_wstring(std::u8string_view v, std::wstring& str)
{
    details::transcode_utf8_to_wchar_t(v, str);
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

    // TODO: On Linux, this should(should it?) convert UTF-8 to UTF-32

    for (auto const ch: v)
    {
        str.push_back(static_cast<char32_t>(ch));
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

#endif

namespace details
{
    template <typename OutIter>
    OutIter
    write_to_wchar_t(char32_t ch, OutIter it)
    {
        if constexpr (sizeof(wchar_t) == 2)
        {
            // wchar_t is UTF-16
            it = m::utf::encode_utf16(ch, it);
        }
        else
        {
            it = m::utf::encode_utf32(ch, it);
        }

        return it;
    }

    //
    // Templatized form because the UTF-8 data can come in
    // possibly 3 different "byte" sized chunks, std::byte,
    // char, and char8_t.
    //
    template <typename Utf8CharT>
    void
    transcode_utf8_to_wchar_t(std::basic_string_view<Utf8CharT> v, std::wstring& str)
    {
        str.erase();

        std::size_t wchar_count{};

        auto       it   = v.begin();
        auto const last = v.end();

        while (it != last)
        {
            auto [newit, ch] = m::utf::decode_utf8(it, last);
            wchar_count += m::utf::compute_encoded_utf16_count(ch);
            it = newit;
        }

        str.reserve(wchar_count);

        it = v.begin();

        auto outit = std::back_inserter(str);

        while (it != last)
        {
            auto [newit, ch] = m::utf::decode_utf8(it, last);
            outit            = details::write_to_wchar_t(ch, outit);
        }
    }
} // namespace details


//
// to_u8string
//

