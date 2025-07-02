// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string>
#include <string_view>

#include <m/strings/convert.h>

#include <m/pil/registry.h>

//
// This is the implementation of the Platform Isolation Layer's
// Registry Pass-Through Key object's operations on keys. that's
// a mouthful.
//

//
// The Pass-through layer is a simple implementation of the interface which
// only passes through the calls to the next layer. It forms a basis for
// copy-pasting new implementations mostly.
//

#include "registry_passthrough.h"

namespace m::pil::impl::registry::passthrough
{
    key::key() {}

    key::key(std::shared_ptr<ikey> const& key): m_key(key) {}

    key::key(std::shared_ptr<ikey>&& key): m_key(std::move(key)) {}

    key&
    key::operator=(key&& other) noexcept
    {
        using std::swap;
        swap(m_key, other.m_key);
        return *this;
    }

    ikey::create_key_disposition
    key::create_key(ikey::create_key_flags             flags,
                    std::u16string_view                name,
                    sam                                sam_desired,
                    std::optional<security_attributes> sa,
                    std::shared_ptr<ikey>&             returned_key)
    {
        return m_key->create_key(flags, name, sam_desired, sa, returned_key);
    }

    ikey::delete_key_disposition
    key::delete_key(ikey::delete_key_flags flags, std::u16string_view name, sam sam_desired)
    {
        return m_key->delete_key(flags, name, sam_desired);
    }

    ikey::delete_tree_disposition
    key::delete_tree(ikey::delete_tree_flags flags, std::optional<std::u16string_view> name)
    {
        return m_key->delete_tree(flags, name);
    }

    ikey::enumerate_keys_disposition
    key::enumerate_keys(ikey::enumerate_keys_flags                      flags,
                        std::size_t                                     index,
                        std::span<std::u16string, std::dynamic_extent>& key_names)
    {
        return m_key->enumerate_keys(flags, index, key_names);
    }

    ikey::flush_disposition
    key::flush(ikey::flush_flags flags)
    {
        return m_key->flush(flags);
    }

    ikey::open_key_disposition
    key::open_key(ikey::open_key_flags               flags,
                  std::optional<std::u16string_view> name,
                  sam                                sam_desired,
                  std::shared_ptr<ikey>&             returned_key)
    {
        return m_key->open_key(flags, name, sam_desired, returned_key);
    }

    ikey::query_information_key_disposition
    key::query_information_key(ikey::query_information_key_flags flags,
                               std::size_t&                      subkey_count,
                               std::size_t&                      value_count,
                               std::size_t&                      security_descriptor_size,
                               m::pil::time_point&               last_write_time)
    {
        return m_key->query_information_key(
            flags, subkey_count, value_count, security_descriptor_size, last_write_time);
    }

    ikey::rename_key_disposition
    key::rename_key(ikey::rename_key_flags             flags,
                    std::optional<std::u16string_view> old_name,
                    std::u16string_view                new_name)
    {
        return m_key->rename_key(flags, old_name, new_name);
    }

    ikey::delete_value_disposition
    key::delete_value(ikey::delete_value_flags flags, std::u16string_view name)
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
    key::get_value_size(ikey::get_value_size_flags flags,
                        std::u16string_view        value_name,
                        std::size_t&               size)
    {
        return m_key->get_value_size(flags, value_name, size);
    }

    ikey::get_value_type_disposition
    key::get_value_type(ikey::get_value_type_flags flags,
                        std::u16string_view        value_name,
                        reg_value_type&            type)
    {
        return m_key->get_value_type(flags, value_name, type);
    }

    ikey::get_value_disposition
    key::get_value(ikey::get_value_flags       flags,
                   std::u16string_view         value_name,
                   reg_value_type&             type,
                   std::span<std::byte>&       value,
                   std::optional<std::size_t>& new_bytes_required)
    {
        return m_key->get_value(flags, value_name, type, value, new_bytes_required);
    }

    ikey::set_value_disposition
    key::set_value(ikey::set_value_flags      flags,
                   std::u16string_view        value_name,
                   reg_value_type             type,
                   std::span<std::byte const> value)
    {
        return m_key->set_value(flags, value_name, type, value);
    }
} // namespace m::pil::impl::registry::passthrough
