// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

//
//

#include "pcwstr.h"
#include "registry_win32.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::registry::win32
{
    //
    std::shared_ptr<m::pil::registry::interfaces::key>
    make_predefined_key(pil::registry::predefined_key k)
    {
        hkey hk{}; // all of these are pseudo-keys but use the managed type for tidiness

        switch (k)
        {
            using enum pil::registry::predefined_key;

            case classes_root: hk.reset(HKEY_CLASSES_ROOT); break;
            case current_user: hk.reset(HKEY_CURRENT_USER); break;
            case local_machine: hk.reset(HKEY_LOCAL_MACHINE); break;
            case users: hk.reset(HKEY_USERS); break;
            case performance_data: hk.reset(HKEY_PERFORMANCE_DATA); break;
            case current_config: hk.reset(HKEY_CURRENT_CONFIG); break;
            case current_user_local_settings: hk.reset(HKEY_CURRENT_USER_LOCAL_SETTINGS); break;
            case performance_text: hk.reset(HKEY_PERFORMANCE_TEXT); break;
            case performance_nlstext: hk.reset(HKEY_PERFORMANCE_NLSTEXT); break;

            default: throw std::runtime_error("invalid predefined key value passed");
        }

        return std::make_shared<key>(std::move(hk));
    }

    key::key(key&& other) noexcept
    {
        using std::swap;
        swap(m_hkey, other.m_hkey);
    }

    key::key(hkey&& hk) noexcept: m_hkey(std::move(hk)) {}

    key&
    key::operator=(key&& other)
    {
        using std::swap;
        swap(m_hkey, other.m_hkey);
        return *this;
    }

    m::pil::registry::interfaces::key::create_key_disposition
    key::create_key(m::pil::registry::interfaces::key::create_key_flags flags,
                    std::u16string_view                                 name,
                    std::optional<std::u16string_view>                  class_name,
                    m::pil::registry::sam                               sam_in,
                    std::optional<security_attributes>                  sa,
                    std::shared_ptr<m::pil::registry::interfaces::key>& returned_key)
    {
        if (flags != pil::registry::interfaces::key::create_key_flags{})
            throw std::runtime_error("Invalid flags to key::create_key() call");

        hkey new_key;

        auto const namez               = pcwstr(name);
        auto const class_namez         = pcwstr(class_name);
        DWORD      dwOptions           = 0;
        auto const sam_desired         = static_cast<REGSAM>(sam_in);
        auto       security_attributes = win32_security_attributes(sa);
        DWORD      dwDisposition{};

        auto status =
            ::RegCreateKeyExW(m_hkey.get(),                                        // hKey
                              namez,                                               // lpSubKey
                              0,                                                   // Reserved
                              const_cast<PWSTR>(static_cast<PCWSTR>(class_namez)), // lpClass
                              dwOptions,                                           // dwOptions
                              sam_desired,                                         // samDesired
                              security_attributes, // lpSecurityAttributes
                              new_key.ptr(),       // phkResult
                              &dwDisposition       // lpdwDisposition
            );

        if (status != ERROR_SUCCESS)
        {
            m::throw_win32_error_code(status);
        }

        returned_key = std::make_shared<key>(std::move(new_key));

        return pil::registry::interfaces::key::create_key_disposition{};
    }

    m::pil::registry::interfaces::key::delete_key_disposition
    key::delete_key(m::pil::registry::interfaces::key::delete_key_flags flags,
                    std::u16string_view                                 name,
                    m::pil::registry::sam                               sam_in)
    {
        if (flags != pil::registry::interfaces::key::delete_key_flags{})
            throw std::runtime_error("Invalid flags to key::delete_key() call");

        auto const namez       = pcwstr(name);
        auto const sam_desired = static_cast<REGSAM>(sam_in);

        auto status = ::RegDeleteKeyExW(m_hkey.get(), namez, sam_desired, 0);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return pil::registry::interfaces::key::delete_key_disposition{};
    }

    m::pil::registry::interfaces::key::delete_tree_disposition
    key::delete_tree(m::pil::registry::interfaces::key::delete_tree_flags flags,
                     std::u16string_view                                  name)
    {
        if (flags != delete_tree_flags{})
            throw std::runtime_error("Invalid flags to key::delete_tree() call");

        auto const namez = pcwstr(name);

        auto status = ::RegDeleteTreeW(m_hkey, namez);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return delete_tree_disposition{};
    }

    m::pil::registry::interfaces::key::enumerate_keys_disposition
    key::enumerate_keys(m::pil::registry::interfaces::key::enumerate_keys_flags flags,
                        std::size_t                                             index,
                        std::optional<std::u16string>&                          key_name)
    {
        key_name = std::nullopt;

        if (flags != enumerate_keys_flags{})
            throw std::runtime_error("Invalid flags to key::enumerate_keys() call");

        std::u16string str;

        // https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
        // This page documnents the maximum key length as 255 characters so we define
        // a buffer of 256 characters to allow for the terminal null character.
        //
        wchar_t key_name_buffer[256];
        key_name_buffer[0] = L'\0';

        auto status = ::RegEnumKeyW(
            m_hkey, m::to<DWORD>(index), key_name_buffer, RTL_NUMBER_OF(key_name_buffer));

        // In the case of no more items, the indicator is to leave the key_name
        // parameter with no value.
        if (status != ERROR_NO_MORE_ITEMS)
        {
            auto len      = std::char_traits<wchar_t>::length(key_name_buffer);
            auto in_begin = &key_name_buffer[0];
            auto in_end   = &key_name_buffer[len];

            str.resize_and_overwrite(len, [in_begin, in_end](char16_t* buf, std::size_t) {
                auto wch_to_char16t = [](wchar_t wch) -> char16_t {
                    return static_cast<char16_t>(wch);
                };
                auto outit = std::transform(in_begin, in_end, buf, wch_to_char16t);
                return outit - buf;
            });

            key_name = str;
        }

        return enumerate_keys_disposition{};
    }

    m::pil::registry::interfaces::key::flush_disposition
    key::flush(m::pil::registry::interfaces::key::flush_flags flags)
    {
        if (flags != flush_flags{})
            throw std::runtime_error("Invalid flags to key::flush() call");

        auto status = ::RegFlushKey(m_hkey);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return flush_disposition{};
    }

    m::pil::registry::interfaces::key::open_key_disposition
    key::open_key(m::pil::registry::interfaces::key::open_key_flags   flags,
                  std::u16string_view                                 name,
                  m::pil::registry::sam                               sam_in,
                  std::shared_ptr<m::pil::registry::interfaces::key>& returned_key)
    {
        if (flags != pil::registry::interfaces::key::open_key_flags{})
            throw std::runtime_error("Invalid flags to key::open_key() call");

        hkey new_key;

        auto const namez       = pcwstr(name);
        DWORD      ulOptions   = 0;
        auto const sam_desired = static_cast<REGSAM>(sam_in);

        auto status = ::RegOpenKeyExW(m_hkey,       // hKey
                                      namez,        // lpSubKey
                                      ulOptions,    // dwOptions
                                      sam_desired,  // samDesired
                                      new_key.ptr() // phkResult
        );

        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        returned_key = std::make_shared<key>(std::move(new_key));

        return pil::registry::interfaces::key::open_key_disposition{};
    }

    m::pil::registry::interfaces::key::query_information_key_disposition
    key::query_information_key(m::pil::registry::interfaces::key::query_information_key_flags flags,
                               std::size_t&        subkey_count,
                               std::size_t&        value_count,
                               std::size_t&        security_descriptor_size,
                               m::pil::time_point& last_write_time)
    {
        subkey_count             = 0;
        value_count              = 0;
        security_descriptor_size = 0;
        last_write_time          = (m::pil::time_point::min)();

        if (flags != query_information_key_flags{})
            throw std::runtime_error("Invalid flags to key::query_information_key() call");

        DWORD    dw_subkey_count{};
        DWORD    dw_value_count{};
        DWORD    dw_security_descriptor_size{};
        FILETIME ft_last_write_time{};

        auto status = ::RegQueryInfoKeyW(m_hkey,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         &dw_subkey_count,
                                         nullptr,
                                         nullptr,
                                         &dw_value_count,
                                         nullptr,
                                         nullptr,
                                         &dw_security_descriptor_size,
                                         &ft_last_write_time);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        subkey_count = dw_subkey_count;
        value_count  = dw_value_count;
        security_descriptor_size = dw_security_descriptor_size;
        // last_write_time          = std::chrono::time_point_cast<>;

        return query_information_key_disposition{};
    }

    m::pil::registry::interfaces::key::rename_key_disposition
        key::rename_key(m::pil::registry::interfaces::key::rename_key_flags flags,
            std::optional<std::u16string_view>                  old_name,
            std::u16string_view                                 new_name)
    {
        if (flags != rename_key_flags{})
            throw std::runtime_error("Invalid flags to key::rename_key() call");

        auto const old_namez = pcwstr(old_name);
        auto const new_namez = pcwstr(new_name);

        auto status = ::RegRenameKey(m_hkey, old_namez, new_namez);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return rename_key_disposition{};
    }

} // namespace m::pil::impl::registry::win32
