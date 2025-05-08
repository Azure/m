// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <ios>
#include <iostream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/utility/zstring.h>

#include <Windows.h>

using namespace std::string_literals;

namespace m
{
    namespace dbg_format_details
    {
        inline std::atomic<uint64_t> g_dbg_format_stdout_message_count;

        inline bool
        try_allocate_std_out_message()
        {
            uint64_t expected = g_dbg_format_stdout_message_count.load(std::memory_order_acquire);

            // Try to grab a ticket if the count is non-zero
            while (expected > 0)
            {
                if (g_dbg_format_stdout_message_count.compare_exchange_strong(
                        expected, expected - 1, std::memory_order_acq_rel))
                    return true;
            }

            // If we reach zero, there are no output lines left to give out
            return false;
        }

        template <typename CharT, size_t length>
        class output_debug_string_buffer;

        template <typename CharT, size_t length>
        class output_debug_string_iter
        {
            static constexpr inline std::array<CharT, 4> long_line_chars = {'.', '.', '.', '\n'};

            static constexpr inline std::basic_string_view<CharT> long_line_suffix =
                std::basic_string_view<CharT>(long_line_chars.data(), long_line_chars.size());

            static constexpr inline size_t limit = length - (long_line_suffix.size() + 1);

        public:
            using difference_type   = std::ptrdiff_t;
            using iter_value_t      = CharT;
            using iter_reference_t  = CharT&;
            using iterator_category = std::output_iterator_tag;

            output_debug_string_iter(output_debug_string_iter const& iter):
                _buffer(iter._buffer), _index(iter._index)
            {}

            output_debug_string_iter&
            operator=(output_debug_string_iter const& iter)
            {
                _buffer = iter._buffer;
                _index  = iter._index;
                return *this;
            }

            output_debug_string_iter&
            operator++()
            {
                if (_index == limit)
                {
                    // If we're at the end of line, add the ellipsis, call
                    // OutputDebugStringW() and restart.
                    std::copy_n(long_line_suffix.begin(),
                                long_line_suffix.size() + 1,
                                &_buffer->_array[_index + 1]);

                    if constexpr (std::is_same_v<CharT, wchar_t>)
                        ::OutputDebugStringW(&_buffer->_array[0]);
                    else
                        ::OutputDebugStringA(&_buffer->_array[0]);

                    _index = 0;
                }
                else
                    _index++;

                return *this;
            }

            output_debug_string_iter
            operator++(int)
            {
                output_debug_string_iter old(*this);

                if (_index == limit)
                {
                    // If we're at the end of line, add the ellipsis, call
                    // OutputDebugStringW() and restart.
                    std::copy_n(long_line_suffix.begin(),
                                long_line_suffix.size() + 1,
                                &_buffer->_array[_index]);

                    if constexpr (std::is_same_v<CharT, wchar_t>)
                        ::OutputDebugStringW(&_buffer->_array[0]);
                    else
                        ::OutputDebugStringA(&_buffer->_array[0]);

                    if (dbg_format_details::try_allocate_std_out_message())
                    {
                        if constexpr (std::is_same_v<CharT, char>)
                        {
                            std::cout << &_buffer->_array[0];
                        }
                        else
                        {
                            std::wcout << &_buffer->_array[0];
                        }
                    }
                    _index = 0;
                }
                else
                    _index++;

                return old;
            }

            CharT&
            operator*() const
            {
                return _buffer->_array[_index];
            }

        private:
            output_debug_string_iter(output_debug_string_buffer<CharT, length>* buffer):
                _buffer(buffer), _index(0)
            {}

            output_debug_string_buffer<CharT, length>* _buffer;
            size_t                                     _index;

            friend class output_debug_string_buffer<CharT, length>;
        };

        template <typename CharT, size_t length>
        class output_debug_string_buffer
        {
            static_assert(length > 10);

        public:
            output_debug_string_buffer()  = default;
            ~output_debug_string_buffer() = default;

            output_debug_string_iter<CharT, length>
            begin()
            {
                return output_debug_string_iter<CharT, length>(this);
            }

            CharT*
            data()
            {
                return _array.data();
            }

        private:
            std::array<CharT, length + 1> _array;

            friend class output_debug_string_iter<CharT, length>;
        };

    } // namespace dbg_format_details

    template <typename CharT>
    void
    dbg_format_v(std::basic_string_view<CharT> fmt, const std::format_args args)
    {
        dbg_format_details::output_debug_string_buffer<char, 512> buffer;
        auto                                                      iter = buffer.begin();

        iter    = std::vformat_to(iter, fmt, args);
        *iter++ = 0;

        OutputDebugStringA(buffer.data());

        if (dbg_format_details::try_allocate_std_out_message())
            std::cout << buffer.data();
    }

    template <int = 0>
    void
    dbg_format_v(std::wstring_view fmt, const std::wformat_args args)
    {
        dbg_format_details::output_debug_string_buffer<wchar_t, 512> buffer;
        auto                                                         iter = buffer.begin();

        iter    = std::vformat_to(iter, fmt, args);
        *iter++ = 0;

        OutputDebugStringW(buffer.data());

        if (dbg_format_details::try_allocate_std_out_message())
            std::wcout << buffer.data();
    }

    template <typename... Types>
    void
    dbg_format(std::format_string<Types...> fmt, Types&&... args)
    {
        dbg_format_v(fmt.get(), std::make_format_args(args...));
    }

    template <typename... Types>
    void
    dbg_format(std::wformat_string<Types...> fmt, Types&&... args)
    {
        dbg_format_v(fmt.get(), std::make_wformat_args(args...));
    }

    template <typename CharT, typename... Types>
    void
    dbg_format(std::basic_format_string<CharT, Types...> fmt, Types const&... args)
    {
        if constexpr (std::is_same_v<CharT, char>)
        {
            auto formatArgs = std::make_format_args(args...);

            dbg_format_details::output_debug_string_buffer<char, 512> buffer;
            auto                                                      iter = buffer.begin();

            iter    = std::vformat_to(iter, fmt.get(), formatArgs);
            *iter++ = 0;

            OutputDebugStringA(buffer.data());

            if (dbg_format_details::try_allocate_std_out_message())
                std::cout << buffer.data();
        }
        else
        {
            auto formatArgs = std::make_wformat_args(args...);

            dbg_format_details::output_debug_string_buffer<wchar_t, 512> buffer;
            auto                                                         iter = buffer.begin();

            iter    = std::vformat_to(iter, fmt.get(), formatArgs);
            *iter++ = L'\0';

            OutputDebugStringW(buffer.data());

            if (dbg_format_details::try_allocate_std_out_message())
                std::wcout << buffer.data();
        }
    }
} // namespace m
