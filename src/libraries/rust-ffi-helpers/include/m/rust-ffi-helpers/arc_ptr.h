// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <utility>

namespace m::rust
{
    // Example TraitsT for the arc_ptr<> class template
    struct arc_ptr_traits
    {
        // using raw_arc_ptr = SomeArcPtrOrOther*;
        // static void addref(raw_arc_ptr p) noexcept { rust_type_specific_addref_function(p); }
        // static void release(raw_arc_ptr p) noexcept { rust_type_specific_release_function(p); }
    };

    // Marker, value not used, for constructor where the raw pointer is
    // adopted into the arc_ptr without having its refcount incremented.
    //
    struct adopt_arc_ptr_t
    {
        explicit adopt_arc_ptr_t() = default;
    };
    constexpr inline adopt_arc_ptr_t adopt_arc_ptr{};

    /// <summary>
    ///
    /// The arc_ptr<> class template is intended to implement a very simple RAII
    /// discipline around a pointer where a Rust FFI returned a
    /// Arc::into_raw(Arc<T>::new()), and there are functions exposed that call
    /// Arc::increment_strong_count() and Arc::decrement_strong_count() on the
    /// pointer passed in.
    ///
    /// The behavior of the type is controlled via the type parameter,
    /// ArcPtrTraitsT, which should be a struct with the following public members:
    ///
    ///     raw_arc_ptr:
    ///         A type alias for the pointer type returned from the Rust FFI
    ///         interfaces.
    ///
    ///     static void addref(raw_arc_ptr p) noexcept;
    ///         static void function called with a raw_arc_ptr value to increment
    ///         the reference count.
    ///
    ///     static void release(raw_arc_ptr p) noexcept;
    ///         static void function called with a raw_arc_ptr value to
    ///         decrement the reference count.
    ///
    ///
    /// It is preferable to "load" the raw_arc_ptr into the arc_ptr<> object via
    /// the constructor. The constructor also takes an (unused) adopt_arc_ptr_t
    /// marker parameter which is there simply to ensure that the caller is aware
    /// that the arc_ptr is taking ownership of the reference count that the
    /// raw_arc_ptr already presumably has.
    ///
    /// A .get_raw_ptr() function is also provided for cases where the Rust API
    /// requires a pointer to the pointer because the return value is a status
    /// code or other indicator of success / failure of the function. (The
    /// common Rust idiom of Result<T, E> cannot be passed across the FFI
    /// boundary, so implementers are limited to some error code numbers or
    /// bool for error case indications.)
    /// 
    /// No special mechanisms to increase or decrease the reference count are
    /// provided - since you are defining the type including providing the 
    /// names of the functions to increment the reference counts directly,
    /// you can do that yourself if necessary.
    /// 
    /// </summary>
    /// <typeparam name="ArcPtrTraitsT"></typeparam>
    template <typename ArcPtrTraitsT>
    class arc_ptr
    {
    public:
        using arc_ptr_traits = ArcPtrTraitsT;
        using raw_arc_ptr    = typename arc_ptr_traits::raw_arc_ptr;

        constexpr arc_ptr() noexcept: m_p{} {}

        // There is no non-adopt constructor, but the adopt marker is
        // specifically present so that callers are aware that the
        // constructor does not addref the pointer.
        constexpr explicit arc_ptr(raw_arc_ptr p, adopt_arc_ptr_t) noexcept: m_p(p) {}

        constexpr arc_ptr(arc_ptr&& other) noexcept: m_p{} 
        {
            using std::swap;
            swap(m_p, other.m_p);
        }

        arc_ptr(arc_ptr const& other) noexcept: m_p{}
        {
            if (auto p = other.m_p; p != nullptr)
            {
                addref(p);
                m_p = p;
            }
        }

        ~arc_ptr() { reset(); }

        arc_ptr&
        operator=(arc_ptr&& other) noexcept
        {
            using std::swap;
            swap(m_p, other.m_p);
            return *this;
        }

        arc_ptr&
        operator=(arc_ptr const& other) noexcept
        {
            // always addref "other" before releasing ourselves in case
            // this is assignment to ourselves
            auto otherp = other.m_p;
            if (otherp != nullptr)
                addref(otherp);

            if (auto oldp = std::exchange(m_p, otherp); oldp != nullptr)
                release(oldp);

            return *this;
        }

        constexpr raw_arc_ptr
        get() const noexcept
        {
            return m_p;
        }

        // Use judiciously!
        raw_arc_ptr*
        get_raw_ptr() noexcept
        {
            return &m_p;
        }

        void
        reset() noexcept
        {
            if (auto p = std::exchange(m_p, nullptr); p != nullptr)
                release(p);
        }

        friend constexpr void
        swap(arc_ptr& left, arc_ptr& right) noexcept
        {
            using std::swap;
            swap(left.m_p, right.m_p);
        }

    private:
        static void
        addref(raw_arc_ptr p) noexcept
        {
            arc_ptr_traits::addref(p);
        }

        static void
        release(raw_arc_ptr p) noexcept
        {
            arc_ptr_traits::release(p);
        }

        raw_arc_ptr m_p;
    };

} // namespace m::rust
