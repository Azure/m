// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

#include <m/io/units.h>

namespace m
{
    namespace byte_streams
    {
        class seq_in
        {
        public:
            /// <summary>
            /// Read bytes from an input byte stream starting at the current position. If the end of
            /// file is reached, the read is completed with fewer bytes than the span can contain,
            /// the span is overwritten with a new span of dynamic extent and with a size that is
            /// the number of bytes read into the buffer.
            ///
            /// Delays execution until the span's maximum size can be read from the input stream,
            /// the end of stream condition is reached, or an error condition occurs. Errors are
            /// indicated by exceptions.
            ///
            /// </summary>
            /// <param name="span">Span of bytes to be read. On successful exit, overwritten with
            /// an updated span with a size corresponding to the number of bytes read. If fewer
            /// bytes were read than were specified in the original span, the end of the stream was
            /// encountered.</param>
            size_t
            read(std::span<std::byte>& span)
            {
                return do_read(span);
            }

        protected:
            virtual size_t
            do_read(std::span<std::byte>& span) = 0;
        };

        class ra_in
        {
        public:
            using position_t = m::io::position_t;
            using offset_t   = m::io::offset_t;

            std::size_t
            read(position_t position, std::span<std::byte>& s)
            {
                do_read(position, s);
                return s.size();
            }

            //
            // If the span is const, the returned size is nodiscard
            //
            [[nodiscard]]
            std::size_t
            read(position_t position, std::span<std::byte> const& s)
            {
                std::span<std::byte> span_copy = s;
                return do_read(position, span_copy);
            }

            template <typename T>
            void
            read(position_t p, T& v)
            {
                if (read(p, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
                    throw std::runtime_error("end of file");
            }

        protected:
            virtual size_t
            do_read(position_t position, std::span<std::byte>& s) = 0;
        };

        class seq_out
        {
        public:
        };
    } // namespace byte_streams
} // namespace m
