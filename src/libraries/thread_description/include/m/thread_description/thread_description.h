// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>

#include <m/utility/pointers.h>

namespace m
{
    namespace thread_description_impl
    {
        constexpr std::size_t pointers_required =
#ifdef WIN32
            1
#else
            4
#endif
            ;

        using saved_thread_state = std::array<void*, 4>;

        // Implemented in a per-platform means but the fundamental interface must be this trivial.

        // Sets the thread description to `description`, saves the current state in `state`.
        void
        set_thread_description(std::wstring_view description, saved_thread_state& state) noexcept;

        // Restores the thread description that was saved in `state`.
        void
        restore_thread_description(saved_thread_state& state) noexcept;
    }

    class thread_description
    {
    public:
        explicit thread_description(std::wstring_view description)
        {
            thread_description_impl::set_thread_description(description, m_saved_state);
        }

        ~thread_description()
        {
            thread_description_impl::restore_thread_description(m_saved_state);
        }

    private:
        thread_description_impl::saved_thread_state m_saved_state;
    };


} // namespace m
