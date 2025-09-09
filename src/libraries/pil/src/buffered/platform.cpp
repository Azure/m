// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "buffered.h"

using namespace std::string_view_literals;

namespace m::pil::impl::buffered
{
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const& underlying_platform)
    {
        return std::make_shared<platform>(underlying_platform);
    }

    platform::platform(std::shared_ptr<iplatform> const& underlying_platform):
        m_underlying_platform(underlying_platform),
        m_registry(std::make_shared<registry>(m_underlying_platform->get_registry()))
    {}

    platform::platform(std::shared_ptr<iplatform>&& underlying_platform):
        m_underlying_platform(std::move(underlying_platform)),
        m_registry(std::make_shared<registry>(m_underlying_platform->get_registry()))
    {}

    iplatform::get_registry_disposition
    platform::get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry)
    {
        returned_registry.reset();

        if (flags != get_registry_flags{})
            throw std::runtime_error("iplatform::get_registry() called with invalid flags");

        auto l            = std::unique_lock(m_mutex);
        returned_registry = m_registry;
        return get_registry_disposition{};
    }

    iplatform::save_disposition
    platform::save(save_flags flags, save_contents contents, pugi::xml_node& platform_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // we don't save anything today, just pass through.

        return m_underlying_platform->save(flags, contents, platform_element);
    }

    void
    platform::save(persistence_format pf, std::filesystem::path const& p) const
    {
        M_VALIDATE_PARAMETER(pf, pf == persistence_format::xml);

        auto l = std::unique_lock(m_mutex);

        // We only support one persistence format today but go ahead and use the switch
        // syntax in case we add additional:

        switch (pf)
        {
            using enum persistence_format;

            case xml: save_xml(m::locked, p); return;

            default: M_UNREACHABLE_CODE();
        }
    }

    void
    platform::save_xml(m::locked_t, std::filesystem::path const& p) const
    {
        pugi::xml_document doc;

        auto doc_node = doc.document_element();

        doc_node.set_name(M_PUGIXML_T("Platform"sv));

        m_registry->save_xml(doc_node);

        doc.save_file(p.native().c_str());
    }

} // namespace m::pil::impl::buffered
