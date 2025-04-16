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

#include <gsl/gsl>

#include <m/byte_streams/byte_streams.h>
#include <m/math/math.h>
#include <m/memory/memory.h>

#include "offset_t.h"

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        // struct _IMAGE_FILE_HEADER
        //{
        //  [+000]   USHORT Machine;
        //  [+002]   USHORT NumberOfSections;
        //  [+004]   ULONG  TimeDateStamp;
        //  [+008]   ULONG  PointerToSymbolTable;
        //  [+012]   ULONG  NumberOfSymbols;
        //  [+016]   USHORT SizeOfOptionalHeader;
        //  [+018]   USHORT Characteristics;
        // };

        struct image_file_header
        {
            static inline constexpr offset_t k_offset_machine = offset_t{0};
            static inline constexpr offset_t k_offset_number_of_sections = offset_t{2};
            static inline constexpr offset_t k_offset_time_date_stamp    = offset_t{4};
            static inline constexpr offset_t k_offset_pointer_to_symbol_table = offset_t{8};
            static inline constexpr offset_t k_offset_number_of_symbols       = offset_t{12};
            static inline constexpr offset_t k_offset_size_of_optional_header = offset_t{16};
            static inline constexpr offset_t k_offset_characteristics         = offset_t{18};

            static inline constexpr std::size_t k_image_file_header_size = 20;

            enum class machine_t : uint16_t
            {
                unknown     = 0,
                alpha       = 0x184,
                alpha64     = 0x284,
                am33        = 0x1d3,
                amd64       = 0x8664,
                arm         = 0x1c0,
                arm64       = 0xaa64,
                armnt       = 0x1c4,
                ebc         = 0xebc,
                i386        = 0x14c,
                ia64        = 0x200,
                loongarch32 = 0x6232,
                loongarch64 = 0x6264,
                m32r        = 0x9041,
                mips16      = 0x266,
                mipsfpu     = 0x366,
                mipsfpu16   = 0x466,
                powerpc     = 0x1f0,
                powerpcfp   = 0x1f1,
                r4000       = 0x166,
                riscv32     = 0x5032,
                riscv64     = 0x5064,
                riscv128    = 0x5128,
                sh3         = 0x1a2,
                sh3dsp      = 0x1a3,
                sh4         = 0x1a6,
                sh5         = 0x1a8,
                thumb       = 0x1c2,
                wcemipsv2   = 0x169,
            };

            struct characteristics
            {
                union
                {
                    uint16_t m_data;
                    struct bitfields
                    {
                        uint16_t relocs_stripped : 1;
                        uint16_t executable_image : 1;
                        uint16_t line_nums_stripped : 1;
                        uint16_t local_syms_stripped : 1;
                        uint16_t aggressive_ws_trim : 1;
                        uint16_t large_address_aware : 1;
                        uint16_t bytes_reversed_lo : 1;
                        uint16_t x32bit_machine : 1;
                        uint16_t debug_stripped : 1;
                        uint16_t removable_run_from_swap : 1;
                        uint16_t net_from_from_swap : 1;
                        uint16_t system : 1;
                        uint16_t dll : 1;
                        uint16_t up_system_only : 1;
                        uint16_t bytes_reversed_hi : 1;
                    };
                } m_union;
            };

            static_assert(sizeof(characteristics) == 2);

            machine_t       m_machine;
            uint16_t        m_number_of_sections;
            uint32_t        m_time_date_stamp;
            uint32_t        m_pointer_to_symbol_table;
            uint32_t        m_number_of_symbols;
            uint16_t        m_size_of_optional_header;
            characteristics m_characteristics;

            template <typename SourceT>
            static image_file_header
            load_from(SourceT s, typename SourceT::element_type::position_t origin)
            {
                image_file_header ifh{};

                m::load_from_position_context lfpc(s, origin);

                lfpc.load_into(ifh.m_machine, k_offset_machine);
                lfpc.load_into(ifh.m_number_of_sections,
                               image_file_header::k_offset_number_of_sections);
                lfpc.load_into(ifh.m_time_date_stamp, image_file_header::k_offset_time_date_stamp);
                lfpc.load_into(ifh.m_pointer_to_symbol_table,
                               image_file_header::k_offset_pointer_to_symbol_table);
                lfpc.load_into(ifh.m_number_of_symbols,
                               image_file_header::k_offset_number_of_symbols);
                lfpc.load_into(ifh.m_size_of_optional_header,
                               image_file_header::k_offset_size_of_optional_header);
                lfpc.load_into(ifh.m_characteristics, image_file_header::k_offset_characteristics);

                return ifh;
            }
        };
    } // namespace pe
} // namespace m

template <>
struct std::formatter<m::pe::image_file_header, wchar_t>
{
    formatter()                 = default;
    formatter(formatter const&) = default;
    formatter(formatter&&)      = default;
    ~formatter()                = default;

    formatter&
    operator=(formatter const& other)
    {
        // no state??
        return *this;
    }

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
    format(m::pe::image_file_header const& ifh, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(),
                              L"{{ "
                              L"m_machine: {}, "
                              L"m_number_of_sections: 0n{}, "
                              L"m_time_date_stamp: {}, "
                              L"m_pointer_to_symbol_table: {:#x}, "
                              L"m_number_of_symbols: 0n{}, "
                              L"m_size_of_optional_header: 0n{}, "
                              L"m_characteristics: {} "
                              L"}}",
                              ifh.m_machine,
                              ifh.m_number_of_sections,
                              ifh.m_time_date_stamp,
                              ifh.m_pointer_to_symbol_table,
                              ifh.m_number_of_symbols,
                              ifh.m_size_of_optional_header,
                              ifh.m_characteristics);
    }
};

template <>
struct std::formatter<m::pe::image_file_header::machine_t, wchar_t>
{
    using machine_t = m::pe::image_file_header::machine_t;

    formatter()                 = default;
    formatter(formatter const&) = default;
    formatter(formatter&&)      = default;
    ~formatter()                = default;

    formatter&
    operator=(formatter const& other)
    {
        // no state??
        return *this;
    }

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
    format(machine_t machine, FormatContext& ctx) const
    {
        auto const x = static_cast<uint16_t>(machine); // cast to underlying type

        wstring_view name = L"<no mapping>"sv;

        // TODO: Round out this mapping, maybe move to utility function?
        switch (machine)
        {
            default: break;
            case machine_t::unknown: name = L"unknown"sv; break;
            case machine_t::alpha: name = L"alpha"sv; break;
            case machine_t::alpha64: name = L"alpha64"sv; break;
            case machine_t::amd64: name = L"amd64"sv; break;
            case machine_t::arm64: name = L"arm64"sv; break;
            case machine_t::i386: name = L"i386"sv; break;
            case machine_t::riscv64: name = L"riscv64"sv; break;
        }

        return std::format_to(ctx.out(), L"{} ({:#x})", name, x);
    }
};

template <>
struct std::formatter<m::pe::image_file_header::characteristics, wchar_t>
{
    using characteristics = m::pe::image_file_header::characteristics;

    formatter()                 = default;
    formatter(formatter const&) = default;
    formatter(formatter&&)      = default;
    ~formatter()                = default;

    formatter&
    operator=(formatter const& other)
    {
        // no state??
        return *this;
    }

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
        //
        // Chicken way out
        //

        return std::format_to(ctx.out(), L"{:#x}", c.m_union.m_data);
    }
};

