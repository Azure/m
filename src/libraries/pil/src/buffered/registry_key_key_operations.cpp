// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/tracing/tracing.h>
#include <m/utility/make_span.h>

#include "buffered.h"

namespace m::pil::impl::buffered
{
    key::key(time_point last_write_time): m_last_write_time(last_write_time) {}

    key::key(std::shared_ptr<ikey> const& underlying_key): m_underlying_key(underlying_key)
    {
        initialize_overlay();
    }

    key::key(std::shared_ptr<ikey>&& underlying_key) noexcept:
        m_underlying_key(std::move(underlying_key))
    {
        initialize_overlay();
    }

    void
    key::initialize_overlay()
    {
        initialize_keys_overlay();
        initialize_values_overlay();
    }

    void
    key::initialize_keys_overlay()
    {
        if (!m_underlying_key)
            return;

        std::array<pil::key_path, 32>                 key_array;
        std::span<pil::key_path, std::dynamic_extent> key_span{key_array};
        std::size_t                                         index{};

        for (;;)
        {
            auto const d =
                m_underlying_key->enumerate_keys(enumerate_keys_flags{}, index, key_span);
            M_INTERNAL_ERROR_CHECK(!d); // no flags in, no disposition out

            for (auto&& e: key_span)
                m_keys.emplace(e.native(),
                               key_node{.m_key             = {},
                                        .m_last_write_time = {},
                                        .m_deleted         = false,
                                        .m_mirrored        = true});

            if (key_span.size() != key_array.size())
                break;

            index += key_array.size();
        }
    }

    ikey::create_key_disposition
    key::create_key(ikey::create_key_flags     flags,
                    pil::key_path const& key_name,
                    sam                        sam_desired,
                    std::optional<security_attributes>,
                    std::shared_ptr<ikey>& returned_key)
    {
        auto const entry_time = time_point::clock::now();

        returned_key.reset();

        if (flags != create_key_flags{})
            throw m::invalid_parameter("ikey::create_key.flags");

        if (key_name.has_parent_path())
            throw m::invalid_parameter("ikey::create_key.key_name");

        // TODO: Slice the name by path

        auto lock = std::unique_lock(m_mutex);

        //
        // For a creation, first see if we have a local definition, including
        // a tombstone. If we do, we can proceed with the creation without
        // checking in with the underlying registry, if there is one.
        //
        // If we don't already have a key registered, we may have to check
        // with the underlying registry to see if we do not have access to the
        // key and have to fail the creation on that basis.
        //
        auto [insertion_it, inserted] =
            m_keys.emplace(std::make_pair(key_name.string(),
                                          key_node{.m_key = std::make_shared<key>(entry_time),
                                                   .m_last_write_time = entry_time,
                                                   .m_deleted         = false,
                                                   .m_mirrored        = false}));

        if (inserted)
        {
            returned_key = insertion_it->second.m_key;
            return create_key_disposition{};
        }

        M_INTERNAL_ERROR_CHECK(insertion_it != m_keys.end());

        auto& node = insertion_it->second;

        if (node.m_deleted)
        {
            M_INTERNAL_ERROR_CHECK(!node.m_mirrored); // If the node was made a tombstone, it should
                                                      // have been marked as not mirrored any more

            node.m_key     = std::make_shared<key>(node.m_last_write_time);
            node.m_deleted = false;
        }
        else if (node.m_mirrored)
        {
            // Mirrored keys may not have been materialized yet.
            if (!node.m_key)
            {
                std::shared_ptr<ikey> child_key{};

                try
                {
                    child_key = m_underlying_key->open_key(key_name, sam_desired);
                }
                catch (m::not_found const&)
                {
                    // if the key is not found we are ok with this. It means
                    // that since the enumeration happened when this key
                    // was created and when the child key was opened, the subkey
                    // was deleted. Probably unusual but nonetheless it can
                    // happen.
                    //
                    // It would still be better to have a contract with open_key to
                    // not throw when the key is not found and have a disposition
                    // but for now this is acceptably scoped.
                }

                node.m_key = std::make_shared<key>(child_key);
            }

            node.m_mirrored = false;
        }

        node.m_last_write_time = entry_time;
        returned_key           = node.m_key;

        return create_key_disposition{};
    }

    ikey::delete_key_disposition
    key::delete_key(ikey::delete_key_flags flags, pil::key_path const& key_name, sam)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::delete_key", flags);

        if (key_name.has_parent_path())
            throw m::invalid_parameter("ikey::delete_key.key_name");

        auto lock = std::unique_lock(m_mutex);

        auto const find_result = m_keys.find(key_name.native());

        if (find_result == m_keys.end())
            throw m::not_found("ikey::delete_key() registry key not found");

        auto& node = find_result->second;

        if (node.m_deleted)
            throw m::not_found("ikey::delete_key() registry key not found");

        if (!is_subkey_empty(key_name))
            throw m::not_empty("ikey::delete_key() subkey not empty");

        node.m_deleted  = true;
        node.m_mirrored = false;

        return delete_key_disposition{};
    }

    ikey::delete_tree_disposition
    key::delete_tree(ikey::delete_tree_flags flags, std::optional<pil::key_path> const& name)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::delete_tree", flags);

        std::ignore = name;

        throw m::not_implemented("buffered key delete tree not implemented");

        // return delete_tree_disposition{};
    }

    ikey::enumerate_keys_disposition
    key::enumerate_keys(ikey::enumerate_keys_flags                           flags,
                        std::size_t                                          index,
                        std::span<pil::key_path, std::dynamic_extent>& key_names)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::enumerate_keys", flags);

        auto const  lock = std::unique_lock(m_mutex);
        auto        it   = m_keys.begin();
        std::size_t span_index{};

        while (it != m_keys.end() && span_index < key_names.size())
        {
            // We have to burn through the 'index' input argument
            // iterations first
            if (index != 0)
            {
                it++;
                index--;
                continue;
            }

            key_names[span_index] = pil::key_path(it->first);
            span_index++;
            it++;
        }

        M_INTERNAL_ERROR_CHECK(span_index <= key_names.size());

        key_names = key_names.subspan(0, span_index);

        return enumerate_keys_disposition{};
    }

    ikey::flush_disposition
    key::flush(ikey::flush_flags flags)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::flush", flags);

        auto lock = std::unique_lock(m_mutex);

        if (m_underlying_key)
            m_underlying_key->flush();

        return flush_disposition{};
    }

    ikey::open_key_disposition
    key::open_key(ikey::open_key_flags                      flags,
                  std::optional<pil::key_path> const& key_name,
                  sam                                       sam_desired,
                  std::shared_ptr<ikey>&                    returned_key)
    {
        returned_key.reset();

        M_API_PARAMETER_MUST_BE_ZERO("ikey::open_key", flags);

        if (key_name.has_value() && key_name.value().has_parent_path())
            throw m::invalid_parameter("ikey::open_key.key_name");

        auto lock = std::unique_lock(m_mutex);

        // key name not present is "reopen key with different security attributes"
        // we don't do anything with security today so just give a new reference
        // to the same object. To really implement security correctly we will need
        // to introduce a "handle" layer in front of the objects which is a whole
        // new level of rearchitecture.
        if (!key_name)
        {
            returned_key = shared_from_this();
            return open_key_disposition{};
        }

        auto find_iter = m_keys.find(key_name.value().native());

        if (find_iter == m_keys.end())
            throw m::not_found("ikey::open_key(): Key not found");

        auto& node = find_iter->second;

        if (node.m_deleted)
            throw m::not_found("ikey::open_key(): Key not found");

        if (node.m_mirrored)
        {
            // Mirrored keys may not have been materialized yet.
            if (!node.m_key)
            {
                std::shared_ptr<ikey> child_key{};

                try
                {
                    child_key = m_underlying_key->open_key(key_name, sam_desired);
                }
                catch (m::not_found const&)
                {
                    // if the key is not found we are ok with this. It means
                    // that since the enumeration happened when this key
                    // was created and when the child key was opened, the subkey
                    // was deleted. Probably unusual but nonetheless it can
                    // happen.
                    //
                    // It would still be better to have a contract with open_key to
                    // not throw when the key is not found and have a disposition
                    // but for now this is acceptably scoped.
                }

                node.m_key = std::make_shared<key>(child_key);
            }

            node.m_mirrored = false;
        }

        returned_key = node.m_key;

        return open_key_disposition{};
    }

    ikey::query_information_key_disposition
    key::query_information_key(ikey::query_information_key_flags flags,
                               std::size_t&                      subkey_count,
                               std::size_t&                      value_count,
                               std::size_t&                      security_descriptor_size,
                               m::pil::time_point&               last_write_time)
    {
        subkey_count             = 0;
        value_count              = 0;
        security_descriptor_size = 0;
        last_write_time          = (time_point::min)();
        M_API_PARAMETER_MUST_BE_ZERO("ikey::query_information_key", flags);

        auto lock = std::unique_lock(m_mutex);

        subkey_count             = m_keys.size();
        value_count              = m_values.size();
        security_descriptor_size = m_security_descriptor.size();
        last_write_time          = m_last_write_time;

        return query_information_key_disposition{};
    }

    ikey::rename_key_disposition
    key::rename_key(ikey::rename_key_flags                    flags,
                    std::optional<pil::key_path> const& old_key_name,
                    pil::key_path const&                new_key_name)
    {
        auto const entry_time = time_point::clock::now();

        M_API_PARAMETER_MUST_BE_ZERO("ikey::rename_key", flags);

        // we have no way to navigate to our parent in the hierarchy in the
        // buffered registry, at least not now, so with the buffered
        // registry throw the not-supported exception if the old_name is
        // omitted.
        if (!old_key_name)
            throw m::not_supported("buffered registry does not support rename_key(self, new-name)");

        auto const& old_key_name_value = old_key_name.value();

        if (old_key_name_value.has_parent_path())
            throw m::invalid_parameter("ikey::rename_key.old_key_name");

        if (new_key_name.has_parent_path())
            throw m::invalid_parameter("ikey::rename_key.new_key_name");

        auto lock = std::unique_lock(m_mutex);

        auto const find_iter = m_keys.find(old_key_name_value.native());

        if (find_iter == m_keys.end())
            throw m::not_found("ikey::rename_key(): Key not found for rename");

        auto& node = find_iter->second;
        if (node.m_deleted)
            throw m::not_found("ikey::rename_key(): Key not found for rename");

        key_node new_node{.m_key             = node.m_key,
                          .m_last_write_time = entry_time,
                          .m_deleted         = false,
                          .m_mirrored        = false};

        auto const [new_it, succeeded] = m_keys.emplace(new_key_name.string(), new_node);

        if (!succeeded)
        {
            M_INTERNAL_ERROR_CHECK(new_it != m_keys.end());

            auto& colliding_node = new_it->second;

            if (colliding_node.m_deleted)
            {
                colliding_node.m_deleted         = false;
                colliding_node.m_mirrored        = false;
                colliding_node.m_key             = node.m_key;
                colliding_node.m_last_write_time = entry_time;

                // We have taken over the new node!
            }
            else
            {
                colliding_node.m_deleted  = true;
                colliding_node.m_mirrored = false;
                colliding_node.m_key.reset();
            }
        }

        return rename_key_disposition{};
    }

    bool
    key::is_subkey_empty(pil::key_path const& key_name)
    {
        auto const it = m_keys.find(key_name.native());

        if (it == m_keys.end())
            return true;

        auto& node = it->second;

        if (node.m_deleted)
            return true;

        if (!node.m_key)
        {
            M_INTERNAL_ERROR_CHECK(node.m_mirrored);

            unmirror_node(key_name, node);

            if (node.m_mirrored)
            {
                auto const& key_name_string = key_name.native();
                auto const  c_str           = key_name_string.c_str();
                auto const  ws              = m::to_wstring(c_str);

                using namespace std::string_literals;
                m::wtrace(L"Attempt to unmirror subkey {} left the subkey mirrored",
                          ws.value_or(L">empty<"s));
                return false;
            }
        }

        pil::key_path                                 subkey_name;
        std::span<pil::key_path, std::dynamic_extent> subkey_name_span(&subkey_name, 1);

        auto const d = node.m_key->enumerate_keys(enumerate_keys_flags{}, 0, subkey_name_span);
        M_INTERNAL_ERROR_CHECK(!d);

        return subkey_name_span.size() == 0;
    }

    void
    key::unmirror_node(pil::key_path const& key_name, key_node& node)
    {
        std::ignore = key_name;
        std::ignore = node;
        // to do
    }

    ikey::get_path_disposition
    key::get_path(ikey::get_path_flags flags, m::pil::key_path& path_out)
    {
        return m_underlying_key->get_path(flags, path_out);
    }
} // namespace m::pil::impl::buffered
