// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::pil
{
    struct security_attributes
    {
        void*       m_security_descriptor; // opaque
        std::size_t m_security_descriptor_length;
        bool        m_inherit_handle;
    };

    //
    // These conversions are *shallow*. They do not copy the
    // security descriptor, they only copy the inherit_handle and
    // the pointer to the security descriptor.
    //

    std::optional<security_attributes>
    to_security_attributes(const LPSECURITY_ATTRIBUTES sa);

    security_attributes
    to_security_attributes(SECURITY_ATTRIBUTES const& sa);
} // namespace m::pil
