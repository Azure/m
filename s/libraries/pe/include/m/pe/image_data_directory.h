// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include <m/byte_streams/byte_streams.h>
#include <m/math/math.h>
#include <m/memory/memory.h>

#include "offset_t.h"
#include "position_t.h"
#include "rva_t.h"

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        struct image_data_directory
        {
            // typedef struct _IMAGE_DATA_DIRECTORY
            // {
            //  [+000]   DWORD VirtualAddress;
            //  [+004]   DWORD Size;
            // } IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

            static inline constexpr offset_t k_offset_virtual_address = offset_t{0};
            static inline constexpr offset_t k_offset_size            = offset_t{4};

            static inline constexpr std::size_t k_size = 8;

            rva_t    m_virtual_address;
            uint32_t m_size;

            template <typename SourceT>
            static image_data_directory
            load_from(SourceT s, position_t origin, offset_t offset, std::size_t limit)
            {
                image_data_directory idd{};

                m::load_from_position_context lfpc(s, origin, limit);

                lfpc.load_into(idd.m_virtual_address, k_offset_virtual_address + offset);
                lfpc.load_into(idd.m_size, k_offset_size + offset);

                return idd;
            }
        };

        //
        // Some (one) data directory uses a file offset in the virtual address
        // field instead of a RVA.
        //
        struct image_data_directory_file_offset
        {
            // typedef struct _IMAGE_DATA_DIRECTORY
            // {
            //  [+000]   DWORD VirtualAddress;
            //  [+004]   DWORD Size;
            // } IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

            static inline constexpr offset_t k_offset_virtual_address = offset_t{0};
            static inline constexpr offset_t k_offset_size            = offset_t{4};

            static inline constexpr std::size_t k_size = 8;

            offset_t m_virtual_address;
            uint32_t m_size;

            template <typename SourceT>
            static image_data_directory_file_offset
            load_from(SourceT s, position_t origin, offset_t offset, std::size_t limit)
            {
                image_data_directory_file_offset idd{};

                m::load_from_position_context lfpc(s, origin, limit);

                lfpc.load_into(idd.m_virtual_address, k_offset_virtual_address + offset);
                lfpc.load_into(idd.m_size, k_offset_size + offset);

                return idd;
            }
        };

        static_assert(image_data_directory::k_size == image_data_directory_file_offset::k_size);

        //
        // These offsets are from m_data_directory_position
        //

        static inline constexpr offset_t k_offset_export_table            = offset_t{0};
        static inline constexpr offset_t k_offset_import_table = offset_t{8};
        static inline constexpr offset_t k_offset_resource_table          = offset_t{16};
        static inline constexpr offset_t k_offset_exception_table         = offset_t{24};
        static inline constexpr offset_t k_offset_certificate_table       = offset_t{32};
        static inline constexpr offset_t k_offset_base_relocation_table   = offset_t{40};
        static inline constexpr offset_t k_offset_debug_table             = offset_t{48};
        static inline constexpr offset_t k_offset_architecture_mbz        = offset_t{56};
        static inline constexpr offset_t k_offset_global_ptr              = offset_t{64};
        static inline constexpr offset_t k_offset_tls_table               = offset_t{72};
        static inline constexpr offset_t k_offset_load_config_table       = offset_t{80};
        static inline constexpr offset_t k_offset_bound_import            = offset_t{88};
        static inline constexpr offset_t k_offset_iat                     = offset_t{96};
        static inline constexpr offset_t k_offset_delay_import_descriptor = offset_t{104};
        static inline constexpr offset_t k_offset_clr_runtime_header      = offset_t{112};
        static inline constexpr offset_t k_offset_reserved_mbz            = offset_t{120};

        static inline constexpr std::size_t k_directory_entry_export           = 0;
        static inline constexpr std::size_t k_directory_entry_import           = 1;
        static inline constexpr std::size_t k_directory_entry_resource         = 2;
        static inline constexpr std::size_t k_directory_entry_exception        = 3;
        static inline constexpr std::size_t k_directory_entry_certificate      = 4;
        static inline constexpr std::size_t k_directory_entry_base_relocation  = 5;
        static inline constexpr std::size_t k_directory_entry_debug            = 6;
        static inline constexpr std::size_t k_directory_entry_architecture_mbz = 7;
        static inline constexpr std::size_t k_directory_entry_global_ptr       = 8;
        static inline constexpr std::size_t k_directory_entry_tls              = 9;
        static inline constexpr std::size_t k_directory_entry_load_config      = 10;
        static inline constexpr std::size_t k_directory_entry_bound_import     = 11;
        static inline constexpr std::size_t k_directory_entry_iat              = 12;
        static inline constexpr std::size_t k_directory_entry_delay_import     = 13;
        static inline constexpr std::size_t k_directory_entry_clr_runtime      = 14;
        static inline constexpr std::size_t k_directory_entry_reserved_mbz     = 15;
    } // namespace pe
} // namespace m

template <>
struct std::formatter<m::pe::image_data_directory, wchar_t>
{
    template <typename ParseContext>
    constexpr decltype(auto)
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(m::pe::image_data_directory const& idd, FormatContext& ctx) const
    {
        if (idd.m_size == 0 && !idd.m_virtual_address)
            return std::format_to(ctx.out(), L"{{ -null- }}");

        return std::format_to(ctx.out(),
                              L"{{ m_virtual_address: {}, m_size: 0n{} }}",
                              idd.m_virtual_address,
                              idd.m_size);
    }
};

template <>
struct std::formatter<m::pe::image_data_directory_file_offset, wchar_t>
{
    template <typename ParseContext>
    constexpr decltype(auto)
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(m::pe::image_data_directory_file_offset const& idd, FormatContext& ctx) const
    {
        if (idd.m_size == 0 && !idd.m_virtual_address)
            return std::format_to(ctx.out(), L"{{ -null- }}");

        return std::format_to(ctx.out(),
                              L"{{ m_virtual_address: {}, m_size: 0n{} }}",
                              idd.m_virtual_address,
                              idd.m_size);
    }
};
