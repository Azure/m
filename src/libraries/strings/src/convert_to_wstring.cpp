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

namespace m
{
    std::optional<std::wstring>
    to_wstring(cwzstring str)
    {
        if (str == nullptr)
            return std::nullopt;

        return to_wstring(m::not_null(str));
    }

    std::wstring
    to_wstring(m::not_null<cwzstring> str)
    {
        return std::wstring{str};
    }

    std::wstring
    to_wstring(std::wstring_view v)
    {
        return std::wstring(v);
    }

    void
    to_wstring(std::wstring_view v, std::wstring& str)
    {
        str = v;
    }

    std::wstring
    to_wstring(std::wstring const& s)
    {
        return std::wstring(s);
    }

    void
    to_wstring(std::wstring const& s, std::wstring& str)
    {
        str = s;
    }

    std::optional<std::wstring>
    to_wstring(std::optional<std::wstring_view> v)
    {
        if (v)
            return std::wstring(v.value());

        return std::nullopt;
    }

    void
    to_wstring(std::optional<std::wstring_view> v, std::optional<std::wstring>& str)
    {
        if (v)
            str = v;
        else
            str = std::nullopt;
    }

    std::optional<std::wstring>
    to_wstring(std::optional<std::wstring> const& s)
    {
        if (s)
            return std::wstring(s.value());

        return std::nullopt;
    }

    void
    to_wstring(std::optional<std::wstring> s, std::optional<std::wstring>& str)
    {
        if (s)
            str = s;
        else
            str = std::nullopt;
    }




}
