// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/strings/punning.h>

std::u8string_view
m::as_u8string_view(std::string_view const& view)
{
    std::u8string_view rv(reinterpret_cast<char8_t const*>(view.data()), view.size());
    return rv;
}

std::u8string_view
m::as_u8string_view(std::u8string_view const& view)
{
    return view;
}

std::string_view
m::as_string_view(std::u8string_view const& view)
{
    std::string_view rv(reinterpret_cast<char const*>(view.data()), view.size());
    return rv;
}

std::string_view
m::as_string_view(std::string_view const& view)
{
    return view;
}
