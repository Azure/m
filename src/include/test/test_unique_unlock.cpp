// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>

#include <m/utility/unique_unlock.h>

TEST(UniqueUnlock, UnlockBasic1)
{
    std::mutex m;

    auto ul = std::unique_lock(m);

    EXPECT_TRUE(ul.owns_lock());

    auto uul = m::unique_unlock(ul);

    EXPECT_FALSE(ul.owns_lock());
}

TEST(UniqueUnlock, UnlockBasic2)
{
    std::mutex m;

    auto ul = std::unique_lock(m);

    EXPECT_TRUE(ul.owns_lock());

    {
        auto uul = m::unique_unlock(ul);

        EXPECT_FALSE(ul.owns_lock());
    }

    EXPECT_TRUE(ul.owns_lock());
}

TEST(UniqueUnlock, UnlockUnlocked)
{
    std::mutex m;

    auto ul = std::unique_lock(m);
    ul.unlock();

    EXPECT_THROW(auto uul = m::unique_unlock(ul), std::runtime_error);
}

TEST(UniqueUnlock, TryReset)
{
    std::mutex m;

    auto ul = std::unique_lock(m);

    EXPECT_TRUE(ul.owns_lock());

    {
        auto uul = m::unique_unlock(ul);

        EXPECT_FALSE(ul.owns_lock());

        EXPECT_TRUE(uul.unlocked_lock());

        uul.reset();

        EXPECT_TRUE(ul.owns_lock());
        EXPECT_FALSE(uul.unlocked_lock());
    }

    EXPECT_TRUE(ul.owns_lock());
}

TEST(UniqueUnlock, TryRelease)
{
    std::mutex m;

    auto ul = std::unique_lock(m);

    EXPECT_TRUE(ul.owns_lock());

    {
        auto uul = m::unique_unlock(ul);

        EXPECT_FALSE(ul.owns_lock());
        EXPECT_TRUE(uul.unlocked_lock());

        uul.release();

        EXPECT_FALSE(ul.owns_lock());

        // Since the unique_unlock no longer manages the lock,
        // it don't claim the management responsibiliites.
        EXPECT_FALSE(uul.unlocked_lock());
    }

    //
    // Since the unique unlock disowned the lock,
    // it didn't re-acquire when it went out of scope.
    //
    EXPECT_FALSE(ul.owns_lock());
}

TEST(UniqueUnlock, VerifyOtherMemberFunctions)
{
    std::mutex m;

    auto ul = std::unique_lock(m);

    EXPECT_TRUE(ul.owns_lock());

    {
        auto uul = m::unique_unlock(ul);

        EXPECT_FALSE(ul.owns_lock());
        EXPECT_TRUE(uul.unlocked_lock());

        EXPECT_EQ(uul.mutex(), ul.mutex());
        EXPECT_EQ(uul.underlying_lock(), &ul);

        EXPECT_EQ(static_cast<bool>(uul), uul.unlocked_lock());
    }

}
