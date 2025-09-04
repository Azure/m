// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

//
//

#include "pcwstr.h"
#include "win32_registry.h"
#include "win32_security_attributes.h"

namespace m::pil::impl::registry::win32
{
    //
    std::shared_ptr<ikey>
    make_predefined_key(predefined_key pk)
    {
        m::win32::registry::hkey hk{}; // all of these are pseudo-keys but use the
                                       // managed type for tidiness
        switch (pk)
        {
            using enum predefined_key;

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

        return std::make_shared<key>(std::move(hk), m::pil::registry::path(pk));
    }

    key::key(m::win32::registry::hkey&& hk, m::pil::registry::path p):
        m_hkey(std::move(hk)), m_path(std::move(p))
    {}

    ikey::create_key_disposition
    key::create_key(create_key_flags                   flags,
                    pil::registry::path const&         relative_path,
                    sam                                sam_in,
                    std::optional<security_attributes> sa,
                    std::shared_ptr<ikey>&             returned_key)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, create_key_flags{});

        m::pil::registry::path pth = m_path + relative_path;

        m::win32::registry::hkey new_key;

        auto const namez               = pcwstr(relative_path.native().c_str());
        DWORD      dwOptions           = 0;
        auto const sam_desired         = static_cast<REGSAM>(sam_in);
        auto       security_attributes = win32_security_attributes(sa);
        DWORD      dwDisposition{};

        auto status = ::RegCreateKeyExW(m_hkey.get(),        // hKey
                                        namez,               // lpSubKey
                                        0,                   // Reserved
                                        nullptr,             // lpClass
                                        dwOptions,           // dwOptions
                                        sam_desired,         // samDesired
                                        security_attributes, // lpSecurityAttributes
                                        new_key.addressof(), // phkResult
                                        &dwDisposition       // lpdwDisposition
        );

        if (status != ERROR_SUCCESS)
        {
            m::throw_win32_error_code(status);
        }

        returned_key = std::make_shared<key>(std::move(new_key), pth);

        return create_key_disposition{};
    }

    ikey::delete_key_disposition
    key::delete_key(delete_key_flags flags, pil::registry::path const& name, sam sam_in)
    {
        if (flags != delete_key_flags{})
            throw std::runtime_error("Invalid flags to key::delete_key() call");

        auto const namez       = pcwstr(name);
        auto const sam_desired = static_cast<REGSAM>(sam_in);

        auto status = ::RegDeleteKeyExW(m_hkey.get(), namez, sam_desired, 0);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return delete_key_disposition{};
    }

    ikey::delete_tree_disposition
    key::delete_tree(ikey::delete_tree_flags flags, std::optional<pil::registry::path> const& name)
    {
        if (flags != delete_tree_flags{})
            throw std::runtime_error("Invalid flags to key::delete_tree() call");

        auto const namez = pcwstr(name);

        auto status = ::RegDeleteTreeW(m_hkey, namez);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return delete_tree_disposition{};
    }

    ikey::enumerate_keys_disposition
    key::enumerate_keys(enumerate_keys_flags                                 flags,
                        std::size_t                                          index,
                        std::span<pil::registry::path, std::dynamic_extent>& key_names)
    {
        if (flags != enumerate_keys_flags{})
            throw std::runtime_error("Invalid flags to key::enumerate_keys() call");

        std::size_t key_name_index = 0;

        while (key_name_index < key_names.size())
        {
            // https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-element-size-limits
            // This page documnents the maximum key length as 255 characters so we define
            // a buffer of 256 characters to allow for the terminal null character.
            //
            wchar_t key_name_buffer[256];
            key_name_buffer[0] = L'\0';

            auto status = ::RegEnumKeyW(
                m_hkey, m::to<DWORD>(index), key_name_buffer, RTL_NUMBER_OF(key_name_buffer));

            if (status == ERROR_NO_MORE_ITEMS)
                break;

            if (status != ERROR_SUCCESS)
                m::throw_win32_error_code(status);

            pil::registry::path new_path(key_name_buffer);

            using std::swap;
            swap(new_path, key_names[key_name_index]);

            key_name_index++;
            index++;
        }

        M_INTERNAL_ERROR_CHECK(key_name_index <= key_names.size());

        key_names = key_names.subspan(0, key_name_index);

        return enumerate_keys_disposition{};
    }

    ikey::flush_disposition
    key::flush(ikey::flush_flags flags)
    {
        if (flags != flush_flags{})
            throw std::runtime_error("Invalid flags to key::flush() call");

        auto status = ::RegFlushKey(m_hkey);
        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        return flush_disposition{};
    }

    ikey::open_key_disposition
    key::open_key(ikey::open_key_flags                      flags,
                  std::optional<pil::registry::path> const& relative_path,
                  sam                                       sam_in,
                  std::shared_ptr<ikey>&                    returned_key)
    {
        if (flags != open_key_flags{})
            throw std::runtime_error("Invalid flags to key::open_key() call");

        m::win32::registry::hkey new_key;
        m::pil::registry::path   new_path = m_path + relative_path;

        auto const namez       = pcwstr(relative_path);
        DWORD      ulOptions   = 0;
        auto const sam_desired = static_cast<REGSAM>(sam_in);

        auto status = ::RegOpenKeyExW(m_hkey,             // hKey
                                      namez,              // lpSubKey
                                      ulOptions,          // dwOptions
                                      sam_desired,        // samDesired
                                      new_key.addressof() // phkResult
        );

        if (status != ERROR_SUCCESS)
            m::throw_win32_error_code(status);

        returned_key = std::make_shared<key>(std::move(new_key), std::move(new_path));

        return open_key_disposition{};
    }

    ikey::query_information_key_disposition
    key::query_information_key(query_information_key_flags flags,
                               std::size_t&                subkey_count,
                               std::size_t&                value_count,
                               std::size_t&                security_descriptor_size,
                               m::pil::time_point&         last_write_time)
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

        subkey_count             = dw_subkey_count;
        value_count              = dw_value_count;
        security_descriptor_size = dw_security_descriptor_size;
        // last_write_time          = std::chrono::time_point_cast<>;

        return query_information_key_disposition{};
    }

    ikey::rename_key_disposition
    key::rename_key(rename_key_flags                          flags,
                    std::optional<pil::registry::path> const& old_name,
                    pil::registry::path const&                new_name)
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

    ikey::get_path_disposition
    key::get_path(ikey::get_path_flags flags, m::pil::registry::path& path_out)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, get_path_flags{});
        path_out = m_path;
        return get_path_disposition{};
    }
} // namespace m::pil::impl::registry::win32
