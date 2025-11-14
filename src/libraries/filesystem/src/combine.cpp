// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/filesystem/filesystem.h>

namespace m::filesystem
{
    std::filesystem::path
    combine(std::optional<std::filesystem::path>&& base_path,
            std::filesystem::path const&           overlay_path)
    {
        std::filesystem::path return_value;

        if (base_path.has_value())
            return_value = base_path.value() / overlay_path;
        else
            return_value = overlay_path;

        return std::filesystem::absolute(return_value);
    }

    std::filesystem::path
    combine(std::optional<std::filesystem::path> const& base_path,
            std::filesystem::path const&                overlay_path)
    {
        std::filesystem::path return_value;

        if (base_path.has_value())
            return_value = base_path.value() / overlay_path;
        else
            return_value = overlay_path;

        return std::filesystem::absolute(return_value);
    }

    std::filesystem::path
    combine(std::optional<std::filesystem::path>&& base_path,
            std::filesystem::path const&           overlay_path,
            std::filesystem::path const&           replacement_extension)
    {
        std::filesystem::path return_value;

        if (base_path.has_value())
            return_value = base_path.value() / overlay_path;
        else
            return_value = overlay_path;

        return std::filesystem::absolute(return_value).replace_extension(replacement_extension);
    }

    std::filesystem::path
    combine(std::optional<std::filesystem::path> const& base_path,
            std::filesystem::path const&                overlay_path,
            std::filesystem::path const&                replacement_extension)
    {
        std::filesystem::path return_value;

        if (base_path.has_value())
            return_value = base_path.value() / overlay_path;
        else
            return_value = overlay_path;

        return std::filesystem::absolute(return_value).replace_extension(replacement_extension);
    }

} // namespace m::filesystem