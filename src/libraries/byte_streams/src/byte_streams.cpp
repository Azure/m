// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <memory>

#include <m/byte_streams/byte_streams.h>

#include <m/byte_streams/memory_based_byte_streams.h>

#include "memory_stream.h"

std::shared_ptr<m::byte_streams::memory_based_byte_stream>
m::byte_streams::construct_memory_based_byte_stream(std::unique_ptr<std::byte[]>&& array, size_t count)
{
    return std::make_shared<m::byte_streams_impl::memory_ro_ra_seq>(std::move(array), count);
}
