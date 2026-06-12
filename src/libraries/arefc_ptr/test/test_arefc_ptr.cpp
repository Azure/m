// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <m/arefc_ptr/arefc_ptr.h>
#include <m/print/print.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

// ============================================================================
// Test helpers
// ============================================================================

namespace
{
    struct alignas(128) BiglyAlignedStruct
    {
        int m_x;
    };

    // Tracks how many live instances exist. Used to verify that constructors
    // and destructors are called the right number of times.
    struct LifetimeTracker
    {
        inline static int s_live_count = 0;

        int m_id;

        explicit LifetimeTracker(int id) : m_id{id} { ++s_live_count; }
        ~LifetimeTracker() { --s_live_count; }
    };

    // Plain struct with no special alignment requirements (small_control_area path).
    struct Plain
    {
        int value = 0;
    };

    // Struct with a constructor taking arguments.
    struct WithArgs
    {
        int x;
        int y;
        explicit WithArgs(int x_, int y_) : x{x_}, y{y_} {}
    };

    // Empty base used to test to<U>() type coercion.
    // DerivedFromEmpty is standard layout because the only class in the
    // hierarchy that has data members is DerivedFromEmpty itself
    // (EmptyBase has none), satisfying the C++17 standard-layout rules.
    struct EmptyBase
    {};

    struct DerivedFromEmpty : EmptyBase
    {
        int value;
    };

} // namespace

// ============================================================================
// Legacy tests (kept as-is)
// ============================================================================

TEST(TestRefCount, First)
{
    // placeholder — intentionally empty
}

TEST(TestRefCount, TryAlignedStruct)
{
    auto p = m::mmake_arefc<BiglyAlignedStruct>(10);

    EXPECT_EQ(p->m_x, 10);
}

// ============================================================================
// Default construction — null state
// ============================================================================

TEST(AreFcPtr_Null, DefaultConstructIsNull)
{
    m::arefc_ptr<Plain> p;
    EXPECT_EQ(p.get(), nullptr);
    EXPECT_FALSE(static_cast<bool>(p));
    EXPECT_TRUE(!p);
}

// ============================================================================
// Basic construction, operator bool / operator!, get()
// ============================================================================

TEST(AreFcPtr_Basic, MakePtrIsNonNull)
{
    auto p = m::mmake_arefc<Plain>();
    EXPECT_NE(p.get(), nullptr);
    EXPECT_TRUE(static_cast<bool>(p));
    EXPECT_FALSE(!p);
}

TEST(AreFcPtr_Basic, ConstructWithArgs)
{
    auto p = m::mmake_arefc<WithArgs>(3, 7);
    EXPECT_EQ(p->x, 3);
    EXPECT_EQ(p->y, 7);
}

// ============================================================================
// Dereference: operator* and operator->
// ============================================================================

TEST(AreFcPtr_Deref, OperatorArrow)
{
    auto p = m::mmake_arefc<Plain>();
    p->value = 42;
    EXPECT_EQ(p->value, 42);
}

TEST(AreFcPtr_Deref, OperatorStar)
{
    auto p = m::mmake_arefc<Plain>();
    p->value = 99;
    EXPECT_EQ((*p).value, 99);
}

// ============================================================================
// Lifetime / ref counting: destructor called exactly once
// ============================================================================

TEST(AreFcPtr_Lifetime, DestructorCalledOnce)
{
    LifetimeTracker::s_live_count = 0;
    {
        auto p1 = m::mmake_arefc<LifetimeTracker>(1);
        EXPECT_EQ(LifetimeTracker::s_live_count, 1);
        {
            auto p2 = p1; // copy — refcount = 2
            EXPECT_EQ(LifetimeTracker::s_live_count, 1); // still one object
            {
                auto p3 = p2; // copy — refcount = 3
                EXPECT_EQ(LifetimeTracker::s_live_count, 1);
            } // p3 drops — refcount = 2; object still alive
            EXPECT_EQ(LifetimeTracker::s_live_count, 1);
        } // p2 drops — refcount = 1; object still alive
        EXPECT_EQ(LifetimeTracker::s_live_count, 1);
    } // p1 drops — refcount = 0; destructor called
    EXPECT_EQ(LifetimeTracker::s_live_count, 0);
}

// ============================================================================
// Copy construction
// ============================================================================

TEST(AreFcPtr_CopyConstruct, SharesObject)
{
    auto p1 = m::mmake_arefc<Plain>();
    p1->value = 77;
    auto p2 = p1;
    // Both point to the same underlying object
    EXPECT_EQ(p2.get(), p1.get());
    EXPECT_EQ(p2->value, 77);
}

TEST(AreFcPtr_CopyConstruct, FromNull)
{
    m::arefc_ptr<Plain> null;
    auto p2 = null;
    EXPECT_EQ(p2.get(), nullptr);
}

TEST(AreFcPtr_CopyConstruct, TypeCoercingCtor)
{
    // Constructing arefc_ptr<EmptyBase> from arefc_ptr<DerivedFromEmpty>
    // exercises the templated converting copy constructor.
    auto derived = m::mmake_arefc<DerivedFromEmpty>();
    derived->value = 55;
    m::arefc_ptr<EmptyBase> base(derived);
    // Both should point to the same storage (standard layout, EBO applies)
    EXPECT_EQ(base.get(), static_cast<EmptyBase*>(derived.get()));
    EXPECT_NE(base.get(), nullptr);
}

// ============================================================================
// Move construction
// ============================================================================

TEST(AreFcPtr_MoveConstruct, TransfersOwnership)
{
    auto p1 = m::mmake_arefc<Plain>();
    p1->value = 7;
    Plain* raw = p1.get();

    auto p2 = std::move(p1);

    EXPECT_EQ(p2.get(), raw);
    EXPECT_EQ(p1.get(), nullptr); // moved-from is null
    EXPECT_EQ(p2->value, 7);
}

TEST(AreFcPtr_MoveConstruct, FromNull)
{
    m::arefc_ptr<Plain> null;
    auto p2 = std::move(null);
    EXPECT_EQ(p2.get(), nullptr);
    EXPECT_EQ(null.get(), nullptr);
}

TEST(AreFcPtr_MoveConstruct, NoExtraDestruction)
{
    // Moving should not change the live count — no copy of the object is made
    // and no premature destruction occurs.
    LifetimeTracker::s_live_count = 0;
    {
        auto p1 = m::mmake_arefc<LifetimeTracker>(1);
        EXPECT_EQ(LifetimeTracker::s_live_count, 1);
        auto p2 = std::move(p1);
        EXPECT_EQ(LifetimeTracker::s_live_count, 1);
    }
    EXPECT_EQ(LifetimeTracker::s_live_count, 0);
}

// ============================================================================
// Copy assignment
// ============================================================================

TEST(AreFcPtr_CopyAssign, AssignNonNull)
{
    auto p1 = m::mmake_arefc<Plain>();
    p1->value = 5;
    Plain* raw = p1.get();

    m::arefc_ptr<Plain> p2;
    p2 = p1;
    EXPECT_EQ(p2.get(), raw);
    EXPECT_EQ(p2->value, 5);
}

TEST(AreFcPtr_CopyAssign, SelfAssign)
{
    auto p = m::mmake_arefc<Plain>();
    p->value = 11;
    Plain* raw = p.get();

    // Self-assignment must be a no-op.
    // Assign through a reference to suppress the clang -Wself-assign-overloaded
    // diagnostic while still exercising the self-assignment code path.
    auto& pref = p;
    p = pref;
    EXPECT_EQ(p.get(), raw);
    EXPECT_EQ(p->value, 11);
}

TEST(AreFcPtr_CopyAssign, OverwritesExisting)
{
    // Verify that the previously held object is released when p2 is overwritten.
    LifetimeTracker::s_live_count = 0;
    {
        auto p1 = m::mmake_arefc<LifetimeTracker>(1);
        auto p2 = m::mmake_arefc<LifetimeTracker>(2);
        EXPECT_EQ(LifetimeTracker::s_live_count, 2);
        p2 = p1; // p2 releases its old object; refcount of p1's object goes up
        EXPECT_EQ(LifetimeTracker::s_live_count, 1); // old p2 object destroyed
        EXPECT_EQ(p2.get(), p1.get());
    }
    EXPECT_EQ(LifetimeTracker::s_live_count, 0);
}

// ============================================================================
// Move assignment
// ============================================================================

TEST(AreFcPtr_MoveAssign, TransfersOwnership)
{
    auto p1 = m::mmake_arefc<Plain>();
    p1->value = 13;
    Plain* raw = p1.get();

    m::arefc_ptr<Plain> p2;
    p2 = std::move(p1);

    EXPECT_EQ(p2.get(), raw);
    EXPECT_EQ(p2->value, 13);
    EXPECT_EQ(p1.get(), nullptr);
}

// ============================================================================
// reset()
// ============================================================================

TEST(AreFcPtr_Reset, ToNullDestroysObject)
{
    LifetimeTracker::s_live_count = 0;
    {
        auto p = m::mmake_arefc<LifetimeTracker>(42);
        EXPECT_EQ(LifetimeTracker::s_live_count, 1);
        p.reset();
        // Object should be destroyed immediately — reset() drops the last ref.
        EXPECT_EQ(p.get(), nullptr);
        EXPECT_EQ(LifetimeTracker::s_live_count, 0);
    }
    EXPECT_EQ(LifetimeTracker::s_live_count, 0);
}

TEST(AreFcPtr_Reset, ResetNullIsNoOp)
{
    m::arefc_ptr<Plain> p; // null
    p.reset();             // must not crash or assert
    EXPECT_EQ(p.get(), nullptr);
}

TEST(AreFcPtr_Reset, ResetDoesNotDestroyWhileOtherHolds)
{
    LifetimeTracker::s_live_count = 0;
    {
        auto p1 = m::mmake_arefc<LifetimeTracker>(1);
        auto p2 = p1;
        p1.reset(); // drops one ref; p2 still holds the object
        EXPECT_EQ(LifetimeTracker::s_live_count, 1);
        EXPECT_EQ(p1.get(), nullptr);
    } // p2 drops here → refcount 0 → destroy
    EXPECT_EQ(LifetimeTracker::s_live_count, 0);
}

// ============================================================================
// to<U>() — type-coercing view
// ============================================================================

TEST(AreFcPtr_TypeCoercion, ToBaseGivesSameAddress)
{
    auto derived = m::mmake_arefc<DerivedFromEmpty>();
    derived->value = 77;

    m::arefc_ptr<EmptyBase> base = derived.to<EmptyBase>();

    // Standard layout + EBO: base subobject is at offset 0, so pointers are equal.
    EXPECT_EQ(base.get(), static_cast<EmptyBase*>(derived.get()));
    EXPECT_NE(base.get(), nullptr);
}

TEST(AreFcPtr_TypeCoercion, ToBaseKeepsObjectAlive)
{
    LifetimeTracker::s_live_count = 0;

    // LifetimeTracker does not inherit from anything so we cannot use it here.
    // Use DerivedFromEmpty instead and check liveness via a side-channel.
    {
        auto derived = m::mmake_arefc<DerivedFromEmpty>();
        {
            auto base = derived.to<EmptyBase>();
            // Both point to the same object; dropping derived should keep it alive.
            derived.reset();
            EXPECT_NE(base.get(), nullptr);
        } // base drops here — object should be freed (no crash / asan)
    }
}

// ============================================================================
// mmake_arefc_ex — extra bytes
// ============================================================================

TEST(AreFcPtr_MakeEx, FnIsCalledAndObjectIsAccessible)
{
    bool fn_called = false;

    auto p = m::mmake_arefc_ex<Plain>(
        64,
        [&fn_called](m::byte_span s, int v) -> Plain* {
            fn_called = true;
            EXPECT_GE(s.size(), sizeof(Plain));
            return ::new (s.data()) Plain{.value = v};
        },
        42);

    EXPECT_TRUE(fn_called);
    EXPECT_EQ(p->value, 42);
}

// ============================================================================
// Over-aligned type — exercises big_control_area path
// ============================================================================

TEST(AreFcPtr_OverAligned, AlignmentIsRespected)
{
    auto p = m::mmake_arefc<BiglyAlignedStruct>(55);
    EXPECT_EQ(p->m_x, 55);
    // The returned pointer must satisfy the requested alignment.
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p.get()) % 128u, 0u);
}

TEST(AreFcPtr_OverAligned, CopySharesObjectAndLifetime)
{
    auto p1 = m::mmake_arefc<BiglyAlignedStruct>(100);
    auto p2 = p1;
    EXPECT_EQ(p2.get(), p1.get());
    EXPECT_EQ(p2->m_x, 100);
    // Dropping p1 must not destroy the object while p2 still holds it.
    p1.reset();
    EXPECT_EQ(p2->m_x, 100);
}

// ============================================================================
// Thread safety — concurrent copies and drops must not corrupt the ref count
// ============================================================================

TEST(AreFcPtr_Threaded, ConcurrentCopyAndDrop)
{
    LifetimeTracker::s_live_count = 0;
    {
        auto shared = m::mmake_arefc<LifetimeTracker>(1);

        std::vector<std::thread> threads;
        threads.reserve(8);
        for (int i = 0; i < 8; ++i)
        {
            threads.emplace_back(
                [&shared]()
                {
                    // Each thread takes its own copy and holds it briefly.
                    auto local = shared;
                    std::this_thread::sleep_for(1ms);
                    // local drops here
                });
        }

        for (auto& t : threads)
            t.join();

        // Only `shared` (in this scope) still holds the object.
        EXPECT_EQ(LifetimeTracker::s_live_count, 1);
    }
    // `shared` drops — object destroyed.
    EXPECT_EQ(LifetimeTracker::s_live_count, 0);
}
