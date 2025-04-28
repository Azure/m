// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/strings/convert.h>
#include <m/utf/utf_decode.h>
#include <m/utf/utf_encode.h>

constexpr wchar_t
m::byte_to_wchar(std::byte b)
{
    return static_cast<wchar_t>(b);
}

constexpr wchar_t
m::char_to_wchar(char ch)
{
    return static_cast<wchar_t>(ch);
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

    for (auto const ch: v)
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
    // new basic_string<> and then copy whereas this could avoid a new allocation.
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

    std::size_t char_count = std::transform_reduce(
        v.begin(), v.end(), std::size_t{}, std::plus{}, [](char32_t ch) -> std::size_t {
            return m::utf::compute_encoded_utf16_count(ch);
        });

    str.reserve(char_count);

    auto outit = std::back_inserter(str);

    for (auto const ch: v)
        outit = utf::encode_utf16(ch, outit);
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

    auto       it  = v.begin();
    auto const end = v.end();

    while (it != end)
    {
        auto [new_it, ch] = utf::decode_utf8(it, end);
        s++;
        it = new_it;
    }

    str.erase();
    str.reserve(s);

    it          = v.begin();
    auto out_it = std::back_inserter(str);

    while (it != end)
    {
        auto [new_it, ch] = utf::decode_utf8(it, end);
        out_it            = m::utf::encode_utf32(ch, out_it);
        it                = new_it;
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
    std::size_t s{};

    auto       it  = v.begin();
    auto const end = v.end();

    while (it != end)
    {
        auto x = utf::decode_utf16(it, end);
        // Each code point may consume two of the input values but
        // then only one output position.
        s++;
        it = x.it;
    }

    str.erase();
    str.reserve(s);

    it          = v.begin();
    auto out_it = std::back_inserter(str);

    while (it != end)
    {
        auto x = utf::decode_utf16(it, end);
        out_it = m::utf::encode_utf32(x.ch, out_it);
        it     = x.it;
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
    // new basic_string<> and then copy whereas this could avoid a new allocation.
    //
    str.erase();
    str.reserve(v.size());

    for (auto const ch: v)
    {
        str.push_back(ch);
    }
}
