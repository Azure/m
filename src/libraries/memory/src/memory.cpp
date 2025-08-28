// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/memory/memory.h>

namespace m
{
    std::span<std::byte>
    aligned_alloc(std::align_val_t alignment, std::size_t bytes)
    {
        std::byte* ptr{};

#ifdef WIN32
        ptr = reinterpret_cast<std::byte*>(
            _aligned_malloc(bytes, static_cast<std::size_t>(alignment)));
#else
        ptr = reinterpret_cast<std::byte*>(
            std::aligned_alloc(static_cast<std::size_t>(alignment), bytes));
#endif

        if (!ptr)
            throw std::bad_alloc();

        std::fill_n(ptr, bytes, std::byte{});

        return std::span(ptr, bytes);
    }

    void
    aligned_free(std::span<std::byte> s)
    {
        if (auto ptr = s.data(); ptr != nullptr)
        {
            using byte_int_type     = std::underlying_type_t<std::byte>;
            constexpr auto min_byte = (std::numeric_limits<byte_int_type>::min)();
            constexpr auto max_byte = (std::numeric_limits<byte_int_type>::max)();

            thread_local std::mt19937                            twisty(std::random_device{}());
            thread_local std::uniform_int_distribution<uint32_t> distro(min_byte, max_byte);

            // Fill the memory to be freed with random numbers
            for (auto&& e: s)
                e = static_cast<std::byte>(distro(twisty));

#ifdef WIN32
            _aligned_free(ptr);
#else
            std::free(ptr);
#endif
        }
    }
} // namespace m
