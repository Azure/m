// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/windows_strings/convert.h>

namespace m::string_conversion_details
{
    std::u16string_view
    sch<wchar_t const*, std::u16string_view>::make_view(cwzstring str)
    {
        if (str == nullptr)
            return std::u16string_view();

        return std::u16string_view(reinterpret_cast<cu16zstring>(static_cast<cwzstring>(str)));
    }

    std::u16string_view
    sch<std::wstring_view, std::u16string_view>::make_view(std::wstring_view view)
    {
        return std::u16string_view(reinterpret_cast<char16_t const*>(view.data()), view.size());
    }

    std::optional<std::u16string_view>
    sch<std::wstring_view, std::u16string_view>::make_view(std::optional<std::wstring_view> view)
    {
        if (!view.has_value())
            return std::nullopt;

        return make_view(view.value());
    }

    std::u16string_view
    sch<std::wstring, std::u16string_view>::make_view(std::wstring const& str)
    {
        return std::u16string_view(reinterpret_cast<char16_t const*>(str.data()), str.size());
    }

    std::optional<std::u16string_view>
    sch<std::wstring, std::u16string_view>::make_view(std::optional<std::wstring> const& str)
    {
        if (!str.has_value())
            return std::nullopt;

        return make_view(str.value());
    }

} // namespace m::string_conversion_details
