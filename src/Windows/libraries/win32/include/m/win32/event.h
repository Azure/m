// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <system_error>
#include <utility>

#include <m/utility/enum_operations.h.h>
#include <m/win32/handle.h>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::win32
{
    enum class create_event_flags : uint32_t
    {
        initial_set  = 0x00000002, // CREATE_EVENT_INITIAL_SET
        manual_reset = 0x00000001, // CREATE_EVENT_MANUAL_RESET
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(create_event_flags);

    class event : protected handle
    {
        using base_type = handle;

    public:
        event() = default;

        enum class event_kind
        {
            automatic,
            manual,
        };

        event(create_event_flags flags, DWORD desired_access = SYNCHRONIZE | EVENT_MODIFY_STATE);

        event(event_kind kind, DWORD desired_access = SYNCHRONIZE | EVENT_MODIFY_STATE);

        constexpr event(HANDLE hdl, event_kind kind) noexcept: base_type(hdl), m_event_kind(kind) {}

        constexpr event(event&& other) noexcept: m_event_kind{}
        {
            using std::swap;
            swap(m_event_kind, other.m_event_kind);
            swap(m_handle, other.m_handle);
        }

        event(event const& other) = delete;

        constexpr event&
        operator=(event&& other) noexcept
        {
            using std::swap;
            swap(m_handle, other.m_handle);
            return *this;
        }

        event&
        operator=(event const&) = delete;

        ~event() = default;

        constexpr void
        swap(event& other) noexcept
        {
            using std::swap;

            swap(m_handle, other.m_handle);
        }

        using base_type::addressof;
        using base_type::get;
        using base_type::handle;
        using base_type::is_valid;
        using base_type::reset;

        void
        create(create_event_flags flags, DWORD desired_access = SYNCHRONIZE | EVENT_MODIFY_STATE);

        [[nodiscard]] std::error_code
        createq(create_event_flags flags, DWORD desired_access = SYNCHRONIZE | EVENT_MODIFY_STATE);

        enum class event_state
        {
            reset,
            set,
        };

        void
        set_event_state(event_state state);

    private:
        event_kind m_event_kind;
    };

} // namespace m::win32
