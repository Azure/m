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
    std::optional<std::u16string>
    to_u16string(cu8zstring ptr)
    {
        if (ptr == nullptr)
            return std::nullopt;

        return to_u16string(m::not_null(ptr));
    }

    std::u16string
    to_u16string(m::not_null<cu8zstring> ptr)
    {
        std::u16string str;
        utf::transcode(std::u8string_view{ptr}, str);
        return str;
    }

    std::optional<std::u16string>
    to_u16string(cu16zstring ptr)
    {
        if (ptr == nullptr)
            return std::nullopt;

        return to_u16string(m::not_null(ptr));
    }

    std::u16string
    to_u16string(m::not_null<cu16zstring> ptr)
    {
        std::u16string str;
        utf::transcode(std::u16string_view{ptr}, str);
        return str;
    }

    std::optional<std::u16string>
    to_u16string(cu32zstring ptr)
    {
        if (ptr == nullptr)
            return std::nullopt;

        return to_u16string(m::not_null(ptr));
    }

    std::u16string
    to_u16string(m::not_null<cu32zstring> ptr)
    {
        std::u16string str;
        utf::transcode(std::u32string_view{ptr}, str);
        return str;
    }

    //
    // std::u8string -> std::u16string
    // std::u8string_view -> std::u16string
    // std::optional<std::u8string_view> -> std::optional<std::u16string>
    //

    void
    to_u16string(std::u8string_view v, std::u16string& str)
    {
        utf::transcode(v, str);
    }

    std::u16string
    to_u16string(std::u8string_view v)
    {
        std::u16string str;
        to_u16string(v, str);
        return str;
    }

    void
    to_u16string(std::u8string const& s, std::u16string& str)
    {
        utf::transcode(std::u8string_view{s}, str);
    }

    std::u16string
    to_u16string(std::u8string const& s)
    {
        std::u16string str;
        to_u16string(s, str);
        return str;
    }

    void
    to_u16string(std::optional<std::u8string_view> v, std::optional<std::u16string>& str)
    {
        if (v)
        {
            std::u16string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    std::optional<std::u16string>
    to_u16string(std::optional<std::u8string_view> v)
    {
        if (v)
            return to_u16string(v.value());

        return std::nullopt;
    }

    //
    // std::u16string -> std::u16string
    // std::u16string_view -> std::u16string
    // std::optional<std::u16string_view> -> std::optional<std::u16string>
    //

    std::u16string
    to_u16string(std::u16string_view v)
    {
        return std::u16string(v);
    }

    void
    to_u16string(std::u16string_view v, std::u16string& str)
    {
        str = v;
    }

    std::u16string
    to_u16string(std::u16string const& s)
    {
        return s;
    }

    void
    to_u16string(std::u16string const& s, std::u16string& str)
    {
        str = s;
    }

    std::optional<std::u16string>
    to_u16string(std::optional<std::u16string_view> v)
    {
        if (v)
            return std::u16string(v.value());

        return std::nullopt;
    }

    void
    to_u16string(std::optional<std::u16string_view> v, std::optional<std::u16string>& str)
    {
        str = v;
    }

    //
    // std::u32string -> std::u16string
    // std::u32string_view -> std::u16string
    // std::optional<std::u32string_view> -> std::optional<std::u16string>
    //

    void
    to_u16string(std::u32string_view v, std::u16string& str)
    {
        utf::transcode(v, str);
    }

    std::u16string
    to_u16string(std::u32string_view v)
    {
        std::u16string str;
        to_u16string(v, str);
        return str;
    }

    void
    to_u16string(std::u32string const& s, std::u16string& str)
    {
        utf::transcode(std::u32string_view{s}, str);
    }

    std::u16string
    to_u16string(std::u32string const& s)
    {
        std::u16string str;
        to_u16string(s, str);
        return str;
    }

    void
    to_u16string(std::optional<std::u32string_view> v, std::optional<std::u16string>& str)
    {
        if (v)
        {
            std::u16string t;
            utf::transcode(v.value(), t);
            str = t;
        }
        else
            str = std::nullopt;
    }

    std::optional<std::u16string>
    to_u16string(std::optional<std::u32string_view> v)
    {
        std::optional<std::u16string> str;
        to_u16string(v, str);
        return str;
    }

} // namespace m
