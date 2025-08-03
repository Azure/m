// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/tracing/close_flush_option.h>
#include <m/tracing/sink_registration_impl.h>

namespace m::tracing::internal
{
    sink_registration_impl::sink_registration_impl(std::shared_ptr<sink_shim> const& shim):
        m_sink_shim(shim)
    {}

    sink_registration_impl::sink_registration_impl(sink_registration_impl&& other)
    {
        using std::swap;

        swap(m_sink_shim, other.m_sink_shim);
    }

    sink_registration_impl::~sink_registration_impl() { m_sink_shim->close(close_flush_option::normal); }
} // namespace m::tracing::internal
