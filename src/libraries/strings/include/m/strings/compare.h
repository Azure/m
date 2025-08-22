// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#if 0
#include <algorithm>
#include <cctype>
#include <iterator>
#include <numeric>
#include <ranges>
#include <type_traits>
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

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
    template <typename StringT>
    struct case_insensitive_less;

    template <typename T, typename CharT>
    concept has_view = requires(T x) {
        { x.view() } noexcept -> std::same_as<std::basic_string_view<CharT>>;
    };

    template <typename T>
    concept has_some_view = (has_view<T, char> || has_view<T, char8_t> || has_view<T, char16_t> ||
                             has_view<T, char32_t> || has_view<T, wchar_t>);

    template <typename T>
        requires(std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::wstring> || std::is_same_v<T, std::wstring_view> ||
                 std::is_same_v<T, std::u8string> || std::is_same_v<T, std::u8string_view> ||
                 std::is_same_v<T, std::u16string> || std::is_same_v<T, std::u16string_view> ||
                 std::is_same_v<T, std::u32string> || std::is_same_v<T, std::u32string_view> ||
                 has_some_view<T>)
    struct case_insensitive_less<T>
    {
        using is_transparent          = char;
        using template_parameter_type = T;
        using value_type              = typename template_parameter_type::value_type;
        using string_type             = std::basic_string<value_type>;
        using view_type               = std::basic_string_view<value_type>;
        using opt_string_type         = std::optional<string_type>;
        using opt_view_type           = std::optional<view_type>;

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

        bool
        operator()(view_type const& l, view_type const& r) const
        {
            return m::strings::impl::ordinal_case_insensitive::less(l, r);
        }

        template <typename LeftT>
            requires(has_view<LeftT, value_type>)
        bool
        operator()(LeftT const& l, view_type const& r) const
        {
            return m::strings::impl::ordinal_case_insensitive::less(l.view(), r);
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

    };

    template <typename T>
        requires(std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::wstring> || std::is_same_v<T, std::wstring_view> ||
                 std::is_same_v<T, std::u8string> || std::is_same_v<T, std::u8string_view> ||
                 std::is_same_v<T, std::u16string> || std::is_same_v<T, std::u16string_view> ||
                 std::is_same_v<T, std::u32string> || std::is_same_v<T, std::u32string_view> ||
                 has_some_view<T>)
    struct case_insensitive_less<std::optional<T>> : public case_insensitive_less<T>
    {};

} // namespace m
