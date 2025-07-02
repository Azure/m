// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace m::pil
{

    //
    // Note that by convention, passing `flags` of InputFlagsT{0} _must_
    // return a disposition which is false (that is, both the code and flags
    // are also zero).
    // 
    // This is the "simple callers get simple results" rule. Callers must
    // opt in to receive more complex behaviors.
    // 
    // For this reason, return values of type `disposition` are not
    // necessarily marked as [nodiscard]. You may still do it, the syntax is
    // not necessarily over onerous but the question is what would the caller
    // do with it?
    // 
    // 
    //

    template <typename CodeT, typename FlagsT>
        requires(std::is_scoped_enum_v<CodeT> && std::is_scoped_enum_v<FlagsT>)
    class disposition
    {
    public:
        using code_type  = CodeT;
        using flags_type = FlagsT;

        constexpr disposition(code_type  code  = code_type{},
                              flags_type flags = flags_type{}) noexcept:
            m_code(code), m_flags(flags)
        {}

        // The operator bool returns true if anything "out of the ordinary" was returned.
        constexpr
        operator bool() const
        {
            return (m_code != code_type{}) || (m_flags != flags_type{});
        }

        constexpr code_type
        code() const
        {
            return m_code;
        }

        constexpr bool
        code_ok() const
        {
            return m_code == code_type{};
        }

        constexpr flags_type
        flags() const
        {
            return m_flags;
        }

        constexpr bool
        flags_none() const
        {
            return m_flags == flags_type{};
        }

        /// <summary>
        /// Tests whether the 0-indexed bit specified in `bit_index` is
        /// set in the flags in the disposition.
        /// </summary>
        /// <param name="bit_index">Zero-indexed bit number to test in the
        /// flags within the disposition object. Since dispositions have
        /// at most 32 flags, if the bit number is over 31, the result
        /// will always be false.</param>
        /// <returns>`true` if the bit is set, `false` if it is
        /// clear.</returns>
        constexpr bool
        flag_bit_set(uint8_t bit_index) const
        {
            //
            // Not worrying about bit_index "out of range" because
            // all those bits are implicitly zero and thus are clear.
            //
            return (m_flags & (1ull << bit_index)) != 0;
        }

        /// <summary>
        /// Tests whether the 0-indexed bit specified in `bit_index` is
        /// clear in the flags in the disposition.
        /// </summary>
        /// <param name="bit_index">Zero-indexed bit number to test in the
        /// flags within the disposition object. Since dispositions have
        /// at most 32 flags, if the bit number is over 31, the result
        /// will always be true.</param>
        /// <returns>`true` if the bit is clear, `false` if it is
        /// set.</returns>
        constexpr bool
        flag_bit_clear(uint8_t bit_index) const
        {
            //
            // Not worrying about bit_index "out of range" because
            // all those bits are implicitly zero and thus are clear.
            //
            return (m_flags & (1ull << bit_index)) == 0;
        }

        friend constexpr std::strong_ordering
        operator<=>(disposition<code_type, flags_type> l, disposition<code_type, flags_type> r)
        {
            auto const c1 = operator<=>(std::to_underlying(l.m_code), std::to_underlying(r.m_code));

            if ((c1 == std::strong_ordering::less) || (c1 == std::strong_ordering::greater))
                return c1;

            return operator<=>(std::to_underlying(l.m_flags), std::to_underlying(r.m_flags));
        }

    private:
        code_type  m_code;
        flags_type m_flags;
    };
} // namespace m::pil
