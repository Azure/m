// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <m/pil/registry.h>

namespace m::pil::impl::registry::passthrough
{
    class key : public m::pil::registry::interfaces::key, public std::enable_shared_from_this<key>
    {
    public:
        key();
        key(std::shared_ptr<m::pil::registry::interfaces::key> const& key);
        key(std::shared_ptr<m::pil::registry::interfaces::key>&& key);
        ~key() = default;
        key&
        operator=(key const& other) = delete;
        key&
        operator=(key&& other);

        friend void
        swap(key& l, key& r) noexcept
        {
            using std::swap;
            swap(l.m_key, r.m_key);
        }

        m::pil::registry::interfaces::key::create_key_disposition
        create_key(m::pil::registry::interfaces::key::create_key_flags flags,
                   std::u16string_view                                 name,
                   std::optional<std::u16string_view>                  class_name,
                   m::pil::registry::sam                               sam_desired,
                   std::optional<security_attributes>                  sa,
                   std::shared_ptr<m::pil::registry::interfaces::key>& returned_key) override;

        m::pil::registry::interfaces::key::delete_key_disposition
        delete_key(m::pil::registry::interfaces::key::delete_key_flags flags,
                   std::u16string_view                                 name,
                   m::pil::registry::sam                               sam_desired) override;

        m::pil::registry::interfaces::key::delete_tree_disposition
        delete_tree(m::pil::registry::interfaces::key::delete_tree_flags flags,
                    std::u16string_view                                  name) override;

        m::pil::registry::interfaces::key::enumerate_keys_disposition
        enumerate_keys(m::pil::registry::interfaces::key::enumerate_keys_flags flags,
                       std::size_t                                             index,
                       std::optional<std::u16string>&                          key_name) override;

        m::pil::registry::interfaces::key::flush_disposition
        flush(m::pil::registry::interfaces::key::flush_flags flags) override;

        m::pil::registry::interfaces::key::open_key_disposition
        open_key(m::pil::registry::interfaces::key::open_key_flags   flags,
                 std::u16string_view                                 name,
                 m::pil::registry::sam                               sam_desired,
                 std::shared_ptr<m::pil::registry::interfaces::key>& returned_key) override;

        m::pil::registry::interfaces::key::query_information_key_disposition
        query_information_key(m::pil::registry::interfaces::key::query_information_key_flags flags,
                              std::size_t&        subkey_count,
                              std::size_t&        value_count,
                              std::size_t&        security_descriptor_size,
                              m::pil::time_point& last_write_time) override;

        m::pil::registry::interfaces::key::rename_key_disposition
        rename_key(m::pil::registry::interfaces::key::rename_key_flags flags,
                   std::optional<std::u16string_view>                  old_name,
                   std::u16string_view                                 new_name) override;

        m::pil::registry::interfaces::key::delete_value_disposition
        delete_value(m::pil::registry::interfaces::key::delete_value_flags flags,
                     std::u16string_view                                   name) override;

        m::pil::registry::interfaces::key::enumerate_values_disposition
        enumerate_values(m::pil::registry::interfaces::key::enumerate_values_flags flags,
                         std::size_t                                               index,
                         std::u16string& value_name_buffer,
                         std::optional<m::pil::registry::interfaces::key::enumerate_values_value>&
                             value) override;

        m::pil::registry::interfaces::key::get_value_size_disposition
        get_value_size(m::pil::registry::interfaces::key::get_value_size_flags flags,
                       std::u16string_view                                     value_name,
                       std::size_t&                                            size) override;

        m::pil::registry::interfaces::key::get_value_type_disposition
        get_value_type(m::pil::registry::interfaces::key::get_value_type_flags flags,
                       std::u16string_view                                     value_name,
                       pil::registry::value_type&                              type) override;

        m::pil::registry::interfaces::key::get_value_disposition
        get_value(m::pil::registry::interfaces::key::get_value_flags flags,
                  std::u16string_view                                value_name,
                  pil::registry::value_type&                         type,
                  std::span<std::byte>&                              value,
                  std::optional<std::size_t>&                        new_bytes_required) override;

        m::pil::registry::interfaces::key::set_value_disposition
        set_value(m::pil::registry::interfaces::key::set_value_flags flags,
                  std::u16string_view                                value_name,
                  pil::registry::value_type                          type,
                  std::span<std::byte const>                         value) override;

    private:
        std::shared_ptr<m::pil::registry::interfaces::key> m_key;
    };
} // namespace m::pil::impl::registry::passthrough
