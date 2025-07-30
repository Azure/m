// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <type_traits>
#include <utility>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m
{
    template <typename T>
    class local_ptr
    {
    public:
        constexpr local_ptr() = default;

        explicit constexpr local_ptr(T* ptr): m_ptr{ptr} {}

        ~local_ptr()
        {
            reset();
        }

        constexpr T*
        release()
        {
            return std::exchange(m_ptr, nullptr);
        }

        // The const-ness of the local_ptr is shallow, it does not flow into the
        // ptr.
        constexpr T*
        get() const
        {
            return m_ptr;
        }

        // The const-ness of the local_ptr is shallow, it does not flow into the
        // ptr.
        constexpr T*
        operator->() const
        {
            return m_ptr;
        }

        void
        reset(T* newptr = nullptr)
        {
            if (auto const oldptr = std::exchange(m_ptr, newptr);
                (oldptr != nullptr) && (oldptr != newptr))
            {
                do_free(oldptr);
            }
        }

    protected:
        static void
            do_free(T* ptr)
        {
            // silly renaming just to establish pattern.
            using T0 = T;
            auto const ptr0 = ptr;

            // T may be const or volatile qualified. This takes a little care to
            // peel away.
            using T1 = std::remove_const_t<T0>;
            auto const ptr1 = const_cast<T1*>(ptr0);

            using T2 = std::remove_volatile_t<T1>;
            auto const ptr2 = const_cast<T2*>(ptr1);

            using T3 = std::remove_reference_t<T2>;
            auto const ptr3 = static_cast<T3*>(ptr2);

            // ptr3 is a plain, non-const, non-volatile, non-reference pointer to whatever
            // T really points to. This means that it can be trivially (e.g. static_cast)
            // cast to void* which is what LocalFree() really wants.
            ::LocalFree(static_cast<HLOCAL>(ptr3));
        }
        T* m_ptr{};
    };

} // namespace m
