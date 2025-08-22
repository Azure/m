// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <utility>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::win32
{
    class handle
    {
    public:
        handle() = default;

        constexpr handle(HANDLE hdl) noexcept: m_handle(hdl) {}

        constexpr handle(handle&& other) noexcept
        {
            using std::swap;
            swap(m_handle, other.m_handle);
        }

        handle(handle const& other) = delete;

        constexpr handle&
        operator=(handle&& other) noexcept
        {
            using std::swap;
            swap(m_handle, other.m_handle);
            return *this;
        }

        handle&
        operator=(handle const&) = delete;

        ~handle() { reset(); }

        constexpr void
        swap(handle& other) noexcept
        {
            using std::swap;

            swap(m_handle, other.m_handle);
        }

        void
        reset(HANDLE new_handle = HANDLE{})
        {
            auto const old_handle = std::exchange(m_handle, new_handle);
            close_handle(old_handle);
        }

        HANDLE*
        addressof()
        {
            return &m_handle;
        }

        constexpr HANDLE
        get() const
        {
            return m_handle;
        }

        constexpr
        operator HANDLE() const
        {
            return m_handle;
        }

        constexpr bool
        is_valid() const
        {
            return closable_handle(m_handle);
        }

    protected:
        static constexpr bool
        closable_handle(HANDLE h)
        {
            return h != HANDLE{} && h != INVALID_HANDLE_VALUE;
        }

        static void
        close_handle(HANDLE h)
        {
            if (closable_handle(h))
                ::CloseHandle(h);
        }

        HANDLE m_handle{};
    };

} // namespace m::win32
