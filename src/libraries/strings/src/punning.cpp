// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/strings/punning.h>

m::strings::byte_string_view
m::strings::as_byte_string_view(std::u8string_view const& view)
{
    m::strings::byte_string_view rv(reinterpret_cast<std::byte const*>(view.data()), view.size());
    return rv;
}

m::strings::byte_string_view
m::strings::as_byte_string_view(std::string_view const& view)
{
    m::strings::byte_string_view rv(reinterpret_cast<std::byte const*>(view.data()), view.size());
    return rv;
}

std::u8string_view
m::strings::as_u8string_view(std::string_view const& view)
{
    std::u8string_view rv(reinterpret_cast<char8_t const*>(view.data()), view.size());
    return rv;
}

#ifndef WIN32
std::u32string_view
m::strings::as_u32string_view(std::wstring_view const& view)
{
    std::u32string_view rv(reinterpret_cast<char32_t const*>(view.data()), view.size());
    return rv;
}
#endif
