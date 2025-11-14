// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <concepts>
#include <optional>
#include <string>
#include <type_traits>

#include <m/utility/concepts.h>

namespace m
{
    template <typename CharT>
    class basic_string_insert_iterator
    {
    public:
        using value_type      = CharT;
        using string_type     = std::basic_string<value_type>;
        using container_type  = std::basic_string<value_type>;
        using pointer_type    = value_type*;
        using reference_type  = value_type&;
        using size_type       = typename string_type::size_type;
        using difference_type = typename string_type::difference_type;

        constexpr basic_string_insert_iterator() noexcept: m_string(nullptr) {}

        constexpr explicit basic_string_insert_iterator(string_type& string) noexcept:
            m_string(&string)
        {}

        basic_string_insert_iterator(basic_string_insert_iterator const& other) noexcept:
            m_string(other.m_string), m_index(other.m_index)
        {}

        constexpr basic_string_insert_iterator(basic_string_insert_iterator&& other) noexcept:
            m_string(nullptr), m_index(0)
        {
            using std::swap;
            swap(m_string, other.m_string);
            swap(m_index, other.m_index);
        }

        basic_string_insert_iterator &
        operator=(basic_string_insert_iterator const& other)
        {
            m_string = other.m_string;
            m_index  = other.m_index;
            return *this;
        }

        basic_string_insert_iterator &
        operator=(basic_string_insert_iterator&& other)
        {
            using std::swap;
            swap(m_string, other.m_string);
            swap(m_index, other.m_index);
            return *this;
        }

        void
        swap(basic_string_insert_iterator& other) noexcept
        {
            using std::swap;
            swap(m_string, other.m_string);
            swap(m_index, other.m_index);
        }

        [[nodiscard]] constexpr value_type&
        operator*() noexcept
        {
            if (m_string->size() <= m_index)
                m_string->resize(m_index + 1);

            return (*m_string)[m_index];
        }

        constexpr basic_string_insert_iterator&
        operator++() noexcept
        {
            m_index++;
            return *this;
        }

        constexpr basic_string_insert_iterator
        operator++(int) noexcept
        {
            basic_string_insert_iterator old = *this;
            operator++();
            return old;
        }

    private:
        string_type* m_string;
        size_type    m_index{};
    };

    template <typename CharT>
        requires(m::character<CharT>)
    struct string_inserter_helper
    {
        static auto
        make_inserter_iterator(std::basic_string<CharT>& string)
        {
            return basic_string_insert_iterator<CharT>(string);
        }
    };

    //
    // Note that while the function signature has allowance for
    // the TExtra parameter types and parameters, typically
    // they are not used. They are there as a future looking
    // mechanism for a container that perhaps wants to do
    // something fancy for back inserter iterator construction
    // someday. As if.
    //
    template <typename CharT, typename... TExtra>
        requires(m::character<CharT>)
    auto
    string_inserter(std::basic_string<CharT>& string, TExtra&&... extra)
    {
        return string_inserter_helper<CharT>::make_inserter_iterator(
            string, std::forward<TExtra>(extra)...);
    }

    namespace string_inserter_static_testing
    {
        static inline void
        test_function()
        {

            using namespace std::string_literals;

            std::string s1;

            auto test_iterator = string_inserter(s1);
            static_assert(std::weakly_incrementable<decltype(test_iterator)>);
            // static_assert(std::output_iterator<char, decltype(test_iterator)>);
        }
    }

} // namespace m
