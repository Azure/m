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

#include "image_magic_t.h"
#include "image_optional_headers.h"
#include "offset_t.h"
#include "position_t.h"

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        struct image_optional_header64
        {
            using offset_t = io::offset_t;

            // struct _IMAGE_OPTIONAL_HEADER64
            //{
            // [+000]    WORD                 Magic;
            // [+002]    BYTE                 MajorLinkerVersion;
            // [+003]    BYTE                 MinorLinkerVersion;
            // [+004]    DWORD                SizeOfCode;
            // [+008]    DWORD                SizeOfInitializedData;
            // [+012]    DWORD                SizeOfUninitializedData;
            // [+016]    DWORD                AddressOfEntryPoint;
            // [+020]    DWORD                BaseOfCode;
            // [+024]    ULONGLONG            ImageBase;
            // [+032]    DWORD                SectionAlignment;
            // [+036]    DWORD                FileAlignment;
            // [+040]    WORD                 MajorOperatingSystemVersion;
            // [+042]    WORD                 MinorOperatingSystemVersion;
            // [+044]    WORD                 MajorImageVersion;
            // [+046]    WORD                 MinorImageVersion;
            // [+048]    WORD                 MajorSubsystemVersion;
            // [+050]    WORD                 MinorSubsystemVersion;
            // [+052]    DWORD                Win32VersionValue;
            // [+056]    DWORD                SizeOfImage;
            // [+060]    DWORD                SizeOfHeaders;
            // [+064]    DWORD                CheckSum;
            // [+068]    WORD                 Subsystem;
            // [+070]    WORD                 DllCharacteristics;
            // [+072]    ULONGLONG            SizeOfStackReserve;
            // [+080]    ULONGLONG            SizeOfStackCommit;
            // [+088]    ULONGLONG            SizeOfHeapReserve;
            // [+096]    ULONGLONG            SizeOfHeapCommit;
            // [+104]    DWORD                LoaderFlags;
            // [+108]    DWORD                NumberOfRvaAndSizes;
            // [+000]    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
            // } IMAGE_OPTIONAL_HEADER64, *PIMAGE_OPTIONAL_HEADER64;

            static inline constexpr offset_t k_offset_magic                          = offset_t{0};
            static inline constexpr offset_t k_offset_major_linker_version           = offset_t{2};
            static inline constexpr offset_t k_offset_minor_linker_version           = offset_t{3};
            static inline constexpr offset_t k_offset_size_of_code                   = offset_t{4};
            static inline constexpr offset_t k_offset_size_of_initialized_data       = offset_t{8};
            static inline constexpr offset_t k_offset_size_of_uninitialized_data     = offset_t{12};
            static inline constexpr offset_t k_offset_address_of_entry_point         = offset_t{16};
            static inline constexpr offset_t k_offset_base_of_code                   = offset_t{20};
            static inline constexpr offset_t k_offset_image_base                     = offset_t{24};
            static inline constexpr offset_t k_offset_section_alignment              = offset_t{32};
            static inline constexpr offset_t k_offset_file_alignment                 = offset_t{36};
            static inline constexpr offset_t k_offset_major_operating_system_version = offset_t{40};
            static inline constexpr offset_t k_offset_minor_operating_system_version = offset_t{42};
            static inline constexpr offset_t k_offset_major_image_version            = offset_t{44};
            static inline constexpr offset_t k_offset_minor_image_version            = offset_t{46};
            static inline constexpr offset_t k_offset_major_subsystem_version        = offset_t{48};
            static inline constexpr offset_t k_offset_minor_subsystem_version        = offset_t{50};
            static inline constexpr offset_t k_offset_win32_version_value            = offset_t{52};
            static inline constexpr offset_t k_offset_size_of_image                  = offset_t{56};
            static inline constexpr offset_t k_offset_size_of_headers                = offset_t{60};
            static inline constexpr offset_t k_offset_check_sum                      = offset_t{64};
            static inline constexpr offset_t k_offset_subsystem                      = offset_t{68};
            static inline constexpr offset_t k_offset_dll_characteristics            = offset_t{70};
            static inline constexpr offset_t k_offset_size_of_stack_reserve          = offset_t{72};
            static inline constexpr offset_t k_offset_size_of_stack_commit           = offset_t{80};
            static inline constexpr offset_t k_offset_size_of_heap_reserve           = offset_t{88};
            static inline constexpr offset_t k_offset_size_of_heap_commit            = offset_t{96};
            static inline constexpr offset_t k_offset_loader_flags            = offset_t{104};
            static inline constexpr offset_t k_offset_number_of_rva_and_sizes = offset_t{108};
            static inline constexpr offset_t k_offset_data_directory          = offset_t{112};

            image_magic_t m_magic;
            uint8_t       m_major_linker_version;
            uint8_t       m_minor_linker_version;
            uint32_t      m_size_of_code;
            uint32_t      m_size_of_initialized_data;
            uint32_t      m_size_of_uninitialized_data;
            uint32_t      m_address_of_entry_point;
            uint32_t      m_base_of_code;
            uint64_t      m_image_base;
            uint32_t      m_section_alignment;
            uint32_t      m_file_alignment;
            uint16_t      m_major_operating_system_version;
            uint16_t      m_minor_operating_system_version;
            uint16_t      m_major_image_version;
            uint16_t      m_minor_image_version;
            uint16_t      m_major_subsystem_version;
            uint16_t      m_minor_subsystem_version;
            uint32_t      m_win32_version_value;
            uint32_t      m_size_of_image;
            uint32_t      m_size_of_headers;
            uint32_t      m_check_sum;
            uint16_t      m_subsystem;
            uint16_t      m_dll_characteristics;
            uint64_t      m_size_of_stack_reserve;
            uint64_t      m_size_of_stack_commit;
            uint64_t      m_size_of_heap_reserve;
            uint64_t      m_size_of_heap_commit;
            uint32_t      m_loader_flags;
            uint32_t      m_number_of_rva_and_sizes;

            template <typename SourceT>
            static image_optional_header64
            load_from(SourceT s, typename SourceT::element_type::position_t origin, size_t limit)
            {
                image_optional_header64 ioh{};

                m::load_from_position_context lfpc(s, origin, limit);

                lfpc.load_into(ioh.m_magic, k_offset_magic);
                lfpc.load_into(ioh.m_major_linker_version, k_offset_major_linker_version);
                lfpc.load_into(ioh.m_minor_linker_version, k_offset_minor_linker_version);
                lfpc.load_into(ioh.m_size_of_code, k_offset_size_of_code);
                lfpc.load_into(ioh.m_size_of_initialized_data, k_offset_size_of_initialized_data);
                lfpc.load_into(ioh.m_size_of_uninitialized_data,
                               k_offset_size_of_uninitialized_data);
                lfpc.load_into(ioh.m_address_of_entry_point, k_offset_address_of_entry_point);
                lfpc.load_into(ioh.m_base_of_code, k_offset_base_of_code);
                lfpc.load_into(ioh.m_image_base, k_offset_image_base);
                lfpc.load_into(ioh.m_section_alignment, k_offset_section_alignment);
                lfpc.load_into(ioh.m_file_alignment, k_offset_file_alignment);
                lfpc.load_into(ioh.m_major_operating_system_version,
                               k_offset_major_operating_system_version);
                lfpc.load_into(ioh.m_minor_operating_system_version,
                               k_offset_minor_operating_system_version);
                lfpc.load_into(ioh.m_major_image_version, k_offset_major_image_version);
                lfpc.load_into(ioh.m_minor_image_version, k_offset_minor_image_version);
                lfpc.load_into(ioh.m_major_subsystem_version, k_offset_major_subsystem_version);
                lfpc.load_into(ioh.m_minor_subsystem_version, k_offset_minor_subsystem_version);
                lfpc.load_into(ioh.m_win32_version_value, k_offset_win32_version_value);
                lfpc.load_into(ioh.m_size_of_image, k_offset_size_of_image);
                lfpc.load_into(ioh.m_size_of_headers, k_offset_size_of_headers);
                lfpc.load_into(ioh.m_check_sum, k_offset_check_sum);
                lfpc.load_into(ioh.m_subsystem, k_offset_subsystem);
                lfpc.load_into(ioh.m_dll_characteristics, k_offset_dll_characteristics);
                lfpc.load_into(ioh.m_size_of_stack_reserve, k_offset_size_of_stack_reserve);
                lfpc.load_into(ioh.m_size_of_stack_commit, k_offset_size_of_stack_commit);
                lfpc.load_into(ioh.m_size_of_heap_reserve, k_offset_size_of_heap_reserve);
                lfpc.load_into(ioh.m_size_of_heap_commit, k_offset_size_of_heap_commit);
                lfpc.load_into(ioh.m_loader_flags, k_offset_loader_flags);
                lfpc.load_into(ioh.m_number_of_rva_and_sizes, k_offset_number_of_rva_and_sizes);

                return ioh;
            }
        };

        struct image_optional_header32
        {
            using offset_t = io::offset_t;

            // typedef struct _IMAGE_OPTIONAL_HEADER
            //{
            //     //
            //     // Standard fields.
            //     //
            // [+000]    USHORT Magic;
            // [+002]    UCHAR  MajorLinkerVersion;
            // [+003]    UCHAR  MinorLinkerVersion;
            // [+004]    ULONG  SizeOfCode;
            // [+008]    ULONG  SizeOfInitializedData;
            // [+012]    ULONG  SizeOfUninitializedData;
            // [+016]    ULONG  AddressOfEntryPoint;
            // [+020]    ULONG  BaseOfCode;
            // [+024]    ULONG  BaseOfData;
            //     //
            //     // NT additional fields.
            //     //
            // [+028]    ULONG                ImageBase;
            // [+032]    ULONG                SectionAlignment;
            // [+036]    ULONG                FileAlignment;
            // [+040]    USHORT               MajorOperatingSystemVersion;
            // [+042]    USHORT               MinorOperatingSystemVersion;
            // [+044]    USHORT               MajorImageVersion;
            // [+046]    USHORT               MinorImageVersion;
            // [+048]    USHORT               MajorSubsystemVersion;
            // [+050]    USHORT               MinorSubsystemVersion;
            // [+052]    ULONG                Win32VersionValue;
            // [+056]    ULONG                SizeOfImage;
            // [+060]    ULONG                SizeOfHeaders;
            // [+064]    ULONG                CheckSum;
            // [+068]    USHORT               Subsystem;
            // [+070]    USHORT               DllCharacteristics;
            // [+072]    ULONG                SizeOfStackReserve;
            // [+076]    ULONG                SizeOfStackCommit;
            // [+080]    ULONG                SizeOfHeapReserve;
            // [+084]    ULONG                SizeOfHeapCommit;
            // [+088]    ULONG                LoaderFlags;
            // [+092]    ULONG                NumberOfRvaAndSizes;
            // } IMAGE_OPTIONAL_HEADER32, *PIMAGE_OPTIONAL_HEADER32;

            static inline constexpr offset_t k_offset_magic = offset_t{0};
            static inline constexpr offset_t k_offset_major_linker_version = offset_t{2};
            static inline constexpr offset_t k_offset_minor_linker_version = offset_t{3};
            static inline constexpr offset_t k_offset_size_of_code         = offset_t{4};
            static inline constexpr offset_t k_offset_size_of_initialized_data = offset_t{8};
            static inline constexpr offset_t k_offset_size_of_uninitialized_data = offset_t{12};
            static inline constexpr offset_t k_offset_address_of_entry_point     = offset_t{16};
            static inline constexpr offset_t k_offset_base_of_code               = offset_t{20};
            static inline constexpr offset_t k_offset_base_of_data               = offset_t{24};
            static inline constexpr offset_t k_offset_image_base                 = offset_t{28};
            static inline constexpr offset_t k_offset_section_alignment          = offset_t{32};
            static inline constexpr offset_t k_offset_file_alignment             = offset_t{36};
            static inline constexpr offset_t k_offset_major_operating_system_version = offset_t{40};
            static inline constexpr offset_t k_offset_minor_operating_system_version = offset_t{42};
            static inline constexpr offset_t k_offset_major_image_version            = offset_t{44};
            static inline constexpr offset_t k_offset_minor_image_version            = offset_t{46};
            static inline constexpr offset_t k_offset_major_subsystem_version        = offset_t{48};
            static inline constexpr offset_t k_offset_minor_subsystem_version        = offset_t{50};
            static inline constexpr offset_t k_offset_win32_version_value            = offset_t{52};
            static inline constexpr offset_t k_offset_size_of_image                  = offset_t{56};
            static inline constexpr offset_t k_offset_size_of_headers                = offset_t{60};
            static inline constexpr offset_t k_offset_check_sum                      = offset_t{64};
            static inline constexpr offset_t k_offset_subsystem                      = offset_t{68};
            static inline constexpr offset_t k_offset_dll_characteristics            = offset_t{70};
            static inline constexpr offset_t k_offset_size_of_stack_reserve          = offset_t{72};
            static inline constexpr offset_t k_offset_size_of_stack_commit           = offset_t{76};
            static inline constexpr offset_t k_offset_size_of_heap_reserve           = offset_t{80};
            static inline constexpr offset_t k_offset_size_of_heap_commit            = offset_t{84};
            static inline constexpr offset_t k_offset_loader_flags                   = offset_t{88};
            static inline constexpr offset_t k_offset_number_of_rva_and_sizes        = offset_t{92};
            static inline constexpr offset_t k_offset_data_directory                 = offset_t{96};

            uint16_t m_magic;
            uint8_t  m_major_linker_version;
            uint8_t  m_minor_linker_version;
            uint32_t m_size_of_code;
            uint32_t m_size_of_initialized_data;
            uint32_t m_size_of_uninitialized_data;
            uint32_t m_address_of_entry_point;
            uint32_t m_base_of_code;
            uint32_t m_base_of_data;
            uint32_t m_image_base;
            uint32_t m_section_alignment;
            uint32_t m_file_alignment;
            uint16_t m_major_operating_system_version;
            uint16_t m_minor_operating_system_version;
            uint16_t m_major_image_version;
            uint16_t m_minor_image_version;
            uint16_t m_major_subsystem_version;
            uint16_t m_minor_subsystem_version;
            uint32_t m_win32_version_value;
            uint32_t m_size_of_image;
            uint32_t m_size_of_headers;
            uint32_t m_check_sum;
            uint16_t m_subsystem;
            uint16_t m_dll_characteristics;
            uint32_t m_size_of_stack_reserve;
            uint32_t m_size_of_stack_commit;
            uint32_t m_size_of_heap_reserve;
            uint32_t m_size_of_heap_commit;
            uint32_t m_loader_flags;
            uint32_t m_number_of_rva_and_sizes;

            template <typename SourceT>
            static image_optional_header32
            load_from(SourceT s, typename SourceT::element_type::position_t origin, size_t limit)
            {
                image_optional_header32 ioh{};

                m::load_from_position_context lfpc(s, origin, limit);

                lfpc.load_into(ioh.m_magic, k_offset_magic);
                lfpc.load_into(ioh.m_major_linker_version, k_offset_major_linker_version);
                lfpc.load_into(ioh.m_minor_linker_version, k_offset_minor_linker_version);
                lfpc.load_into(ioh.m_size_of_code, k_offset_size_of_code);
                lfpc.load_into(ioh.m_size_of_initialized_data, k_offset_size_of_initialized_data);
                lfpc.load_into(ioh.m_size_of_uninitialized_data,
                               k_offset_size_of_uninitialized_data);
                lfpc.load_into(ioh.m_address_of_entry_point, k_offset_address_of_entry_point);
                lfpc.load_into(ioh.m_base_of_code, k_offset_base_of_code);
                lfpc.load_into(ioh.m_base_of_data, k_offset_base_of_data);
                lfpc.load_into(ioh.m_image_base, k_offset_image_base);
                lfpc.load_into(ioh.m_section_alignment, k_offset_section_alignment);
                lfpc.load_into(ioh.m_file_alignment, k_offset_file_alignment);
                lfpc.load_into(ioh.m_major_operating_system_version,
                               k_offset_major_operating_system_version);
                lfpc.load_into(ioh.m_minor_operating_system_version,
                               k_offset_minor_operating_system_version);
                lfpc.load_into(ioh.m_major_image_version, k_offset_major_image_version);
                lfpc.load_into(ioh.m_minor_image_version, k_offset_minor_image_version);
                lfpc.load_into(ioh.m_major_subsystem_version, k_offset_major_subsystem_version);
                lfpc.load_into(ioh.m_minor_subsystem_version, k_offset_minor_subsystem_version);
                lfpc.load_into(ioh.m_win32_version_value, k_offset_win32_version_value);
                lfpc.load_into(ioh.m_size_of_image, k_offset_size_of_image);
                lfpc.load_into(ioh.m_size_of_headers, k_offset_size_of_headers);
                lfpc.load_into(ioh.m_check_sum, k_offset_check_sum);
                lfpc.load_into(ioh.m_subsystem, k_offset_subsystem);
                lfpc.load_into(ioh.m_dll_characteristics, k_offset_dll_characteristics);
                lfpc.load_into(ioh.m_size_of_stack_reserve, k_offset_size_of_stack_reserve);
                lfpc.load_into(ioh.m_size_of_stack_commit, k_offset_size_of_stack_commit);
                lfpc.load_into(ioh.m_size_of_heap_reserve, k_offset_size_of_heap_reserve);
                lfpc.load_into(ioh.m_size_of_heap_commit, k_offset_size_of_heap_commit);
                lfpc.load_into(ioh.m_loader_flags, k_offset_loader_flags);
                lfpc.load_into(ioh.m_number_of_rva_and_sizes, k_offset_number_of_rva_and_sizes);

                return ioh;
            }
        };
    } // namespace pe
} // namespace m
