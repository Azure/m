// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/multi_byte/multi_byte_to_utf16.h>
#include <m/multi_byte/utf16_to_multi_byte.h>
#include <m/utility/concepts.h>
#include <m/utility/make_span.h>

#include <Windows.h>

namespace m
{
    //
    // to char, presumably from utf-16
    //

    template <>
    void
    view_to_tstring(multi_byte::code_page, std::string_view in, std::string& out)
    {
        out = std::string(in);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp, std::wstring_view in, std::string& out)
    {
        m::utf16_to_multi_byte(cp, in, out);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp, std::u16string_view in, std::string& out)
    {
        m::utf16_to_multi_byte(cp, in, out);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page, std::string_view in, std::string& out, std::error_code&)
    {
        out = std::string(in);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp,
                    std::wstring_view     in,
                    std::string&          out,
                    std::error_code&      ec)
    {
        m::utf16_to_multi_byte(cp, in, out, ec);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp,
                    std::u16string_view   in,
                    std::string&          out,
                    std::error_code&      ec)
    {
        m::utf16_to_multi_byte(cp, in, out, ec);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp, std::u8string_view in, std::string& out)
    {
        std::wstring w_in = to_wstring(in);
        view_to_tstring(cp, std::wstring_view(w_in.begin(), w_in.end()), out);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp,
                    std::u8string_view    in,
                    std::string&          out,
                    std::error_code&      ec)
    {
        std::wstring w_in = to_wstring(in);
        view_to_tstring(cp, std::wstring_view(w_in.begin(), w_in.end()), out, ec);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp, std::u32string_view in, std::string& out)
    {
        std::wstring w_in = to_wstring(in);
        view_to_tstring(cp, std::wstring_view(w_in.begin(), w_in.end()), out);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp,
                    std::u32string_view   in,
                    std::string&          out,
                    std::error_code&      ec)
    {
        std::wstring w_in = to_wstring(in);
        view_to_tstring(cp, std::wstring_view(w_in.begin(), w_in.end()), out, ec);
    }

    //
    // to wchar_t, presumably from multi-byte (char)
    //

    template <>
    void
    view_to_tstring(multi_byte::code_page cp, std::string_view in, std::wstring& out)
    {
        m::multi_byte_to_utf16(cp, in, out);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp,
                    std::string_view      in,
                    std::wstring&         out,
                    std::error_code&      ec)
    {
        m::multi_byte_to_utf16(cp, in, out, ec);
    }

    //
    // to char8_t, presumably from multi-byte (char)
    //
    // There is no direct path from mbcs to UTF-8, so this will have an extra
    // memory allocation unfortunately.
    //

    template <>
    void
    view_to_tstring(multi_byte::code_page cp, std::string_view in, std::u8string& out)
    {
        std::wstring temp;
        m::multi_byte_to_utf16(cp, in, temp);
        auto const wview = std::wstring_view(temp.begin(), temp.end());
        out              = to_u8string(wview);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp,
                    std::string_view      in,
                    std::u8string&        out,
                    std::error_code&      ec)
    {
        std::wstring temp;
        m::multi_byte_to_utf16(cp, in, temp, ec);
        if (!m::failed(ec))
        {
            auto const wview = std::wstring_view(temp.begin(), temp.end());
            out              = to_u8string(wview);
        }
    }

    //
    // to char16_t, presumably from multi-byte (char)
    //

    template <>
    void
    view_to_tstring(multi_byte::code_page cp, std::string_view in, std::u16string& out)
    {
        m::multi_byte_to_utf16(cp, in, out);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp,
                    std::string_view      in,
                    std::u16string&       out,
                    std::error_code&      ec)
    {
        m::multi_byte_to_utf16(cp, in, out, ec);
    }

    //
    // to char32_t, presumably from multi-byte (char)
    //
    // There is no direct path from mbcs to UTF-32, so this will have an extra
    // memory allocation unfortunately.
    //

    template <>
    void
    view_to_tstring(multi_byte::code_page cp, std::string_view in, std::u32string& out)
    {
        std::wstring temp;
        m::multi_byte_to_utf16(cp, in, temp);
        auto const wview = std::wstring_view(temp.begin(), temp.end());
        out              = to_u32string(wview);
    }

    template <>
    void
    view_to_tstring(multi_byte::code_page cp,
                    std::string_view      in,
                    std::u32string&       out,
                    std::error_code&      ec)
    {
        std::wstring temp;
        m::multi_byte_to_utf16(cp, in, temp, ec);
        if (!m::failed(ec))
        {
            auto const wview = std::wstring_view(temp.begin(), temp.end());
            out              = to_u32string(wview);
        }
    }

#if 0
    //
    // to wchar_t, presumably from utf-16
    //
    template <typename TCharFrom>
        requires utf16_character<TCharFrom>
    void
    view_to_tstring(multi_byte::code_page             cp,
                    std::basic_string_view<TCharFrom> in,
                    std::wstring&                     out)
    {
        m::utf16_to_multi_byte(cp, in, out);
    }

    template <typename TCharFrom>
        requires utf16_character<TCharFrom>
    std::wstring
    view_to_tstring(multi_byte::code_page cp, std::basic_string_view<TCharFrom> in)
    {
        std::wstring out;
        m::utf16_to_multi_byte(cp, in, out);
        return out;
    }

    template <typename TCharFrom>
        requires utf16_character<TCharFrom>
    void
    view_to_tstring(multi_byte::code_page             cp,
                    std::basic_string_view<TCharFrom> in,
                    std::wstring&                     out,
                    std::error_code&                  ec)
    {
        m::utf16_to_multi_byte(cp, in, out, ec);
    }

    template <typename TCharFrom>
        requires utf16_character<TCharFrom>
    std::wstring
    view_to_tstring(multi_byte::code_page             cp,
                    std::basic_string_view<TCharFrom> in,
                    std::error_code&                  ec)
    {
        std::wstring out;
        m::utf16_to_multi_byte(cp, in, out, ec);
        return out;
    }
#endif

} // namespace m
