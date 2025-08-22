// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <span>
#include <string_view>

#include <m/memory/memory.h>
#include <m/print/print.h>

using namespace std::string_view_literals;

std::array const byte_array_abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
// auto const       byte_span_abc = std::span(byte_array_abc);

TEST(MemoryRawAllocator, ByteSpanUnconstructed)
{
    m::raw_array_allocator<std::byte> ra(byte_array_abc.size());
    EXPECT_EQ(ra.size(), byte_array_abc.size());
    EXPECT_EQ(ra.constructed(), 0);
}

TEST(MemoryRawAllocator, ByteSpanConstructed)
{
    m::raw_array_allocator<std::byte> ra(byte_array_abc.size());
    ra.default_construct();
    EXPECT_EQ(ra.size(), byte_array_abc.size());
    EXPECT_EQ(ra.constructed(), ra.size());
}

namespace
{
    // owc == object_with_constructor
    //
    // Silly type just there to let us count number of various operators executing.
    //

    struct owc
    {
        owc(): m_b{} { ms_dccnt++; }
        owc(std::byte b): m_b(b) { ms_bccnt++; }
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
        static inline std::atomic<uintmax_t> ms_bccnt{0};  // byte constructor count
        static inline std::atomic<uintmax_t> ms_cccnt{0};  // copy constructor count
        static inline std::atomic<uintmax_t> ms_mvccnt{0}; // move constructor count
        static inline std::atomic<uintmax_t> ms_aocnt{0};  // assignment operator count
        static inline std::atomic<uintmax_t> ms_mvocnt{0}; // move operator count
        static inline std::atomic<uintmax_t> ms_dcnt{0};   // destructor count

        std::byte m_b;
    };

    #if 0
    constexpr bool
    operator==(owc const& l, owc const& r) noexcept
    {
        return l.m_b == r.m_b;
    }
    #endif

    struct owc_stats
    {
        owc_stats() noexcept
        {
            m_dccnt  = owc::ms_dccnt;
            m_bccnt  = owc::ms_bccnt;
            m_cccnt  = owc::ms_cccnt;
            m_mvccnt = owc::ms_mvccnt;
            m_aocnt  = owc::ms_aocnt;
            m_mvocnt = owc::ms_mvocnt;
            m_dcnt   = owc::ms_dcnt;
        }

        uintmax_t m_dccnt;
        uintmax_t m_bccnt;
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
            m_bccnt_delta{0},
            m_cccnt_delta{0},
            m_mvccnt_delta{0},
            m_aocnt_delta{0},
            m_mvocnt_delta{0},
            m_dcnt_delta{0}
        {}

        constexpr owc_statdiff(owc_statdiff const& other) noexcept:
            m_dccnt_delta{other.m_dccnt_delta},
            m_bccnt_delta{other.m_bccnt_delta},
            m_cccnt_delta{other.m_cccnt_delta},
            m_mvccnt_delta{other.m_mvccnt_delta},
            m_aocnt_delta{other.m_aocnt_delta},
            m_mvocnt_delta{other.m_mvocnt_delta},
            m_dcnt_delta{other.m_dcnt_delta}
        {}

        constexpr owc_statdiff(owc_stats const& first, owc_stats const& second) noexcept
        {
            m_dccnt_delta  = second.m_dccnt - first.m_dccnt;
            m_bccnt_delta  = second.m_bccnt - first.m_bccnt;
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
            m_bccnt_delta  = other.m_bccnt_delta;
            m_cccnt_delta  = other.m_cccnt_delta;
            m_mvccnt_delta = other.m_mvccnt_delta;
            m_aocnt_delta  = other.m_aocnt_delta;
            m_mvocnt_delta = other.m_mvocnt_delta;
            m_dcnt_delta   = other.m_dcnt_delta;
            return *this;
        }

        intmax_t m_dccnt_delta;
        intmax_t m_bccnt_delta;
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

std::array const owc_array{owc(std::byte{'a'}), owc(std::byte{'b'}), owc(std::byte{'c'})};

TEST(MemoryRawAllocator, CountOpsUnconstructed)
{
    owc_stats             stats1;
    m::raw_array_allocator<owc> ra(owc_array.size());
    owc_stats             stats2;
    EXPECT_EQ(ra.size(), owc_array.size());
    EXPECT_EQ(ra.constructed(), 0);

    //
    // No construction, so no deltas expected
    //
    auto const diff1 = stats2 - stats1;

    m::println("diff1: dccnt: {} bccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
                 diff1.m_dccnt_delta,
                 diff1.m_bccnt_delta,
                 diff1.m_cccnt_delta,
                 diff1.m_mvccnt_delta,
                 diff1.m_aocnt_delta,
                 diff1.m_mvocnt_delta,
                 diff1.m_dcnt_delta);

    EXPECT_EQ(diff1.m_dccnt_delta, 0);
    EXPECT_EQ(diff1.m_bccnt_delta, 0);
    EXPECT_EQ(diff1.m_cccnt_delta, 0);
    EXPECT_EQ(diff1.m_mvccnt_delta, 0);
    EXPECT_EQ(diff1.m_aocnt_delta, 0);
    EXPECT_EQ(diff1.m_mvocnt_delta, 0);
    EXPECT_EQ(diff1.m_dcnt_delta, 0);
}

TEST(MemoryRawAllocator, CountOpsConstructed)
{
    owc_stats             stats1;
    m::raw_array_allocator<owc> ra(owc_array.size());
    owc_stats             stats2;
    EXPECT_EQ(ra.size(), owc_array.size());
    EXPECT_EQ(ra.constructed(), 0);
    ra.default_construct();
    owc_stats stats3;
    ra.reset();
    owc_stats stats4;

    //
    // No construction, so no deltas expected
    //
    auto const diff1 = stats2 - stats1;

    m::println("diff1: dccnt: {} bccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
                 diff1.m_dccnt_delta,
                 diff1.m_bccnt_delta,
                 diff1.m_cccnt_delta,
                 diff1.m_mvccnt_delta,
                 diff1.m_aocnt_delta,
                 diff1.m_mvocnt_delta,
                 diff1.m_dcnt_delta);

    EXPECT_EQ(diff1.m_dccnt_delta, 0);
    EXPECT_EQ(diff1.m_bccnt_delta, 0);
    EXPECT_EQ(diff1.m_cccnt_delta, 0);
    EXPECT_EQ(diff1.m_mvccnt_delta, 0);
    EXPECT_EQ(diff1.m_aocnt_delta, 0);
    EXPECT_EQ(diff1.m_mvocnt_delta, 0);
    EXPECT_EQ(diff1.m_dcnt_delta, 0);

    auto const diff2 = stats3 - stats2;

    m::println("diff2: dccnt: {} bccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
                 diff2.m_dccnt_delta,
                 diff2.m_bccnt_delta,
                 diff2.m_cccnt_delta,
                 diff2.m_mvccnt_delta,
                 diff2.m_aocnt_delta,
                 diff2.m_mvocnt_delta,
                 diff2.m_dcnt_delta);

    EXPECT_EQ(diff2.m_dccnt_delta, 3);
    EXPECT_EQ(diff2.m_bccnt_delta, 0);
    EXPECT_EQ(diff2.m_cccnt_delta, 0);
    EXPECT_EQ(diff2.m_mvccnt_delta, 0);
    EXPECT_EQ(diff2.m_aocnt_delta, 0);
    EXPECT_EQ(diff2.m_mvocnt_delta, 0);
    EXPECT_EQ(diff2.m_dcnt_delta, 0);

    auto const diff3 = stats4 - stats3;

    m::println("diff3: dccnt: {} bccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
                 diff3.m_dccnt_delta,
                 diff3.m_bccnt_delta,
                 diff3.m_cccnt_delta,
                 diff3.m_mvccnt_delta,
                 diff3.m_aocnt_delta,
                 diff3.m_mvocnt_delta,
                 diff3.m_dcnt_delta);

    EXPECT_EQ(diff3.m_dccnt_delta, 0);
    EXPECT_EQ(diff3.m_bccnt_delta, 0);
    EXPECT_EQ(diff3.m_cccnt_delta, 0);
    EXPECT_EQ(diff3.m_mvccnt_delta, 0);
    EXPECT_EQ(diff3.m_aocnt_delta, 0);
    EXPECT_EQ(diff3.m_mvocnt_delta, 0);
    EXPECT_EQ(diff3.m_dcnt_delta, 3);
}
