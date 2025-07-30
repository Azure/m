// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <print>
#include <span>
#include <string_view>

#include <m/memory/memory.h>

using namespace std::string_view_literals;

std::array const byte_array_abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
auto const              byte_span_abc = std::span(byte_array_abc);

TEST(MemoryUniqueSpan, ByteSpan1)
{
    auto const us = m::unique_span(byte_span_abc);

    EXPECT_EQ(us.size(), byte_span_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_span_abc, us));
}

TEST(MemoryUniqueSpan, ByteSpan2)
{
    auto const us = m::unique_span(byte_array_abc);

    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

namespace
{

    // owc == object_with_constructor
    //
    // Silly type just there to let us count number of various operators executing.
    //

    struct owc
    {
        owc(std::byte b): m_b(b) { ms_dccnt++; }
        owc(owc const& other): m_b(other.m_b) { ms_cccnt++; }
        owc(owc&& other): m_b(std::byte{0})
        {
            using std::swap;
            swap(m_b, other.m_b);
            ms_mvccnt++;
        }
        ~owc() { ms_dcnt++; }

        owc&
        operator=(owc const& other) noexcept
        {
            m_b = other.m_b;
            ms_aocnt++;
            return *this;
        }

        owc&
        operator=(owc&& other) noexcept
        {
            using std::swap;
            swap(m_b, other.m_b);
            ms_mvocnt++;
            return *this;
        }

        static inline std::atomic<uintmax_t> ms_dccnt{0};  // default constructor count
        static inline std::atomic<uintmax_t> ms_cccnt{0};  // copy constructor count
        static inline std::atomic<uintmax_t> ms_mvccnt{0}; // move constructor count
        static inline std::atomic<uintmax_t> ms_aocnt{0};  // assignment operator count
        static inline std::atomic<uintmax_t> ms_mvocnt{0}; // move operator count
        static inline std::atomic<uintmax_t> ms_dcnt{0};   // destructor count

        std::byte m_b;
    };

    constexpr bool
    operator==(owc const& l, owc const& r) noexcept
    {
        return l.m_b == r.m_b;
    }

    struct owc_stats
    {
        owc_stats() noexcept
        {
            m_dccnt  = owc::ms_dccnt;
            m_cccnt  = owc::ms_cccnt;
            m_mvccnt = owc::ms_mvccnt;
            m_aocnt  = owc::ms_aocnt;
            m_mvocnt = owc::ms_mvocnt;
            m_dcnt   = owc::ms_dcnt;
        }

        uintmax_t m_dccnt;
        uintmax_t m_cccnt;
        uintmax_t m_mvccnt;
        uintmax_t m_aocnt;
        uintmax_t m_mvocnt;
        uintmax_t m_dcnt;
    };

    struct owc_statdiff
    {
        constexpr owc_statdiff() noexcept:
            m_dccnt_delta{0},
            m_cccnt_delta{0},
            m_mvccnt_delta{0},
            m_aocnt_delta{0},
            m_mvocnt_delta{0},
            m_dcnt_delta{0}
        {}

        constexpr owc_statdiff(owc_statdiff const& other) noexcept:
            m_dccnt_delta{other.m_dccnt_delta},
            m_cccnt_delta{other.m_cccnt_delta},
            m_mvccnt_delta{other.m_mvccnt_delta},
            m_aocnt_delta{other.m_aocnt_delta},
            m_mvocnt_delta{other.m_mvocnt_delta},
            m_dcnt_delta{other.m_dcnt_delta}
        {}

        constexpr owc_statdiff(owc_stats const& first, owc_stats const& second) noexcept
        {
            m_dccnt_delta  = second.m_dccnt - first.m_dccnt;
            m_cccnt_delta  = second.m_cccnt - first.m_cccnt;
            m_mvccnt_delta = second.m_mvccnt - first.m_mvccnt;
            m_aocnt_delta  = second.m_aocnt - first.m_aocnt;
            m_mvocnt_delta = second.m_mvocnt - first.m_mvocnt;
            m_dcnt_delta   = second.m_dcnt - first.m_dcnt;
        }

        constexpr owc_statdiff&
        operator=(owc_statdiff const& other) noexcept
        {
            m_dccnt_delta  = other.m_dccnt_delta;
            m_cccnt_delta  = other.m_cccnt_delta;
            m_mvccnt_delta = other.m_mvccnt_delta;
            m_aocnt_delta  = other.m_aocnt_delta;
            m_mvocnt_delta = other.m_mvocnt_delta;
            m_dcnt_delta   = other.m_dcnt_delta;
            return *this;
        }

        intmax_t m_dccnt_delta;
        intmax_t m_cccnt_delta;
        intmax_t m_mvccnt_delta;
        intmax_t m_aocnt_delta;
        intmax_t m_mvocnt_delta;
        intmax_t m_dcnt_delta;
    };

    owc_statdiff
    operator-(owc_stats const& l, owc_stats const& r)
    {
        return owc_statdiff(r, l);
    }
} // namespace

TEST(MemoryUniqueSpan, CountOps1)
{
    owc_statdiff     diff1;
    owc_statdiff     diff2;
    owc_statdiff     diff3;
    owc_statdiff     diff4;
    owc_stats        stats1;
    std::array const owc_array{owc(std::byte{'a'}), owc(std::byte{'b'}), owc(std::byte{'c'})};
    owc_stats        stats2;
    auto             us = m::unique_span(owc_array);
    owc_stats        stats3;
    auto const       s = us.span();
    owc_stats        stats4;

    EXPECT_TRUE(std::ranges::equal(owc_array, us));
    EXPECT_TRUE(std::ranges::equal(owc_array, s));

    diff1 = stats2 - stats1;

    std::println("diff1: dccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
                 diff1.m_dccnt_delta,
                 diff1.m_cccnt_delta,
                 diff1.m_mvccnt_delta,
                 diff1.m_aocnt_delta,
                 diff1.m_mvocnt_delta,
                 diff1.m_dcnt_delta);

    EXPECT_EQ(diff1.m_dccnt_delta, 3);
    EXPECT_EQ(diff1.m_cccnt_delta, 0);
    EXPECT_EQ(diff1.m_mvccnt_delta, 0);
    EXPECT_EQ(diff1.m_aocnt_delta, 0);
    EXPECT_EQ(diff1.m_mvocnt_delta, 0);
    EXPECT_EQ(diff1.m_dcnt_delta, 0);

    diff2 = stats3 - stats2;

    std::println("diff2: dccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
                 diff2.m_dccnt_delta,
                 diff2.m_cccnt_delta,
                 diff2.m_mvccnt_delta,
                 diff2.m_aocnt_delta,
                 diff2.m_mvocnt_delta,
                 diff2.m_dcnt_delta);

    EXPECT_EQ(diff2.m_dccnt_delta, 0);
    EXPECT_EQ(diff2.m_cccnt_delta, 3);
    EXPECT_EQ(diff2.m_mvccnt_delta, 0);
    EXPECT_EQ(diff2.m_aocnt_delta, 0);
    EXPECT_EQ(diff2.m_mvocnt_delta, 0);
    EXPECT_EQ(diff2.m_dcnt_delta, 0);

    diff3 = stats4 - stats3;

    std::println("diff3: dccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
                 diff3.m_dccnt_delta,
                 diff3.m_cccnt_delta,
                 diff3.m_mvccnt_delta,
                 diff3.m_aocnt_delta,
                 diff3.m_mvocnt_delta,
                 diff3.m_dcnt_delta);

    EXPECT_EQ(diff3.m_dccnt_delta, 0);
    EXPECT_EQ(diff3.m_cccnt_delta, 0);
    EXPECT_EQ(diff3.m_mvccnt_delta, 0);
    EXPECT_EQ(diff3.m_aocnt_delta, 0);
    EXPECT_EQ(diff3.m_mvocnt_delta, 0);
    EXPECT_EQ(diff3.m_dcnt_delta, 0);

    us.reset();

    owc_stats stats5;
    diff4 = stats5 - stats4;

    std::println("diff4: dccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
                 diff4.m_dccnt_delta,
                 diff4.m_cccnt_delta,
                 diff4.m_mvccnt_delta,
                 diff4.m_aocnt_delta,
                 diff4.m_mvocnt_delta,
                 diff4.m_dcnt_delta);

    EXPECT_EQ(diff4.m_dccnt_delta, 0);
    EXPECT_EQ(diff4.m_cccnt_delta, 0);
    EXPECT_EQ(diff4.m_mvccnt_delta, 0);
    EXPECT_EQ(diff4.m_aocnt_delta, 0);
    EXPECT_EQ(diff4.m_mvocnt_delta, 0);
    EXPECT_EQ(diff4.m_dcnt_delta, 3);

    EXPECT_EQ(us.size(), 0);
}
