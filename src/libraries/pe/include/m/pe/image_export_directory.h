// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "image_data_directory.h"
#include "image_magic_t.h"
#include "offset_t.h"
#include "position_t.h"
#include "rva_stream.h"
#include "rva_t.h"

namespace m
{
    namespace pe
    {
        struct image_export_directory
        {
            // typedef struct _IMAGE_EXPORT_DIRECTORY
            //{
            // [+000]    DWORD Characteristics;
            // [+004]    DWORD TimeDateStamp;
            // [+008]    WORD  MajorVersion;
            // [+010]    WORD  MinorVersion;
            // [+012]    DWORD Name;
            // [+016]    DWORD Base;
            // [+020]    DWORD NumberOfFunctions;
            // [+024]    DWORD NumberOfNames;
            // [+028]    DWORD AddressOfFunctions;    // RVA from base of image
            // [+032]    DWORD AddressOfNames;        // RVA from base of image
            // [+036]    DWORD AddressOfNameOrdinals; // RVA from base of image
            // } IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;

            static inline constexpr offset_t k_offset_characteristics = offset_t{0};
            static inline constexpr offset_t k_offset_time_date_stamp = offset_t{4};
            static inline constexpr offset_t k_offset_major_version   = offset_t{8};
            static inline constexpr offset_t k_offset_minor_version   = offset_t{10};
            static inline constexpr offset_t k_offset_name            = offset_t{12};
            static inline constexpr offset_t k_offset_base            = offset_t{16};
            static inline constexpr offset_t k_offset_number_of_functions = offset_t{20};
            static inline constexpr offset_t k_offset_number_of_names     = offset_t{24};
            static inline constexpr offset_t k_offset_functions           = offset_t{28};
            static inline constexpr offset_t k_offset_names               = offset_t{32};
            static inline constexpr offset_t k_offset_ordinals            = offset_t{36};

            static inline constexpr std::size_t k_size = 40;

            uint32_t m_characteristics;
            uint32_t m_time_date_stamp;
            uint16_t m_major_version;
            uint16_t m_minor_version;
            rva_t    m_name;
            uint32_t m_base;
            uint32_t m_number_of_functions;
            uint32_t m_number_of_names;
            rva_t    m_functions;
            rva_t    m_names;
            rva_t    m_ordinals;

            template <typename SourceT>
            static image_export_directory
            load_from(SourceT s, image_magic_t image_magic, image_data_directory const& idd)
            {
                image_export_directory ied{};

                using ContextT = load_from_rva_context<SourceT>;

                ContextT lfrc(s, idd.m_virtual_address);

                lfrc.load_into(ied.m_characteristics, k_offset_characteristics);
                lfrc.load_into(ied.m_time_date_stamp, k_offset_time_date_stamp);
                lfrc.load_into(ied.m_major_version, k_offset_major_version);
                lfrc.load_into(ied.m_minor_version, k_offset_minor_version);
                lfrc.load_into(ied.m_name, k_offset_name);
                lfrc.load_into(ied.m_base, k_offset_base);
                lfrc.load_into(ied.m_number_of_functions, k_offset_number_of_functions);
                lfrc.load_into(ied.m_number_of_names, k_offset_number_of_names);
                lfrc.load_into(ied.m_functions, k_offset_functions);
                lfrc.load_into(ied.m_names, k_offset_names);
                lfrc.load_into(ied.m_ordinals, k_offset_ordinals);

                return ied;
            }
        };
    } // namespace pe
} // namespace m

template <>
struct std::formatter<m::pe::image_export_directory, wchar_t>
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
    format(m::pe::image_export_directory const& ied, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(),
                              L"{{ "
                              L"m_characteristics: {}, "
                              L"m_time_date_stamp: {}, "
                              L"m_major_version: {}, "
                              L"m_minor_version: {}, "
                              L"m_name: {}, "
                              L"m_base: {}, "
                              L"m_number_of_functions: {}, "
                              L"m_number_of_names: {}, "
                              L"m_functions: {}, "
                              L"m_names: {}, "
                              L"m_ordinals: {}, "
                              L"}}",
                              ied.m_characteristics,
                              ied.m_time_date_stamp,
                              ied.m_major_version,
                              ied.m_minor_version,
                              ied.m_name,
                              ied.m_base,
                              ied.m_number_of_functions,
                              ied.m_number_of_names,
                              ied.m_functions,
                              ied.m_names,
                              ied.m_ordinals);
    }
};
