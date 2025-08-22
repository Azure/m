// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <m/chrono/chrono.h>
#include <m/utility/pointers.h>
#include <m/win32/event.h>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::win32::threadpool
{
    class tp_work
    {
    public:
        tp_work() = default;
        tp_work(tp_work&& other) noexcept;
        tp_work(tp_work const& other) = delete;

        tp_work(PTP_WORK_CALLBACK    pfnwk,
                PVOID                pv   = nullptr,
                PTP_CALLBACK_ENVIRON pcbe = PTP_CALLBACK_ENVIRON{});

        ~tp_work();

        tp_work&
        operator=(tp_work&&) noexcept;

        tp_work&
        operator=(tp_work const&) = delete;

        constexpr
        operator PTP_WORK() const noexcept
        {
            return m_pwork;
        }

        constexpr explicit
        operator bool() const noexcept
        {
            return m_pwork != PTP_WORK{};
        }

        void
        reset();

        void
        submit();

        void
        wait_for_callbacks(bool cancel_pending_callbacks);

    private:
        PTP_WORK m_pwork{PTP_WORK{}};
    };

    class tp_wait
    {
    public:
        tp_wait() = default;
        tp_wait(tp_wait&& other) noexcept;
        tp_wait(tp_wait const& other) = delete;

        tp_wait(PTP_WAIT_CALLBACK    pfnwk,
                PVOID                pv   = nullptr,
                PTP_CALLBACK_ENVIRON pcbe = PTP_CALLBACK_ENVIRON{});

        ~tp_wait();

        tp_wait&
        operator=(tp_wait&&) noexcept;

        tp_wait&
        operator=(tp_wait const&) = delete;

        constexpr
        operator PTP_WAIT() const noexcept
        {
            return m_pwait;
        }

        constexpr explicit
        operator bool() const noexcept
        {
            return m_pwait != PTP_WAIT{};
        }

        void
        reset();

        void
        wait_for_callbacks(bool cancel_pending_callbacks);

        void
        set_wait(HANDLE h);

        void
        set_wait(event const& e)
        {
            set_wait(e.get());
        }

        template <typename Rep, typename Period>
        void
        set_wait_for(HANDLE h, std::chrono::duration<Rep, Period> dur)
        {
            do_set_wait_for(h, std::chrono::duration_cast<std::chrono::milliseconds>(dur));
        }

        template <typename Rep, typename Period>
        void
        set_wait_for(event const& e, std::chrono::duration<Rep, Period> dur)
        {
            set_wait_for(e.get(), dur);
        }

        template <typename Clock, typename Duration>
        void
        set_wait_until(HANDLE h, std::chrono::time_point<Clock, Duration> tp)
        {
            do_set_wait_unil(h, std::chrono::time_point_cast<utc_time_point>(tp));
        }

        template <typename Clock, typename Duration>
        void
        set_wait_until(event const& e, std::chrono::time_point<Clock, Duration> tp)
        {
            do_set_wait_unil(e.get(), tp);
        }

    private:
        void
        do_set_wait_for(HANDLE h, std::chrono::milliseconds dur);

        void
        do_set_wait_until(HANDLE h, utc_time_point when);

        PTP_WAIT m_pwait{PTP_WAIT{}};
    };

    class tp_io
    {
    public:
        tp_io() = default;
        tp_io(tp_io&& other) noexcept;
        tp_io(tp_io const& other) = delete;

        /// <summary>
        /// Create and manage a PTP_IO object.
        ///
        /// See
        /// https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-createthreadpoolio
        /// for details of parameters.
        /// </summary>
        /// <param name="fl"></param>
        /// <param name="pfnio"></param>
        /// <param name="pv"></param>
        /// <param name="pcbe"></param>
        tp_io(HANDLE                fl,
              PTP_WIN32_IO_CALLBACK pfnio,
              PVOID                 pv   = nullptr,
              PTP_CALLBACK_ENVIRON  pcbe = nullptr);

        ~tp_io();

        tp_io&
        operator=(tp_io&& other) noexcept;

        tp_io&
        operator=(tp_io const&) = delete;

        constexpr PTP_IO
        get() const
        {
            return m_pio;
        }

        constexpr explicit
        operator PTP_IO() const
        {
            return m_pio;
        }

        /// <summary>
        /// Calls CancelThreadpoolIo on the managed PTP_IO.
        ///
        /// The object must be managing a PTP_IO or this will fail with a precondition failure.
        ///
        /// See
        /// https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-cancelthreadpoolio
        ///
        /// </summary>
        void
        cancel();

        /// <summary>
        /// If the ptp_io object is managing a PTP_IO, cancels any pending IOs, waiting
        /// for them to complete and then closes the PTP_IO.
        ///
        /// The CloseThreadpoolIo() API is not exposed as a member function because
        /// it is dangerous and would release the underlying PTP_IO out from under
        /// the `ptp_io` instance. If you really need to do this yourself, well,
        /// first, why are you using ptp_io, but if you must, then use the `release()`
        /// function to release the PTP_IO out from being under management.
        ///
        /// </summary>
        void
        reset();

        PTP_IO
        release() noexcept;

        void
        reopen(HANDLE                fl,
               PTP_WIN32_IO_CALLBACK pfnio,
               PVOID                 pv   = nullptr,
               PTP_CALLBACK_ENVIRON  pcbe = nullptr);

        class io
        {
        public:
            constexpr io() noexcept: m_tp_io{}, m_armed(false) {}

            constexpr io(io&& other) noexcept
            {
                using std::swap;
                swap(m_tp_io, other.m_tp_io);
                swap(m_armed, other.m_armed);
            }

            ~io();

            constexpr io&
            operator=(io&& other) noexcept
            {
                using std::swap;

                swap(m_tp_io, other.m_tp_io);
                swap(m_armed, other.m_armed);

                return *this;
            }

            constexpr bool
            armed() const
            {
                return m_armed;
            }

            constexpr void
            release()
            {
                m_armed = false;
            }

        private:
            constexpr io(m::not_null<tp_io*> ptr) noexcept: m_tp_io(ptr), m_armed(true) {}

            tp_io* m_tp_io{};
            bool   m_armed{false};

            friend class tp_io;
        };
        /// <summary>
        /// Calls StartThreadpoolIo() on the managed PTP_IO. If *this is not managing a
        /// PTP_IO, will fail with a precondition failure.
        ///
        /// See
        /// https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-startthreadpoolio
        /// </summary>
        io
        start();

        /// <summary>
        /// Calls the WaitForPendingCallbacks() function on the managed
        /// PTP_IO. If *this is not managing a PTP_IO, fails with a
        /// precondition failure.
        ///
        /// See
        /// https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-waitforthreadpooliocallbacks
        /// for details.
        /// </summary>
        /// <param name="cancel_pending_callbacks"></param>
        void
        wait_for_pending_callbacks(bool cancel_pending_callbacks);

    private:
        PTP_IO m_pio{PTP_IO{}};
    };
} // namespace m::win32::threadpool
