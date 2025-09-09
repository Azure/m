// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <new>
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

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace m::pil::impl::buffered
{
    registry::registry(std::shared_ptr<iregistry> const& underlying_registry):
        m_underlying_registry(underlying_registry)
    {}

    registry::registry(std::shared_ptr<iregistry>&& underlying_registry) noexcept:
        m_underlying_registry(std::move(underlying_registry))
    {}

    iregistry::open_predefined_key_disposition
    registry::open_predefined_key(open_predefined_key_flags flags,
                                  predefined_key            pk,
                                  sam,
                                  std::shared_ptr<m::pil::ikey>& returned_key)
    {
        if (flags != open_predefined_key_flags{})
            throw std::runtime_error("Invalid flags to call to iregistry::open_predefined_key()");

        auto lock = std::unique_lock(m_mutex);

        auto find_location = m_predefined_keys.find(pk);
        if (find_location != m_predefined_keys.end())
        {
            returned_key = find_location->second;
            return open_predefined_key_disposition{};
        }

        // there are two axis to consider here: the underlying registry's predefined key, if there
        // is one, and then our wrapper of it.
        std::shared_ptr<ikey> underlying_predefined_key;
        if (m_underlying_registry)
            underlying_predefined_key = m_underlying_registry->open_predefined_key(pk);

        auto const [insertion_location, insertted] = m_predefined_keys.emplace(
            std::make_pair(pk, std::make_shared<key>(std::move(underlying_predefined_key))));
        M_INTERNAL_ERROR_CHECK(insertted);

        returned_key = std::static_pointer_cast<pil::ikey>(insertion_location->second);

        return open_predefined_key_disposition{};
    }

    iregistry::monitor_disposition
    registry::monitor(monitor_flags                               flags,
                      std::shared_ptr<m::pil::iregistry_monitor>& returned_registry_monitor)
    {
        if (flags != monitor_flags{})
            throw std::runtime_error("Invalid flags to call to iregistry::monitor()");

        auto lock = std::unique_lock(m_mutex);

        if (!m_monitor)
            initialize_monitor(m::locked);

        M_INTERNAL_ERROR_CHECK(m_monitor);

        returned_registry_monitor = m_monitor;
        return monitor_disposition{};
    }

    void
    registry::initialize_monitor(m::locked_t)
    {
        if (m_monitor)
            return;

        auto underlying_monitor = m_underlying_registry->monitor();

        m_monitor = std::make_shared<registry_monitor>(std::move(underlying_monitor));
    }

    void
    registry::save_xml(pugi::xml_node& doc_node) const
    {
        auto l = std::unique_lock(m_mutex);

        auto reg_node = doc_node.append_child(M_PUGIXML_T("Registry"sv));

        //
        // The predefined nodes are special and so are handled here
        // in a custom fashion. If major structural changes are made to
        // how the Key element is written be sure to synchronize them
        // between here and there.
        //

        for (auto const& e: m_predefined_keys)
        {
            auto key_node  = reg_node.append_child(M_PUGIXML_T("Key"sv));
            auto name_attr = key_node.append_attribute(M_PUGIXML_T("name"sv));
            name_attr.set_value(m::to_string(map_predefined_key_to_string(e.first).view()).c_str());

            e.second->save_xml(key_node);
        }
    }

} // namespace m::pil::impl::buffered
