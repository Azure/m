// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <m/strings/convert.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include "common.h"
#include "disposition.h"
#include "security_attributes.h"

#include "registry_base_types.h"

//
// Note that the registry values are "binary" here.
//
// This means that their values are expected to be as they would be in the
// Windows physical registry.
//
// Strings must be UTF-16 encoded with trailing null characters.
//
// The m::pil::registry convenience layer will ensure that this happens.
// In general, when working at the "interfaces" layer, you should probably
// not be worrying about the encodings of values, until you hit persistence.
// Store them, in memory, with trailing null characters. This may cause some
// issues during loading / parsing / depersistence / deserialization or
// whatever you like to call it but at saving time, ignoring the trailing
// null characters should essentially never be a problem in the cases where
// this matters. (meaning, if calling an API that requires null terminated
// strings, they will already be there. If calling an API that needs counted
// strings, you simply remove them from the count.)
//
// This is confusing in the Windows registry code also. The Win32 layer
// always checks for "string types" if the value included a trailing null and
// if the buffer has space for one if the storage did not include it. The
// Win32 layer deals entirely in null terminated strings so the inputs always
// include nulls, so they are written to storage. It's only code written to
// the NT layer that will easily write non-null-terminated REG_SZ values.
//

namespace m::pil::registry::interfaces
{
    struct key
    {
        virtual ~key() {}

        //
        //  create_key
        //

        enum class create_key_flags : uint64_t
        {
        };

        enum class create_key_result_code : uint32_t
        {
        };

        enum class create_key_result_flags : uint32_t
        {
        };

        using create_key_disposition = disposition<create_key_result_code, create_key_result_flags>;

        virtual create_key_disposition
        create_key(create_key_flags                   flags,
                   std::u16string_view                key_name,
                   std::optional<std::u16string_view> class_name,
                   m::pil::registry::sam              sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<key>&              returned_key) = 0;

        // Should this kind of convenience function be provided for each virtual
        // member? unclear.
        void
        create_key(std::u16string_view                key_name,
                   std::optional<std::u16string_view> class_name,
                   m::pil::registry::sam              sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<key>&              returned_key)
        {
            auto const d =
                create_key(create_key_flags{}, key_name, class_name, sam_desired, sa, returned_key);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  delete_key
        //

        enum class delete_key_flags : uint64_t
        {
        };

        enum class delete_key_result_code : uint32_t
        {
        };

        enum class delete_key_result_flags : uint32_t
        {
        };

        using delete_key_disposition = disposition<delete_key_result_code, delete_key_result_flags>;

        virtual delete_key_disposition
        delete_key(delete_key_flags      flags,
                   std::u16string_view   key_name,
                   m::pil::registry::sam sam_desired) = 0;

        void
        delete_key(std::u16string_view key_name)
        {
            auto const d =
                delete_key(delete_key_flags{}, key_name, m::pil::registry::sam::default_delete_key);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  delete_tree
        //

        enum class delete_tree_flags : uint64_t
        {
        };

        enum class delete_tree_result_code : uint32_t
        {
        };

        enum class delete_tree_result_flags : uint32_t
        {
        };

        using delete_tree_disposition =
            disposition<delete_tree_result_code, delete_tree_result_flags>;

        virtual delete_tree_disposition
        delete_tree(delete_tree_flags flags, std::u16string_view name) = 0;

        void
        delete_tree(std::u16string_view key_name)
        {
            auto const d = delete_tree(delete_tree_flags{}, key_name);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  enumerate_keys
        //

        enum class enumerate_keys_flags : uint64_t
        {
        };

        enum class enumerate_keys_result_code : uint32_t
        {
        };

        enum class enumerate_keys_result_flags : uint32_t
        {
        };

        using enumerate_keys_disposition =
            disposition<enumerate_keys_result_code, enumerate_keys_result_flags>;

        /// <summary>
        /// Enumerates, one by one, the subkeys' names under this key.
        ///
        /// When the end of the list is reached, returns a subkey name in
        /// the std::optional<> in `key_name` that has no value.
        /// </summary>
        /// <param name="flags"></param>
        /// <param name="index"></param>
        /// <param name="key_name"></param>
        /// <returns></returns>
        virtual enumerate_keys_disposition
        enumerate_keys(enumerate_keys_flags           flags,
                       std::size_t                    index,
                       std::optional<std::u16string>& key_name) = 0;

        std::optional<std::u16string>
        enumerate_keys(std::size_t index)
        {
            std::optional<std::u16string> key_name;

            auto const d = enumerate_keys(enumerate_keys_flags{}, index, key_name);
            M_INTERNAL_ERROR_CHECK(!d);
            return key_name;
        }

        //
        //  flush
        //

        enum class flush_flags : uint64_t
        {
        };

        enum class flush_result_code : uint32_t
        {
        };

        enum class flush_result_flags : uint32_t
        {
        };

        using flush_disposition = disposition<flush_result_code, flush_result_flags>;

        virtual flush_disposition
        flush(flush_flags flags) = 0;

        void
        flush()
        {
            auto const d = flush(flush_flags{});
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  open_key
        //

        enum class open_key_flags : uint64_t
        {
            open_link = 1ull < 0, // semantically maps to REG_OPTION_OPEN_LINK
        };

        enum class open_key_result_code : uint32_t
        {
        };

        enum class open_key_result_flags : uint32_t
        {
        };

        using open_key_disposition = disposition<open_key_result_code, open_key_result_flags>;

        virtual open_key_disposition
        open_key(open_key_flags        flags,
                 std::u16string_view   key_name,
                 m::pil::registry::sam sam_desired,
                 std::shared_ptr<key>& returned_key) = 0;

        std::shared_ptr<key>
        open_key(std::u16string_view key_name)
        {
            std::shared_ptr<key> returned_key;
            auto const           d = open_key(
                open_key_flags{}, key_name, m::pil::registry::sam::default_open_key, returned_key);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_key;
        }

        //
        //  query_information_key
        //

        enum class query_information_key_flags : uint64_t
        {
        };

        enum class query_information_key_result_code : uint32_t
        {
        };

        enum class query_information_key_result_flags : uint32_t
        {
        };

        using query_information_key_disposition =
            disposition<query_information_key_result_code, query_information_key_result_flags>;

        virtual query_information_key_disposition
        query_information_key(query_information_key_flags flags,
                              std::size_t&                subkey_count,
                              std::size_t&                value_count,
                              std::size_t&                security_descriptor_size,
                              m::pil::time_point&         last_write_time) = 0;

        //
        //  rename_key
        //

        enum class rename_key_flags : uint64_t
        {
        };

        enum class rename_key_result_code : uint32_t
        {
        };

        enum class rename_key_result_flags : uint32_t
        {
        };

        using rename_key_disposition = disposition<rename_key_result_code, rename_key_result_flags>;

        virtual rename_key_disposition
        rename_key(rename_key_flags                   flags,
                   std::optional<std::u16string_view> old_name,
                   std::u16string_view                new_name) = 0;

        void
        rename_key(std::optional<std::u16string_view> old_name, std::u16string_view new_name)
        {
            auto const d = rename_key(rename_key_flags{}, old_name, new_name);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  delete_value
        //

        enum class delete_value_flags : uint64_t
        {
        };

        enum class delete_value_result_code : uint32_t
        {
        };

        enum class delete_value_result_flags : uint32_t
        {
        };

        using delete_value_disposition =
            disposition<delete_value_result_code, delete_value_result_flags>;

        virtual delete_value_disposition
        delete_value(delete_value_flags flags, std::u16string_view value_name) = 0;

        void
        delete_value(std::u16string_view value_name)
        {
            auto const d = delete_value(delete_value_flags{}, value_name);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  enumerate_values
        //

        enum class enumerate_values_flags : uint64_t
        {
        };

        enum class enumerate_values_result_code : uint32_t
        {
        };

        enum class enumerate_values_result_flags : uint32_t
        {
        };

        using enumerate_values_disposition =
            disposition<enumerate_values_result_code, enumerate_values_result_flags>;

        struct enumerate_values_value
        {
            std::u16string_view m_value_name; // Name of the value
            value_type          m_type;
        };

        virtual enumerate_values_disposition
        enumerate_values(enumerate_values_flags                 flags,
                         std::size_t                            index,
                         std::u16string&                        value_name_buffer,
                         std::optional<enumerate_values_value>& value) = 0;

        std::optional<enumerate_values_value>
        enumerate_values(std::size_t index, std::u16string& value_name_buffer)
        {
            std::optional<enumerate_values_value> value_value;

            auto const d =
                enumerate_values(enumerate_values_flags{}, index, value_name_buffer, value_value);
            M_INTERNAL_ERROR_CHECK(!!d);
            return value_value;
        }

        //
        //  get_value_size
        //

        enum class get_value_size_flags : uint64_t
        {
        };

        enum class get_value_size_result_code : uint32_t
        {
        };

        enum class get_value_size_result_flags : uint32_t
        {
        };

        using get_value_size_disposition =
            disposition<get_value_size_result_code, get_value_size_result_flags>;

        virtual get_value_size_disposition
        get_value_size(get_value_size_flags flags,
                       std::u16string_view  value_name,
                       std::size_t&         size) = 0;

        std::size_t
        get_value_size(std::u16string_view value_name)
        {
            std::size_t size{};

            auto const d = get_value_size(get_value_size_flags{}, value_name, size);
            M_INTERNAL_ERROR_CHECK(!d);

            return size;
        }

        //
        //  get_value_type
        //

        enum class get_value_type_flags : uint64_t
        {
        };

        enum class get_value_type_result_code : uint32_t
        {
        };

        enum class get_value_type_result_flags : uint32_t
        {
        };

        using get_value_type_disposition =
            disposition<get_value_type_result_code, get_value_type_result_flags>;

        virtual get_value_type_disposition
        get_value_type(get_value_type_flags flags,
                       std::u16string_view  value_name,
                       value_type&          type) = 0;

        value_type
        get_value_type(std::u16string_view value_name)
        {
            value_type type{};

            auto const d = get_value_type(get_value_type_flags{}, value_name, type);
            M_INTERNAL_ERROR_CHECK(!d);

            return type;
        }

        //
        //  get_value
        //

        enum class get_value_flags : uint64_t
        {
        };

        enum class get_value_result_code : uint32_t
        {
        };

        enum class get_value_result_flags : uint32_t
        {
        };

        using get_value_disposition = disposition<get_value_result_code, get_value_result_flags>;

        /// <summary>
        /// Gets the registry value named by `value_name` into the buffer at
        /// `value`. If the buffer is not large enough, `new_bytes_required`
        /// is populated with the number of bytes required to hold the value.
        ///
        /// Note that registry values may be changing concurrently with the
        /// application running so in general the application must be
        /// prepared for the buffer size requirements to increase over
        /// initial estimates.
        ///
        /// Best practices are to use this API in a loop, with a self-check
        /// that the new required buffer size is larger than the previous one
        /// supplied.
        /// </summary>
        /// <param name="flags"></param>
        /// <param name="value_name"></param>
        /// <param name="value_type"></param>
        /// <param name="value"></param>
        /// <param name="new_bytes_required"></param>
        /// <returns></returns>
        virtual get_value_disposition
        get_value(get_value_flags             flags,
                  std::u16string_view         value_name,
                  value_type&                 vt,
                  std::span<std::byte>&       value,
                  std::optional<std::size_t>& new_bytes_required) = 0;

        //
        //  set_value
        //

        enum class set_value_flags : uint64_t
        {
        };

        enum class set_value_result_code : uint32_t
        {
        };

        enum class set_value_result_flags : uint32_t
        {
        };

        using set_value_disposition = disposition<set_value_result_code, set_value_result_flags>;

        virtual set_value_disposition
        set_value(set_value_flags            flags,
                  std::u16string_view        value_name,
                  value_type                 type,
                  std::span<std::byte const> value) = 0;
    };

    struct registry
    {
        virtual ~registry() {}

        //
        //  open_predefined_key
        //

        enum class open_predefined_key_flags : uint64_t
        {
        };

        enum class open_predefined_key_result_code : uint32_t
        {
        };

        enum class open_predefined_key_result_flags : uint32_t
        {
        };

        using open_predefined_key_disposition =
            disposition<open_predefined_key_result_code, open_predefined_key_result_flags>;

        virtual open_predefined_key_disposition
        open_predefined_key(open_predefined_key_flags                           flags,
                            m::pil::registry::predefined_key                    pk,
                            m::pil::registry::sam                               sam_desired,
                            std::shared_ptr<m::pil::registry::interfaces::key>& returned_key) = 0;

        std::shared_ptr<m::pil::registry::interfaces::key>
        open_predefined_key(m::pil::registry::predefined_key pk

        )
        {
            std::shared_ptr<m::pil::registry::interfaces::key> returned_key;
            auto const d = open_predefined_key(open_predefined_key_flags{},
                                               pk,
                                               m::pil::registry::sam::default_open_key,
                                               returned_key);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_key;
        }
    };

    struct hive
    {};
} // namespace m::pil::registry::interfaces