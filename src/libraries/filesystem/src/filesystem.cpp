// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/filesystem/filesystem.h>

#include "seekable_input_file.h"
#include "sequential_output_file.h"

std::shared_ptr<m::filesystem::seekable_input_file>
m::filesystem::open_seekable_input_file(std::filesystem::path const& path)
{
    return std::make_shared<m::filesystem_impl::seekable_input_file>(path);
}

std::shared_ptr<m::filesystem::sequential_output_file>
m::filesystem::make_sequential_output_file(std::filesystem::path const& path)
{
    return std::make_shared<m::filesystem_impl::sequential_output_file>(path);
}