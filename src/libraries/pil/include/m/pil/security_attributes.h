// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

namespace m::pil
{
    struct security_attributes
    {
        void* m_security_descriptor; // opaque
        bool  m_inherit_handle;
    };
} // namespace m::pil

