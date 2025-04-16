// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdint>
#include <memory>
#include <utility>

#include <gsl/gsl>

#include <m/cast/to.h>
#include <m/math/math.h>

#include "memory_stream.h"

m::byte_streams_impl::memory_ro_ra_seq::memory_ro_ra_seq(std::unique_ptr<std::byte[]>&& array,
                                                         size_t                         count):
    m_current_position{}, m_array(std::move(array))
{
    m_span = gsl::span<std::byte>(m_array.get(), count);
    //
}

std::size_t
m::byte_streams_impl::memory_ro_ra_seq::do_read(gsl::span<std::byte>& span)
{
    auto l = std::unique_lock(m_mutex);

    // We are assuming that m_current_position <= m_span.size()

    io::position_t start = m_current_position;
    io::position_t end = start + span.size();

    if (end > m_span.size())
        end = m::io::position_t{m_span.size()};

    io::offset_t diff = end - start;

    auto const size = m::to<std::size_t>(std::to_underlying(diff));

    std::copy_n(m_span.subspan(std::to_underlying(start), size).begin(),
                std::to_underlying(diff),
                span.begin());
    span = span.subspan(0, size);

    m_current_position = m_current_position + size;

    return size;
}

std::size_t
m::byte_streams_impl::memory_ro_ra_seq::do_read(io::position_t p, gsl::span<std::byte>& span)
{
    // We do not need to lock anything for random access

    if (p >= m_span.size())
    {
        span = span.subspan(0, 0);
        return 0;
    }

    io::position_t start = p;
    io::position_t end   = start + span.size();
    ;

    if (end > m_span.size())
        end = io::position_t{m_span.size()};

    io::offset_t diff = end - start;

    auto const size = m::to<std::size_t>(std::to_underlying(diff));

    std::copy_n(m_span.subspan(std::to_underlying(start), size).begin(),
                std::to_underlying(diff),
                span.begin());
    span = span.subspan(0, size);

    return size;
}
