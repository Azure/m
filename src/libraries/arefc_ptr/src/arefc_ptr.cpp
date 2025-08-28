// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/arefc_ptr/arefc_ptr.h>

namespace m::arefc_ptr_impl
{
    void
    control_area::increment_ref()
    {
        m_refcount.fetch_add(1, std::memory_order_acq_rel);
    }

    bool
    control_area::decrement_ref()
    {
        return m_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

} // namespace m::arefc_ptr_impl