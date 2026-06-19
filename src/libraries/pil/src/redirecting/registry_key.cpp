// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string>
#include <string_view>

#include <m/strings/convert.h>

#include <m/pil/registry.h>

#include "redirecting.h"

namespace m::pil::impl::redirecting
{
    key::key(std::shared_ptr<ikey> const& key, std::shared_ptr<redirector> const& redir):
        m_key(key), m_redirector(redir)
    {
        M_INTERNAL_ERROR_CHECK(m_redirector.get() != nullptr);
    }

    path
    key::public_to_private(path const& p) const
    {
        return m_redirector->map_public_to_private(p);
    }

    std::optional<path>
    key::public_to_private(std::optional<path> const& p) const
    {
        if (p.has_value())
            return public_to_private(p.value());

        return std::nullopt;
    }

    path
    key::private_to_public(path const& p) const
    {
        return m_redirector->map_private_to_public(p);
    }

    std::optional<path>
    key::private_to_public(std::optional<path> const& p) const
    {
        if (p.has_value())
            return private_to_public(p.value());

        return std::nullopt;
    }

    ikey::create_key_disposition
    key::create_key(ikey::create_key_flags             flags,
                    pil::key_path const&               key_path,
                    sam                                sam_desired,
                    std::optional<security_attributes> sa,
                    std::shared_ptr<ikey>&             returned_key)
    {
        std::shared_ptr<ikey> unmapped_returned_key;
        auto                  d = m_key->create_key(
            flags, public_to_private(key_path), sam_desired, sa, unmapped_returned_key);
        if (unmapped_returned_key)
            returned_key = std::make_shared<key>(unmapped_returned_key, m_redirector);
        return d;
    }

    ikey::delete_key_disposition
    key::delete_key(ikey::delete_key_flags flags, pil::key_path const& key_path, sam sam_desired)
    {
        return m_key->delete_key(flags, public_to_private(key_path), sam_desired);
    }

    ikey::delete_tree_disposition
    key::delete_tree(ikey::delete_tree_flags flags, std::optional<pil::key_path> const& key_path)
    {
        return m_key->delete_tree(flags, public_to_private(key_path));
    }

    ikey::enumerate_keys_disposition
    key::enumerate_keys(ikey::enumerate_keys_flags                     flags,
                        std::size_t                                    index,
                        std::span<pil::key_path, std::dynamic_extent>& key_names)
    {
        auto d = m_key->enumerate_keys(flags, index, key_names);

        for (auto&& e: key_names)
            e = private_to_public(e);

        return d;
    }

    ikey::flush_disposition
    key::flush(ikey::flush_flags flags)
    {
        return m_key->flush(flags);
    }

    ikey::open_key_disposition
    key::open_key(ikey::open_key_flags                flags,
                  std::optional<pil::key_path> const& key_path,
                  sam                                 sam_desired,
                  std::shared_ptr<ikey>&              returned_key,
                  std::error_code&                    ec)
    {
        std::shared_ptr<ikey> temp_key;
        auto d = m_key->open_key(flags, public_to_private(key_path), sam_desired, temp_key, ec);
        if (temp_key)
            returned_key = std::make_shared<key>(temp_key, m_redirector);
        return d;
    }

    ikey::query_information_key_disposition
    key::query_information_key(ikey::query_information_key_flags flags,
                               std::size_t&                      subkey_count,
                               std::size_t&                      value_count,
                               std::size_t&                      security_descriptor_size,
                               m::pil::time_point_type&          last_write_time)
    {
        return m_key->query_information_key(
            flags, subkey_count, value_count, security_descriptor_size, last_write_time);
    }

    ikey::rename_key_disposition
    key::rename_key(ikey::rename_key_flags              flags,
                    std::optional<pil::key_path> const& old_name,
                    pil::key_path const&                new_name)
    {
        return m_key->rename_key(flags, public_to_private(old_name), public_to_private(new_name));
    }

    ikey::delete_value_disposition
    key::delete_value(ikey::delete_value_flags flags, value_name_string_type const& name)
    {
        return m_key->delete_value(flags, name);
    }

    ikey::enumerate_value_names_and_types_disposition
    key::enumerate_value_names_and_types(
        ikey::enumerate_value_names_and_types_flags                            flags,
        std::size_t                                                            index,
        std::span<enumerate_value_names_and_types_value, std::dynamic_extent>& values_span)
    {
        return m_key->enumerate_value_names_and_types(flags, index, values_span);
    }

    ikey::get_value_size_disposition
    key::get_value_size(ikey::get_value_size_flags    flags,
                        value_name_string_type const& value_name,
                        std::size_t&                  size)
    {
        return m_key->get_value_size(flags, value_name, size);
    }

    ikey::get_value_type_disposition
    key::get_value_type(ikey::get_value_type_flags    flags,
                        value_name_string_type const& value_name,
                        reg_value_type&               type)
    {
        return m_key->get_value_type(flags, value_name, type);
    }

    ikey::get_value_disposition
    key::get_value(ikey::get_value_flags         flags,
                   value_name_string_type const& value_name,
                   reg_value_type&               type,
                   std::span<std::byte>&         value,
                   std::optional<std::size_t>&   new_bytes_required)
    {
        return m_key->get_value(flags, value_name, type, value, new_bytes_required);
    }

    ikey::set_value_disposition
    key::set_value(ikey::set_value_flags         flags,
                   value_name_string_type const& value_name,
                   reg_value_type                type,
                   std::span<std::byte const>    value)
    {
        return m_key->set_value(flags, value_name, type, value);
    }

    ikey::get_path_disposition
    key::get_path(ikey::get_path_flags flags, m::pil::key_path& path_out)
    {
        path temp_path;
        auto d   = m_key->get_path(flags, temp_path);
        path_out = private_to_public(temp_path);
        return d;
    }

} // namespace m::pil::impl::redirecting
