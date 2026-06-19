// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>

#include <m/error_handling/macros.h>
#include <m/pil/registry.h>

#include "journaling.h"

namespace m::pil::impl::journaling
{
    key::key(std::shared_ptr<ikey> const& underlying_key, std::shared_ptr<journal> const& journal_ptr):
        m_key(underlying_key), m_journal(journal_ptr)
    {
        M_INTERNAL_ERROR_CHECK(m_journal.get() != nullptr);
    }

    ikey::create_key_disposition
    key::create_key(ikey::create_key_flags             flags,
                    pil::key_path const&               key_path,
                    sam                                sam_desired,
                    std::optional<security_attributes> sa,
                    std::shared_ptr<ikey>&             returned_key)
    {
        std::shared_ptr<ikey> unmapped_returned_key;
        auto d = m_key->create_key(flags, key_path, sam_desired, sa, unmapped_returned_key);
        if (unmapped_returned_key)
            returned_key = std::make_shared<key>(unmapped_returned_key, m_journal);

        auto entry = std::make_unique<create_key_entry>(ikey::get_path(), key_path);
        m_journal->add(entry);
        return d;
    }

    ikey::delete_key_disposition
    key::delete_key(ikey::delete_key_flags flags, pil::key_path const& key_path, sam sam_desired)
    {
        auto d = m_key->delete_key(flags, key_path, sam_desired);

        auto entry = std::make_unique<delete_key_entry>(ikey::get_path(), key_path);
        m_journal->add(entry);
        return d;
    }

    ikey::delete_tree_disposition
    key::delete_tree(ikey::delete_tree_flags flags, std::optional<pil::key_path> const& key_path)
    {
        auto d = m_key->delete_tree(flags, key_path);

        auto entry = std::make_unique<delete_tree_entry>(ikey::get_path(), key_path);
        m_journal->add(entry);
        return d;
    }

    ikey::enumerate_keys_disposition
    key::enumerate_keys(ikey::enumerate_keys_flags                     flags,
                        std::size_t                                    index,
                        std::span<pil::key_path, std::dynamic_extent>& key_names)
    {
        return m_key->enumerate_keys(flags, index, key_names);
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
        auto                  d = m_key->open_key(flags, key_path, sam_desired, temp_key, ec);
        if (temp_key)
            returned_key = std::make_shared<key>(temp_key, m_journal);
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
                    std::optional<pil::key_path> const& sub_key_name,
                    pil::key_path const&                new_key_name)
    {
        auto d = m_key->rename_key(flags, sub_key_name, new_key_name);

        auto entry =
            std::make_unique<rename_key_entry>(ikey::get_path(), sub_key_name, new_key_name);
        m_journal->add(entry);
        return d;
    }

    ikey::delete_value_disposition
    key::delete_value(ikey::delete_value_flags flags, value_name_string_type const& name)
    {
        auto d = m_key->delete_value(flags, name);

        auto entry = std::make_unique<delete_value_entry>(ikey::get_path(), name);
        m_journal->add(entry);
        return d;
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
        auto d = m_key->set_value(flags, value_name, type, value);

        auto entry = std::make_unique<set_value_entry>(ikey::get_path(), value_name, type, value);
        m_journal->add(entry);
        return d;
    }

    ikey::get_path_disposition
    key::get_path(ikey::get_path_flags flags, m::pil::key_path& path_out)
    {
        return m_key->get_path(flags, path_out);
    }
} // namespace m::pil::impl::journaling
