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
} // namespace details

namespace m
{
    std::optional<std::string>
    to_string(czstring str)
    {
        if (str == nullptr)
            return std::nullopt;

        return to_string(m::not_null(str));
    }

    std::string
    to_string(m::not_null<czstring> str)
    {
        return std::string(str);
    }

    std::string
    to_string(std::string_view v)
    {
        return std::string(v);
    }

    void
    to_string(std::string_view v, std::string& str)
    {
        str = v;
    }

    std::string
    to_string(std::string const& s)
    {
        return std::string(std::string_view{s});
    }

    void
    to_string(std::string const& s, std::string& str)
    {
        str = s;
    }

    std::optional<std::string>
    to_string(std::optional<std::string_view> v)
    {
        if (v)
            return std::string(v.value());

        return std::nullopt;
    }

    void
    to_string(std::optional<std::string_view> v, std::optional<std::string>& str)
    {
        if (v)
            str = v;
        else
            str = std::nullopt;
    }
}
