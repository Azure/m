// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

#include <m/pil/file_path.h>
#include <m/pil/webcore_interfaces.h>
#include <m/strings/convert.h>

#include "logging.h"

using namespace std::string_view_literals;

namespace m::pil::impl::logging
{
    //
    // Webcore.Activate log entry
    //

    webcore_activate_log_entry::webcore_activate_log_entry(iwebcore::activate_flags flags,
                                                           activation_request const& request):
        m_flags(flags),
        m_app_host_config(request.app_host_config),
        m_root_web_config(request.root_web_config),
        m_instance_name(request.instance_name),
        m_disposition{},
        m_ec{}
    {}

    void
    webcore_activate_log_entry::set_disposition(iwebcore::activate_disposition disposition,
                                                std::error_code const& ec)
    {
        m_disposition = disposition;
        m_ec = ec;
    }

    void
    webcore_activate_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Webcore.Activate"sv));

        write_attribute(n, M_PUGIXML_T("appHostConfig"sv), m_app_host_config);
        if (m_root_web_config)
            write_attribute(n, M_PUGIXML_T("rootWebConfig"sv), *m_root_web_config);
        write_attribute(n, M_PUGIXML_T("instanceName"sv), m::to_string(m_instance_name));
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
        if (m_ec)
        {
            write_attribute(n, M_PUGIXML_T("errorCode"sv), m_ec.value());
            write_attribute(n, M_PUGIXML_T("errorMessage"sv), m_ec.message());
        }
    }

    //
    // Webcore.SetMetadata log entry
    //

    webcore_set_metadata_log_entry::webcore_set_metadata_log_entry(
        iwebcore::set_metadata_flags flags,
        std::u16string_view type,
        std::u16string_view value):
        m_flags(flags),
        m_type(type),
        m_value(value),
        m_disposition{},
        m_ec{}
    {}

    void
    webcore_set_metadata_log_entry::set_disposition(iwebcore::set_metadata_disposition disposition,
                                                    std::error_code const& ec)
    {
        m_disposition = disposition;
        m_ec = ec;
    }

    void
    webcore_set_metadata_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Webcore.SetMetadata"sv));

        write_attribute(n, M_PUGIXML_T("type"sv), m::to_string(m_type));
        write_attribute(n, M_PUGIXML_T("value"sv), m::to_string(m_value));
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
        if (m_ec)
        {
            write_attribute(n, M_PUGIXML_T("errorCode"sv), m_ec.value());
            write_attribute(n, M_PUGIXML_T("errorMessage"sv), m_ec.message());
        }
    }

    //
    // Webcore.Shutdown log entry
    //

    webcore_shutdown_log_entry::webcore_shutdown_log_entry(bool immediate):
        m_immediate(immediate)
    {}

    void
    webcore_shutdown_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Webcore.Shutdown"sv));

        write_attribute(n, M_PUGIXML_T("immediate"sv), m_immediate);
    }

    //
    // webcore_instance - logging wrapper for iwebcore_instance
    //

    webcore_instance::webcore_instance(std::unique_ptr<iwebcore_instance> underlying_instance,
                                       std::shared_ptr<log> const& log_ptr,
                                       bool immediate_shutdown):
        m_underlying_instance(std::move(underlying_instance)),
        m_log(log_ptr),
        m_immediate_shutdown(immediate_shutdown)
    {}

    webcore_instance::~webcore_instance()
    {
        // Log the shutdown when the instance is destroyed
        auto entry = std::make_unique<webcore_shutdown_log_entry>(m_immediate_shutdown);
        m_log->add(entry);
    }

    //
    // webcore - logging wrapper for iwebcore
    //

    webcore::webcore(std::shared_ptr<iwebcore> const& underlying_webcore,
                     std::shared_ptr<log> const& log_ptr):
        m_webcore(underlying_webcore),
        m_log(log_ptr)
    {}

    iwebcore::activate_disposition
    webcore::activate(activate_flags                      flags,
                      activation_request const&           request,
                      std::unique_ptr<iwebcore_instance>& returned_instance,
                      std::error_code&                    ec)
    {
        auto entry = std::make_unique<webcore_activate_log_entry>(flags, request);

        std::unique_ptr<iwebcore_instance> underlying_instance;
        auto disposition = m_webcore->activate(flags, request, underlying_instance, ec);

        entry->set_disposition(disposition, ec);
        m_log->add(entry);

        if (!ec && underlying_instance)
        {
            bool immediate_shutdown =
                (flags & activate_flags::immediate_shutdown_on_release) != activate_flags{};
            returned_instance = std::make_unique<webcore_instance>(
                std::move(underlying_instance), m_log, immediate_shutdown);
        }

        return disposition;
    }

    iwebcore::set_metadata_disposition
    webcore::set_metadata(set_metadata_flags  flags,
                          std::u16string_view type,
                          std::u16string_view value,
                          std::error_code&    ec)
    {
        auto entry = std::make_unique<webcore_set_metadata_log_entry>(flags, type, value);

        auto disposition = m_webcore->set_metadata(flags, type, value, ec);

        entry->set_disposition(disposition, ec);
        m_log->add(entry);

        return disposition;
    }

} // namespace m::pil::impl::logging
