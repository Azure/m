// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/tracing/close_flush_option.h>

#include "sink_registration_impl.h"
#include "sink_shim.h"

namespace m::tracing_impl
{
    sink_registration_impl::sink_registration_impl(std::shared_ptr<sink_shim> const& shim):
        m_sink_shim(shim)
    {}

    sink_registration_impl::sink_registration_impl(sink_registration_impl&& other)
    {
        using std::swap;

        swap(m_sink_shim, other.m_sink_shim);
    }

    sink_registration_impl::~sink_registration_impl()
    {
        m_sink_shim->close(m::tracing::close_flush_option::normal);
    }
} // namespace m::tracing_impl
