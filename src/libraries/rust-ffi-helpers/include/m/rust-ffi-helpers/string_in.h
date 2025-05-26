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
    class string_in
    {
    public:
        template <typename CharT>
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
