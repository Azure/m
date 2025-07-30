// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include <m/byte_streams/byte_streams.h>

namespace m
{
    namespace filesystem
    {
        std::vector<std::byte>
        load(std::filesystem::path const& p);

        std::optional<std::vector<std::byte>>
        load(std::filesystem::path const& p, std::error_code& ec);

        void
        store(std::filesystem::path const& p, std::span<std::byte const, std::dynamic_extent> s);

        void
        store(std::filesystem::path const&                    p,
              std::span<std::byte const, std::dynamic_extent> s,
              std::error_code&                                ec);
    } // namespace filesystem
} // namespace m