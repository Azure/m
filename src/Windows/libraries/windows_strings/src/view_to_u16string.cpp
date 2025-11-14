// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/windows_strings/convert.h>

namespace m
{
    std::u16string_view
    string_converter<wchar_t const*, std::u16string_view>::make_view(cwzstring str)
    {
        if (str == nullptr)
            return std::u16string_view();

        return std::u16string_view(reinterpret_cast<cu16zstring>(static_cast<cwzstring>(str)));
    }

    std::u16string_view
    string_converter<std::wstring_view, std::u16string_view>::make_view(std::wstring_view view)
    {
        return std::u16string_view(reinterpret_cast<char16_t const*>(view.data()), view.size());
    }

    std::optional<std::u16string_view>
    string_converter<std::wstring_view, std::u16string_view>::make_view(
        std::optional<std::wstring_view> const& view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_view(view.value());
    }

    std::u16string_view
    string_converter<std::wstring, std::u16string_view>::make_view(std::wstring const& str)
    {
        return std::u16string_view(reinterpret_cast<char16_t const*>(str.data()), str.size());
    }

    std::optional<std::u16string_view>
    string_converter<std::wstring, std::u16string_view>::make_view(
        std::optional<std::wstring> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_view(str.value());
    }

} // namespace m
