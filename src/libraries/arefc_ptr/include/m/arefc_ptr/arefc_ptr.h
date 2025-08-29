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
#include <m/memory/memory.h>
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

    namespace arefc_ptr_impl
    {
        struct control_area
        {
            void
            increment_ref();

            bool
            decrement_ref();

            std::atomic<uint64_t> m_refcount;
            destroyer_fn          m_destroyer;
        };

        /// <summary>
        /// aggregate is a struct that allows us to find a size for an
        /// object that would give us the correct alignment for T.
        ///
        /// if alignof(T) is greater than the default alignment,
        /// the actual control_area at run time will not be at the
        /// beginning of the object. It will be at address_of(m_object)
        /// -sizeof(control_area).
        ///
        /// This is because T may derive from U which may be of lesser
        /// alignment and we still need to be able to find the
        /// control area at run time. So the control area must
        /// always immediately precede the object, even if
        /// `aggregate<>` would have put padding between them.
        /// </summary>
        /// <typeparam name="T"></typeparam>
        template <typename T>
            requires(std::is_standard_layout_v<T>)
        struct aggregate
        {
            control_area m_control_area;
            T            m_object;

            control_area*
            get_control_area() const;
 
            static void
            destroyer(void* ptr)
            {
                std::destroy_n(reinterpret_cast<T*>(ptr), 1);
            }

            inline static std::span<std::byte>
                allocate(std::size_t bytes)
            {
                if constexpr (alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                {
                    return std::span(new std::byte[bytes], bytes);
                }
                else
                {
                    auto const ptr = m::aligned_alloc(std::align_val_t{alignof(aggregate)}, bytes);
                    return std::span(ptr, bytes);
                }
            }

            inline static void
                deallocate(std::span<std::byte> s)
            {
                if constexpr (alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                {
                    delete[] s.data();
                }
                else
                {
                    m::aligned_free(s);
                }

            }
        };

        /// <summary>
        /// Returns a pointer to the control area for *this.
        /// 
        /// Depending on alignof(T), may not be at offsetof(aggregate, m_control_area).
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <returns></returns>
        template <typename T>
            requires(std::is_standard_layout_v<T>)
        control_area*
        aggregate<T>::get_control_area() const
        {
            if constexpr (alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__)
            {
                return const_cast<control_area*>(&m_control_area);
            }
            else
            {
                // just some dumb assertions but they are kind of assumed
                // in the below pointer math
                static_assert(offsetof(aggregate, m_control_area) == 0);
                static_assert(offsetof(aggregate, m_object) > offsetof(aggregate, m_control_area));
                auto const cobjptr = &m_object;
                auto const uobjptr = reinterpret_cast<uintptr_t>(cobjptr);
                auto const uca     = uobjptr - sizeof(control_area);
                return reinterpret_cast<control_area*>(uca);
            }
        }
        /// <summary>
        /// Returns the control_area* for any given pointer.
        /// 
        /// Note that it is unnecessary to use the aggregate to
        /// find the control area. The control area is always immediately
        /// before the object.
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="ptr"></param>
        /// <returns></returns>
        template <typename T>
        control_area*
        get_control_area(T* ptr)
        {
            auto const uptr = reinterpret_cast<uintptr_t>(ptr);
            auto const uca  = uptr - sizeof(control_area);
            return reinterpret_cast<control_area*>(uca);
        }

    } // namespace arefc_ptr_impl

    template <typename T>
        requires(std::is_standard_layout_v<T>)
    class arefc_ptr;

    template <typename T, typename... Args>
        requires(std::is_standard_layout_v<T>)
    arefc_ptr<T>
    mmake_arefc(Args&&... args);

    template <typename T, typename Fn, typename... Args>
        requires(std::is_standard_layout_v<T> &&
                 std::invocable<Fn, std::span<std::byte>, Args && ...>)
    arefc_ptr<T>
    mmake_arefc_ex(std::size_t  extra_bytes_required,
                   destroyer_fn destroyer,
                   Fn&&         fn,
                   Args&&... args);

    /// <summary>
    /// The arefc_ptr type is pointer to a reference counted object, it is
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
    class arefc_ptr // ala shared_ptr
    {
    public:
        arefc_ptr() = default;

        arefc_ptr(arefc_ptr const& other): m_ptr{other.addref()} {}

        template <typename U>
            requires(std::is_standard_layout_v<U> && std::convertible_to<U*, T*>)
        arefc_ptr(arefc_ptr<U> const& other): m_ptr{other.addref()}
        {}

        arefc_ptr(arefc_ptr&& other) noexcept
        {
            auto const thisptr  = get();
            auto const otherptr = other.get();
            put(otherptr);
            other.put(thisptr);
        }

        ~arefc_ptr() { reset(); }

        arefc_ptr&
        operator=(arefc_ptr& other)
        {
            if (this != &other)
            {
                reset();
                put(other.addref());
            }

            return *this;
        }

        template <typename U>
            requires(std::is_standard_layout_v<U> && std::convertible_to<U*, T*>)
        arefc_ptr&
        operator=(arefc_ptr<U> const& other)
        {
            if (this != &other)
            {
                reset();
                put(other.addref());
            }

            return *this;
        }

        arefc_ptr&
        operator=(arefc_ptr&& other) noexcept
        {
            auto thisvalue  = get();
            auto othervalue = other.get();
            put(othervalue);
            other.put(thisvalue);
            return *this;
        }

        explicit
        operator bool() const
        {
            return get() != nullptr;
        }

        bool
        operator!() const
        {
            return get() == nullptr;
        }

        void
        reset(T* ptr_in = nullptr)
        {
            auto const ptr = m_ptr.exchange(increment_ref(ptr_in), std::memory_order_acq_rel);
            decrement_ref(ptr);
        }

        T&
        operator*() const
        {
            return *get();
        }

        T*
        operator->() const
        {
            return get();
        }

        T*
        get() const
        {
            return m_ptr.load(std::memory_order_acquire);
        }

        template <typename U>
            requires(std::is_standard_layout_v<U> && std::convertible_to<T*, U*>)
        arefc_ptr<U>
        to() const
        {
            arefc_ptr<U> v(*this);
            return v;
        }

        bool
        compare_exchange_strong(arefc_ptr& expected, arefc_ptr const& desired)
        {
            // The trick here is to not mess up the reference counting!
            //
            // it would seem trivial to just "pass through" the m_ptr values but that's
            // only part of the story.
            //

            T* e = expected.get();
            T* d = desired.get();

            T* old_e = e; // save a copy so we don't have to re-load

            if (m_ptr.compare_exchange_strong(e, d, std::memory_order_acq_rel))
            {
                // the swap succeeded. this means that the arefc_ptr that d refers
                // to needs to be addref'd, the one that e refers to needs
                // to be released.
                if (d != e)
                {
                    increment_ref(d);
                    decrement_ref(e);
                }

                expected.put(d);

                return true;
            }

            // In the other case, `e` now has the value that was actually in m_ptr
            // so we will increment it, while we decrement the value of expected that
            // came in which we stashed in old_e.
            if (e != old_e)
            {
                increment_ref(e);
                decrement_ref(old_e);
                expected.put(e);
            }

            return false;
        }

    private:
        constexpr arefc_ptr(T* ptr) noexcept: m_ptr(ptr) {}

        constexpr static inline auto control_area_offset =
            offsetof(arefc_ptr_impl::aggregate<T>, m_object);

        arefc_ptr_impl::control_area*
        get_control_area() const
        {
            auto const ptr = get();
            if (ptr == nullptr)
                return nullptr;

            return get_control_area(ptr);
        }

        static arefc_ptr_impl::control_area*
        get_control_area(T* ptr)
        {
            auto const uptr    = reinterpret_cast<uintptr_t>(ptr);
            auto const ca_uptr = uptr - control_area_offset;
            return reinterpret_cast<arefc_ptr_impl::control_area*>(ca_uptr);
        }

        T*
        addref() const
        {
            return increment_ref(get());
        }

        /// <summary>
        /// Increments the reference count associated with the object
        /// passed in `ptr`, if not nullptr. If nullptr, takes no action.
        ///
        /// In either case, returns `ptr`.
        /// </summary>
        /// <param name="ptr"></param>
        /// <returns></returns>
        static T*
        increment_ref(T* ptr)
        {
            if (ptr != nullptr)
                get_control_area(ptr)->increment_ref();

            return ptr;
        }

        static void
        decrement_ref(T* ptr)
        {
            if (ptr == nullptr)
                return;

            if (auto const ca = get_control_area(ptr); ca->decrement_ref())
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

        /// <summary>
        /// Internal use only! Puts a value directly into m_ptr to avoid typing and
        /// re-typing `.store(<x>, std::memory_order_release)`.
        /// </summary>
        void
        put(T* ptr)
        {
            m_ptr.store(ptr, std::memory_order_release);
        }

        std::atomic<T*> m_ptr{};

        template <typename T1, typename... Args>
            requires(std::is_standard_layout_v<T1>)
        friend arefc_ptr<T1>
        mmake_arefc(Args&&... args);

        template <typename T1, typename Fn, typename... Args>
            requires(std::is_standard_layout_v<T1> &&
                     std::invocable<Fn, std::span<std::byte>, Args && ...>)
        friend arefc_ptr<T1>
        mmake_arefc_ex(std::size_t  extra_bytes_required,
                       destroyer_fn destroyer,
                       Fn&&         fn,
                       Args&&... args);
    };

    template <typename T, typename Fn, typename... Args>
        requires(std::is_standard_layout_v<T> &&
                 std::invocable<Fn, std::span<std::byte>, Args && ...>)
    arefc_ptr<T>
    mmake_arefc_ex(std::size_t  extra_bytes_required,
                   destroyer_fn destroyer,
                   Fn&&         fn,
                   Args&&... args)
    {
        using aggregate_type = arefc_ptr_impl::aggregate<T>;

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
        arefc_ptr<T> retval(ptr);
        return retval;
    }

    template <typename T, typename... Args>
        requires(std::is_standard_layout_v<T>)
    arefc_ptr<T>
    mmake_arefc(Args&&... args)
    {
        return mmake_arefc_ex<T>(
            sizeof(T),
            &arefc_ptr_impl::aggregate<T>::destroyer,
            [](std::span<std::byte> s, Args&&... args1) {
                M_INTERNAL_ERROR_CHECK(s.size() >= sizeof(T));
                return ::new (s.data()) T(std::forward<Args>(args1)...);
            },
            std::forward<Args>(args)...);
    }
} // namespace m
