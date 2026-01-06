// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

using namespace std::string_view_literals;

namespace m::pil
{
    std::optional<security_attributes>
    to_security_attributes(const LPSECURITY_ATTRIBUTES sa)
    {
        if (sa == nullptr)
            return std::nullopt;

        return security_attributes{.m_security_descriptor        = sa->lpSecurityDescriptor,
                                   .m_security_descriptor_length = sa->nLength,
                                   .m_inherit_handle             = !!sa->bInheritHandle};
    }

    security_attributes
    to_security_attributes(SECURITY_ATTRIBUTES const& sa)
    {
        return security_attributes{.m_security_descriptor        = sa.lpSecurityDescriptor,
                                   .m_security_descriptor_length = sa.nLength,
                                   .m_inherit_handle             = !!sa.bInheritHandle};
    }

} // namespace m::pil
