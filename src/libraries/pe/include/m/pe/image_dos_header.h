// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "offset_t.h"

namespace m
{
    namespace pe
    {
        // struct _IMAGE_DOS_HEADER
        //{                      // DOS .EXE header
        //  [+000]   USHORT e_magic;    // Magic number
        //  [+002]   USHORT e_cblp;     // Bytes on last page of file
        //  [+004]   USHORT e_cp;       // Pages in file
        //  [+006]   USHORT e_crlc;     // Relocations
        //  [+008]   USHORT e_cparhdr;  // Size of header in paragraphs
        //  [+010]   USHORT e_minalloc; // Minimum extra paragraphs needed
        //  [+012]   USHORT e_maxalloc; // Maximum extra paragraphs needed
        //  [+014]   USHORT e_ss;       // Initial (relative) SS value
        //  [+016]   USHORT e_sp;       // Initial SP value
        //  [+018]   USHORT e_csum;     // Checksum
        //  [+020]   USHORT e_ip;       // Initial IP value
        //  [+024]   USHORT e_cs;       // Initial (relative) CS value
        //  [+026]   USHORT e_lfarlc;   // File address of relocation table
        //  [+028]   USHORT e_ovno;     // Overlay number
        //  [+030]   USHORT e_res[4];   // Reserved words
        //  [+038]   USHORT e_oemid;    // OEM identifier (for e_oeminfo)
        //  [+040]   USHORT e_oeminfo;  // OEM information; e_oemid specific
        //  [+042]   USHORT e_res2[10]; // Reserved words
        //  [+060]   LONG   e_lfanew;   // File address of new exe header
        // };

        struct image_dos_header
        {
            static inline constexpr uint16_t k_magic_mark_zibokowski =
                static_cast<uint16_t>('M') | static_cast<uint16_t>('Z') << 8;
            static inline constexpr uint16_t k_magic_dos_header =
                static_cast<uint16_t>('M') | static_cast<uint16_t>('Z') << 8;

            static inline constexpr offset_t k_offset_magic  = offset_t{0};
            static inline constexpr offset_t k_offset_lfanew = offset_t{60};
        };
    } // namespace pe
} // namespace m
