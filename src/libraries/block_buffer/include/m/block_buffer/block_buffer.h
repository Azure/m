// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <mutex>

namespace m
{
    namespace block_buffer
    {
        using file_position_t = uint64_t;

        using fetch_bytes_t = std::function<void()>;

        template <
            std::size_t Log2BufferCount = 6, // 64 buffer by default
            std::size_t Log2BufferSize = 16 // 65536 (64kb) bytes in each buffer
        >
        struct buffer_traits
        {
            //
            // Realistically there's no way to have buffers that expand to
            // std::size_t but this prevents worrying about the arithmetic
            // overflowing.
            //
            static_assert(Log2BufferCount <= (CHAR_BIT * sizeof(std::size_t)));
            static_assert(Log2BufferSize <= (CHAR_BIT * sizeof(std::size_t)));
            constexpr static inline std::size_t buffer_count = 1ull << Log2BufferCount;
            constexpr static inline std::size_t buffer_size = 1ull << Log2BufferSize;
        };

        class block_buffer
        {
            // workaround while figuring out how to templatize
            using traits_type = buffer_traits<>;

        public:
            constexpr static inline std::size_t buffer_count = 100; // traits_type::buffer_count;

            constexpr static inline std::size_t buffer_size = traits_type::buffer_size;

            template <
                typename Callable,
                typename... Types>
            block_buffer(Callable f, Types... args) : m_fetch_bytes(std::bind(f, args...))
            {
            }
        private:
            fetch_bytes_t m_fetch_bytes;

            using buffer_type = std::array<std::byte, buffer_size>;

            //
            // The compiler is free to break bitfields apart on its own whims
            // subject to backwards compatibility, but in practice the way to
            // more or less ensure that the fields are together is to pick
            // a single type for all the fields.
            //
            // Many of the fields are logically Boolean valued, but as some
            // may hold non-Boolean values, uintmax_t is used so that unsigned
            // values are stored and are almost certainly packed together.
            // 
            // The entire struct is unioned with a value which is usable with
            // the InterlockedCompareExchange128 intrinsic. The
            // InterlockedCompareExchange128 primitive is somewhat strangely
            // specified, but it's best to be able to avoid casting.
            //
            struct buffer_control_word
            {
                constexpr buffer_control_word() : m_busy(0), m_reserved_unused(0) {}

                uintptr_t m_busy : 1;

                uintptr_t m_reserved_unused : 63;
            };

            static_assert(sizeof(buffer_control_word) == sizeof(uintptr_t));

            struct buffer_control_block
            {
                constexpr buffer_control_block() : m_control_word() {}
                file_position_t m_file_position;
                buffer_control_word m_control_word;
            };

            //
            // TODO: Change this constant to the constant that represents the
            // cache line size, can't remember it offhand. The control block
            // may have several pointers etc in it but don't let it get out
            // of hand.
            //
            static_assert(sizeof(buffer_control_block) <= 64);

            std::mutex m_mutex;

            //
            // Counter for which block to use next. always increment, use modulus
            // to determine which buffer/control block is selected.
            //
            std::size_t m_allocated_buffer_counter;
            std::map<file_position_t, buffer_control_block const*> m_bcbmap;
            std::array<buffer_control_block, buffer_count> m_control_blocks;
            std::array<buffer_type, buffer_count> m_buffers;
        };
    }
}