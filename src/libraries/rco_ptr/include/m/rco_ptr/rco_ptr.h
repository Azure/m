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
    class rco_ptr;

    template <typename T, typename... Args>
    rco_ptr<T>
    make_rco(Args&&... args);

    template <typename T, typename Fn, typename... Args>
        requires(std::invocable<Fn, std::span<std::byte>, Args && ...>)
    rco_ptr<T>
    make_rco_ex(std::size_t extra_bytes_required, destroyer_fn destroyer, Fn&& fn, Args&&... args);

    /// <summary>
    /// The rco_ptr type is pointer to a reference counted object, it is
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
    class rco_ptr // ala shared_ptr
    {
    public:
        rco_ptr() = default;

        // This handles copy construction as well as casting
        template <typename U>
            requires(std::convertible_to<U*, T*>)
        rco_ptr(rco_ptr<U> const& other)
        {
            // control area and ptr left unassigned until after refcount increment
            if (other.m_control_area)
            {
                other.m_control_area->m_refcount.fetch_inc(1, std::memory_order_relaxed);
                m_control_area = other.m_control_area;
                m_ptr          = other.m_ptr;
            }
        }

        rco_ptr(rco_ptr&& other)
        {
            using std::swap;
            swap(m_control_area, other.m_control_area);
            swap(m_ptr, other.m_ptr);
        }

        ~rco_ptr() { reset(); }

        template <typename U>
            requires(std::convertible_to<U*, T*>)
        rco_ptr&
        operator=(rco_ptr<U> const& other)
        {
            if (this != &other)
            {
                reset();

                // control area and ptr left unassigned until after refcount increment
                if (other.m_control_area)
                {
                    other.m_control_area->m_refcount.fetch_inc(1, std::memory_order_relaxed);
                    m_control_area = other.m_control_area;
                    m_ptr          = other.m_ptr;
                }
            }

            return *this;
        }

        explicit
        operator bool() const
        {
            return m_control_area != nullptr;
        }

        bool
        operator!() const
        {
            return m_control_area == nullptr;
        }

        void
        reset()
        {
            if (auto const control_area = std::exchange(m_control_area, nullptr);
                control_area != nullptr)
            {
                if (control_area->m_refcount.fetch_sub(1, std::memory_order_relaxed) == 1)
                {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    auto const objptr =
                        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(control_area) +
                                                offsetof(refcount_impl::aggregate<T>, m_object));

                    // If the type's destructor is trivial it may not have a destructor and so
                    // it may be entirely omitted.
                    auto const destroyer = control_area->m_destroyer;
                    if (destroyer)
                        (destroyer)(objptr);

                    std::unique_ptr<std::byte[]> uniqptr(
                        reinterpret_cast<std::byte*>(control_area));
                }
                m_ptr = nullptr;
            }
        }

        T&
        operator*() const
        {
            return m_ptr;
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

        template <typename ToT>
            requires(std::convertible_to<T*, ToT*>)
        rco_ptr<ToT>
        to() const
        {
            rco_ptr<ToT> v(*this);
            return v;
        }

    private:
        constexpr rco_ptr(refcount_impl::control_area* control_area, T* ptr) noexcept:
            m_control_area(control_area), m_ptr(ptr)
        {}

        refcount_impl::control_area* m_control_area{};
        T*                           m_ptr{};

        template <typename T, typename... Args>
        friend rco_ptr<T>
        make_rco(Args&&... args);

        template <typename T, typename Fn, typename... Args>
            requires(std::invocable<Fn, std::span<std::byte>, Args && ...>)
        friend rco_ptr<T>
        make_rco_ex(std::size_t  extra_bytes_required,
                    destroyer_fn destroyer,
                    Fn&&         fn,
                    Args&&... args);
    };

    template <typename T, typename Fn, typename... Args>
        requires(std::invocable<Fn, std::span<std::byte>, Args && ...>)
    rco_ptr<T>
    make_rco_ex(std::size_t extra_bytes_required, destroyer_fn destroyer, Fn&& fn, Args&&... args)
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
        rco_ptr<T> retval(&aggptr->m_control_area, ptr);
        return retval;
    }

#if 0
    template <typename T, typename... Args>
    rco_ptr<T>
    make_rco(Args&&... args)
    {
        using aggregate_type = refcount_impl::aggregate<T>;

        auto const                   bytes_required = sizeof(aggregate_type);
        std::unique_ptr<std::byte[]> uniqptr        = std::make_unique<std::byte[]>(bytes_required);
        auto const                   aggptr = reinterpret_cast<aggregate_type*>(uniqptr.get());
        aggptr->m_control_area.m_refcount.store(1, std::memory_order_relaxed);
        aggptr->m_control_area.m_destroyer = &aggregate_type::destroyer;

        auto const ptr = ::new (&aggptr->m_object) T(std::forward<Args>(args)...);
        uniqptr.release();
        rco_ptr<T> retval(&aggptr->m_control_area, ptr);
        return retval;
    }
#else

    template <typename T, typename... Args>
    rco_ptr<T>
    make_rco(Args&&... args)
    {
        return make_rco_ex<T>(
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
