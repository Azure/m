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

    key::key(std::shared_ptr<m::pil::registry::interfaces::key> const& key): m_key(key) {}

    key::key(std::shared_ptr<m::pil::registry::interfaces::key>&& key): m_key(std::move(key)) {}

    key&
    key::operator=(key&& other)
    {
        using std::swap;
        swap(m_key, other.m_key);
        return *this;
    }

    m::pil::registry::interfaces::key::create_key_disposition
    key::create_key(m::pil::registry::interfaces::key::create_key_flags flags,
                    std::u16string_view                                 name,
                    std::optional<std::u16string_view>                  class_name,
                    m::pil::registry::sam                               sam_desired,
                    std::optional<security_attributes>                  sa,
                    std::shared_ptr<m::pil::registry::interfaces::key>& returned_key)
    {
        return m_key->create_key(flags, name, class_name, sam_desired, sa, returned_key);
    }

    m::pil::registry::interfaces::key::delete_key_disposition
    key::delete_key(m::pil::registry::interfaces::key::delete_key_flags flags,
                    std::u16string_view                                 name,
                    m::pil::registry::sam                               sam_desired)
    {
        return m_key->delete_key(flags, name, sam_desired);
    }

    m::pil::registry::interfaces::key::delete_tree_disposition
    key::delete_tree(m::pil::registry::interfaces::key::delete_tree_flags flags,
                     std::u16string_view                                  name)
    {
        return m_key->delete_tree(flags, name);
    }

    m::pil::registry::interfaces::key::enumerate_keys_disposition
    key::enumerate_keys(m::pil::registry::interfaces::key::enumerate_keys_flags flags,
                        std::size_t                                             index,
                        std::optional<std::u16string>&                          key_name)
    {
        return m_key->enumerate_keys(flags, index, key_name);
    }

    m::pil::registry::interfaces::key::flush_disposition
    key::flush(m::pil::registry::interfaces::key::flush_flags flags)
    {
        return m_key->flush(flags);
    }

    m::pil::registry::interfaces::key::open_key_disposition
    key::open_key(m::pil::registry::interfaces::key::open_key_flags   flags,
                  std::u16string_view                                 name,
                  m::pil::registry::sam                               sam_desired,
                  std::shared_ptr<m::pil::registry::interfaces::key>& returned_key)
    {
        return m_key->open_key(flags, name, sam_desired, returned_key);
    }

    m::pil::registry::interfaces::key::query_information_key_disposition
    key::query_information_key(m::pil::registry::interfaces::key::query_information_key_flags flags,
                               std::size_t&        subkey_count,
                               std::size_t&        value_count,
                               std::size_t&        security_descriptor_size,
                               m::pil::time_point& last_write_time)
    {
        return m_key->query_information_key(
            flags, subkey_count, value_count, security_descriptor_size, last_write_time);
    }

    m::pil::registry::interfaces::key::rename_key_disposition
    key::rename_key(m::pil::registry::interfaces::key::rename_key_flags flags,
                    std::optional<std::u16string_view>                  old_name,
                    std::u16string_view                                 new_name)
    {
        return m_key->rename_key(flags, old_name, new_name);
    }

    m::pil::registry::interfaces::key::delete_value_disposition
    key::delete_value(m::pil::registry::interfaces::key::delete_value_flags flags,
                      std::u16string_view                                   name)
    {
        return m_key->delete_value(flags, name);
    }

    m::pil::registry::interfaces::key::enumerate_values_disposition
    key::enumerate_values(
        m::pil::registry::interfaces::key::enumerate_values_flags                 flags,
        std::size_t                                                               index,
        std::u16string&                                                           value_name_buffer,
        std::optional<m::pil::registry::interfaces::key::enumerate_values_value>& value)
    {
        return m_key->enumerate_values(flags, index, value_name_buffer, value);
    }

    m::pil::registry::interfaces::key::get_value_size_disposition
    key::get_value_size(m::pil::registry::interfaces::key::get_value_size_flags flags,
                        std::u16string_view                                     value_name,
                        std::size_t&                                            size)
    {
        return m_key->get_value_size(flags, value_name, size);
    }

    m::pil::registry::interfaces::key::get_value_type_disposition
    key::get_value_type(m::pil::registry::interfaces::key::get_value_type_flags flags,
                        std::u16string_view                                     value_name,
                        pil::registry::value_type&                              type)
    {
        return m_key->get_value_type(flags, value_name, type);
    }

    m::pil::registry::interfaces::key::get_value_disposition
    key::get_value(m::pil::registry::interfaces::key::get_value_flags flags,
                   std::u16string_view                                value_name,
                   pil::registry::value_type&                         type,
                   std::span<std::byte>&                              value,
                   std::optional<std::size_t>&                        new_bytes_required)
    {
        return m_key->get_value(flags, value_name, type, value, new_bytes_required);
    }

    m::pil::registry::interfaces::key::set_value_disposition
    key::set_value(m::pil::registry::interfaces::key::set_value_flags flags,
                   std::u16string_view                                value_name,
                   pil::registry::value_type                          type,
                   std::span<std::byte const>                         value)
    {
        return m_key->set_value(flags, value_name, type, value);
    }
} // namespace m::pil::impl::registry::passthrough
