// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <m/strings/convert.h>

namespace m::rust
{
    // Helper type that always converts to utf-8 and makes the data and length
    // that Rust APIs want easily available.
    //
    // A specialization for char8_t should be authored that does not copy the
    // input string. This is specifically for dealing with input data and the
    // caller is already responsible for keeping the data alive through the
    // duration of the call.
    //
    template <typename CharT>
    class string_in
    {
    public:
        string_in(std::basic_string_view<CharT> sv): m_string(m::to_u8string(sv))
        {}

        uint8_t const*
        data() const noexcept
        {
            return reinterpret_cast<uint8_t const*>(m_string.data());
        }

        std::size_t
        size() const noexcept
        {
            return m_string.length();
        }

    protected:
        std::u8string m_string;
    };

} // namespace m::rust
