// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <system_error>

#include <m/pil/webcore_interfaces.h>

#include "fault.h"

namespace m::pil::impl::fault
{
    webcore::webcore(std::shared_ptr<iwebcore> const&     underlying_webcore,
                     std::shared_ptr<fault_script> const& script):
        m_webcore(underlying_webcore),
        m_script(script)
    {}

    iwebcore::activate_disposition
    webcore::activate(activate_flags                      flags,
                      activation_request const&           request,
                      std::unique_ptr<iwebcore_instance>& returned_instance,
                      std::error_code&                    ec)
    {
        // Check for fault injection before forwarding to underlying. If a rule
        // fires, this throws and the activation never reaches the underlying
        // layer.
        m_script->check_webcore(fault_operation::webcore_activate, request.instance_name);

        return m_webcore->activate(flags, request, returned_instance, ec);
    }

    iwebcore::set_metadata_disposition
    webcore::set_metadata(set_metadata_flags  flags,
                          std::u16string_view type,
                          std::u16string_view value,
                          std::error_code&    ec)
    {
        // set_metadata has no fault injection; forward to underlying.
        return m_webcore->set_metadata(flags, type, value, ec);
    }

} // namespace m::pil::impl::fault
