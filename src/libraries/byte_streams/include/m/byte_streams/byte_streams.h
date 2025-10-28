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
        class seq_in;
        class ra_in;

        /// <summary>
        /// The input_stream concept template can be used to determine if a given type is an input
        /// byte stream.
        /// </summary>
        template <typename T>
        concept input_stream = (std::derived_from<T, seq_in> || std::derived_from<T, ra_in>);

        template <typename T>
        concept input_stream_pointer =
            requires(T x) { requires input_stream<std::remove_reference_t<decltype(*x)>>; };

        /// <summary>
        /// Provides methods to read bytes or typed values from an input byte stream, handling
        /// end-of-stream and error conditions.
        /// </summary>
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

            //
            // If the span is const, the returned size is nodiscard
            //

            /// <summary>
            /// Reads data into the provided byte span and returns the number of bytes read.
            /// </summary>
            /// <param name="s">A constant reference to a span of bytes where the data will be read
            /// into.</param> <returns>The number of bytes successfully read.</returns>
            [[nodiscard]]
            std::size_t
            read(std::span<std::byte> const& s)
            {
                std::span<std::byte> span_copy = s;
                return do_read(span_copy);
            }

            /// <summary>
            /// Reads a value of type T from an input source into the provided variable. Throws an
            /// exception if the read operation fails to read the expected number of bytes.
            /// </summary>
            /// <typeparam name="T">The type of the value to be read.</typeparam>
            /// <param name="v">A reference to the variable where the read value will be
            /// stored.</param>
            template <typename T>
            void
            read(T& v)
            {
                if (read(std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
                    throw std::runtime_error("end of file");
            }

        protected:
            virtual ~seq_in() = default;

            virtual size_t
            do_read(std::span<std::byte>& span) = 0;
        };

        /// <summary>
        /// The ra_in class provides an interface for reading data from a random-access input source
        /// at specified positions, supporting both raw byte spans and typed values.
        /// </summary>
        class ra_in
        {
        public:
            using position_t = m::io::position_t;

            /// <summary>
            /// Reads data from a specified position into a span of bytes.
            ///
            /// The span passed in `s` is updated to represent the span of bytes that were read, and
            /// the number of bytes read is returned.
            /// </summary>
            /// <param name="position">The position from which to start reading.</param>
            /// <param name="s">A reference to a span of bytes that will receive the data.</param>
            /// <returns>The number of bytes read, which is equal to the size of the span.</returns>
            std::size_t
            read(position_t position, std::span<std::byte>& s)
            {
                do_read(position, s);
                return s.size();
            }

            /// <summary>
            /// Reads data from a specified position into a byte span.
            ///
            /// Caller must use the returned value to determine how many bytes were actually
            /// read from the stream.
            /// </summary>
            /// <param name="position">The position from which to start reading.</param>
            /// <param name="s">A span of bytes to store the data read.</param>
            /// <returns>The number of bytes successfully read.</returns>
            [[nodiscard]]
            std::size_t
            read(position_t position, std::span<std::byte> const& s)
            {
                std::span<std::byte> span_copy = s;
                return do_read(position, span_copy);
            }

            /// <summary>
            /// Reads a value of type T from the specified position into the provided variable.
            /// </summary>
            /// <typeparam name="T">The type of the value to read.</typeparam>
            /// <param name="p">The position from which to read the value.</param>
            /// <param name="v">A reference to the variable where the read value will be
            /// stored.</param>
            template <typename T>
            void
            read(position_t p, T& v)
            {
                if (read(p, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
                    throw std::runtime_error("end of file");
            }

            /// <summary>
            /// Reads a value of type T from the specified position.
            /// </summary>
            /// <typeparam name="T">The type of value to read. Must be copyable and
            /// default-initializable.</typeparam> <param name="p">The position from which to read
            /// the value.</param> <returns>The value of type T read from the specified
            /// position.</returns>
            template <typename T>
                requires(std::copyable<T> && std::default_initializable<T>)
            T
            read(position_t p)
            {
                T retval{};
                read(p, retval);
                return retval;
            }

        protected:
            virtual ~ra_in() = default;

            virtual size_t
            do_read(position_t position, std::span<std::byte>& s) = 0;
        };

        /// <summary>
        /// Provides an interface for objects that support seeking to a specific position or offset
        /// and reporting their current position.
        /// </summary>
        class seekable
        {
        public:
            using position_t = m::io::position_t;
            using offset_t   = m::io::offset_t;

            /// <summary>
            /// Moves the current position to the specified location.
            /// </summary>
            /// <param name="p">The position to seek to.</param>
            void
            seek(position_t p)
            {
                do_seek(p);
            }

            /// <summary>
            /// Moves the current position to the specified offset.
            /// </summary>
            /// <param name="o">The offset to seek to.</param>
            void
            seek(offset_t o)
            {
                do_seek(o);
            }

            /// <summary>
            /// Returns the current position indicator.
            /// </summary>
            /// <returns>The current position as a value of type position_t.</returns>
            position_t
            tell()
            {
                return do_tell();
            }

        protected:
            virtual ~seekable() = default;

            virtual void
            do_seek(position_t p) = 0;

            virtual void
            do_seek(offset_t o) = 0;

            virtual position_t
            do_tell() = 0;
        };

        /// <summary>
        /// Provides an interface for writing bytes or typed values to a specified position in a
        /// random-access output stream.
        /// </summary>
        class ra_out
        {
        public:
            using position_t = m::io::position_t;

            /// <summary>
            /// Writes a sequence of bytes to a specified position.
            /// </summary>
            /// <param name="position">The position at which to begin writing.</param>
            /// <param name="s">A span containing the bytes to write.</param>
            void
            write(position_t position, std::span<std::byte const> s)
            {
                do_write(position, s);
            }

            /// <summary>
            /// Writes a value of type T to the specified position.
            /// </summary>
            /// <typeparam name="T">The type of the value to write.</typeparam>
            /// <param name="p">The position at which to write the value.</param>
            /// <param name="v">The value to write.</param>
            template <typename T>
            void
            write(position_t p, T const& v)
            {
                write(p, std::as_bytes(std::span(&v, 1)));
            }

        protected:
            virtual ~ra_out() = default;

            virtual void
            do_write(position_t position, std::span<std::byte const> s) = 0;
        };

        /// <summary>
        /// Provides an interface for writing sequences of bytes or the binary representation of
        /// values to an output destination.
        /// </summary>
        class seq_out
        {
        public:
            /// <summary>
            /// Writes a sequence of bytes to the underlying output using the provided span.
            /// </summary>
            /// <param name="s">A span of constant bytes representing the data to write.</param>
            void
            write(std::span<std::byte const> s)
            {
                return do_write(s);
            }

            /// <summary>
            /// Writes the binary representation of a value to an output destination.
            /// </summary>
            /// <typeparam name="T">The type of the value to write.</typeparam>
            /// <param name="v">The value to be written as bytes.</param>
            template <typename T>
            void
            write(T const& v)
            {
                write(std::as_bytes(std::span<T>(&v, 1)));
            }

        protected:
            virtual ~seq_out() = default;

            virtual void
            do_write(std::span<std::byte const> s) = 0;
        };
    } // namespace byte_streams
} // namespace m
