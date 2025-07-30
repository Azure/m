// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <string>
#include <string_view>

#include <m/cast/cast.h>
#include <m/filesystem/filesystem.h>
#include <m/strings/convert.h>
#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

std::filesystem::path
m::filesystem::make_path(std::string_view v)
{
#ifdef WIN32
    return std::filesystem::path(m::acp_to_wstring(v));
#else
    return std::filesystem::path(v);
#endif
}

std::filesystem::path
m::filesystem::make_path(std::wstring_view v)
{
#ifdef WIN32
    return std::filesystem::path(v);
#else
    return std::filesystem::path(m::to_string(v));
#endif
}

std::filesystem::path
m::filesystem::make_path(std::filesystem::path const& p, std::string_view v)
{
#ifdef WIN32
    return p / std::filesystem::path(m::acp_to_wstring(v));
#else
    return p / std::filesystem::path(v);
#endif
}

std::filesystem::path
m::filesystem::make_path(std::filesystem::path const& p, std::wstring_view v)
{
#ifdef WIN32
    return p / std::filesystem::path(v);
#else
    return p / std::filesystem::path(m::to_string(v));
#endif
}
