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

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

#include <m/pil/platform.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry.h>
#include <m/pil/registry_interfaces.h>

#define THROW_IF_NOT_ERROR_SUCCESS(e)                                                              \
    do                                                                                             \
    {                                                                                              \
        auto local_status_value = (e);                                                             \
        if (local_status_value != ERROR_SUCCESS)                                                   \
            throw std::runtime_error(#e##" returned status other than ERROR_SUCCESS");             \
    } while (false)

namespace m::pil::impl::registry::win32
{
    class hkey
    {
    public:
        constexpr hkey() noexcept: m_hkey{} {}
        constexpr hkey(hkey&& other) noexcept: m_hkey{}
        {
            using std::swap;
            swap(m_hkey, other.m_hkey);
        }

        hkey&
        operator=(hkey&& other) noexcept
        {
            using std::swap;
            swap(m_hkey, other.m_hkey);
            return *this;
        }

        ~hkey() { reset(); }

        void
        reset(HKEY new_hkey = HKEY{})
        {
            auto const old_hkey = std::exchange(m_hkey, new_hkey);
            close_hkey(old_hkey);
        }

        HKEY*
        ptr()
        {
            return &m_hkey;
        }

        constexpr HKEY
        get() const
        {
            return m_hkey;
        }

        constexpr
        operator HKEY() const
        {
            return m_hkey;
        }

    private:
        static constexpr bool
        closable_hkey(HKEY h)
        {
            return h != HKEY{} && h != HKEY_CLASSES_ROOT && h != HKEY_CURRENT_CONFIG &&
                   h != HKEY_CURRENT_USER && h != HKEY_CURRENT_USER_LOCAL_SETTINGS &&
                   h != HKEY_LOCAL_MACHINE && h != HKEY_PERFORMANCE_DATA &&
                   h != HKEY_PERFORMANCE_NLSTEXT && h != HKEY_PERFORMANCE_TEXT && h != HKEY_USERS;
        }

        static void
        close_hkey(HKEY h)
        {
            if (closable_hkey(h))
                ::RegCloseKey(h);
        }

        HKEY m_hkey;
    };

    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = default;

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;

    private:
    };

    class registry : public iregistry, public std::enable_shared_from_this<registry>
    {
    public:
        registry() = default;

        open_predefined_key_disposition
        open_predefined_key(open_predefined_key_flags flags,
                            predefined_key            pk,
                            sam                       sam_desired,
                            std::shared_ptr<ikey>&    returned_key) override;

    private:
    };

    std::shared_ptr<ikey>
    make_predefined_key(predefined_key k);

    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key()                 = default;
        key(key const& other) = delete;
        key(key&& other) noexcept;
        key(hkey&& hk) noexcept;
        ~key() = default;
        key&
        operator=(key const& other) = delete;
        key&
        operator=(key&& other) noexcept;

        friend void
        swap(key& l, key& r) noexcept
        {
            using std::swap;
            swap(l.m_hkey, r.m_hkey);
        }

        create_key_disposition
        create_key(create_key_flags                   flags,
                   std::u16string_view                name,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<ikey>&             returned_key) override;

        delete_key_disposition
        delete_key(delete_key_flags flags, std::u16string_view name, sam sam_desired) override;

        delete_tree_disposition
        delete_tree(delete_tree_flags flags, std::optional<std::u16string_view> name) override;

        enumerate_keys_disposition
        enumerate_keys(ikey::enumerate_keys_flags                      flags,
                       std::size_t                                     index,
                       std::span<std::u16string, std::dynamic_extent>& key_names) override;

        flush_disposition
        flush(flush_flags flags) override;

        open_key_disposition
        open_key(open_key_flags                     flags,
                 std::optional<std::u16string_view> key_name,
                 sam                                sam_desired,
                 std::shared_ptr<ikey>&             returned_key) override;

        query_information_key_disposition
        query_information_key(query_information_key_flags flags,
                              std::size_t&                subkey_count,
                              std::size_t&                value_count,
                              std::size_t&                security_descriptor_size,
                              m::pil::time_point&         last_write_time) override;

        rename_key_disposition
        rename_key(rename_key_flags                   flags,
                   std::optional<std::u16string_view> old_name,
                   std::u16string_view                new_name) override;

        delete_value_disposition
        delete_value(delete_value_flags flags, std::u16string_view value_name) override;

        enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(enumerate_value_names_and_types_flags flags,
                                        std::size_t                           index,
                                        std::span<enumerate_value_names_and_types_value,
                                                  std::dynamic_extent>&       values_span) override;

        get_value_size_disposition
        get_value_size(get_value_size_flags flags,
                       std::u16string_view  value_name,
                       std::size_t&         size) override;

        get_value_type_disposition
        get_value_type(get_value_type_flags flags,
                       std::u16string_view  value_name,
                       reg_value_type&      type) override;

        get_value_disposition
        get_value(get_value_flags             flags,
                  std::u16string_view         value_name,
                  reg_value_type&             type,
                  std::span<std::byte>&       value,
                  std::optional<std::size_t>& new_bytes_required) override;

        set_value_disposition
        set_value(set_value_flags            flags,
                  std::u16string_view        value_name,
                  reg_value_type             type,
                  std::span<std::byte const> value) override;

    private:
        hkey m_hkey;
    };
} // namespace m::pil::impl::registry::win32