// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/win32/event.h>

TEST(Win32Event, CreateqSetsEventKindManual)
{
    m::win32::event e;
    auto const      ec = e.createq(m::win32::create_event_flags::manual_reset);

    EXPECT_FALSE(ec) << ec.message();
    EXPECT_NE(e.get(), nullptr);
    EXPECT_EQ(e.get_event_kind(), m::win32::event::event_kind::manual);
}

TEST(Win32Event, CreateqSetsEventKindAutomatic)
{
    m::win32::event e;
    auto const      ec = e.createq(m::win32::create_event_flags::zero);

    EXPECT_FALSE(ec) << ec.message();
    EXPECT_NE(e.get(), nullptr);
    EXPECT_EQ(e.get_event_kind(), m::win32::event::event_kind::automatic);
}

TEST(Win32Event, IsValid)
{
    m::win32::event e_null;
    EXPECT_FALSE(e_null.is_valid());

    m::win32::event e_valid(m::win32::create_event_flags::zero);
    EXPECT_TRUE(e_valid.is_valid());

    e_valid.reset();
    EXPECT_FALSE(e_valid.is_valid());
}

TEST(Win32Event, MoveAssign)
{
    auto e1 = m::win32::event(m::win32::create_event_flags::manual_reset);
    auto e2 = m::win32::event();

    EXPECT_TRUE(e1.is_valid());
    EXPECT_FALSE(e2.is_valid());
    EXPECT_EQ(e1.get_event_kind(), m::win32::event::event_kind::manual);

    e2 = std::move(e1);

    EXPECT_FALSE(e1.is_valid());
    EXPECT_EQ(e1.get_event_kind(), m::win32::event::event_kind::none);
    EXPECT_TRUE(e2.is_valid());
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::manual);
}

TEST(Win32Event, CombinedFlagsInitialSetManualReset)
{
    // Create an initially-set manual-reset event.
    auto flags = m::win32::create_event_flags::initial_set | m::win32::create_event_flags::manual_reset;
    auto e     = m::win32::event(flags);

    EXPECT_TRUE(e.is_valid());
    EXPECT_EQ(e.get_event_kind(), m::win32::event::event_kind::manual);

    // Because it was created initially signalled, WaitForSingleObject should return
    // immediately without blocking.
    auto const result = ::WaitForSingleObject(e.get(), 0);
    EXPECT_EQ(result, WAIT_OBJECT_0);
}

TEST(Win32Event, SetEventStateSetThenReset)
{
    // Manually-reset event starts unsignalled.
    auto e = m::win32::event(m::win32::create_event_flags::manual_reset);

    // Should not be signalled yet.
    EXPECT_EQ(::WaitForSingleObject(e.get(), 0), WAIT_TIMEOUT);

    e.set_event_state(m::win32::event::event_state::set);
    EXPECT_EQ(::WaitForSingleObject(e.get(), 0), WAIT_OBJECT_0);

    e.set_event_state(m::win32::event::event_state::reset);
    EXPECT_EQ(::WaitForSingleObject(e.get(), 0), WAIT_TIMEOUT);
}

TEST(Win32Event, AutoResetEventClearsAfterWait)
{
    // Auto-reset event created initially signalled.
    auto flags = m::win32::create_event_flags::initial_set; // no manual_reset bit
    auto e     = m::win32::event(flags);
    EXPECT_EQ(e.get_event_kind(), m::win32::event::event_kind::automatic);

    // First wait succeeds and atomically resets the event.
    EXPECT_EQ(::WaitForSingleObject(e.get(), 0), WAIT_OBJECT_0);

    // Second wait times out — event was auto-cleared.
    EXPECT_EQ(::WaitForSingleObject(e.get(), 0), WAIT_TIMEOUT);
}

TEST(Win32Event, SignalAndWaitAcrossThreads)
{
    auto e = m::win32::event(m::win32::create_event_flags::manual_reset);

    // Signal from a threadpool callback, then wait on the main thread.
    auto callback = [](PTP_CALLBACK_INSTANCE, PVOID pv, PTP_WORK) noexcept {
        ::SetEvent(static_cast<HANDLE>(*static_cast<m::win32::event*>(pv)));
    };

    PTP_WORK work = ::CreateThreadpoolWork(callback, &e, nullptr);
    ASSERT_NE(work, PTP_WORK{});

    ::SubmitThreadpoolWork(work);
    ::WaitForThreadpoolWorkCallbacks(work, FALSE);
    ::CloseThreadpoolWork(work);

    EXPECT_EQ(::WaitForSingleObject(e.get(), 0), WAIT_OBJECT_0);
}

TEST(Win32Event, ConstructNull)
{
    auto e1 = m::win32::event();

    EXPECT_EQ(static_cast<HANDLE>(e1), nullptr);
}

TEST(Win32Event, ConstructManualReset)
{
    auto e1 = m::win32::event();

    EXPECT_EQ(static_cast<HANDLE>(e1), nullptr);
    EXPECT_EQ(e1.get_event_kind(), m::win32::event::event_kind::none);

    // Not going to mess around with desired access for these tests. Probably
    // should but from a white box point of view, we know that there is no
    // specific handling of the desired_access in terms of mapping it.

    auto e2 = m::win32::event(m::win32::create_event_flags::manual_reset);

    EXPECT_NE(static_cast<HANDLE>(e2), nullptr);
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::manual);

    using std::swap;
    swap(e1, e2);

    EXPECT_NE(static_cast<HANDLE>(e1), nullptr);
    EXPECT_EQ(e1.get_event_kind(), m::win32::event::event_kind::manual);

    EXPECT_EQ(static_cast<HANDLE>(e2), nullptr);
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::none);

    auto e3 = m::win32::event(m::win32::event::event_kind::manual);
    EXPECT_NE(static_cast<HANDLE>(e3), nullptr);
    EXPECT_EQ(e3.get_event_kind(), m::win32::event::event_kind::manual);

    swap(e2, e3);

    EXPECT_NE(static_cast<HANDLE>(e2), nullptr);
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::manual);

    EXPECT_EQ(static_cast<HANDLE>(e3), nullptr);
    EXPECT_EQ(e3.get_event_kind(), m::win32::event::event_kind::none);

    e1.reset();
    EXPECT_EQ(static_cast<HANDLE>(e1), nullptr);
    EXPECT_EQ(e1.get_event_kind(), m::win32::event::event_kind::none);

    e2.reset();
    EXPECT_EQ(static_cast<HANDLE>(e2), nullptr);
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::none);

    e3.reset();
    EXPECT_EQ(static_cast<HANDLE>(e3), nullptr);
    EXPECT_EQ(e3.get_event_kind(), m::win32::event::event_kind::none);
}

TEST(Win32Event, ConstructAutoReset)
{
    auto e1 = m::win32::event();

    EXPECT_EQ(static_cast<HANDLE>(e1), nullptr);
    EXPECT_EQ(e1.get_event_kind(), m::win32::event::event_kind::none);

    // Not going to mess around with desired access for these tests. Probably
    // should but from a white box point of view, we know that there is no
    // specific handling of the desired_access in terms of mapping it.

    auto e2 = m::win32::event(m::win32::create_event_flags::zero);

    EXPECT_NE(static_cast<HANDLE>(e2), nullptr);
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::automatic);

    using std::swap;
    swap(e1, e2);

    EXPECT_NE(static_cast<HANDLE>(e1), nullptr);
    EXPECT_EQ(e1.get_event_kind(), m::win32::event::event_kind::automatic);

    EXPECT_EQ(static_cast<HANDLE>(e2), nullptr);
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::none);

    auto e3 = m::win32::event(m::win32::event::event_kind::automatic);
    EXPECT_NE(static_cast<HANDLE>(e3), nullptr);
    EXPECT_EQ(e3.get_event_kind(), m::win32::event::event_kind::automatic);

    swap(e2, e3);

    EXPECT_NE(static_cast<HANDLE>(e2), nullptr);
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::automatic);

    EXPECT_EQ(static_cast<HANDLE>(e3), nullptr);
    EXPECT_EQ(e3.get_event_kind(), m::win32::event::event_kind::none);

    e1.reset();
    EXPECT_EQ(static_cast<HANDLE>(e1), nullptr);
    EXPECT_EQ(e1.get_event_kind(), m::win32::event::event_kind::none);

    e2.reset();
    EXPECT_EQ(static_cast<HANDLE>(e2), nullptr);
    EXPECT_EQ(e2.get_event_kind(), m::win32::event::event_kind::none);

    e3.reset();
    EXPECT_EQ(static_cast<HANDLE>(e3), nullptr);
    EXPECT_EQ(e3.get_event_kind(), m::win32::event::event_kind::none);
}
