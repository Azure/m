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

#include <m/chrono/chrono.h>
#include <m/error_handling/macros.h>
#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/pil/registry_base_types.h>
#include <m/pil/key_path.h>
#include <m/pil/security_attributes.h>
#include <m/strings/convert.h>
#include <m/utility/enum_operations.h.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

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

namespace m::pil
{
    using key_name_type      = std::u16string;
    using key_name_view_type = std::u16string_view;

    using value_name_type      = std::u16string;
    using value_name_view_type = std::u16string_view;

    struct ikey
    {
        virtual ~ikey() {}

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
                   key_path const&              path,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<ikey>&             returned_key) = 0;

        std::shared_ptr<ikey>
        create_key(key_path const&              path,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa)
        {
            std::shared_ptr<ikey> returned_key;
            auto const d = create_key(create_key_flags{}, path, sam_desired, sa, returned_key);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_key;
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
        delete_key(delete_key_flags flags, key_path const& path, sam sam_desired) = 0;

        void
        delete_key(key_path const& path)
        {
            auto const d = delete_key(delete_key_flags{}, path, sam::default_delete_key);
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
        delete_tree(delete_tree_flags flags, std::optional<key_path> const& key_name) = 0;

        void
        delete_tree(std::optional<key_name_view_type> const& key_name)
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
        enumerate_keys(enumerate_keys_flags                            flags,
                       std::size_t                                     starting_index,
                       std::span<key_path, std::dynamic_extent>& key_names) = 0;

        std::optional<key_path>
        enumerate_keys(std::size_t index)
        {
            key_path key_name;
            auto           s = std::span<key_path, std::dynamic_extent>(&key_name, 1);

            auto const d = enumerate_keys(enumerate_keys_flags{}, index, s);
            M_INTERNAL_ERROR_CHECK(!d);

            if (s.size() == 0)
                return std::nullopt;

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
        open_key(open_key_flags                       flags,
                 std::optional<key_path> const& path,
                 sam                                  sam_desired,
                 std::shared_ptr<ikey>&               returned_key) = 0;

        std::shared_ptr<ikey>
        open_key(std::optional<key_path> const& path, sam sam_desired)
        {
            std::shared_ptr<ikey> returned_key;
            auto const            d = open_key(open_key_flags{}, path, sam_desired, returned_key);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_key;
        }

        std::shared_ptr<ikey>
        open_key(std::optional<key_path> const& path)
        {
            return open_key(path, sam::default_open_key);
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

        time_point
        last_write_time()
        {
            std::size_t subkey_count{};
            std::size_t value_count{};
            std::size_t security_descriptor_size{};
            time_point  lwt{};
            auto const  d = query_information_key(query_information_key_flags{},
                                                 subkey_count,
                                                 value_count,
                                                 security_descriptor_size,
                                                 lwt);
            M_INTERNAL_ERROR_CHECK(!d);
            return lwt;
        }

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
        rename_key(rename_key_flags                     flags,
                   std::optional<key_path> const& old_key_name,
                   key_path const&                new_key_name) = 0;

        void
        rename_key(std::optional<key_path> const& old_key_name,
                   key_path const&                new_key_name)
        {
            auto const d = rename_key(rename_key_flags{}, old_key_name, new_key_name);
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
        delete_value(delete_value_flags flags, value_name_view_type value_name) = 0;

        void
        delete_value(value_name_view_type value_name)
        {
            auto const d = delete_value(delete_value_flags{}, value_name);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  enumerate_value_names_and_types
        //

        enum class enumerate_value_names_and_types_flags : uint64_t
        {
        };

        enum class enumerate_value_names_and_types_result_code : uint32_t
        {
        };

        enum class enumerate_value_names_and_types_result_flags : uint32_t
        {
        };

        using enumerate_value_names_and_types_disposition =
            disposition<enumerate_value_names_and_types_result_code,
                        enumerate_value_names_and_types_result_flags>;

        struct enumerate_value_names_and_types_value
        {
            enumerate_value_names_and_types_value() = default;

            enumerate_value_names_and_types_value(value_name_view_type value_name,
                                                  reg_value_type       type):
                m_value_name(value_name), m_reg_value_type(type)
            {}

            enumerate_value_names_and_types_value(value_name_type&& value_name,
                                                  reg_value_type    type):
                m_value_name(std::move(value_name)), m_reg_value_type(type)
            {}

            enumerate_value_names_and_types_value(
                enumerate_value_names_and_types_value&& other) noexcept
            {
                using std::swap;
                swap(m_value_name, other.m_value_name);
                swap(m_reg_value_type, other.m_reg_value_type);
            }

            enumerate_value_names_and_types_value(
                enumerate_value_names_and_types_value const& other):
                m_value_name(other.m_value_name), m_reg_value_type(other.m_reg_value_type)
            {}

            ~enumerate_value_names_and_types_value() = default;

            enumerate_value_names_and_types_value&
            operator=(enumerate_value_names_and_types_value&& other) noexcept
            {
                using std::swap;
                swap(m_value_name, other.m_value_name);
                swap(m_reg_value_type, other.m_reg_value_type);
                return *this;
            }

            enumerate_value_names_and_types_value&
            operator=(enumerate_value_names_and_types_value const& other)
            {
                m_value_name     = other.m_value_name;
                m_reg_value_type = other.m_reg_value_type;
                return *this;
            }

            friend constexpr void
            swap(enumerate_value_names_and_types_value& l,
                 enumerate_value_names_and_types_value& r) noexcept
            {
                using std::swap;
                swap(l.m_value_name, r.m_value_name);
                swap(l.m_reg_value_type, r.m_reg_value_type);
            }

            value_name_type m_value_name;
            reg_value_type  m_reg_value_type{reg_value_type::none};
        };

        virtual enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(
            enumerate_value_names_and_types_flags                                  flags,
            std::size_t                                                            beginning_index,
            std::span<enumerate_value_names_and_types_value, std::dynamic_extent>& values_span) = 0;

        void
        enumerate_value_names_and_types(
            std::size_t                                                            beginning_index,
            std::span<enumerate_value_names_and_types_value, std::dynamic_extent>& values_span)
        {
            auto const d = enumerate_value_names_and_types(
                enumerate_value_names_and_types_flags{}, beginning_index, values_span);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        std::optional<enumerate_value_names_and_types_value>
        enumerate_value_names_and_types(std::size_t index)
        {
            enumerate_value_names_and_types_value                                 value_value;
            std::span<enumerate_value_names_and_types_value, std::dynamic_extent> value_span =
                std::span(&value_value, 1);

            auto const d = enumerate_value_names_and_types(
                enumerate_value_names_and_types_flags{}, index, value_span);
            M_INTERNAL_ERROR_CHECK(!d);

            if (value_span.size() != 0)
                return value_value;

            return std::nullopt;
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
                       value_name_view_type value_name,
                       std::size_t&         size) = 0;

        std::size_t
        get_value_size(value_name_view_type value_name)
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
                       value_name_view_type value_name,
                       reg_value_type&      type) = 0;

        reg_value_type
        get_value_type(value_name_view_type value_name)
        {
            reg_value_type type{};

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
        /// <param name="reg_value_type"></param>
        /// <param name="value"></param>
        /// <param name="new_bytes_required"></param>
        /// <returns></returns>
        virtual get_value_disposition
        get_value(get_value_flags             flags,
                  value_name_view_type        value_name,
                  reg_value_type&             vt,
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
                  value_name_view_type       value_name,
                  reg_value_type             type,
                  std::span<std::byte const> value) = 0;

        //
        // get_path
        //

        enum class get_path_flags : uint64_t
        {
        };

        enum class get_path_result_code : uint32_t
        {
        };

        enum class get_path_result_flags : uint32_t
        {
        };

        using get_path_disposition = disposition<get_path_result_code, get_value_result_flags>;

        virtual get_path_disposition
        get_path(get_path_flags flags, pil::key_path& path) = 0;

        pil::key_path
        get_path()
        {
            pil::key_path p;
            auto                d = get_path(get_path_flags{}, p);
            M_INTERNAL_ERROR_CHECK(!d);
            return p;
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::create_key_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::create_key_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::delete_key_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::delete_key_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::delete_tree_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::delete_tree_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::enumerate_keys_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::enumerate_keys_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::flush_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::flush_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::open_key_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::open_key_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::query_information_key_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::query_information_key_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::rename_key_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::rename_key_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::delete_value_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::delete_value_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::enumerate_value_names_and_types_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::enumerate_value_names_and_types_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::get_value_size_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::get_value_size_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::get_value_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::get_value_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::set_value_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::set_value_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::get_path_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ikey::get_path_result_flags);

    struct iregistry_monitor_change_notification
    {
        virtual void
        on_begin(utc_time_point when) = 0;

        struct requeue_key_access_attempt
        {
            std::chrono::milliseconds m_milliseconds;
        };

        virtual std::optional<requeue_key_access_attempt>
        on_key_access_failure(utc_time_point           when,
                              key_path const&    key,
                              std::system_error const& ec) = 0;

        struct requeue_change_notification_attempt
        {
            std::chrono::milliseconds m_milliseconds;
        };

        virtual std::optional<requeue_change_notification_attempt>
        on_change_notification_attempt_failure(utc_time_point           when,
                                               key_path const&    key,
                                               std::system_error const& ec) = 0;

        virtual void
        on_change(utc_time_point when, key_path const& key) = 0;

        virtual void
        on_cancelled(utc_time_point when) = 0;

    protected:
        virtual ~iregistry_monitor_change_notification() {}
    };

    struct iregistry_monitor_token
    {
        virtual ~iregistry_monitor_token() {}
    };

    struct iregistry_monitor
    {
        virtual ~iregistry_monitor() {}

        enum class register_watch_flags : uint64_t
        {
            watch_subtree     = 1ull << 0,
            key_changes       = 1ull << 1,
            attribute_changes = 1ull << 2,
            value_changes     = 1ull << 3,
            security_changes  = 1ull << 4,
        };

        enum class register_watch_result_code : uint32_t
        {
        };

        enum class register_watch_result_flags : uint32_t
        {
        };

        using register_watch_disposition =
            disposition<register_watch_result_code, register_watch_result_flags>;

        virtual register_watch_disposition
        register_watch(register_watch_flags                                flags,
                       pil::key_path const&                          path,
                       m::not_null<iregistry_monitor_change_notification*> change_notification_ptr,
                       std::unique_ptr<iregistry_monitor_token>&           returned_ptr) = 0;

        std::unique_ptr<iregistry_monitor_token>
        register_watch(pil::key_path const&                          path,
                       m::not_null<iregistry_monitor_change_notification*> change_notification_ptr)
        {
            std::unique_ptr<iregistry_monitor_token> returned_ptr;
            auto const                               d =
                register_watch(register_watch_flags{}, path, change_notification_ptr, returned_ptr);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_ptr;
        }

        std::unique_ptr<iregistry_monitor_token>
        register_watch(register_watch_flags                                flags,
                       pil::key_path const&                          path,
                       m::not_null<iregistry_monitor_change_notification*> change_notification_ptr)
        {
            std::unique_ptr<iregistry_monitor_token> returned_ptr;
            auto const d = register_watch(flags, path, change_notification_ptr, returned_ptr);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_ptr;
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iregistry_monitor::register_watch_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iregistry_monitor::register_watch_result_flags);

    struct iregistry
    {
        virtual ~iregistry() {}

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
        open_predefined_key(open_predefined_key_flags      flags,
                            predefined_key                 pk,
                            sam                            sam_desired,
                            std::shared_ptr<m::pil::ikey>& returned_key) = 0;

        std::shared_ptr<m::pil::ikey>
        open_predefined_key(predefined_key pk)
        {
            std::shared_ptr<m::pil::ikey> returned_key;
            auto const                    d = open_predefined_key(
                open_predefined_key_flags{}, pk, sam::default_open_key, returned_key);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_key;
        }

        //
        //  monitor
        //

        enum class monitor_flags : uint64_t
        {
        };

        enum class monitor_result_code : uint32_t
        {
        };

        enum class monitor_result_flags : uint32_t
        {
        };

        using monitor_disposition = disposition<monitor_result_code, monitor_result_flags>;

        virtual monitor_disposition
        monitor(monitor_flags                               flags,
                std::shared_ptr<m::pil::iregistry_monitor>& returned_registry_monitor) = 0;

        std::shared_ptr<m::pil::iregistry_monitor>
        monitor()
        {
            std::shared_ptr<m::pil::iregistry_monitor> returned_monitor;
            auto const d = monitor(monitor_flags{}, returned_monitor);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_monitor;
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iregistry::open_predefined_key_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iregistry::open_predefined_key_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iregistry::monitor_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iregistry::monitor_result_flags);

    struct hive
    {};

} // namespace m::pil