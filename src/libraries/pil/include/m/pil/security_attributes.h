// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>

namespace m::pil
{
    struct security_attributes
    {
        void*       m_security_descriptor;        // opaque
        std::size_t m_security_descriptor_length; // length of the descriptor in bytes
        bool        m_inherit_handle;
    };
} // namespace m::pil
