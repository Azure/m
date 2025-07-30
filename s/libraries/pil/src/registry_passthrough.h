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
    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key();
        key(std::shared_ptr<ikey> const& key);
        key(std::shared_ptr<ikey>&& key);
        ~key() = default;
        key&
        operator=(key const& other) = delete;
        key&
        operator=(key&& other) noexcept;

        friend void
        swap(key& l, key& r) noexcept
        {
            using std::swap;
            swap(l.m_key, r.m_key);
        }

        ikey::create_key_disposition
        create_key(ikey::create_key_flags             flags,
                   std::u16string_view                name,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<ikey>&             returned_key) override;

        ikey::delete_key_disposition
        delete_key(ikey::delete_key_flags flags,
                   std::u16string_view    name,
                   sam                    sam_desired) override;

        ikey::delete_tree_disposition
        delete_tree(ikey::delete_tree_flags            flags,
                    std::optional<std::u16string_view> name) override;

        ikey::enumerate_keys_disposition
        enumerate_keys(ikey::enumerate_keys_flags                      flags,
                       std::size_t                                     index,
                       std::span<std::u16string, std::dynamic_extent>& key_names) override;

        ikey::flush_disposition
        flush(ikey::flush_flags flags) override;

        ikey::open_key_disposition
        open_key(ikey::open_key_flags               flags,
                 std::optional<std::u16string_view> key_name,
                 sam                                sam_desired,
                 std::shared_ptr<ikey>&             returned_key) override;

        ikey::query_information_key_disposition
        query_information_key(ikey::query_information_key_flags flags,
                              std::size_t&                      subkey_count,
                              std::size_t&                      value_count,
                              std::size_t&                      security_descriptor_size,
                              m::pil::time_point&               last_write_time) override;

        ikey::rename_key_disposition
        rename_key(ikey::rename_key_flags             flags,
                   std::optional<std::u16string_view> old_key_name,
                   std::u16string_view                new_key_name) override;

        ikey::delete_value_disposition
        delete_value(ikey::delete_value_flags flags, std::u16string_view value_name) override;

        ikey::enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(ikey::enumerate_value_names_and_types_flags flags,
                                        std::size_t                                 index,
                                        std::span<enumerate_value_names_and_types_value,
                                                  std::dynamic_extent>& values_span) override;

        ikey::get_value_size_disposition
        get_value_size(ikey::get_value_size_flags flags,
                       std::u16string_view        value_name,
                       std::size_t&               size) override;

        ikey::get_value_type_disposition
        get_value_type(ikey::get_value_type_flags flags,
                       std::u16string_view        value_name,
                       reg_value_type&            type) override;

        ikey::get_value_disposition
        get_value(ikey::get_value_flags       flags,
                  std::u16string_view         value_name,
                  reg_value_type&             type,
                  std::span<std::byte>&       value,
                  std::optional<std::size_t>& new_bytes_required) override;

        ikey::set_value_disposition
        set_value(ikey::set_value_flags      flags,
                  std::u16string_view        value_name,
                  reg_value_type             type,
                  std::span<std::byte const> value) override;

    private:
        std::shared_ptr<ikey> m_key;
    };
} // namespace m::pil::impl::registry::passthrough
