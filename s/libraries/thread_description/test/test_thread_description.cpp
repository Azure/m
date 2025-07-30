// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <latch>
#include <span>
#include <string>
#include <string_view>

#include <m/debugging/dbg_format.h>
#include <m/thread_description/thread_description.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(ThreadDescription, BasicThreadDescription)
{
    m::thread_description td(L"This is a thread description!"sv);
}

TEST(ThreadDescription, Scopes1)
{
    m::thread_description td(L"xyzzy"sv);
    {
        m::thread_description td2(L"abc123"sv);

        PWSTR pwsz{};

        if (SUCCEEDED(::GetThreadDescription(GetCurrentThread(), &pwsz)))
        {
            std::wstring d(pwsz);

            if (d.size() != 0)
                EXPECT_EQ(d, L"abc123"s);
        }

        ::LocalFree(pwsz);
    }
    PWSTR pwsz{};

    if (SUCCEEDED(::GetThreadDescription(GetCurrentThread(), &pwsz)))
    {
        std::wstring d(pwsz);

        if (d.size() != 0)
            EXPECT_EQ(d, L"xyzzy"s);
    }

    ::LocalFree(pwsz);
}
