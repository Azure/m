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

#include "file_offset_t.h"
#include "offset_t.h"
#include "rva_t.h"

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        struct image_section_header
        {
            static inline constexpr offset_t k_offset_name                    = offset_t{0};
            static inline constexpr offset_t k_offset_virtual_size            = offset_t{8};
            static inline constexpr offset_t k_offset_virtual_address         = offset_t{12};
            static inline constexpr offset_t k_offset_size_of_raw_data        = offset_t{16};
            static inline constexpr offset_t k_offset_pointer_to_raw_data     = offset_t{20};
            static inline constexpr offset_t k_offset_pointer_to_relocations  = offset_t{24};
            static inline constexpr offset_t k_offset_pointer_to_line_numbers = offset_t{28};
            static inline constexpr offset_t k_offset_number_of_relocations   = offset_t{32};
            static inline constexpr offset_t k_offset_number_of_line_numbers  = offset_t{34};
            static inline constexpr offset_t k_offset_characteristics         = offset_t{36};

            static inline constexpr std::size_t k_size = 40;

            using name_t = char8_t[8];

            struct characteristics
            {
                static constexpr uint32_t k_bitmask_type_no_pad                 = 0x00000008;
                static constexpr uint32_t k_bitmask_contains_code               = 0x00000020;
                static constexpr uint32_t k_bitmask_contains_initialized_data   = 0x00000040;
                static constexpr uint32_t k_bitmask_contains_uninitialized_data = 0x00000080;
                static constexpr uint32_t k_bitmask_link_other                  = 0x00000100;
                static constexpr uint32_t k_bitmask_link_info                   = 0x00000200;
                static constexpr uint32_t k_bitmask_link_remove                 = 0x00000800;
                static constexpr uint32_t k_bitmask_link_comdat                 = 0x00001000;
                static constexpr uint32_t k_bitmask_gp_relative                 = 0x00002000;
                static constexpr uint32_t k_bitmask_mem_purgeable               = 0x00020000;
                static constexpr uint32_t k_bitmask_mem_locked                  = 0x00040000;
                static constexpr uint32_t k_bitmask_mem_preload                 = 0x00080000;
                static constexpr uint32_t k_bitmask_alignment                   = 0x00f00000;
                static constexpr uint32_t k_section_alignment_1_bytes           = 0x00100000;
                static constexpr uint32_t k_section_alignment_2_bytes           = 0x00200000;
                static constexpr uint32_t k_section_alignment_4_bytes           = 0x00300000;
                static constexpr uint32_t k_section_alignment_8_bytes           = 0x00400000;
                static constexpr uint32_t k_section_alignment_16_bytes          = 0x00500000;
                static constexpr uint32_t k_section_alignment_32_bytes          = 0x00600000;
                static constexpr uint32_t k_section_alignment_64_bytes          = 0x00700000;
                static constexpr uint32_t k_section_alignment_128_bytes         = 0x00800000;
                static constexpr uint32_t k_section_alignment_256_bytes         = 0x00900000;
                static constexpr uint32_t k_section_alignment_512_bytes         = 0x00a00000;
                static constexpr uint32_t k_section_alignment_1024_bytes        = 0x00b00000;
                static constexpr uint32_t k_section_alignment_2048_bytes        = 0x00c00000;
                static constexpr uint32_t k_section_alignment_4096_bytes        = 0x00d00000;
                static constexpr uint32_t k_section_alignment_8192_bytes        = 0x00e00000;
                static constexpr uint32_t k_bitmask_link_extended_relocations   = 0x01000000;
                static constexpr uint32_t k_bitmask_mem_discardable             = 0x02000000;
                static constexpr uint32_t k_bitmask_mem_not_cached              = 0x04000000;
                static constexpr uint32_t k_bitmask_mem_not_paged               = 0x08000000;
                static constexpr uint32_t k_bitmask_mem_not_shared              = 0x10000000;
                static constexpr uint32_t k_bitmask_mem_execute                 = 0x20000000;
                static constexpr uint32_t k_bitmask_mem_read                    = 0x40000000;
                static constexpr uint32_t k_bitmask_mem_write                   = 0x80000000;

                // This is a helper function for formatting, the interface is a little
                // weird.
                static std::optional<std::wstring_view>
                map_bit(uint32_t b)
                {
                    switch (b)
                    {
                        default: return std::nullopt;
                        case k_bitmask_type_no_pad: return L"k_bitmask_type_no_pad"sv;
                        case k_bitmask_contains_code: return L"k_bitmask_contains_code"sv;
                        case k_bitmask_contains_initialized_data:
                            return L"k_bitmask_contains_initialized_data"sv;
                        case k_bitmask_contains_uninitialized_data:
                            return L"k_bitmask_contains_uninitialized_data"sv;
                        case k_bitmask_link_other: return L"k_bitmask_link_other"sv;
                        case k_bitmask_link_info: return L"k_bitmask_link_info"sv;
                        case k_bitmask_link_remove: return L"k_bitmask_link_remove"sv;
                        case k_bitmask_link_comdat: return L"k_bitmask_link_comdat"sv;
                        case k_bitmask_gp_relative: return L"k_bitmask_gp_relative"sv;
                        case k_bitmask_mem_purgeable: return L"k_bitmask_mem_purgeable"sv;
                        case k_bitmask_mem_locked: return L"k_bitmask_mem_locked"sv;
                        case k_bitmask_mem_preload: return L"k_bitmask_mem_preload"sv;
                        case k_bitmask_alignment: return L"k_bitmask_alignment"sv;
                        case k_section_alignment_1_bytes: return L"k_section_alignment_1_bytes"sv;
                        case k_section_alignment_2_bytes: return L"k_section_alignment_2_bytes"sv;
                        case k_section_alignment_4_bytes: return L"k_section_alignment_4_bytes"sv;
                        case k_section_alignment_8_bytes: return L"k_section_alignment_8_bytes"sv;
                        case k_section_alignment_16_bytes: return L"k_section_alignment_16_bytes"sv;
                        case k_section_alignment_32_bytes: return L"k_section_alignment_32_bytes"sv;
                        case k_section_alignment_64_bytes: return L"k_section_alignment_64_bytes"sv;
                        case k_section_alignment_128_bytes:
                            return L"k_section_alignment_128_bytes"sv;
                        case k_section_alignment_256_bytes:
                            return L"k_section_alignment_256_bytes"sv;
                        case k_section_alignment_512_bytes:
                            return L"k_section_alignment_512_bytes"sv;
                        case k_section_alignment_1024_bytes:
                            return L"k_section_alignment_1024_bytes"sv;
                        case k_section_alignment_2048_bytes:
                            return L"k_section_alignment_2048_bytes"sv;
                        case k_section_alignment_4096_bytes:
                            return L"k_section_alignment_4096_bytes"sv;
                        case k_section_alignment_8192_bytes:
                            return L"k_section_alignment_8192_bytes"sv;
                        case k_bitmask_link_extended_relocations:
                            return L"k_bitmask_link_extended_relocations"sv;
                        case k_bitmask_mem_discardable: return L"k_bitmask_mem_discardable"sv;
                        case k_bitmask_mem_not_cached: return L"k_bitmask_mem_not_cached"sv;
                        case k_bitmask_mem_not_paged: return L"k_bitmask_mem_not_paged"sv;
                        case k_bitmask_mem_not_shared: return L"k_bitmask_mem_not_shared"sv;
                        case k_bitmask_mem_execute: return L"k_bitmask_mem_execute"sv;
                        case k_bitmask_mem_read: return L"k_bitmask_mem_read"sv;
                        case k_bitmask_mem_write: return L"k_bitmask_mem_write"sv;
                    }
                }

                union union_t
                {
                    uint32_t m_data;
                    struct bitfield_t
                    {
                        uint32_t reserved0 : 1;                   // 0x00000001
                        uint32_t reserved1 : 1;                   // 0x00000002
                        uint32_t reserved2 : 1;                   // 0x00000004
                        uint32_t type_no_pad : 1;                 // 0x00000008
                        uint32_t reserved4 : 1;                   // 0x00000010
                        uint32_t contains_code : 1;               // 0x00000020
                        uint32_t contains_initialized_data : 1;   // 0x00000040
                        uint32_t contains_uninitialized_data : 1; // 0x00000080
                        uint32_t link_other : 1;                  // 0x00000100
                        uint32_t link_info : 1;                   // 0x00000200
                        uint32_t reserved10 : 1;                  // 0x00000400
                        uint32_t link_remove : 1;                 // 0x00000800
                        uint32_t link_comdat : 1;                 // 0x00001000
                        uint32_t gp_relative : 1;                 // 0x00002000
                        uint32_t reserved14 : 1;                  // 0x00004000
                        uint32_t reserved15 : 1;                  // 0x00008000
                        uint32_t reserved16 : 1;                  // 0x00010000
                        uint32_t mem_purgeable : 1;               // 0x00020000
                        uint32_t mem_locked : 1;                  // 0x00040000
                        uint32_t mem_preload : 1;                 // 0x00080000
                        uint32_t alignment : 4;                   // 0x00f00000
                        uint32_t link_extended_relocations : 1;   // 0x01000000
                        uint32_t mem_discardable : 1;             // 0x02000000
                        uint32_t mem_not_cached : 1;              // 0x04000000
                        uint32_t mem_not_paged : 1;               // 0x08000000
                        uint32_t mem_shared : 1;                  // 0x10000000
                        uint32_t mem_execute : 1;                 // 0x20000000
                        uint32_t mem_read : 1;                    // 0x40000000
                        uint32_t mem_write : 1;                   // 0x80000000
                    } m_bits;
                } m_union;
            };

            static_assert(sizeof(characteristics) == sizeof(uint32_t));

            name_t          m_name;
            uint32_t        m_virtual_size;
            rva_t           m_virtual_address;
            uint32_t        m_size_of_raw_data;
            file_offset_t   m_pointer_to_raw_data;
            uint32_t        m_pointer_to_relocations;
            uint32_t        m_pointer_to_line_numbers;
            uint16_t        m_number_of_relocations;
            uint16_t        m_number_of_line_numbers;
            characteristics m_characteristics;

            template <typename SourceT>
            static image_section_header
            load_from(SourceT s, position_t origin, offset_t offset)
            {
                image_section_header ish{};

                m::load_from_position_context lfpc(s, origin);

                lfpc.load_into(ish.m_name, k_offset_name + offset);

                lfpc.load_into(ish.m_virtual_size, k_offset_virtual_size + offset);

                lfpc.load_into(ish.m_virtual_address, k_offset_virtual_address + offset);

                lfpc.load_into(ish.m_size_of_raw_data, k_offset_size_of_raw_data + offset);

                lfpc.load_into(ish.m_pointer_to_raw_data, k_offset_pointer_to_raw_data + offset);

                lfpc.load_into(ish.m_pointer_to_relocations,
                               k_offset_pointer_to_relocations + offset);

                lfpc.load_into(ish.m_pointer_to_line_numbers,
                               k_offset_pointer_to_line_numbers + offset);

                lfpc.load_into(ish.m_number_of_relocations,
                               k_offset_number_of_relocations + offset);

                lfpc.load_into(ish.m_number_of_line_numbers,
                               k_offset_number_of_line_numbers + offset);

                lfpc.load_into(ish.m_characteristics, k_offset_characteristics + offset);

                return ish;
            }
        };
    } // namespace pe
} // namespace m

template <>
struct std::formatter<m::pe::image_section_header, wchar_t>
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
    format(m::pe::image_section_header const& ish, FormatContext& ctx) const
    {
        auto iter = ctx.out();

        wchar_t buff[9];
        buff[0] = ish.m_name[0];
        buff[1] = ish.m_name[1];
        buff[2] = ish.m_name[2];
        buff[3] = ish.m_name[3];
        buff[4] = ish.m_name[4];
        buff[5] = ish.m_name[5];
        buff[6] = ish.m_name[6];
        buff[7] = ish.m_name[7];
        buff[8] = 0;

        iter = std::format_to(iter,
                              L"{{ "
                              L"m_name: {}, "
                              L"m_virtual_size: {:#x}, "
                              L"m_virtual_address: {}, "
                              L"m_size_of_raw_data: 0n{}, "
                              L"m_pointer_to_raw_data: {}, "
                              L"m_pointer_to_relocations: {:#x}, "
                              L"m_pointer_to_line_numbers: {:#x}, "
                              L"m_number_of_relocations: 0n{}, "
                              L"m_number_of_line_numbers: 0n{}, "
                              L"m_characteristics: {} }}",
                              buff,
                              ish.m_virtual_size,
                              ish.m_virtual_address,
                              ish.m_size_of_raw_data,
                              ish.m_pointer_to_raw_data,
                              ish.m_pointer_to_relocations,
                              ish.m_pointer_to_line_numbers,
                              ish.m_number_of_relocations,
                              ish.m_number_of_line_numbers,
                              ish.m_characteristics);

        return iter;
    }
};

template <>
struct std::formatter<m::pe::image_section_header::characteristics, wchar_t>
{
    using characteristics = m::pe::image_section_header::characteristics;

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
    format(characteristics c, FormatContext& ctx) const
    {
        auto out = ctx.out();

        out = std::format_to(out, L"{{ image_section_header::characteristics ");

        auto data = c.m_union.m_data;

        auto alignment = data & characteristics::k_bitmask_alignment;

        if (alignment != 0)
        {
            // Values in the alignment bitmask are an enumeration
            // so we pull them out. the map_bit() function still does
            // the conversion to string.
            auto mappedalignment = characteristics::map_bit(alignment);

            if (mappedalignment)
                out = std::format_to(
                    out, L"alignment: {} ({:#x}) ", mappedalignment.value(), alignment);
            else
                out = std::format_to(out, L"alignment: [unmapped] ({:#x})", alignment);

            data &= ~characteristics::k_bitmask_alignment;
        }

        for (size_t bit = 0; bit < 32; bit++)
        {
            auto shiftedbit = 1u << bit;
            auto maskedbit  = data & shiftedbit;

            if (maskedbit != 0)
            {
                if (auto mappedbit = characteristics::map_bit(maskedbit); mappedbit)
                    out = std::format_to(out, L"{} ({:#x}) ", mappedbit.value(), maskedbit);
                else
                    out = std::format_to(out, L"[unmapped] ({:x}) ", maskedbit);

                data &= ~maskedbit;
            }
        }

        out = std::format_to(out, L"}}");

        return out;
    }
};
