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

#include <m/pil/registry.h>

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
        constexpr hkey(hkey&& other): m_hkey{}
        {
            using std::swap;
            swap(m_hkey, other.m_hkey);
        }

        hkey&
        operator=(hkey&& other)
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

    std::shared_ptr<m::pil::registry::interfaces::key>
    make_predefined_key(pil::registry::predefined_key k);

    class key : public m::pil::registry::interfaces::key, public std::enable_shared_from_this<key>
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
        operator=(key&& other);

        friend void
        swap(key& l, key& r) noexcept
        {
            using std::swap;
            swap(l.m_hkey, r.m_hkey);
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
        hkey m_hkey;
    };

} // namespace m::pil::impl::registry::win32