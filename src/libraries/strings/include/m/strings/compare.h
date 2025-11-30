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
    struct case_insensitive_less<T, std::enable_if_t<stringish<T> || has_some_view<T>>>
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

#if 0
        bool
        operator()(view_type const& l, view_type const& r) const
        {
            return m::strings::impl::ordinal_case_insensitive::less(l, r);
        }

        template <typename U>
        constexpr bool
        operator()(basic_zstring<value_type const> psz, U&& u) const
        {
            view_type view{};

            if (psz != nullptr)
                view = view_type(psz);

            return operator()(view, std::forward<U>(u));
        }

        template <typename U>
        constexpr bool
        operator()(U&& u, basic_zstring<value_type const> psz) const
        {
            view_type view{};

            if (psz != nullptr)
                view = view_type(psz);

            return operator()(std::forward<U>(u), view);
        }

        template <typename U, typename V>
        bool
        operator()(U&& u, V&& v) const
        {
            return operator()(
                view_converter<remove_cvref_t<U>, view_type>::make_view(std::forward<U>(u)),
                view_converter<remove_cvref_t<V>, view_type>::make_view(std::forward<V>(v)));
        }

#if 0
        bool
        operator()(opt_string_type const& l, opt_view_type const& r) const
        {
            if (!l.has_value())
                return r.has_value();

            return operator()(view_type{l.value()}, r);
        }

        bool
        operator()(opt_string_type const& l, opt_string_type const& r) const
        {
            if (!l.has_value())
                return r.has_value();

            return operator()(view_type{l.value()}, r);
        }

        bool
        operator()(opt_string_type const& l, view_type const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(view_type{l.value()}, r);
        }

        bool
        operator()(opt_string_type const& l, string_type const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(view_type{l.value()}, r);
        }

        bool
        operator()(opt_view_type const& l, opt_view_type const& r) const
        {
            if (!l.has_value())
                return r.has_value();

            return operator()(l.value(), r);
        }

        bool
        operator()(opt_view_type const& l, opt_string_type const& r) const
        {
            if (!l.has_value())
                return r.has_value();

            return operator()(l.value(), r);
        }

        bool
        operator()(opt_view_type const& l, view_type const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(l.value(), r);
        }

        bool
        operator()(opt_view_type const& l, string_type const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(l.value(), r);
        }

        bool
        operator()(string_type const& l, opt_view_type const& r) const
        {
            if (!r.has_value())
                return false;

            return operator()(l, r.value());
        }

        bool
        operator()(string_type const& l, opt_string_type const& r) const
        {
            if (!r.has_value())
                return false;

            return operator()(l, r.value());
        }

        bool
        operator()(string_type const& l, view_type const& r) const
        {
            return operator()(view_type{l}, r);
        }

        bool
        operator()(string_type const& l, string_type const& r) const
        {
            return operator()(view_type{l}, view_type{r});
        }

        bool
        operator()(view_type const& l, opt_view_type const& r) const
        {
            if (!r.has_value())
                return false;

            return operator()(l, r.value());
        }

        bool
        operator()(view_type const& l, opt_string_type const& r) const
        {
            if (!r.has_value())
                return false;

            return operator()(l, view_type{r.value()});
        }

        bool
        operator()(view_type const& l, string_type const& r) const
        {
            return operator()(l, view_type{r});
        }

        template <typename LeftT>
            requires(has_view<LeftT, value_type>)
        bool
        operator()(LeftT const& l, view_type const& r) const
        {
            return operator()(l.view(), r);
        }

        template <typename LeftT>
            requires(has_view<LeftT, value_type>)
        bool
        operator()(std::optional<LeftT> const& l, view_type const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(l.value(), r);
        }

        template <typename LeftT>
            requires(has_view<LeftT, value_type>)
        bool
        operator()(std::optional<LeftT> const& l, std::optional<view_type> const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(l.value(), r);
        }

        template <typename LeftT>
            requires(has_view<LeftT, value_type>)
        bool
        operator()(LeftT const& l, std::optional<view_type> const& r) const
        {
            if (!r.has_value())
                return false;

            return operator()(l, r.value());
        }

        template <typename RightT>
            requires(has_view<RightT, value_type>)
        bool
        operator()(view_type const& l, RightT const& r) const
        {
            return m::strings::impl::ordinal_case_insensitive::less(l, r.view());
        }

        template <typename RightT>
            requires(has_view<RightT, value_type>)
        bool
        operator()(std::optional<view_type> const& l, std::optional<RightT> const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(l.value(), r);
        }

        template <typename RightT>
            requires(has_view<RightT, value_type>)
        bool
        operator()(std::optional<view_type> const& l, RightT const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(l.value(), r);
        }

        template <typename RightT>
            requires(has_view<RightT, value_type>)
        bool
        operator()(view_type const& l, std::optional<RightT> const& r) const
        {
            if (!r.has_value())
                return true;

            return operator()(l, r.value());
        }

        template <typename LeftT, typename RightT>
            requires(has_view<LeftT, value_type> && has_view<RightT, value_type>)
        bool
        operator()(LeftT const& l, RightT const& r) const
        {
            return m::strings::impl::ordinal_case_insensitive::less(l.view(), r.view());
        }

        template <typename LeftT, typename RightT>
            requires(has_view<LeftT, value_type> && has_view<RightT, value_type>)
        bool
        operator()(std::optional<LeftT> const& l, std::optional<RightT> const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(l.value(), r);
        }

        template <typename LeftT, typename RightT>
            requires(has_view<LeftT, value_type> && has_view<RightT, value_type>)
        bool
        operator()(std::optional<LeftT> const& l, RightT const& r) const
        {
            if (!l.has_value())
                return true;

            return operator()(l.value(), r);
        }

        template <typename LeftT, typename RightT>
            requires(has_view<LeftT, value_type> && has_view<RightT, value_type>)
        bool
        operator()(LeftT const& l, std::optional<RightT> const& r) const
        {
            if (!r.has_value())
                return true;

            return operator()(l, r.value());
        }
#endif
#endif
    };

} // namespace m
