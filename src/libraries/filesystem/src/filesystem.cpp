// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/filesystem/filesystem.h>

#include "file.h"

std::shared_ptr<m::filesystem::file>
m::filesystem::open(std::filesystem::path const& path)
{
    return std::make_shared<m::filesystem_impl::file>(path);
}
