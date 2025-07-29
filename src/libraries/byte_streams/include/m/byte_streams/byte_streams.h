// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

#include <m/io/units.h>

namespace m
{
    /// <summary>
    /// The `byte_traits` struct defines a type that meets the `Character Traits`
    /// named requirement and thus may enable use of std::byte in places where
    /// a character type is specified in data structures and algorithms.
    ///
    /// This header defines explicit io stream types that support the byte type
    /// as well as character types, albeit without any encoding / decoding
    /// support.
    /// </summary>
    struct byte_traits
    {
        using char_type           = std::byte;
        using byte_type           = std::byte;
        using int_type            = uint_least64_t;
        using off_type            = std::streamoff;
        using pos_type            = uintmax_t;
        using state_type          = std::mbstate_t;
        using comparison_category = std::strong_ordering;

        static constexpr void
        assign(char_type& c1, const char_type& c2) noexcept
        {
            c1 = c2;
        }

        static constexpr bool
        eq(char_type c1, char_type c2) noexcept
        {
            return c1 == c2;
        }

        static constexpr bool
        lt(char_type c1, char_type c2) noexcept
        {
            return c1 < c2;
        }

        static constexpr int
        compare(const char_type* s1, const char_type* s2, size_t n);

        static constexpr size_t
        length(const char_type* s);

        static constexpr const char_type*
        find(const char_type* s, size_t n, const char_type& a);

        static constexpr char_type*
        move(char_type* s1, const char_type* s2, size_t n);

        static constexpr char_type*
        copy(char_type* s1, const char_type* s2, size_t n);

        static constexpr char_type*
        assign(char_type* s, size_t n, char_type a)
        {
            // constexpr friendly implementation:
            for (byte_type* cursor = s; n > 0; --n, ++cursor)
            {
                *cursor = a;
            }

            return s;
        }

        static constexpr int_type
        not_eof(int_type c) noexcept
        {
            return c != eof() ? c : !!!eof();
        }

        static constexpr char_type
        to_char_type(int_type c) noexcept
        {
            return static_cast<char_type>(c);
        }

        static constexpr int_type
        to_int_type(char_type c) noexcept
        {
            return static_cast<int_type>(c);
        }

        static constexpr bool
        eq_int_type(int_type c1, int_type c2) noexcept
        {
            return c1 == c2;
        }

        static constexpr int_type
        eof() noexcept
        {
            return static_cast<int_type>(EOF);
        }
    };

    /// <summary>
    /// The `basic_raw_streambuf` type is similar to the std::basic_streambuf
    /// except that:
    ///
    /// 1. It explicitly supports std::byte
    ///
    /// 2. It does not perform any encoding changes
    ///
    /// It attempts to mimic the shape of basic_streambuf in general but
    /// not 100%.
    ///
    /// </summary>
    template <typename ByteT = std::byte, typename TraitsT = byte_traits>
    class basic_raw_streambuf
    {
    public:
        using traits_type = TraitsT;
        using byte_type   = ByteT;

        using char_type = byte_type;
        using int_type  = typename traits_type::int_type;
        using pos_type  = typename traits_type::pos_type;
        using off_type  = typename traits_type::off_type;

        using streamsize = std::streamsize;

    protected:
        basic_raw_streambuf() {}

        basic_raw_streambuf(basic_raw_streambuf const& other)
        {
            //
        }

        basic_raw_streambuf&
        operator=(basic_raw_streambuf const& other)
        {
            //
            return *this;
        }

        void
        swap(basic_raw_streambuf& r)
        {
            //
        }

    public:
        virtual ~basic_raw_streambuf()
        {
            //
        }

        // Note that the content from "31.6.3.3.1 locales" is omitted:
        //
        //      locale pubimbue(const locale& loc);
        //      locale getloc() const;
        //
        // because basic_raw_streambuf<> has no locale-sensitive behavior.

        // 31.6.3.3.2, buffer and positioning
        basic_raw_streambuf*
        pubsetbuf(byte_type* s, std::streamsize n)
        {
            return setbuf(s, n);
        }

        pos_type
        pubseekoff(off_type                off,
                   std::ios_base::seekdir  way,
                   std::ios_base::openmode which = std::ios_base::in | std::ios_base::out)
        {
            return seekoff(off, way, which);
        }

        pos_type
        pubseekpos(pos_type                sp,
                   std::ios_base::openmode which = std::ios_base::in | std::ios_base::out)
        {
            return seekpos(sp, which);
        }

        int
        pubsync()
        {
            return sync();
        }

        // get and put areas
        // 31.6.3.3.3, get area
        std::streamsize
        in_avail()
        {
            streamsize c = internal_getbuf_avail_count();
            return 0 < c ? c : showmanyc();
        }

        int_type
        snextc()
        {
            return 1 < internal_getbuf_avail_count() ?
                       traits_type::to_int_type(*internal_getbuf_preinc_pos()) :
                   traits_type::eq_int_type(traits_type::eof(), sbumpc()) ? traits_type::eof() :
                                                                            sgetc();
        }

        int_type
        sbumpc()
        {
            return 0 < internal_getbuf_avail_count() ?
                       traits_type::to_int_type(*internal_getbuf_inc_pos()) :
                       uflow();
        }

        int_type
        sgetc()
        {
            return 0 < internal_getbuf_avail_count() ? traits_type::to_int_type(*gptr()) :
                                                       underflow();
        }

        std::streamsize
        sgetn(byte_type* s, std::streamsize n)
        {
            return internal_sgetn(s, n);
        }

        // 31.6.3.3.4, putback
        int_type
        sputbackc(byte_type b)
        {
            if (gptr() && eback() < gptr() && traits_type::eq(b, gptr()[-1]))
            {
                return traits_type::to_int_type(*internal_getbuf_dec_pos());
            }

            return pbackfail(traits_type::to_int_type(b));
        }

        int_type
        sungetc()
        {
            return gptr() && eback() < gptr() ?
                       traits_type::to_int_type(*internal_getbuf_dec_pos()) :
                       pbackfail();
        }

        // 31.6.3.3.5, put area
        int_type
        sputc(byte_type b)
        {
            return 0 < internal_putbuf_avail_count() ?
                       traits_type::to_int_type(*internal_putbuf_inc_pos() = b) :
                       overflow(traits_type::to_int_type(b));
        }

        std::streamsize
        sputn(const byte_type* b, std::streamsize n)
        {
            return internal_sputn(b, n);
        }

    protected:
        // 31.6.3.4.2, get area access
        byte_type*
        eback() const noexcept
        {
            return *m_getbuf_front_ptr_ptr;
        }

        byte_type*
        gptr() const noexcept
        {
            return *m_getbuf_next_ptr_ptr;
        }

        byte_type*
        pbase() const noexcept
        {
            return *m_putbuf_front_ptr_ptr;
        }

        byte_type*
        pptr() const noexcept
        {
            return *m_putbuf_next_ptr_ptr;
        }

        byte_type*
        egptr() const noexcept
        {
            return *m_getbuf_next_ptr_ptr + *m_getbuf_count_ptr_ptr;
        }

        void
        gbump(int off) noexcept
        {
            // alter current position in read buffer by off
            *m_getbuf_count_ptr_ptr -= off;
            *m_getbuf_next_ptr_ptr += off;
        }

        void
        setg(byte_type* first, byte_type* next, byte_type* last) noexcept
        {
            // set pointers for read buffer
            *m_getbuf_front_ptr_ptr = first;
            *m_getbuf_next_ptr_ptr  = next;
            *m_getbuf_count_ptr_ptr = static_cast<int>(last - next);
        }

        byte_type*
        epptr() const noexcept
        {
            return *m_putbuf_next_ptr_ptr + *m_putbuf_count_ptr_ptr;
        }

        byte_type*
        internal_getbuf_dec_pos() noexcept
        { // decrement current position in read buffer
            ++*m_getbuf_count_ptr_ptr;
            return --*m_getbuf_next_ptr_ptr;
        }

        byte_type*
        internal_getbuf_inc_pos() noexcept
        { // increment current position in read buffer
            --*m_getbuf_count_ptr_ptr;
            return (*m_getbuf_next_ptr_ptr)++;
        }

        byte_type*
        internal_getbuf_preinc_pos() noexcept
        { // preincrement current position in read buffer
            --*m_getbuf_count_ptr_ptr;
            return ++(*m_getbuf_next_ptr_ptr);
        }

        streamsize
        internal_getbuf_avail_count() const noexcept
        { // count number of available elements in read buffer
            return *m_getbuf_next_ptr_ptr ? *m_getbuf_count_ptr_ptr : 0;
        }

        void
        pbump(int off) noexcept
        {
            // alter current position in write buffer by off
            *m_putbuf_count_ptr_ptr -= off;
            *m_putbuf_next_ptr_ptr += off;
        }

        void
        setp(byte_type* first, byte_type* last) noexcept
        {
            // set pointers for write buffer
            *m_putbuf_front_ptr_ptr = first;
            *m_putbuf_next_ptr_ptr  = first;
            *m_putbuf_count_ptr_ptr = static_cast<int>(last - first);
        }

        void
        setp(byte_type* first, byte_type* next, byte_type* last) noexcept
        {
            // set pointers for write buffer, extended version
            *m_putbuf_front_ptr_ptr = first;
            *m_putbuf_next_ptr_ptr  = next;
            *m_putbuf_count_ptr_ptr = static_cast<int>(last - next);
        }

        byte_type*
        internal_putbuf_inc_pos() noexcept
        { // increment current position in write buffer
            --*m_putbuf_count_ptr_ptr;
            return (*m_putbuf_next_ptr_ptr)++;
        }

        streamsize
        internal_putbuf_avail_count() const noexcept
        { // count number of available positions in write buffer
            return *m_putbuf_next_ptr_ptr ? *m_putbuf_count_ptr_ptr : 0;
        }

        void
        internal_init() noexcept
        { // initialize buffer parameters for no buffers
            m_getbuf_front_ptr_ptr = &m_getbuf_front_ptr;
            m_putbuf_front_ptr_ptr = &m_putbuf_front_buffer;
            m_getbuf_next_ptr_ptr  = &m_getbuf_next_ptr;
            m_putbuf_next_ptr_ptr  = &m_putbuf_next_ptr;
            m_getbuf_count_ptr_ptr = &m_getbuf_count_ptr;
            m_putbuf_count_ptr_ptr = &m_putbuf_count_ptr;
            setp(nullptr, nullptr);
            setg(nullptr, nullptr, nullptr);
        }

        void
        internal_init(byte_type** getbuf_front_ptr_ptr,
                      byte_type** getbuf_next_ptr_ptr,
                      int*        getbuf_count_ptr,
                      byte_type** putbuf_front_ptr_ptr,
                      byte_type** putbuf_next_ptr_ptr,
                      int*        putbuf_count_ptr) noexcept
        {
            // initialize buffer parameters as specified
            m_getbuf_front_ptr_ptr = getbuf_front_ptr_ptr;
            m_putbuf_front_ptr_ptr = putbuf_front_ptr_ptr;
            m_getbuf_next_ptr_ptr  = getbuf_next_ptr_ptr;
            m_putbuf_next_ptr_ptr  = putbuf_next_ptr_ptr;
            m_getbuf_count_ptr_ptr = getbuf_count_ptr;
            m_putbuf_count_ptr_ptr = putbuf_count_ptr;
        }

        virtual int_type
        overflow(int_type = traits_type::eof())
        { // put a character to stream (always fail)
            return traits_type::eof();
        }

        virtual int_type
        pbackfail(int_type = traits_type::eof())
        {
            // put a character back to stream (always fail)
            return traits_type::eof();
        }

        virtual streamsize
        showmanyc()
        {
            return 0;
        }

        virtual int_type
        underflow()
        { // get a character from stream, but don't point past it
            return traits_type::eof();
        }

        virtual int_type
        uflow()
        { // get a character from stream, point past it
            return traits_type::eq_int_type(traits_type::eof(), underflow()) ?
                       traits_type::eof() :
                       traits_type::to_int_type(*internal_getbuf_inc_pos());
        }

        virtual streamsize
        internal_sgetn(byte_type* _Ptr, streamsize _Count)
        { // get _Count characters from stream
            const streamsize _Start_count = _Count;

            while (0 < _Count)
            {
                streamsize _Size = internal_getbuf_avail_count();
                if (0 < _Size)
                { // copy from read buffer
                    if (_Count < _Size)
                    {
                        _Size = _Count;
                    }

                    traits_type::copy(_Ptr, gptr(), static_cast<size_t>(_Size));
                    _Ptr += _Size;
                    _Count -= _Size;
                    gbump(static_cast<int>(_Size));
                }
                else
                {
                    const int_type _Meta = uflow();
                    if (traits_type::eq_int_type(traits_type::eof(), _Meta))
                    {
                        break; // end of file, quit
                    }

                    // get a single character
                    *_Ptr++ = traits_type::to_char_type(_Meta);
                    --_Count;
                }
            }

            return _Start_count - _Count;
        }

        virtual streamsize
        internal_sputn(const byte_type* _Ptr, streamsize _Count)
        {
            // put _Count characters to stream
            const streamsize _Start_count = _Count;
            while (0 < _Count)
            {
                streamsize _Size = internal_putbuf_avail_count();
                if (0 < _Size)
                { // copy to write buffer
                    if (_Count < _Size)
                    {
                        _Size = _Count;
                    }

                    traits_type::copy(pptr(), _Ptr, static_cast<size_t>(_Size));
                    _Ptr += _Size;
                    _Count -= _Size;
                    pbump(static_cast<int>(_Size));
                }
                else if (traits_type::eq_int_type(traits_type::eof(),
                                                  overflow(traits_type::to_int_type(*_Ptr))))
                {
                    break; // single character put failed, quit
                }
                else
                { // count character successfully put
                    ++_Ptr;
                    --_Count;
                }
            }

            return _Start_count - _Count;
        }

        virtual pos_type
        seekoff(off_type,
                std::ios_base::seekdir,
                std::ios_base::openmode = std::ios_base::in | std::ios_base::out)
        {
            // change position by offset, according to way and mode
            return pos_type{off_type{-1}};
        }

        virtual pos_type
        seekpos(pos_type, std::ios_base::openmode = std::ios_base::in | std::ios_base::out)
        {
            // change to specified position, according to mode
            return pos_type{off_type{-1}};
        }

        virtual basic_raw_streambuf*
        setbuf(byte_type*, streamsize)
        {
            // offer buffer to external agent (do nothing)
            return this;
        }

        virtual int
        sync()
        { // synchronize with external agent (do nothing)
            return 0;
        }

        // virtual void
        // imbue(const locale&)
        //{} // set locale to argument (do nothing)

    private:
        byte_type*  m_getbuf_front_ptr{};     // beginning of read buffer
        byte_type*  m_putbuf_front_buffer{};  // beginning of write buffer
        byte_type** m_getbuf_front_ptr_ptr{}; // pointer to beginning of read buffer
        byte_type** m_putbuf_front_ptr_ptr{}; // pointer to beginning of write buffer
        byte_type*  m_getbuf_next_ptr{};      // current position in read buffer
        byte_type*  m_putbuf_next_ptr{};      // current position in write buffer
        byte_type** m_getbuf_next_ptr_ptr{};  // pointer to current position in read buffer
        byte_type** m_putbuf_next_ptr_ptr{};  // pointer to current position in write buffer
        int         m_getbuf_count_ptr{};     // length of read buffer
        int         m_putbuf_count_ptr{};     // length of write buffer
        int*        m_getbuf_count_ptr_ptr{}; // pointer to length of read buffer
        int*        m_putbuf_count_ptr_ptr{}; // pointer to length of write buffer
    };

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

            //
            // If the span is const, the returned size is nodiscard
            //
            [[nodiscard]]
            std::size_t
            read(std::span<std::byte> const& s)
            {
                std::span<std::byte> span_copy = s;
                return do_read(span_copy);
            }

            template <typename T>
            void
            read(T& v)
            {
                if (read(std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
                    throw std::runtime_error("end of file");
            }

        protected:
            virtual size_t
            do_read(std::span<std::byte>& span) = 0;
        };

        class ra_in
        {
        public:
            using position_t = m::io::position_t;

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

        class seekable
        {
        public:
            using position_t = m::io::position_t;
            using offset_t   = m::io::offset_t;

            void
            seek(position_t p)
            {
                do_seek(p);
            }

            void
            seek(offset_t o)
            {
                do_seek(o);
            }

            position_t
            tell()
            {
                return do_tell();
            }

        protected:
            virtual void
            do_seek(position_t p) = 0;

            virtual void
            do_seek(offset_t o) = 0;

            virtual position_t
            do_tell() = 0;
        };

        class ra_out
        {
        public:
            using position_t = m::io::position_t;

            void
            write(position_t position, std::span<std::byte const> s)
            {
                do_write(position, s);
            }

            template <typename T>
            void
            write(position_t p, T const& v)
            {
                write(p, std::as_bytes(std::span(&v, 1)));
            }

        protected:
            virtual void
            do_write(position_t position, std::span<std::byte const> s) = 0;
        };

        class seq_out
        {
        public:
            void
            write(std::span<std::byte const> s)
            {
                return do_write(s);
            }

            template <typename T>
            void
            write(T const& v)
            {
                write(std::as_bytes(std::span<T>(&v, 1)));
            }

        private:
            virtual void
            do_write(std::span<std::byte const> s) = 0;
        };
    } // namespace byte_streams
} // namespace m
