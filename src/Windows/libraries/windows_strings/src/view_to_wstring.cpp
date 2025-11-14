// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/windows_strings/convert.h>

namespace m
{
    std::wstring_view
    string_converter<char16_t const*, std::wstring_view>::make_view(cu16zstring str)
    {
        if (str == nullptr)
            return std::wstring_view();

        return std::wstring_view(reinterpret_cast<cwzstring>(static_cast<cu16zstring>(str)));
    }

    std::wstring_view
    string_converter<std::u16string_view, std::wstring_view>::make_view(std::u16string_view view)
    {
        return std::wstring_view(reinterpret_cast<wchar_t const*>(view.data()), view.size());
    }

    std::optional<std::wstring_view>
    string_converter<std::u16string_view, std::wstring_view>::make_view(
        std::optional<std::u16string_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_view(view.value());
    }

    std::wstring_view
    string_converter<std::u16string, std::wstring_view>::make_view(std::u16string const& str)
    {
        return std::wstring_view(reinterpret_cast<wchar_t const*>(str.data()), str.size());
    }

    std::optional<std::wstring_view>
    string_converter<std::u16string, std::wstring_view>::make_view(
        std::optional<std::u16string> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_view(str.value());
    }

} // namespace m
