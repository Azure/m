// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <system_error>

#include <m/pil/webcore_interfaces.h>

#include "redirecting.h"

namespace m::pil::impl::redirecting
{
    webcore::webcore(std::shared_ptr<iwebcore> const&      underlying_webcore,
                     std::shared_ptr<fs_redirector> const& redirector):
        m_webcore(underlying_webcore),
        m_redirector(redirector)
    {}

    iwebcore::activate_disposition
    webcore::activate(activate_flags                      flags,
                      activation_request const&           request,
                      std::unique_ptr<iwebcore_instance>& returned_instance,
                      std::error_code&                    ec)
    {
        // Map the config file paths from public to private before passing to
        // the underlying webcore. This allows the activation to read config
        // files from the isolated filesystem.
        activation_request mapped_request;
        mapped_request.app_host_config = m_redirector->map_public_to_private(request.app_host_config);
        if (request.root_web_config)
            mapped_request.root_web_config = m_redirector->map_public_to_private(*request.root_web_config);
        mapped_request.instance_name = request.instance_name;

        return m_webcore->activate(flags, mapped_request, returned_instance, ec);
    }

    iwebcore::set_metadata_disposition
    webcore::set_metadata(set_metadata_flags  flags,
                          std::u16string_view type,
                          std::u16string_view value,
                          std::error_code&    ec)
    {
        // set_metadata has no path redirection; forward to underlying.
        return m_webcore->set_metadata(flags, type, value, ec);
    }

} // namespace m::pil::impl::redirecting
