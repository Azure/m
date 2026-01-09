// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/multi_byte/convert.h>
#include <m/utility/make_span.h>

#include <Windows.h>

namespace
{
    template <typename Utf16CharT>
    [[nodiscard]]
    std::size_t
    utf16_to_multi_byte_length_fn(m::multi_byte::code_page           cp,
                                  std::basic_string_view<Utf16CharT> view)
    {
        auto const view_size = view.size();
        if (view_size == 0)
            return 0;

        auto i = ::WideCharToMultiByte(m::to_underlying(cp),
                                       WC_NO_BEST_FIT_CHARS,
                                       reinterpret_cast<wchar_t const*>(view.data()),
                                       m::to<int>(view_size),
                                       nullptr,
                                       0,
                                       nullptr,  // lpDefaultChar
                                       nullptr); // lpUsedDefaultChar
        if (i < 1)
            m::throw_last_win32_error();

        return m::to<std::size_t>(i);
    }

    template <typename Utf16CharT>
    [[nodiscard]]
    std::size_t
    utf16_to_multi_byte_length_fn(m::multi_byte::code_page           cp,
                                  std::basic_string_view<Utf16CharT> view,
                                  std::error_code&                   ec)
    {
        auto const view_size = view.size();
        if (view_size == 0)
            return 0;

        auto i = ::WideCharToMultiByte(m::to_underlying(cp),
                                       WC_NO_BEST_FIT_CHARS,
                                       reinterpret_cast<wchar_t const*>(view.data()),
                                       m::to<int>(view_size),
                                       nullptr,
                                       0,
                                       nullptr,  // lpDefaultChar
                                       nullptr); // lpUsedDefaultChar
        if (i < 1)
        {
            ec = m::get_last_win32_error();
            return 0;
        }

        return m::to<std::size_t>(i);
    }

} // namespace

namespace m
{

    template <>
    std::size_t
    utf16_to_multi_byte_length(multi_byte::code_page cp, std::wstring_view view)
    {
        return utf16_to_multi_byte_length_fn(cp, view);
    }

    template <>
    std::size_t
    utf16_to_multi_byte_length(multi_byte::code_page cp, std::u16string_view view)
    {
        return utf16_to_multi_byte_length_fn(cp, view);
    }

    template <>
    std::size_t
    utf16_to_multi_byte_length(multi_byte::code_page cp,
                               std::wstring_view     view,
                               std::error_code&      ec)
    {
        return utf16_to_multi_byte_length_fn(cp, view, ec);
    }

    template <>
    std::size_t
    utf16_to_multi_byte_length(multi_byte::code_page cp,
                               std::u16string_view   view,
                               std::error_code&      ec)
    {
        return utf16_to_multi_byte_length_fn(cp, view, ec);
    }
} // namespace m
