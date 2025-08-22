// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/win32/event.h>

namespace m::win32
{
    event::event(create_event_flags flags, DWORD desired_access): m_event_kind{}
    {
        event temp;

        temp.create(flags, desired_access);
        temp.swap(*this);
    }

    event::event(event_kind kind, DWORD desired_access): m_event_kind{}
    {
        event temp;
        temp.create((kind == event_kind::manual) ? create_event_flags::manual_reset :
                                                   create_event_flags{},
                    desired_access);
        temp.swap(*this);
    }

    void
    event::create(create_event_flags flags, DWORD desired_access)
    {
        auto const evt =
            ::CreateEventExW(nullptr, nullptr, static_cast<DWORD>(flags), desired_access);
        if (evt == NULL)
        {
            m::throw_last_win32_error();
        }

        event e(evt,
                !!(flags & create_event_flags::manual_reset) ? event_kind::manual :
                                                               event_kind::automatic);

        e.swap(*this);
    }

    [[nodiscard]] std::error_code
    event::createq(create_event_flags flags, DWORD desired_access)
    {
        auto const evt =
            ::CreateEventExW(nullptr, nullptr, static_cast<DWORD>(flags), desired_access);
        if (evt == NULL)
        {
            auto const last_error = ::GetLastError();
            return m::make_win32_error_code(last_error);
        }

        event e{evt};
        e.swap(*this);

        return std::error_code{};
    }

    void
    event::set_event_state(event_state state)
    {
        switch (state)
        {
            using enum event_state;

            case reset:
            {
                if (!::ResetEvent(m_handle))
                {
                    m::throw_last_win32_error();
                }
                break;
            }

            case set:
            {
                if (!::SetEvent(m_handle))
                {
                    m::throw_last_win32_error();
                }
                break;
            }

            default: M_UNREACHABLE_CODE();
        }
    }

} // namespace m::win32