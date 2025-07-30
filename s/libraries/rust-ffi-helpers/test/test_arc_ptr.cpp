// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <m/rust-ffi-helpers/arc_ptr.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

struct P
{
    unsigned long x;
};

void
AddRefP(P* p) noexcept
{
    p->x++;
}

void
ReleaseP(P* p) noexcept
{
    p->x--;
}

struct test_arc_traits_1
{
    using raw_arc_ptr = P*;

    static void
    addref(P* p) noexcept
    {
        AddRefP(p);
    }

    static void
    release(P* p) noexcept
    {
        ReleaseP(p);
    }
};

using test_arc = m::rust::arc_ptr<test_arc_traits_1>;

TEST(ArcPtr, VerifyReleaseOnce)
{ 
    P p{100};
    EXPECT_EQ(p.x, 100);
    {
        test_arc arc(&p, m::rust::adopt_arc_ptr);
    }
    EXPECT_EQ(p.x, 99);
}

TEST(ArcPtr, VerifyCopyConstructor)
{
    P p{100};
    EXPECT_EQ(p.x, 100);
    {
        EXPECT_EQ(p.x, 100);
        test_arc arc(&p, m::rust::adopt_arc_ptr);
        EXPECT_EQ(p.x, 100);
        test_arc arc2(arc);
        EXPECT_EQ(p.x, 101);
    }
    EXPECT_EQ(p.x, 99);
}

TEST(ArcPtr, VerifyAssignment)
{
    P p{100};
    EXPECT_EQ(p.x, 100);
    {
        EXPECT_EQ(p.x, 100);
        test_arc arc1(&p, m::rust::adopt_arc_ptr);
        EXPECT_EQ(p.x, 100);
        test_arc arc2;
        EXPECT_EQ(p.x, 100);
        arc2 = arc1;
        EXPECT_EQ(p.x, 101);
    }
    EXPECT_EQ(p.x, 99);
}

TEST(ArcPtr, VerifySelfAssignment)
{
    P p{100};
    EXPECT_EQ(p.x, 100);
    {
        EXPECT_EQ(p.x, 100);
        test_arc arc1(&p, m::rust::adopt_arc_ptr);
        EXPECT_EQ(p.x, 100);
        test_arc arc2;
        EXPECT_EQ(p.x, 100);
        arc2 = arc1;
        EXPECT_EQ(p.x, 101);

        // Here's the actual self-assignment!
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
        arc2 = arc2;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
        EXPECT_EQ(p.x, 101);
    }
    EXPECT_EQ(p.x, 99);
}

//
// This test came to being when the original implementation of
// swap() wasn't being used so the library default implementation was
// which exacerbated a problem but on consideration, it should have
// just worked, so added a test to validate.
//
TEST(ArcPtr, TryMoveSwap)
{
    P p{100};
    P q{200};

    test_arc outer_arc(&p, m::rust::adopt_arc_ptr);

    EXPECT_EQ(p.x, 100);
    EXPECT_EQ(q.x, 200);

    EXPECT_EQ(outer_arc.get(), &p);

    {
        test_arc inner_arc(&q, m::rust::adopt_arc_ptr);

        EXPECT_EQ(p.x, 100);
        EXPECT_EQ(q.x, 200);

        EXPECT_EQ(outer_arc.get(), &p);
        EXPECT_EQ(inner_arc.get(), &q);

        {
            test_arc temp = std::move(outer_arc);

            EXPECT_EQ(p.x, 100);
            EXPECT_EQ(q.x, 200);

            outer_arc = std::move(inner_arc);

            EXPECT_EQ(p.x, 100);
            EXPECT_EQ(q.x, 200);

            inner_arc = std::move(temp);
        }

        EXPECT_EQ(p.x, 100);
        EXPECT_EQ(q.x, 200);

        EXPECT_EQ(outer_arc.get(), &q);
        EXPECT_EQ(inner_arc.get(), &p);
    }

    EXPECT_EQ(p.x, 99);
    EXPECT_EQ(q.x, 200);

    // Note that outer_arc now governs Q not P due to the swap above
    EXPECT_EQ(outer_arc.get(), &q);
}



TEST(ArcPtr, VerifySwap)
{
    P p{100};
    P q{200};

    test_arc outer_arc(&p, m::rust::adopt_arc_ptr);

    EXPECT_EQ(p.x, 100);
    EXPECT_EQ(q.x, 200);

    EXPECT_EQ(outer_arc.get(), &p);

    {
        test_arc inner_arc(&q, m::rust::adopt_arc_ptr);

        EXPECT_EQ(p.x, 100);
        EXPECT_EQ(q.x, 200);

        EXPECT_EQ(outer_arc.get(), &p);
        EXPECT_EQ(inner_arc.get(), &q);

        using std::swap;
        swap(outer_arc, inner_arc);

        EXPECT_EQ(p.x, 100);
        EXPECT_EQ(q.x, 200);

        EXPECT_EQ(outer_arc.get(), &q);
        EXPECT_EQ(inner_arc.get(), &p);
    }

    EXPECT_EQ(p.x, 99);
    EXPECT_EQ(q.x, 200);

    // Note that outer_arc now governs Q not P due to the swap above
    EXPECT_EQ(outer_arc.get(), &q);
}
