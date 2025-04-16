// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <m/cast/to.h>

#include "image_data_directory.h"
#include "image_import_by_name.h"
#include "image_magic_t.h"
#include "offset_t.h"
#include "position_t.h"
#include "rva_stream.h"
#include "rva_t.h"

namespace m
{
    namespace pe
    {
        struct image_import_descriptor
        {
            // typedef struct _IMAGE_IMPORT_DESCRIPTOR
            //{
            //     union
            //     {
            //  [+000]       DWORD Characteristics;
            //  [+000]       DWORD OriginalFirstThunk;
            //     } DUMMYUNIONNAME;
            //  [+004]   DWORD TimeDateStamp;
            //  [+008]   DWORD ForwarderChain;
            //  [+012]   DWORD Name;
            //  [+016]   DWORD FirstThunk;
            // } IMAGE_IMPORT_DESCRIPTOR;

            static inline constexpr offset_t k_offset_import_name_table    = offset_t{0};
            static inline constexpr offset_t k_offset_time_date_stamp   = offset_t{4};
            static inline constexpr offset_t k_offset_forwarder_chain      = offset_t{8};
            static inline constexpr offset_t k_offset_name                 = offset_t{12};
            static inline constexpr offset_t k_offset_import_address_table = offset_t{16};

            static inline constexpr std::size_t k_size = 20;

            bool
            is_zero() const
            {
                return (m_import_name_table == 0) && (m_time_date_stamp == 0) &&
                       (m_forwarder_chain == 0) && (m_name == 0) && (m_import_address_table == 0);
            }

            rva_t    m_import_name_table;
            uint32_t m_time_date_stamp;
            uint32_t m_forwarder_chain;
            rva_t    m_name;
            rva_t    m_import_address_table;

            std::wstring m_name_string;

            static inline constexpr uint32_t k_mask_pe32_image_name_table_ordinal = 0x80000000ul;
            static inline constexpr size_t   k_size_pe32_image_name_table_entry   = 4;

            static inline constexpr uint64_t k_mask_pe32plus_image_name_table_ordinal =
                0x80000000'00000000ull;
            static inline constexpr size_t k_size_pe32plus_image_name_table_entry = 8;

            static inline constexpr uint32_t k_max_image_name_table_ordinal = 0x7fff'fffful;

            using import_name_table_entry = std::variant<image_import_by_name, uint32_t>;

            static inline constexpr std::size_t k_import_name_table_entry_type_index_name    = 0;
            static inline constexpr std::size_t k_import_name_table_entry_type_index_ordinal = 1;

            std::vector<import_name_table_entry> m_import_name_table_entries;

            template <typename SourceT>
            static image_import_descriptor
            load_from(SourceT                     s,
                      image_magic_t               image_magic,
                      image_data_directory const& idd,
                      size_t                      index)
            {
                image_import_descriptor iid{};

                using ContextT = load_from_rva_context<SourceT>;

                ContextT lfrc(s, idd.m_virtual_address);

                offset_t base_offset = offset_t{} + (image_import_descriptor::k_size * index);
                lfrc.load_into(iid.m_import_name_table, base_offset + k_offset_import_name_table);
                lfrc.load_into(iid.m_time_date_stamp, base_offset + k_offset_time_date_stamp);
                lfrc.load_into(iid.m_forwarder_chain, base_offset + k_offset_forwarder_chain);
                lfrc.load_into(iid.m_name, base_offset + k_offset_name);
                lfrc.load_into(iid.m_import_address_table,
                               base_offset + k_offset_import_address_table);

                if (iid.m_name != 0)
                    iid.m_name_string = s->load_ascii(iid.m_name);

                if (iid.m_import_name_table != 0)
                {
                    switch (image_magic)
                    {
                        default:
                            throw std::runtime_error(
                                "Image format is not pe32 or pe32plus and then does not have imports");

                        case image_magic_t::pe32:
                        {
                            size_t i{};

                            for (;;)
                            {
                                uint32_t int_entry{};

                                m::pe::load_into(int_entry,
                                                 s,
                                                 iid.m_import_name_table +
                                                     (i * k_size_pe32_image_name_table_entry));

                                if (int_entry == 0)
                                    break;

                                if ((int_entry & k_mask_pe32_image_name_table_ordinal) != 0)
                                {
                                    iid.m_import_name_table_entries.emplace_back(
                                        import_name_table_entry(
                                            int_entry & ~k_mask_pe32_image_name_table_ordinal));
                                }
                                else
                                    iid.m_import_name_table_entries.emplace_back(
                                        import_name_table_entry(
                                            image_import_by_name::load_from(s, rva_t(int_entry))));

                                i++;
                            }
                        }

                        case image_magic_t::pe32plus:
                        {
                            size_t i{};

                            for (;;)
                            {
                                uint64_t int_entry{};

                                m::pe::load_into(int_entry,
                                                 s,
                                                 iid.m_import_name_table +
                                                     (i * k_size_pe32plus_image_name_table_entry));

                                if (int_entry == 0)
                                    break;

                                if ((int_entry & k_mask_pe32plus_image_name_table_ordinal) != 0)
                                {
                                    int_entry &= ~k_mask_pe32plus_image_name_table_ordinal;

                                    if (int_entry > k_max_image_name_table_ordinal)
                                        throw std::runtime_error(
                                            "invalid pe32plus import name table ordinal entry");

                                    iid.m_import_name_table_entries.emplace_back(
                                        import_name_table_entry(m::to<uint32_t>(int_entry)));
                                }
                                else
                                    iid.m_import_name_table_entries.emplace_back(import_name_table_entry(
                                        image_import_by_name::load_from(s, rva_t(m::to<uint32_t>(int_entry)))));

                                i++;
                            }
                        }
                    }
                }

                return iid;
            }

#if 0
            template <typename SourceT>
            static image_import_descriptor
            load_from(SourceT                                 s,
                      image_magic_t                           image_magic,
                      image_data_directory_file_offset const& idd,
                      size_t                                  index)
            {
                image_import_descriptor iid{};

                m::load_from_position_context lfpc(
                    s, position_t{} + idd.m_virtual_address, idd.m_size);

                offset_t base_offset = offset_t{} + (image_import_descriptor::k_size * index);
                lfpc.load_into(iid.m_import_name_table, base_offset + k_offset_import_name_table);
                lfpc.load_into(iid.m_time_date_stamp, base_offset + k_offset_time_date_stamp);
                lfpc.load_into(iid.m_forwarder_chain, base_offset + k_offset_forwarder_chain);
                lfpc.load_into(iid.m_name, base_offset + k_offset_name);
                lfpc.load_into(iid.m_import_address_table,
                               base_offset + k_offset_import_address_table);

                if (iid.m_name != 0)
                    iid.m_name_string = s->load_ascii(iid.m_name);

                switch (image_magic)
                {
                    //
                }
                return iid;
            }
#endif
        };
    } // namespace pe
} // namespace m

template <>
struct std::formatter<m::pe::image_import_descriptor, wchar_t>
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
    format(m::pe::image_import_descriptor const& iid, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(),
                              L"{{ "
                              L"m_import_name_table: {}, "
                              L"m_time_date_stamp: {:#x}, "
                              L"m_forwarder_chain: {:#x}, "
                              L"m_name: {}, "
                              L"m_import_address_table: {}, "
                              L"m_name_string: \"{}\" "
                              L"}}",
                              iid.m_import_name_table,
                              iid.m_time_date_stamp,
                              iid.m_forwarder_chain,
                              iid.m_name,
                              iid.m_import_address_table,
                              iid.m_name_string);
    }
};
