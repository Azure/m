// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/strings/conversion_details.h>
#include <m/strings/view_conversions.h>
#include <m/utility/concepts.h>
#include <m/utility/type_traits.h>
#include <m/utility/view_converter.h>
#include <m/utility/zstring.h>

namespace m::strings::impl::ordinal_case_insensitive
{
    //
    // The per-operating system platform libraries each provide their
    // own implementation of these.
    //
    bool
    less(std::string_view const& l, std::string_view const& r);
    bool
    less(std::wstring_view const& l, std::wstring_view const& r);
    bool
    less(std::u8string_view const& l, std::u8string_view const& r);
    bool
    less(std::u16string_view const& l, std::u16string_view const& r);
    bool
    less(std::u32string_view const& l, std::u32string_view const& r);

} // namespace m::strings::impl::ordinal_case_insensitive

namespace m
{
    template <typename T, typename Enable = void>
    struct case_insensitive_less;

    template <typename LeftT, typename RightT, typename Enable = void>
    struct case_insensitive_less_helper;

    template <typename CharT>
    struct case_insensitive_less_helper<std::basic_string_view<CharT>,
                                        std::basic_string_view<CharT>,
                                        void>
    {
        static bool
        less(std::basic_string_view<CharT> lhs, std::basic_string_view<CharT> rhs)
        {
            return m::strings::impl::ordinal_case_insensitive::less(lhs, rhs);
        }

        static bool
        less(std::optional<std::basic_string_view<CharT>> const& lhs,
             std::basic_string_view<CharT>                       rhs)
        {
            if (!lhs.has_value())
                return true;

            return m::strings::impl::ordinal_case_insensitive::less(lhs.value(), rhs);
        }

        static bool
        less(std::basic_string_view<CharT>                       lhs,
             std::optional<std::basic_string_view<CharT>> const& rhs)
        {
            if (!rhs.has_value())
                return false;

            return m::strings::impl::ordinal_case_insensitive::less(lhs, rhs.value());
        }

        static bool
        less(std::optional<std::basic_string_view<CharT>> const& lhs,
             std::optional<std::basic_string_view<CharT>> const& rhs)
        {
            if (!rhs.has_value())
                return false;

            if (!lhs.has_value())
                return true;

            return m::strings::impl::ordinal_case_insensitive::less(lhs.value(), rhs.value());
        }
    };

    template <typename T>
    struct case_insensitive_less<T, std::enable_if_t<any_stringish<T> || has_some_view<T>>>
    {
        using is_transparent  = char;
        using value_type      = typename T::value_type;
        using string_type     = std::basic_string<value_type>;
        using view_type       = std::basic_string_view<value_type>;
        using opt_string_type = std::optional<string_type>;
        using opt_view_type   = std::optional<view_type>;

        template <typename U, typename V>
        bool
        operator()(U&& u, V&& v) const
        {
            using unfancy_u = conversion_details::template conversion_strip_t<remove_cvref_t<U>>;
            using unfancy_v = conversion_details::template conversion_strip_t<remove_cvref_t<V>>;

            auto const u_view = view_converter<unfancy_u, view_type>::make_view(std::forward<U>(u));
            auto const v_view = view_converter<unfancy_v, view_type>::make_view(std::forward<V>(v));

            using u_view_unfancy_t =
                conversion_details::template conversion_strip_t<remove_cvref_t<decltype(u_view)>>;
            using v_view_unfancy_t =
                conversion_details::template conversion_strip_t<remove_cvref_t<decltype(v_view)>>;

            using helper_t = case_insensitive_less_helper<u_view_unfancy_t, v_view_unfancy_t>;

            return helper_t::less(u_view, v_view);
        }
    };

} // namespace m
