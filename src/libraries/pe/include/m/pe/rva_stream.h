// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <span>
#include <vector>

#include <m/cast/to.h>
#include <m/utility/make_span.h>

#include "basic_types.h"
#include "file_offset_t.h"
#include "rva_t.h"

//
// TODO:
//
// ensure that loads from an RVA don't go past section ends.
//
//

namespace m
{
    namespace pe
    {
        template <typename SourceT>
        class rva_ra_in
        {
        public:
            struct section_header_data
            {
                rva_t         m_virtual_address;
                uint32_t      m_size_of_raw_data;
                file_offset_t m_pointer_to_raw_data;
            };

            rva_ra_in(SourceT source, std::span<section_header_data const> const& section_headers):
                m_source(source), m_section_headers(section_headers.begin(), section_headers.end())
            {}

            rva_ra_in(SourceT source, std::span<section_header_data> const& section_headers):
                m_source(source), m_section_headers(section_headers.begin(), section_headers.end())
            {}

            template <typename Callable, typename... Args>
            rva_ra_in(SourceT source, std::size_t n, Callable f, Args... args):
                m_source(source), m_section_headers(n)
            {
                for (std::size_t i = 0; i < n; i++)
                {
                    m_section_headers[i] =
                        std::invoke(std::forward<Callable>(f), i, std::forward<Args>(args)...);
                }
            }

            position_t
            rva_to_position(rva_t rva)
            {
                //
                // Reference source code from somewhere
                //
                // DWORD
                // CImage::RvaToFileOffset(DWORD nRva)
                //{
                //     DWORD n;
                //     for (n = 0; n < m_NtHeader.FileHeader.NumberOfSections; n++)
                //     {
                //         DWORD vaStart = m_SectionHeaders[n].VirtualAddress;
                //         DWORD vaEnd   = vaStart + m_SectionHeaders[n].SizeOfRawData;
                //
                //         if (nRva >= vaStart && nRva < vaEnd)
                //         {
                //             return m_SectionHeaders[n].PointerToRawData + nRva -
                //             m_SectionHeaders[n].VirtualAddress;
                //         }
                //     }
                //     return 0;
                // }

                for (auto&& e: m_section_headers)
                {
                    rva_t start{e.m_virtual_address};
                    rva_t end{start + e.m_size_of_raw_data};

                    if ((rva >= start) && (rva < end))
                    {
                        auto offset = e.m_pointer_to_raw_data + (rva - e.m_virtual_address);
                        return position_t{m::to<std::underlying_type_t<position_t>>(
                            std::to_underlying(offset))};
                    }
                }

                return position_t{};
            }

            std::size_t
            read(rva_t rva, std::span<std::byte> span)
            {
                auto const position = rva_to_position(rva);

                if (position == 0)
                    throw std::runtime_error("bad pe - unmapped rva");

                return m_source->read(position, span);
            }

            //
            // Also expose non-RVA based reads so callers can have a single
            // source to worry about if they like.
            //
            // Maybe that will be confusing? We can always delete.
            //
            std::size_t
            read(position_t position, std::span<std::byte> span)
            {
                return m_source->read(position, span);
            }

            std::wstring
            load_ascii(rva_t rva)
            {
                // The PE has these null-terminated ASCII strings... kind of
                // weird but there you go. ASCII is only defined as 0-127
                // so strictly speaking DLLs shouldn't be able to have
                // filenames with characters beyond the ASCII set, which is
                // clearly not true.
                //
                // In practice, this is a kind of garbage take on the whole
                // thing and will require rework at some point.
                //
                // There are several popular points of view:
                //
                // 1. Do what we're going to do here - copy the 8-bit data
                //      to 16-bit data and look the other way. This will
                //      work for ASCII and ISO Latin-1.
                //
                // 2. Fail if there are characters encoded above 127. This
                //      seems like it will displease everyone.
                //
                // 3. Attempt to use the system code page on Windows to
                //      decode. "But wait,", you say, "this code isn't Windows
                //      specific!". Right, that's the problem. We don't as
                //      of this writing have any code to deal with MBCS
                //      character encodings beyond what C++ / STL may grant
                //      us. And that doesn't actually give us any notion
                //      of what a "system code page" is, or the "ANSI code
                //      page"
                //
                // 4. Be progressive and try to assume Utf-8. This is nice
                //      and progressive, the Microsoft documents even claim
                //      that the section names are Utf-8 encoded, but when
                //      we find an encoding error, we're back to #2 - what do
                //      we actually do when we find an encoding error? Tools
                //      generally are expected to have means to plow through
                //      data errors like these.
                //
                // 5. Proactively escape characters that are not "reasonable".
                //      this would include any non-printable characters like
                //      ESC and DEL but also, for the time being at least,
                //      any characters >127. This is more coding and can be
                //      left for a future work item.
                //
                // So, at this time, we are simply going to use strategy #1.
                // Prepare for garbage in, garbage out.
                //
                std::wstring result;

                for (;;)
                {
                    std::array<std::byte, 64> buffer;
                    auto                      bufferspan = m::make_span(buffer);
                    auto                      length     = read(rva, bufferspan);
                    if (length == 0)
                        throw std::runtime_error("end of file reached while loading string");

                    auto it = std::ranges::find(bufferspan, std::byte{});

                    if (it != bufferspan.end())
                        length = it - bufferspan.begin();

                    result.reserve(result.size() + length);

                    std::ranges::for_each(bufferspan.subspan(0, length), [&](std::byte b) {
                        result.push_back(static_cast<wchar_t>(b));
                    });

                    if (length != buffer.size())
                        break;

                    rva = rva + buffer.size();
                }

                return result;
            }

        private:
            SourceT                          m_source;
            std::vector<section_header_data> m_section_headers;
        };

        template <typename T, typename SourceT>
        void
        load_into(T& v, SourceT s, rva_t origin)
        {
            if (s->read(origin, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
                throw std::runtime_error("end of file");
        }

        template <typename SourceT>
        class load_from_rva_context
        {
            using source_t = SourceT;

        public:
            load_from_rva_context(source_t s, rva_t origin): m_s(s), m_origin(origin) {}

            template <typename T>
            void
            load_into(T& v, offset_t offset) const
            {

                m::pe::load_into(v, m_s, m_origin + offset);
            }

        private:
            source_t m_s;
            rva_t    m_origin;
        };

        template <typename S>
        load_from_rva_context(S, rva_t) -> load_from_rva_context<S>;

    } // namespace pe
} // namespace m
