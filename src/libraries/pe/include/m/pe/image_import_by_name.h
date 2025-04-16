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
        struct image_import_by_name
        {
            // typedef struct _IMAGE_IMPORT_BY_NAME
            //{
            //[+000]    WORD Hint;
            //[+002]    CHAR Name[1];
            // } IMAGE_IMPORT_BY_NAME, *PIMAGE_IMPORT_BY_NAME;

            static inline constexpr offset_t k_offset_hint = offset_t{0};
            static inline constexpr offset_t k_offset_ascii = offset_t{2};

            uint16_t     m_hint;
            std::wstring m_name_string;

            template <typename SourceT>
            static image_import_by_name
            load_from(SourceT s, rva_t rva)
            {
                image_import_by_name iibn{};

                using ContextT = load_from_rva_context<SourceT>;

                ContextT lfrc(s, rva);

                lfrc.load_into(iibn.m_hint, k_offset_hint);

                iibn.m_name_string = s->load_ascii(rva + k_offset_ascii);

                return iibn;
            }
        };

    } // namespace pe
} // namespace m

template <>
struct std::formatter<m::pe::image_import_by_name, wchar_t>
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
    format(m::pe::image_import_by_name const& iibn, FormatContext& ctx) const
    {
        return std::format_to(
            ctx.out(), L"{{ m_hint: {:#x}, m_name_string: \"{}\" }}", iibn.m_hint, iibn.m_name_string);
    }
};
