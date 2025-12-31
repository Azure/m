
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include <m/utility/byte_span.h>

namespace m
{
    byte_span
    aligned_alloc(std::align_val_t alignment, std::size_t bytes);

    void
    aligned_free(byte_span s);

    template <typename T>
    constexpr bool requires_aligned_allocator_t = alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__;
} // namespace m
