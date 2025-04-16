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

#include "image_file_header.h"
#include "offset_t.h"

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        struct image_nt_headers64
        {
            // struct _IMAGE_NT_HEADERS64
            // {
            // [+000]    DWORD                   Signature;
            // [+004]    IMAGE_FILE_HEADER       FileHeader;
            // [+024]    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
            // } IMAGE_NT_HEADERS64, *PIMAGE_NT_HEADERS64;

            static inline constexpr offset_t k_offset_signature       = offset_t{0};
            static inline constexpr offset_t k_offset_file_header     = offset_t{4};
            static inline constexpr offset_t k_offset_optional_header = offset_t{24};

            uint32_t          m_signature;
            image_file_header m_file_header;

            template <typename SourceT>
            static image_nt_headers64
            load_from(SourceT s, position_t origin)
            {
                image_nt_headers64 rv{};

                m::load_from_position_context lfpc(s, origin);

                lfpc.load_into(rv.m_signature, k_offset_signature);
                rv.m_file_header = image_file_header::load_from(s, origin, k_offset_file_header);

                return rv;
            }
        };

        struct image_nt_headers32
        {
            // typedef struct _IMAGE_NT_HEADERS
            // {
            // [+000]    ULONG                   Signature;
            // [+004]    IMAGE_FILE_HEADER       FileHeader;
            // [+024]    IMAGE_OPTIONAL_HEADER32 OptionalHeader;
            // } IMAGE_NT_HEADERS32, *PIMAGE_NT_HEADERS32;

            static inline constexpr offset_t k_offset_signature = offset_t{0};
            static inline constexpr offset_t k_offset_file_header = offset_t{4};
            static inline constexpr offset_t k_offset_optional_header = offset_t{24};

            uint32_t          m_signature;
            image_file_header m_file_header;

            template <typename SourceT>
            static image_nt_headers32
            load_from(SourceT s, position_t origin)
            {
                image_nt_headers32 rv{};

                m::load_from_position_context lfpc(s, origin);

                lfpc.load_into(rv.m_signature, k_offset_signature);
                rv.m_file_header = image_file_header::load_from(s, origin, k_offset_file_header);

                return rv;
            }
        };

        //
    } // namespace pe
} // namespace m
