// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <m/error_handling/macros.h>
#include <m/math/math.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>

namespace m
{
    // A destroyer_fn is a function pointer that takes a void* which is the
    // pointer to the object that is to be destroyed. Note that the destroyer
    // does *not* deallocate the memory, it just tears down the state of
    // the object.
    //
    using destroyer_fn = void (*)(void*);

    namespace refcount_impl
    {
        struct control_area
        {
            std::atomic<uint64_t> m_refcount;
            destroyer_fn          m_destroyer;
        };

        // Example layout of how the object _would look_ if the object were
        // laid out and constructed in the normal fashion.
        //
        // This allows us to use offsetof() to determine the offset of
        // m_object if we were to use this approach, without necessarily using
        // it. For most Ts, offsetof(aggregate<T>, m_object) ==
        // sizeof(control_area), but if T has some high alignment
        // requirements, this could not be the case.
        //
        template <typename T>
            requires(std::is_standard_layout_v<T>)
        struct aggregate
        {
            control_area m_control_area;
            T            m_object;

            static void
            destroyer(void* ptr)
            {
                std::destroy_n(reinterpret_cast<T*>(ptr), 1);
            }
        };
    } // namespace refcount_impl

    template <typename T>
        requires(std::is_standard_layout_v<T>)
    class arc_ptr;

    template <typename T, typename... Args>
        requires(std::is_standard_layout_v<T>)
    arc_ptr<T>
    make_arc(Args&&... args);

    template <typename T, typename Fn, typename... Args>
        requires(std::is_standard_layout_v<T> &&
                 std::invocable<Fn, std::span<std::byte>, Args && ...>)
    arc_ptr<T>
    make_arc_ex(std::size_t extra_bytes_required, destroyer_fn destroyer, Fn&& fn, Args&&... args);

    /// <summary>
    /// The arc_ptr type is pointer to a reference counted object, it is
    /// much like the Rust std::Arc<T> type.
    ///
    /// It is intended to have an interface akin to std::shared_ptr<> but at
    /// the same time, trimmed down for support of these atomic refcounted
    /// objects only. They maintain pointers to the control_area as well as
    /// to the type-ful pointers so that the usual pointer functions like *
    /// and -> can be applied and safe type coercion used while non-invasive
    /// lifetime control is used to manage the lifetime of the object under
    /// management.
    /// </summary>
    /// <typeparam name="T"></typeparam>
    template <typename T>
        requires(std::is_standard_layout_v<T>)
    class arc_ptr // ala shared_ptr
    {
    public:
        arc_ptr() = default;

        arc_ptr(arc_ptr const& other): m_ptr{other.addref()} {}

        template <typename U>
            requires(std::is_standard_layout_v<U> && std::convertible_to<U*, T*>)
        arc_ptr(arc_ptr<U> const& other): m_ptr{other.addref()}
        {}

        arc_ptr(arc_ptr&& other) { m_ptr.exchange(other.m_ptr, std::memory_order_acq_rel); }

        ~arc_ptr() { reset(); }

        arc_ptr&
        operator=(arc_ptr& other)
        {
            if (this != &other)
            {
                reset();
                m_ptr.store(other.addref(), std::memory_order_release);
            }

            return *this;
        }

        template <typename U>
            requires(std::is_standard_layout_v<U> && std::convertible_to<U*, T*>)
        arc_ptr&
        operator=(arc_ptr<U> const& other)
        {
            if (this != &other)
            {
                reset();
                m_ptr.store(other.addref(), std::memory_order_release);
            }

            return *this;
        }

        arc_ptr&
        operator=(arc_ptr&& other) noexcept
        {
            auto thisvalue = m_ptr.load(std::memory_order_acquire);
            auto othervalue = other.m_ptr.load(std::memory_order_acquire);
            m_ptr.store(othervalue, std::memory_order_release);
            other.m_ptr.store(thisvalue, std::memory_order_release);
            return *this;
        }

        explicit
        operator bool() const
        {
            return m_ptr.load(std::memory_order_acquire) != nullptr;
        }

        bool
        operator!() const
        {
            return m_ptr.load(std::memory_order_acquire) == nullptr;
        }

        void
        reset()
        {
            auto const ptr = m_ptr.exchange(nullptr, std::memory_order_acq_rel);
            decrement_ref(ptr);
        }

        T&
        operator*() const
        {
            return *m_ptr;
        }

        T*
        operator->() const
        {
            return m_ptr;
        }

        T*
        get() const
        {
            return m_ptr;
        }

        template <typename U>
            requires(std::is_standard_layout_v<U> && std::convertible_to<T*, U*>)
        arc_ptr<U>
        to() const
        {
            arc_ptr<U> v(*this);
            return v;
        }

        bool
        compare_exchange_strong(arc_ptr& expected, arc_ptr const& desired)
        {
            // The trick here is to not mess up the reference counting!
            //
            // it would seem trivial to just "pass through" the m_ptr values but that's
            // only part of the story.
            //

            T* e = expected.m_ptr.load(std::memory_order_acquire);
            T* d = desired.m_ptr.load(std::memory_order_acquire);

            T* old_e = e; // save a copy so we don't have to re-load

            if (m_ptr.compare_exchange_strong(e, d, std::memory_order_acq_rel))
            {
                // the swap succeeded. this means that the arc_ptr that d refers
                // to needs to be addref'd, the one that e refers to needs
                // to be released.
                if (d != e)
                {
                    increment_ref(d);
                    decrement_ref(e);
                }

                expected.m_ptr.store(d, std::memory_order_release);

                return true;
            }

            // In the other case, `e` now has the value that was actually in m_ptr
            // so we will increment it, while we decrement the value of expected that
            // came in which we stashed in old_e.
            if (e != old_e)
            {
                increment_ref(e);
                decrement_ref(old_e);
                expected.m_ptr.store(e, std::memory_order_release);
            }

            return false;
        }

    private:
        constexpr arc_ptr(T* ptr) noexcept: m_ptr(ptr) {}

        constexpr static inline auto control_area_offset =
            offsetof(refcount_impl::aggregate<T>, m_object);

        refcount_impl::control_area*
        get_control_area() const
        {
            auto const ptr = m_ptr.load(std::memory_order_acquire);
            if (!ptr)
                return nullptr;

            return get_control_area(ptr);
        }

        static refcount_impl::control_area*
        get_control_area(T* ptr)
        {
            auto const uptr    = reinterpret_cast<uintptr_t>(ptr);
            auto const ca_uptr = uptr - control_area_offset;
            return reinterpret_cast<refcount_impl::control_area*>(ca_uptr);
        }

        T*
        addref() const
        {
            auto const ptr = m_ptr.load(std::memory_order_acquire);
            increment_ref(ptr);
            return ptr;
        }

        static void
        increment_ref(T* ptr)
        {
            if (ptr == nullptr)
                return;

            auto const ca = get_control_area(ptr);
            ca->m_refcount.fetch_add(1, std::memory_order_relaxed);
        }

        static void
        decrement_ref(T* ptr)
        {
            if (ptr == nullptr)
                return;

            auto const ca = get_control_area(ptr);
            if (ca->m_refcount.fetch_sub(1, std::memory_order_relaxed) == 1)
            {
                std::atomic_thread_fence(std::memory_order_acquire);

                // If the type's destructor is trivial it may not have a destructor and so
                // it may be entirely omitted.
                auto const destroyer = ca->m_destroyer;
                if (destroyer)
                {
                    auto const objptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ca) +
                                                                control_area_offset);

                    (destroyer)(objptr);
                }

                std::unique_ptr<std::byte[]> uniqptr(reinterpret_cast<std::byte*>(ca));
            }
        }

        std::atomic<T*> m_ptr{};

#if 0
        template <typename U>
            requires(std::is_standard_layout_v<U>)
        friend class arc_ptr<U>;
#endif // 0

        template <typename T, typename... Args>
            requires(std::is_standard_layout_v<T>)
        friend arc_ptr<T>
        make_arc(Args&&... args);

        template <typename T, typename Fn, typename... Args>
            requires(std::is_standard_layout_v<T> &&
                     std::invocable<Fn, std::span<std::byte>, Args && ...>)
        friend arc_ptr<T>
        make_arc_ex(std::size_t  extra_bytes_required,
                    destroyer_fn destroyer,
                    Fn&&         fn,
                    Args&&... args);
    };

    template <typename T, typename Fn, typename... Args>
        requires(std::is_standard_layout_v<T> &&
                 std::invocable<Fn, std::span<std::byte>, Args && ...>)
    arc_ptr<T>
    make_arc_ex(std::size_t extra_bytes_required, destroyer_fn destroyer, Fn&& fn, Args&&... args)
    {
        using aggregate_type = refcount_impl::aggregate<T>;

        auto const bytes_required =
            m::math::add(offsetof(aggregate_type, m_object), extra_bytes_required, std::size_t{});

        auto       uniqptr = std::make_unique<std::byte[]>(bytes_required);
        auto const aggptr  = reinterpret_cast<aggregate_type*>(uniqptr.get());
        aggptr->m_control_area.m_refcount.store(1, std::memory_order_relaxed);
        aggptr->m_control_area.m_destroyer = destroyer;

        auto const ptr = std::invoke<Fn, std::span<std::byte>, Args...>(
            std::forward<Fn>(fn),
            std::span(reinterpret_cast<std::byte*>(&aggptr->m_object), extra_bytes_required),
            std::forward<Args>(args)...);

        uniqptr.release();
        arc_ptr<T> retval(ptr);
        return retval;
    }

#if 0
    template <typename T, typename... Args>
    arc_ptr<T>
    make_arc(Args&&... args)
    {
        using aggregate_type = refcount_impl::aggregate<T>;

        auto const                   bytes_required = sizeof(aggregate_type);
        std::unique_ptr<std::byte[]> uniqptr        = std::make_unique<std::byte[]>(bytes_required);
        auto const                   aggptr = reinterpret_cast<aggregate_type*>(uniqptr.get());
        aggptr->m_control_area.m_refcount.store(1, std::memory_order_relaxed);
        aggptr->m_control_area.m_destroyer = &aggregate_type::destroyer;

        auto const ptr = ::new (&aggptr->m_object) T(std::forward<Args>(args)...);
        uniqptr.release();
        arc_ptr<T> retval(ptr);
        return retval;
    }
#else

    template <typename T, typename... Args>
    arc_ptr<T>
    make_arc(Args&&... args)
    {
        return make_arc_ex<T>(
            sizeof(T),
            &refcount_impl::aggregate<T>::destroyer,
            [](std::span<std::byte> s, Args&&... args1) {
                M_INTERNAL_ERROR_CHECK(s.size() >= sizeof(T));
                return ::new (s.data()) T(std::forward<Args>(args1)...);
            },
            std::forward<Args>(args)...);
    }
#endif

} // namespace m
