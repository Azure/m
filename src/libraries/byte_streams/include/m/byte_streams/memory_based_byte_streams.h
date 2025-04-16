// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <memory>

#include <m/byte_streams/byte_streams.h>

namespace m
{
    namespace byte_streams
    {
        class memory_based_byte_stream : public ra_in, public seq_in
        {
            // Nothing more for the interface
        };

        std::shared_ptr<memory_based_byte_stream>
        construct_memory_based_byte_stream(std::unique_ptr<std::byte[]>&& array, size_t count);

        // Constructs a random access and sequential capable byte stream over
        // a span of bytes. Copies the span in.
        //
        template <typename byte_type_t>
        std::shared_ptr<memory_based_byte_stream>
        make_memory_based_byte_stream(byte_type_t const* data, std::size_t count)
        {
            auto array = std::make_unique<std::byte[]>(count);

            std::transform(
                data, data + count, array.get(), [](auto b) { return static_cast<std::byte>(b); });

            return construct_memory_based_byte_stream(std::move(array), count);
        }
    }
}