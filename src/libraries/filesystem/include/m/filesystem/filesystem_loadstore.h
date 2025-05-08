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
        store_dynamic(std::filesystem::path const&                    p,
                      std::span<std::byte const, std::dynamic_extent> s);

        template <std::size_t Extent>
        void
        store(std::filesystem::path const& p, std::span<std::byte const, Extent> s)
        {
            if constexpr (Extent == std::dynamic_extent)
                store_dynamic(p, s);
            else
                store_dynamic(p,
                              std::span<std::byte const, std::dynamic_extent>(s.data(), s.size()));
        }


        void
        store_dynamic(std::filesystem::path const&                    p,
                      std::span<std::byte const, std::dynamic_extent> s,
                      std::error_code&                                ec);

        template <std::size_t Extent>
        void
        store(std::filesystem::path const&       p,
              std::span<std::byte const, Extent> s,
              std::error_code&                   ec)
        {
            if constexpr (Extent == std::dynamic_extent)
                store_dynamic(p, s, ec);
            else
                store_dynamic(
                    p, std::span<std::byte const, std::dynamic_extent>(s.data(), s.size()), ec);
        }
    } // namespace filesystem
} // namespace m