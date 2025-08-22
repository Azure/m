// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/win32/threadpool.h>

namespace m::win32::threadpool
{
    tp_io::tp_io(HANDLE fl, PTP_WIN32_IO_CALLBACK pfnio, PVOID pv, PTP_CALLBACK_ENVIRON pcbe)
    {
        m_pio = ::CreateThreadpoolIo(fl, pfnio, pv, pcbe);
        if (m_pio == PTP_IO{})
        {
            m::throw_last_win32_error();
        }
    }

    tp_io::tp_io(tp_io&& other) noexcept
    {
        using std::swap;
        swap(m_pio, other.m_pio);
    }

    tp_io&
    tp_io::operator=(tp_io&& other) noexcept
    {
        using std::swap;

        swap(m_pio, other.m_pio);
        return *this;
    }

    tp_io::~tp_io() { reset(); }

    void
    tp_io::cancel()
    {
        M_INTERNAL_ERROR_CHECK(m_pio != PTP_IO{});
        ::CancelThreadpoolIo(m_pio);
    }

    void
    tp_io::reset()
    {
        if (auto const pio = std::exchange(m_pio, PTP_IO{}); pio != PTP_IO{})
        {
            // Neither API here may fail
            WaitForThreadpoolIoCallbacks(pio, TRUE);
            CloseThreadpoolIo(pio);
        }
    }

    PTP_IO
    tp_io::release() noexcept { return std::exchange(m_pio, PTP_IO{}); }

    void
    tp_io::reopen(HANDLE fl, PTP_WIN32_IO_CALLBACK pfnio, PVOID pv, PTP_CALLBACK_ENVIRON pcbe)
    {
        auto const pio = ::CreateThreadpoolIo(fl, pfnio, pv, pcbe);
        if (pio == PTP_IO{})
        {
            m::throw_last_win32_error();
        }

        reset();

        m_pio = pio;
    }

    tp_io::io
    tp_io::start()
    {
        M_INTERNAL_ERROR_CHECK(m_pio != PTP_IO{});
        StartThreadpoolIo(m_pio);
        return io(this);
    }

    void
    tp_io::wait_for_pending_callbacks(bool cancel_pending_callbacks)
    {
        M_INTERNAL_ERROR_CHECK(m_pio != PTP_IO{});
        WaitForThreadpoolIoCallbacks(m_pio, cancel_pending_callbacks);
    }

    tp_io::io::~io()
    {
        if (auto const armed = std::exchange(m_armed, false); armed)
        {
            m_tp_io->cancel();
        }
    }

} // namespace m::win32::threadpool
