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
#include <tuple>
#include <type_traits>
#include <utility>

#include <m/error_handling/macros.h>
#include <m/math/math.h>
#include <m/memory/memory.h>
#include <m/memory/raw_allocation_helper.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>

namespace m
{
    // A destroyer_fn_t is a function pointer that takes a T* which is the
    // pointer to the object that is to be destroyed. Note that the destroyer
    // does *not* deallocate the memory, it just tears down the state of
    // the object.
    //
    template <typename T>
    using destroyer_fn_t = void (*)(T*);

    inline static constexpr std::size_t arefc_max_alignment = 512;

    template <typename T>
    concept arefc_ptr_requirements =
        (std::is_standard_layout_v<T> && (alignof(T) <= arefc_max_alignment));

    template <typename T>
        requires(arefc_ptr_requirements<T>)
    class arefc_ptr;

    namespace arefc_ptr_impl
    {
        template <typename T>
            requires(std::is_standard_layout_v<T>)
        class aggregate;

        template <typename T>
        struct aggregate_deleter
        {
            constexpr void
            operator()(aggregate<T>* ptr) const noexcept
            {
                aggregate<T>::on_delete(ptr);
            }
        };

        template <typename T>
        class base_control_area
        {
        public:
            using value_type        = T;
            using destroyer_fn_type = destroyer_fn_t<value_type>;

            base_control_area()                         = default;
            base_control_area(base_control_area const&) = delete;
            base_control_area(base_control_area&&)      = delete;
            ~base_control_area()                        = default;

            void
            operator=(base_control_area const&) = delete;
            void
            operator=(base_control_area&&) = delete;

            void
            initialize(uint64_t refcount, destroyer_fn_type destroyer_fn) noexcept
            {
                m_refcount.store(refcount, std::memory_order_relaxed);
                m_destroyer_fn = destroyer_fn;
            }

            void
            increment_ref() noexcept
            {
                m_refcount.fetch_add(1, std::memory_order_acq_rel);
            }

            bool
            decrement_ref() noexcept
            {
                return m_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1;
            }

            constexpr destroyer_fn_type
            get_destroyer_fn() const noexcept
            {
                return m_destroyer_fn;
            }

        private:
            std::atomic<uint64_t> m_refcount;
            destroyer_fn_type     m_destroyer_fn;
        };

        template <typename T>
        class small_control_area : public base_control_area<T>
        {
        public:
            small_control_area()                          = default;
            small_control_area(small_control_area const&) = delete;
            small_control_area(small_control_area&&)      = delete;
            ~small_control_area()                         = default;

            void
            operator=(small_control_area const&) = delete;
            void
            operator=(small_control_area&&) = delete;

            void
            record_allocation(byte_span) noexcept
            {}

            using base_control_area<T>::decrement_ref;
            using base_control_area<T>::increment_ref;
            using base_control_area<T>::initialize;
        };

        //
        // The big_control_area includes the original byte_span
        // that was acquired for the aligned_alloc() of the aligned bytes.
        //
        template <typename T>
        class big_control_area : public base_control_area<T>
        {
        public:
            big_control_area()                        = default;
            big_control_area(big_control_area const&) = delete;
            big_control_area(big_control_area&&)      = delete;
            ~big_control_area()                       = default;

            void
            operator=(big_control_area const&) = delete;
            void
            operator=(big_control_area&&) = delete;

            using base_control_area<T>::decrement_ref;
            using base_control_area<T>::increment_ref;
            using base_control_area<T>::initialize;

            void
            record_allocation(byte_span s) noexcept
            {
                m_allocated_span = s;
            }

            inline byte_span
            get_allocated_span() const noexcept
            {
                return m_allocated_span;
            }

        private:
            byte_span m_allocated_span;
        };

        template <typename T>
        constexpr bool uses_big_control_area_v = alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__;

        template <typename T>
        using control_area_t = std::
            conditional_t<uses_big_control_area_v<T>, big_control_area<T>, small_control_area<T>>;

        template <typename T>
        struct aggregate_raw_allocator_traits : basic_raw_allocation_helper_traits<aggregate<T>>
        {
            constexpr static void
            uninitialized_default_construct_n(std::span<T>) noexcept
            {
                // do nothing
            }
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
        class aggregate
        {
        public:
            using value_type             = T;
            using aggregate_deleter_type = aggregate_deleter<value_type>;
            using control_area_type      = control_area_t<value_type>;
            using destroyer_fn_type      = typename control_area_type::destroyer_fn_type;
            using unique_ptr_type        = std::unique_ptr<aggregate, aggregate_deleter_type>;

            // static_assert(std::is_trivially_default_constructible_v<control_area_type>);
            static_assert(std::is_trivially_destructible_v<control_area_type>);

            constexpr aggregate() noexcept {};
            aggregate(aggregate const&) = delete;
            aggregate(aggregate&&)      = delete;
            ~aggregate()                = default;

            void
            operator=(aggregate const&) = delete;

            void
            operator=(aggregate&&) = delete;

            void
            swap(aggregate&) = delete;

            inline static unique_ptr_type
            allocate(destroyer_fn_type destroyer, std::size_t extra_bytes = 0)
            {
                using rah_t = raw_allocation_helper<basic_raw_allocation_helper_traits<aggregate>>;

                typename rah_t::parameters_t parameters{.n = 1, .additional_bytes = extra_bytes};

                rah_t      rah(parameters);
                auto const aggptr  = rah.get().m_value_span.data();
                auto const objptr  = aggptr->get_object().get();
                auto const uobjptr = reinterpret_cast<uintptr_t>(objptr);
                auto const ucaptr  = uobjptr - sizeof(control_area_type);
                auto const captr   = reinterpret_cast<control_area_type*>(ucaptr);

                captr->initialize(1, destroyer);

                auto result = rah.release();

                return unique_ptr_type(result.m_value_span.data());
            }

            auto
            get_object_byte_span()
            {
                return std::as_writable_bytes(std::span(&m_faux_object, 1));
            }

            inline static void
            on_delete(value_type* ptr)
            {
                std::ignore = ptr;
                //
            }

            inline static void
            on_delete(aggregate* aggptr)
            {
                std::ignore = aggptr;
                //
            }

            static void
            destroy_object(value_type* ptr)
            {
                on_delete(ptr);
            }

        private:
            void
            deallocate(bool do_destroy = true)
            {
                deallocate(this, do_destroy);
            }

            inline static void
            deallocate(T* ptr, bool do_destroy = true)
            {
                auto const uptr     = reinterpret_cast<uintptr_t>(ptr);
                auto const uagg_ptr = uptr - offsetof(aggregate, m_faux_object);
                deallocate(reinterpret_cast<aggregate*>(uagg_ptr), do_destroy);
            }

            inline static void
            deallocate(aggregate* aggptr, bool do_destroy = true)
            {
                if (do_destroy)
                {
                    auto const ca = get_control_area(aggptr);
                    if (auto const destroyer = ca->get_destroyer_fn(); destroyer != nullptr)
                    {
                        // We don't want to force the fence when there is no destructor (e.g.
                        // wchar_t array which is a common case) so we do it here which may be
                        // pessimistic.
                        std::atomic_thread_fence(std::memory_order_acquire);
                        (*destroyer)(aggptr->get_object());
                    }
                }

                unique_ptr_type up(aggptr);
                up.reset(); // unneeded but gives a place to set a breakpoint
            }

            m::not_null<T*>
            get_object()
            {
                return reinterpret_cast<T*>(&m_faux_object);
            }

            static m::not_null<T*>
            get_object(aggregate* aggptr)
            {
                return aggptr->get_object();
            }

            static m::not_null<T*>
            get_object(control_area_type* captr)
            {
                auto const ucaptr = reinterpret_cast<uintptr_t>(captr);
                return reinterpret_cast<T*>(ucaptr + sizeof(control_area_type));
            }

            m::not_null<control_area_type*>
            get_control_area()
            {
                return get_control_area(get_object());
            }

            static m::not_null<control_area_type*>
            get_control_area(T* ptr)
            {
                // Should be true by construction but verify.
                static_assert(offsetof(aggregate, m_faux_control_area) <
                              offsetof(aggregate, m_faux_object));

                // Again should be true by construction but this ensures
                // that the pointer subtraction does not go before the
                // possible beginning of the `aggregate<T>` object.
                static_assert(sizeof(control_area_type) <= offsetof(aggregate, m_faux_object));

                auto const uptr = reinterpret_cast<std::uintptr_t>(ptr);
                return reinterpret_cast<control_area_type*>(uptr - sizeof(control_area_type));
            }

            inline static m::not_null<control_area_type*>
            get_control_area(aggregate* aggptr)
            {
                // Should be true by construction but verify.
                static_assert(offsetof(aggregate, m_faux_control_area) <
                              offsetof(aggregate, m_faux_object));

                // Again should be true by construction but this ensures
                // that the pointer subtraction does not go before the
                // possible beginning of the `aggregate<T>` object.
                static_assert(sizeof(control_area_type) <= offsetof(aggregate, m_faux_object));

                return get_control_area(aggptr->get_object());
            }

            inline static m::not_null<aggregate*>
            get_aggregate(T* ptr)
            {
                auto const uptr    = reinterpret_cast<std::uintptr_t>(ptr);
                auto const uaggptr = uptr - offsetof(aggregate, m_faux_object);
                return reinterpret_cast<aggregate*>(uaggptr);
            }

            void
            decrement_ref()
            {
                if (auto const ca = get_control_area(); ca->decrement_ref())
                    deallocate();
            }

            //
            // This reserves space for the control area but obviously is not the
            // the control area. The control area always _immediately_ preceeds the
            // actual object in the aggregate, so that it can be discovered trivially
            // from the object pointer at run time, by subtracting the size of the
            // control area from the pointer to the object.
            //
            struct faux_control_area_t
            {
                alignas(control_area_type) std::array<std::byte, sizeof(control_area_type)> m_data;
            } m_faux_control_area;

            //
            // Uninitialized storage for the object itself, properly aligned.
            //
            struct faux_object_t
            {
                alignas(value_type) std::array<std::byte, sizeof(value_type)> m_data;
            } m_faux_object;

            static_assert(alignof(faux_control_area_t) >= alignof(control_area_type));
            static_assert(alignof(faux_object_t) >= alignof(value_type));

            friend class arefc_ptr<value_type>;
        };

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
        control_area_t<T>*
        get_control_area(T* ptr) noexcept
        {
            auto const uptr = reinterpret_cast<uintptr_t>(ptr);
            auto const uca  = uptr - sizeof(control_area_t<T>);
            return reinterpret_cast<control_area_t<T>*>(uca);
        }

    } // namespace arefc_ptr_impl

    template <typename T>
        requires(arefc_ptr_requirements<T>)
    class arefc_ptr;

    template <typename T, typename... Args>
        requires(arefc_ptr_requirements<T>)
    arefc_ptr<T>
    mmake_arefc(Args&&... args);

    template <typename T, typename Fn, typename... Args>
        requires(arefc_ptr_requirements<T> && std::invocable<Fn, byte_span, Args && ...>)
    arefc_ptr<T>
    mmake_arefc_ex(std::size_t       extra_bytes_required,
                   destroyer_fn_t<T> destroyer,
                   Fn&&              fn,
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
        requires(arefc_ptr_requirements<T>)
    class arefc_ptr // ala shared_ptr
    {
    public:
        arefc_ptr() = default;

        arefc_ptr(arefc_ptr const& other) noexcept: m_ptr{other.addref()} {}

        template <typename U>
            requires(arefc_ptr_requirements<U> && std::convertible_to<U*, T*>)
        arefc_ptr(arefc_ptr<U> const& other) noexcept: m_ptr{other.addref()}
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
        operator=(arefc_ptr& other) noexcept
        {
            if (this != &other)
            {
                reset();
                put(other.addref());
            }

            return *this;
        }

        template <typename U>
            requires(arefc_ptr_requirements<U> && std::convertible_to<U*, T*>)
        arefc_ptr&
        operator=(arefc_ptr<U> const& other) noexcept
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
        operator bool() const noexcept
        {
            return get() != nullptr;
        }

        bool
        operator!() const noexcept
        {
            return get() == nullptr;
        }

        void
        reset(T* ptr_in = nullptr) noexcept
        {
            auto const ptr = m_ptr.exchange(increment_ref(ptr_in), std::memory_order_acq_rel);
            decrement_ref(ptr);
        }

        T&
        operator*() const noexcept
        {
            return *get();
        }

        T*
        operator->() const noexcept
        {
            return get();
        }

        T*
        get() const noexcept
        {
            return m_ptr.load(std::memory_order_acquire);
        }

        /// <summary>
        /// Converts the current arefc_ptr to an arefc_ptr of type U, if U is a standard layout type
        /// and convertible from T.
        /// </summary>
        /// <typeparam name="U">The target type to convert the arefc_ptr to. Must be a standard
        /// layout type and convertible from T*.</typeparam> <returns>An arefc_ptr<U> instance
        /// converted from the current arefc_ptr.</returns>
        template <typename U>
            requires(arefc_ptr_requirements<U> && std::convertible_to<T*, U*>)
        arefc_ptr<U>
        to() const noexcept
        {
            arefc_ptr<U> v(*this);
            return v;
        }

        bool
        compare_exchange_strong(arefc_ptr& expected, arefc_ptr const& desired) noexcept
        {
            // The trick here is to not mess up the reference counting!
            //
            // it would seem trivial to just "pass through" the m_ptr values but that's
            // only part of the story.
            //

            T* e = expected.get();
            T* d = desired.get();

            T* old_e = e; // save a copy so we don't have to re-load

            increment_ref(d);

            if (m_ptr.compare_exchange_strong(e, d, std::memory_order_acq_rel))
            {
                M_INTERNAL_ERROR_CHECK(e == old_e);

                // Account for the fact that m_ptr no longer refers to `e`
                decrement_ref(e);
                return true;
            }

            // The compare_exchange "failed", so now we need to update
            // "expected" to the new value.
            expected.reset(e);

            return false;
        }

    private:
        constexpr arefc_ptr(T* ptr) noexcept: m_ptr(ptr) {}

        arefc_ptr_impl::control_area_t<T>*
        get_control_area() const
        {
            auto const ptr = get();
            if (ptr == nullptr)
                return nullptr;

            return get_control_area(ptr);
        }

        static arefc_ptr_impl::control_area_t<T>*
        get_control_area(T* ptr)
        {
            auto const uptr    = reinterpret_cast<uintptr_t>(ptr);
            auto const ca_uptr = uptr - sizeof(arefc_ptr_impl::control_area_t<T>);
            return reinterpret_cast<arefc_ptr_impl::control_area_t<T>*>(ca_uptr);
        }

        static arefc_ptr_impl::aggregate<T>*
        get_aggregate(T* ptr)
        {
            return arefc_ptr_impl::aggregate<T>::get_aggregate(ptr);
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

            auto const agg = get_aggregate(ptr);
            agg->decrement_ref();
#if 0
            if (auto const ca = get_control_area(ptr); ca->decrement_ref())
            {
                // If the type's destructor is trivial it may not have a destructor and so
                // it may be entirely omitted.
                auto const destroyer = ca->m_destroyer_fn;
                if (destroyer)
                {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    (destroyer)(ptr);
                }

                arefc_ptr_impl::aggregate<T>::deallocate(
                    std::span(reinterpret_cast<std::byte*>(ptr), sizeof(T)));
            }
#endif // 0
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

        std::atomic<T*> m_ptr{nullptr};

        template <typename T1, typename... Args>
            requires(arefc_ptr_requirements<T1>)
        friend arefc_ptr<T1>
        mmake_arefc(Args&&... args);

        template <typename T1, typename Fn, typename... Args>
            requires(arefc_ptr_requirements<T1> && std::invocable<Fn, byte_span, Args && ...>)
        friend arefc_ptr<T1>
        mmake_arefc_ex(std::size_t        extra_bytes_required,
                       destroyer_fn_t<T1> destroyer,
                       Fn&&               fn,
                       Args&&... args);
    };

    template <typename T, typename Fn, typename... Args>
        requires(arefc_ptr_requirements<T> && std::invocable<Fn, byte_span, Args && ...>)
    arefc_ptr<T>
    mmake_arefc_ex(std::size_t       extra_bytes_required,
                   destroyer_fn_t<T> destroyer,
                   Fn&&              fn,
                   Args&&... args)
    {
        using aggregate_type = arefc_ptr_impl::aggregate<T>;

        auto a = aggregate_type::allocate(destroyer, extra_bytes_required);

        auto const object_span = a->get_object_byte_span();
        auto const extended_span =
            std::span(object_span.data(), object_span.size() + extra_bytes_required);

        auto const ptr = std::invoke<Fn, byte_span, Args...>(
            std::forward<Fn>(fn), a->get_object_byte_span(), std::forward<Args>(args)...);

        a.release();
        arefc_ptr<T> retval(ptr);
        return retval;
    }

    template <typename T, typename... Args>
        requires(arefc_ptr_requirements<T>)
    arefc_ptr<T>
    mmake_arefc(Args&&... args)
    {
        return mmake_arefc_ex<T>(
            sizeof(T),
            &arefc_ptr_impl::aggregate<T>::destroy_object,
            [](byte_span s, Args&&... args1) {
                M_INTERNAL_ERROR_CHECK(s.size() >= sizeof(T));
                return ::new (s.data()) T(std::forward<Args>(args1)...);
            },
            std::forward<Args>(args)...);
    }
} // namespace m
