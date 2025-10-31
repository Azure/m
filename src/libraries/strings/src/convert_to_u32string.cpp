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

namespace
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
            outit            = write_to_wchar_t(ch, outit);
        }
    }
} // namespace

namespace m
{
    std::u32string
    to_u32string(char8_t const* ptr)
    {
        std::u32string str;
        utf::transcode(std::u8string_view{ptr}, str);
        return str;
    }
    std::u32string
    to_u32string(char16_t const* ptr)
    {
        std::u32string str;
        utf::transcode(std::u16string_view{ptr}, str);
        return str;
    }

    std::u32string
    to_u32string(char32_t const* ptr)
    {
        return std::u32string(std::u32string_view{ptr});
    }

    //
    // std::u8string -> std::u32string
    // std::u8string_view -> std::u32string
    // std::optional<std::u8string_view> -> std::optional<std::u32string>
    //

    void
    to_u32string(std::u8string_view v, std::u32string& str)
    {
        utf::transcode(v, str);
    }

    std::u32string
    to_u32string(std::u8string_view v)
    {
        std::u32string str;
        to_u32string(v, str);
        return str;
    }

    void
    to_u32string(std::u8string const& s, std::u32string& str)
    {
        utf::transcode(std::u8string_view{s}, str);
    }

    std::u32string
    to_u32string(std::u8string const& s)
    {
        std::u32string str;
        to_u32string(s, str);
        return str;
    }

    void
    to_u32string(std::optional<std::u8string_view> v, std::optional<std::u32string>& str)
    {
        if (v)
        {
            std::u32string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    std::optional<std::u32string>
    to_u32string(std::optional<std::u8string_view> v)
    {
        std::optional<std::u32string> str;
        to_u32string(v, str);
        return str;
    }

    //
    // std::u16string -> std::u32string
    // std::u16string_view -> std::u32string
    // std::optional<std::u16string_view> -> std::optional<std::u32string>
    //

    void
    to_u32string(std::u16string_view v, std::u32string& str)
    {
        utf::transcode(v, str);
    }

    std::u32string
    to_u32string(std::u16string_view v)
    {
        std::u32string str;
        to_u32string(v, str);
        return str;
    }

    std::u32string
    to_u32string(std::u16string const& s)
    {
        std::u32string str;
        to_u32string(s, str);
        return str;
    }

    void
    to_u32string(std::u16string const& s, std::u32string& str)
    {
        utf::transcode(std::u16string_view{s}, str);
    }

    std::optional<std::u32string>
    to_u32string(std::optional<std::u16string_view> v)
    {
        if (v)
            return to_u32string(v.value());

        return std::nullopt;
    }

    void
    to_u32string(std::optional<std::u16string_view> v, std::optional<std::u32string>& str)
    {
        if (v)
        {
            std::u32string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    //
    // std::u32string -> std::u32string
    // std::u32string_view -> std::u32string
    // std::optional<std::u32string_view> -> std::optional<std::u32string>
    //

    std::u32string
    to_u32string(std::u32string_view v)
    {
        return std::u32string(v);
    }

    void
    to_u32string(std::u32string_view v, std::u32string& str)
    {
        str = v;
    }

    std::u32string
    to_u32string(std::u32string const& s)
    {
        return s;
    }

    void
    to_u32string(std::u32string const& s, std::u32string& str)
    {
        str = s;
    }

    std::optional<std::u32string>
    to_u32string(std::optional<std::u32string_view> v)
    {
        if (v)
            return std::u32string(v.value());

        return std::nullopt;
    }

    void
    to_u32string(std::optional<std::u32string_view> v, std::optional<std::u32string>& str)
    {
        str = v;
    }
} // namespace m
