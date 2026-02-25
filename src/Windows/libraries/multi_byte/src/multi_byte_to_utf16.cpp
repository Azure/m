// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/multi_byte/convert.h>
#include <m/strings/convert.h>
#include <m/utility/concepts.h>
#include <m/windows_strings/convert.h>

#include <Windows.h>

namespace
{
    template <typename TCharOut>
        requires m::utf16_character<TCharOut>
    void
    multi_byte_to_utf16_fn(m::multi_byte::code_page cp,
                           std::string_view         in,
                           std::span<TCharOut>&     span,
                           std::error_code&         ec)
    {
        if (in.size() == 0)
        {
            span = span.subspan(0, 0);
            return;
        }

        auto const i = ::MultiByteToWideChar(std::to_underlying(cp),
                                             MB_ERR_INVALID_CHARS,
                                             in.data(),
                                             m::to<int>(in.size()),
                                             reinterpret_cast<LPWSTR>(span.data()),
                                             m::to<int>(span.size()));
        if (i < 1)
        {
            ec = m::get_last_win32_error();
            span = span.subspan(0, 0);
            return;
        }

        span = span.subspan(0, m::to<std::size_t>(i));
    }

    template <typename TCharOut>
        requires m::utf16_character<TCharOut>
    void
    multi_byte_to_utf16_fn(m::multi_byte::code_page cp,
                           std::string_view         view,
                           std::span<TCharOut>&     buffer)
    {
        if (view.size() == 0)
        {
            buffer = buffer.subspan(0, 0);
            return;
        }

        auto const i = ::MultiByteToWideChar(std::to_underlying(cp),
                                             MB_ERR_INVALID_CHARS,
                                             view.data(),
                                             m::to<int>(view.size()),
                                             reinterpret_cast<LPWSTR>(buffer.data()),
                                             m::to<int>(buffer.size()));
        if (i < 1)
            m::throw_last_win32_error();

        auto const i_as_sizet = m::to<std::size_t>(i);

        buffer = buffer.subspan(0, i_as_sizet);
    }

    template <typename TCharOut>
        requires m::utf16_character<TCharOut>
    void
    multi_byte_to_utf16_fn(m::multi_byte::code_page     cp,
                           std::string_view             in,
                           std::basic_string<TCharOut>& out)
    {
        auto const length = m::multi_byte_to_utf16_length(cp, in);
        out.resize_and_overwrite(length, [cp, in](auto buffer, auto buffer_size) -> auto {
            auto span = m::make_span(buffer, buffer_size);
            multi_byte_to_utf16_fn(cp, in, span);
            return span.size();
        });
    }

    template <typename TCharOut>
        requires m::utf16_character<TCharOut>
    void
    multi_byte_to_utf16_fn(m::multi_byte::code_page     cp,
                           std::string_view             in,
                           std::basic_string<TCharOut>& out,
                           std::error_code&             ec)
    {
        auto const length = m::multi_byte_to_utf16_length(cp, in, ec);

        if (m::failed(ec))
            return;

        out.resize_and_overwrite(length, [cp, in, &ec](auto buffer, auto buffer_size) -> auto {
            auto span = m::make_span(buffer, buffer_size);
            multi_byte_to_utf16_fn(cp, in, span, ec);
            return span.size();
        });
    }

} // namespace

namespace m
{
    std::size_t
    multi_byte_to_utf16_length(multi_byte::code_page cp, std::string_view view)
    {
        if (view.size() == 0)
            return 0;

        auto const wchars_needed = ::MultiByteToWideChar(
            to_underlying(cp), MB_ERR_INVALID_CHARS, view.data(), to<int>(view.size()), nullptr, 0);
        if (wchars_needed < 1)
            throw_last_win32_error();

        return to<std::size_t>(wchars_needed);
    }

    std::size_t
    multi_byte_to_utf16_length(multi_byte::code_page cp, std::string_view view, std::error_code& ec)
    {
        if (view.size() == 0)
            return 0;

        auto const wchars_needed = ::MultiByteToWideChar(
            to_underlying(cp), MB_ERR_INVALID_CHARS, view.data(), to<int>(view.size()), nullptr, 0);
        if (wchars_needed < 1)
        {
            ec = m::get_last_win32_error();
            return 0;
        }

        return to<std::size_t>(wchars_needed);
    }

    template <>
    void
    multi_byte_to_utf16(m::multi_byte::code_page cp,
                        std::string_view         view,
                        std::span<wchar_t>&      span)
    {
        multi_byte_to_utf16_fn(cp, view, span);
    }

    template <>
    void
    multi_byte_to_utf16(m::multi_byte::code_page cp,
                        std::string_view         view,
                        std::span<char16_t>&     span)
    {
        multi_byte_to_utf16_fn(cp, view, span);
    }

    template <>
    void
    multi_byte_to_utf16(m::multi_byte::code_page cp,
                        std::string_view         in,
                        std::span<wchar_t>&      span,
                        std::error_code&         ec)
    {
        multi_byte_to_utf16_fn(cp, in, span, ec);
    }

    template <>
    void
    multi_byte_to_utf16(m::multi_byte::code_page cp,
                        std::string_view         in,
                        std::span<char16_t>&     span,
                        std::error_code&         ec)
    {
        return multi_byte_to_utf16_fn(cp, in, span, ec);
    }

    template <>
    void
    multi_byte_to_utf16(m::multi_byte::code_page cp, std::string_view in, std::wstring& out)
    {
        multi_byte_to_utf16_fn(cp, in, out);
    }

    template <>
    void
    multi_byte_to_utf16(m::multi_byte::code_page cp, std::string_view in, std::u16string& out)
    {
        multi_byte_to_utf16_fn(cp, in, out);
    }

    template <>
    void
    multi_byte_to_utf16(m::multi_byte::code_page cp,
                        std::string_view         in,
                        std::wstring&            out,
                        std::error_code&         ec)
    {
        multi_byte_to_utf16_fn(cp, in, out, ec);
    }

    template <>
    void
    multi_byte_to_utf16(m::multi_byte::code_page cp,
                        std::string_view         in,
                        std::u16string&          out,
                        std::error_code&         ec)
    {
        multi_byte_to_utf16_fn(cp, in, out, ec);
    }

    std::wstring
    to_wstring(m::multi_byte::code_page cp, std::string_view view)
    {
        std::wstring string;
        multi_byte_to_utf16(cp, view, string);
        return string;
    }

    void
    to_wstring(m::multi_byte::code_page cp, std::string_view view, std::wstring& str)
    {
        std::wstring t;
        multi_byte_to_utf16(cp, view, t);
        using std::swap;
        swap(t, str);
    }

    std::optional<std::wstring>
    to_wstring(m::multi_byte::code_page cp, std::optional<std::string_view> view)
    {
        if (!view.has_value())
            return std::nullopt;

        std::wstring string;
        multi_byte_to_utf16(cp, view.value(), string);
        return string;
    }

    void
    to_wstring(m::multi_byte::code_page        cp,
               std::optional<std::string_view> view,
               std::optional<std::wstring>&    str)
    {
        if (!view.has_value())
        {
            str = std::nullopt;
            return;
        }

        std::wstring t;
        multi_byte_to_utf16(cp, view.value(), t);
        str = t;
    }

    void
    to_u16string(m::multi_byte::code_page cp, std::string_view view, std::u16string& str)
    {
        std::u16string t;
        multi_byte_to_utf16(cp, view, t);
        using std::swap;
        swap(t, str);
    }

    std::u16string
    to_u16string(m::multi_byte::code_page cp, std::string_view view)
    {
        std::u16string string;
        multi_byte_to_utf16(cp, view, string);
        return string;
    }

    void
    to_u8string(m::multi_byte::code_page cp, std::string_view v, std::u8string& str)
    {
        //
        // There is no direct conversion from multibyte to multibyte. So the best we can do
        // is multibyte to UTF-16 and then back to UTF-8.
        //
        // In a better world we might try to do something to avoid heap
        // allocations with the temporary conversion, but for now, we allocate.
        //
        std::wstring wstr;
        to_wstring(cp, v, wstr);
        to_u8string(wstr, str);
    }

    std::u8string
    to_u8string(m::multi_byte::code_page cp, std::string_view v)
    {
        std::u8string str;
        to_u8string(cp, v, str);
        return str;
    }

    void
    to_u32string(m::multi_byte::code_page cp, std::string_view v, std::u32string& str)
    {
        //
        // There is no direct conversion from multibyte to multibyte. So the best we can do
        // is multibyte to UTF-16 and then back to UTF-8.
        //
        // In a better world we might try to do something to avoid heap
        // allocations with the temporary conversion, but for now, we allocate.
        //
        std::wstring wstr;
        to_wstring(cp, v, wstr);
        to_u32string(wstr, str);
    }

    std::u32string
    to_u32string(m::multi_byte::code_page cp, std::string_view v)
    {
        std::u32string str;
        to_u32string(cp, v, str);
        return str;
    }

} // namespace m