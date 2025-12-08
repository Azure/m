// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <Windows.h>

#include <m/win32/event.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

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
