// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <m/pil/fault.h>
#include <m/utility/exception.h>

#include "fault/fault.h"

namespace m::pil
{
    namespace
    {
        // Map the public operation vocabulary onto the internal one. The two
        // enumerations are defined independently (the public surface owns its
        // contract, the internal layer owns its own); this switch is the single
        // point of correspondence, so a divergence is a compile error here
        // rather than a silent mismatch.
        impl::fault::fault_operation
        to_impl(fault_operation op)
        {
            switch (op)
            {
            case fault_operation::create_key:
                return impl::fault::fault_operation::create_key;
            case fault_operation::open_key:
                return impl::fault::fault_operation::open_key;
            case fault_operation::delete_key:
                return impl::fault::fault_operation::delete_key;
            case fault_operation::delete_tree:
                return impl::fault::fault_operation::delete_tree;
            case fault_operation::rename_key:
                return impl::fault::fault_operation::rename_key;
            case fault_operation::set_value:
                return impl::fault::fault_operation::set_value;
            case fault_operation::delete_value:
                return impl::fault::fault_operation::delete_value;
            case fault_operation::get_value:
                return impl::fault::fault_operation::get_value;
            case fault_operation::create_directory:
                return impl::fault::fault_operation::create_directory;
            case fault_operation::create_file:
                return impl::fault::fault_operation::create_file;
            case fault_operation::open_directory:
                return impl::fault::fault_operation::open_directory;
            case fault_operation::open_file:
                return impl::fault::fault_operation::open_file;
            case fault_operation::remove_entry:
                return impl::fault::fault_operation::remove_entry;
            case fault_operation::delete_tree_entry:
                return impl::fault::fault_operation::delete_tree_entry;
            case fault_operation::rename_entry:
                return impl::fault::fault_operation::rename_entry;
            }

            throw m::invalid_parameter("fault_operation");
        }

        impl::fault::fault_action
        to_impl(fault_action action)
        {
            switch (action)
            {
            case fault_action::not_found:
                return impl::fault::fault_action::not_found;
            case fault_action::access_denied:
                return impl::fault::fault_action::access_denied;
            case fault_action::out_of_resources:
                return impl::fault::fault_action::out_of_resources;
            case fault_action::sharing_violation:
                return impl::fault::fault_action::sharing_violation;
            case fault_action::already_exists:
                return impl::fault::fault_action::already_exists;
            case fault_action::not_supported:
                return impl::fault::fault_action::not_supported;
            }

            throw m::invalid_parameter("fault_action");
        }
    } // namespace

    fault_script::fault_script(): m_impl(std::make_shared<impl::fault::fault_script>()) {}

    fault_script::fault_script(std::shared_ptr<impl::fault::fault_script> impl) noexcept:
        m_impl(std::move(impl))
    {}

    void
    fault_script::add_rule(fault_operation                       op,
                           key_path const&                       target,
                           std::optional<value_name_string_type> value_name,
                           std::uint64_t                         occurrence,
                           fault_action                          action)
    {
        m_impl->add_rule(impl::fault::fault_rule(
            to_impl(op), target, std::move(value_name), occurrence, to_impl(action)));
    }

    void
    fault_script::add_rule(fault_operation  op,
                           file_path const& target,
                           std::uint64_t    occurrence,
                           fault_action     action)
    {
        m_impl->add_rule(
            impl::fault::fault_rule(to_impl(op), target, occurrence, to_impl(action)));
    }

    std::shared_ptr<impl::fault::fault_script> const&
    fault_script::get_impl() const noexcept
    {
        return m_impl;
    }

    fault_script
    parse_fault_script(pugi::xml_node const& fault_script_node)
    {
        return fault_script(impl::fault::parse_fault_script(fault_script_node));
    }

    fault_script
    load_fault_script(std::filesystem::path const& path)
    {
        pugi::xml_document doc;

        auto const result = doc.load_file(path.native().c_str());
        if (!result)
            throw std::runtime_error(std::string("load_fault_script: failed to load ") +
                                     result.description());

        return parse_fault_script(doc.document_element());
    }

    std::shared_ptr<iplatform>
    apply_fault_layer(std::shared_ptr<iplatform> const& underlying_platform,
                      fault_script const&               script)
    {
        return impl::fault::create_platform(underlying_platform, script.get_impl());
    }
} // namespace m::pil
