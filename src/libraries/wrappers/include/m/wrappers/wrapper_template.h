// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <format>
#include <string_view>
#include <type_traits>

namespace m
{
    /// <summary>
    /// The `nonscalar_wrapper` struct template is used to describe
    /// wrappers around types which are not used as scalar values
    /// in the mathematical sense - that is, their scale or relative values
    /// are not significant. Thus, operations like addition, subtraction,
    /// relational operations other than equality are not defined.
    ///
    /// It's required that the type is trivially copyable and swappable
    /// without throwing.
    ///
    /// Note: some use the term "scalar" to mean "unitary" as opposed to
    /// being in a group such as a row/vector. However, the term originated
    /// especially with regards to mathematics in 1591 with François Viète's
    /// "Analytic Art", where he writes,
    ///
    /// Magnitudes that ascend or descend proportionally in keeping with their
    /// nature from one kind to another may be called scalar terms.
    ///
    /// (Latin : Magnitudines quae ex genere ad genus sua vi proportionaliter
    /// adscendunt vel descendunt, vocentur Scalares.)
    ///
    /// https://books.google.com/books?id=BWTyywN39KEC
    ///
    /// thus the fundamental aspect of being a "scalar" is that they have
    /// relative measures to each other, as we shall see.
    ///
    ///
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <typeparam name="UniqueT">The `UniqueT` type param is used by clients
    /// to give
    /// a unique type name for the template instantiation. Since many
    /// `nonscalar_wrapper<DWORD>` instantiations would exist, there must
    /// be some way to distinguish between them. One way would be to require
    /// that clients create a derive type to use this, but in practice, that
    /// is itself a major hassle.
    ///
    /// So instead when defining the type alias for this wrapper, also
    /// pick a name that is very unlikely to be used and put it in this
    /// type parameter as a struct name, like so:
    ///
    /// `using win32_dword_ms = m::nonscalar_wrapper<DWORD, struct
    /// win32_dword_used_as_milliseconds>`
    ///
    /// </typeparam>
    template <typename T, typename Unique>
        requires(std::semiregular<T>)
    struct nonscalar_wrapper
    {
        using value_type = T;

        nonscalar_wrapper() = default;
        constexpr explicit nonscalar_wrapper(T v) noexcept: m_v(v) {}
        constexpr nonscalar_wrapper(nonscalar_wrapper const& other) noexcept: m_v(other.m_v) {}
        constexpr nonscalar_wrapper(nonscalar_wrapper&& other) noexcept: m_v(other.m_v) {}
        ~nonscalar_wrapper() = default;

        constexpr nonscalar_wrapper&
        operator=(nonscalar_wrapper const& other) noexcept
        {
            m_v = other.m_v;
            return *this;
        }
        constexpr nonscalar_wrapper&
        operator=(nonscalar_wrapper&& other) noexcept
        {
            m_v = other.m_v;
            return *this;
        }

        constexpr void
        swap(nonscalar_wrapper& other) noexcept
        {
            using std::swap;
            swap(m_v, other.m_v);
        }

        explicit constexpr
        operator T() const
        {
            return m_v;
        }
        constexpr bool
        operator==(nonscalar_wrapper other) const
        {
            return m_v == other.m_v;
        }

        T m_v;
    };

    template <typename T, typename UniqueT>
        requires(std::semiregular<T> && std::three_way_comparable<T>)
    struct scalar_wrapper : nonscalar_wrapper<T, UniqueT>
    {
    private:
        using base = nonscalar_wrapper<T, UniqueT>;

    public:
        using base::m_v;

        constexpr auto
        operator<=>(scalar_wrapper other) const
        {
            return operator<=>(m_v, other.m_v);
        }
    };

} // namespace m
