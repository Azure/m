// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <span>
#include <string_view>

#include <m/memory/unique_span.h>
#include <m/print/print.h>

using namespace std::string_view_literals;

std::array const byte_array_abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
auto const       byte_span_abc = std::span(byte_array_abc);

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

TEST(MemoryUniqueSpan, CopyConstruction)
{
    auto const us = m::unique_span(byte_array_abc);
    m::unique_span<std::byte> us2(us);
    EXPECT_EQ(us.size(), us2.size());
    EXPECT_TRUE(std::ranges::equal(us, us2));
}

TEST(MemoryUniqueSpan, AssignmentOperator)
{
    auto const                us = m::unique_span(byte_array_abc);
    m::unique_span<std::byte> us2;

    EXPECT_EQ(us2.size(), 0);

    us2 = us;

    EXPECT_EQ(us.size(), us2.size());
    EXPECT_TRUE(std::ranges::equal(us, us2));
}

TEST(MemoryUniqueSpan, VerifyOperators)
{
    auto const us = m::unique_span(byte_array_abc);

    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));

    EXPECT_EQ(us[0], std::byte{'a'});
    EXPECT_EQ(us[1], std::byte{'b'});
    EXPECT_EQ(us[2], std::byte{'c'});

    auto t = us.span();
    EXPECT_EQ(t.size(), us.size());
    EXPECT_EQ(reinterpret_cast<void*>(const_cast<std::byte*>(t.data())),
              reinterpret_cast<void*>(const_cast<std::byte*>(us.data())));
}

TEST(MemoryUniqueSpan, VerifySpanCast)
{
    auto const us = m::unique_span(byte_array_abc);

    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));

    auto t = us.span();
    EXPECT_EQ(t.size(), us.size());
    EXPECT_EQ(reinterpret_cast<void*>(const_cast<std::byte*>(t.data())),
              reinterpret_cast<void*>(const_cast<std::byte*>(us.data())));

    auto u = static_cast<std::span<std::byte const>>(us);

    //
    // The explicit call to span() and the cast to the same type
    // should produce the same results.
    //
    EXPECT_EQ(u.data(), t.data());
    EXPECT_EQ(u.size(), t.size());
}

//
// Ensure that the various operators are set up correctly
// so that the usual return value optimizations etc. work
// as expected.
//

namespace
{
    m::unique_span<std::byte const>
    naive_byte_span_maker_1()
    {
        return m::unique_span(byte_array_abc);
    }

    decltype(auto)
    naive_byte_span_maker_2()
    {
        return m::unique_span(byte_array_abc);
    }

    m::unique_span<std::byte const>
    naive_byte_span_passer_1a()
    {
        return naive_byte_span_maker_1();
    }

    m::unique_span<std::byte const>
    naive_byte_span_passer_1b()
    {
        return naive_byte_span_maker_2();
    }

    decltype(auto)
    naive_byte_span_passer_2a()
    {
        return naive_byte_span_maker_1();
    }

    decltype(auto)
    naive_byte_span_passer_2b()
    {
        return naive_byte_span_maker_2();
    }
} // namespace

TEST(MemoryUniqueSpan, TestNaiveMaker1)
{
    auto const us = naive_byte_span_maker_1();
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaiveMaker2)
{
    auto const us = naive_byte_span_maker_2();
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaivePasser1a)
{
    auto const us = naive_byte_span_passer_1a();
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaivePasser1b)
{
    auto const us = naive_byte_span_passer_1b();
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaivePasser2a)
{
    auto const us = naive_byte_span_passer_2a();
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaivePasser2b)
{
    auto const us = naive_byte_span_passer_2b();
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaiveMaker1_alt)
{
    auto const us{naive_byte_span_maker_1()};
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaiveMaker2_alt)
{
    auto const us{naive_byte_span_maker_2()};
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaivePasser1a_alt)
{
    auto const us{naive_byte_span_passer_1a()};
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaivePasser1b_alt)
{
    auto const us{naive_byte_span_passer_1b()};
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaivePasser2a_alt)
{
    auto const us{naive_byte_span_passer_2a()};
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, TestNaivePasser2b_alt)
{
    auto const us{naive_byte_span_passer_2b()};
    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));
}

TEST(MemoryUniqueSpan, VerifyAt)
{
    auto const us = m::unique_span(byte_array_abc);

    EXPECT_EQ(us.size(), byte_array_abc.size());
    EXPECT_TRUE(std::ranges::equal(byte_array_abc, us));

    EXPECT_EQ(us.at(0), std::byte{'a'});
    EXPECT_EQ(us.at(1), std::byte{'b'});
    EXPECT_EQ(us.at(2), std::byte{'c'});

    EXPECT_THROW(us.at(3), std::out_of_range);
}

TEST(MemoryUniqueSpan, VerifySwap1)
{
    auto us1 = m::unique_span(byte_span_abc);
    auto us2 = m::unique_span(byte_array_abc);

    EXPECT_EQ(us1.size(), us2.size());

    auto const p1 = us1.data();
    auto const p2 = us2.data();

    EXPECT_EQ(us1.data(), p1);
    EXPECT_EQ(us2.data(), p2);

    using std::swap;

    swap(us1, us2);

    EXPECT_EQ(us1.size(), us2.size());

    EXPECT_EQ(us1.data(), p2);
    EXPECT_EQ(us2.data(), p1);
}

std::array const byte_array_abcd{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'}};
auto const       byte_span_abcd = std::span(byte_array_abcd);

TEST(MemoryUniqueSpan, VerifySwap2)
{
    auto us1 = m::unique_span(byte_span_abc);
    auto us2 = m::unique_span(byte_span_abcd);

    EXPECT_EQ(us1.size(), byte_span_abc.size());
    EXPECT_EQ(us2.size(), byte_span_abcd.size());

    auto const p1 = us1.data();
    auto const p2 = us2.data();

    EXPECT_EQ(us1.data(), p1);
    EXPECT_EQ(us2.data(), p2);

    using std::swap;

    swap(us1, us2);

    EXPECT_EQ(us1.size(), byte_span_abcd.size());
    EXPECT_EQ(us2.size(), byte_span_abc.size());

    EXPECT_EQ(us1.data(), p2);
    EXPECT_EQ(us2.data(), p1);
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

    m::println("diff1: dccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
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

    m::println("diff2: dccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
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

    m::println("diff3: dccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
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

    m::println("diff4: dccnt: {} cccnt: {} mvccnt: {} aocnt: {} mvocnt: {} dcnt: {}",
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
