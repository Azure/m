// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <map>
#include <optional>
#include <tuple>

#include <m/pil/pil.h>
#include <m/windows_strings/convert.h>

using namespace std::string_literals;

#include "handle_table.h"

static std::optional<LSTATUS>
decode_win32_error(std::system_error const& se)
{
    auto const& code = se.code();
    if (code.category() == m::hresult_category())
    {
        // The fact that it's in the HRESULT category means that
        // we can perform this cast with (without?) impunity.
        auto value = static_cast<HRESULT>(code.value());

        // If it's not an NTSTATUS mapped into an HRESULT, and the severity bit
        // is set, and the facility is FACILITY_WIN32, this was "created"
        // by HRESULT_FROM_WIN32() so we'll unmap it.

        if (((value & FACILITY_NT_BIT) == 0) && (HRESULT_SEVERITY(value)) &&
            (HRESULT_FACILITY(value) == FACILITY_WIN32))
        {
            return HRESULT_CODE(value);
        }
    }

    return std::nullopt;
}

LSTATUS
APIENTRY
mRegCloseKey(_In_ HKEY hKey)
{
    try
    {
        auto h = m::mwin32_impl::handle::from_HKEY(hKey);
        g_handles.close(h);
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegOpenKeyA(_In_ HKEY hKey, _In_opt_ LPCSTR lpSubKey, _Out_ PHKEY phkResult)
{
    try
    {
        if (phkResult != nullptr)
            *phkResult = nullptr;

        auto h    = m::mwin32_impl::handle::from_HKEY(hKey);
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(h);

        std::optional<m::pil::key_path> sub_key;
        if (lpSubKey != nullptr)
            sub_key = m::pil::key_path(lpSubKey);

        auto result = ikey->open_key(sub_key);
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegOpenKeyW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey, _Out_ PHKEY phkResult)
{
    try
    {
        if (phkResult != nullptr)
            *phkResult = nullptr;

        auto h    = m::mwin32_impl::handle::from_HKEY(hKey);
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(h);

        std::optional<m::pil::key_path> sub_key;
        if (lpSubKey != nullptr)
            sub_key = m::pil::key_path(lpSubKey);

        auto result = ikey->open_key(sub_key);
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegOpenKeyExA(_In_ HKEY       hKey,
               _In_opt_ LPCSTR lpSubKey,
               _In_opt_ DWORD  ulOptions,
               _In_ REGSAM     samDesired,
               _Out_ PHKEY     phkResult)
{
    try
    {
        if (phkResult != nullptr)
            *phkResult = nullptr;

        M_VALIDATE_PARAMETER(ulOptions, ulOptions == 0);

        auto h    = m::mwin32_impl::handle::from_HKEY(hKey);
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(h);

        std::optional<m::pil::key_path> sub_key;
        if (lpSubKey != nullptr)
            sub_key = m::pil::key_path(lpSubKey);

        auto result = ikey->open_key(sub_key, static_cast<m::pil::sam>(samDesired));
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegOpenKeyExW(_In_ HKEY        hKey,
               _In_opt_ LPCWSTR lpSubKey,
               _In_opt_ DWORD   ulOptions,
               _In_ REGSAM      samDesired,
               _Out_ PHKEY      phkResult)
{
    try
    {
        if (phkResult != nullptr)
            *phkResult = nullptr;

        M_VALIDATE_PARAMETER(ulOptions, ulOptions == 0);

        auto h    = m::mwin32_impl::handle::from_HKEY(hKey);
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(h);

        std::optional<m::pil::key_path> sub_key;
        if (lpSubKey != nullptr)
            sub_key = m::pil::key_path(lpSubKey);

        auto result = ikey->open_key(sub_key, static_cast<m::pil::sam>(samDesired));
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegOverridePredefKey(_In_ HKEY hKey, _In_opt_ HKEY hNewHKey)
{
    std::ignore = hKey;
    std::ignore = hNewHKey;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegOpenUserClassesRoot(_In_ HANDLE      hToken,
                        _Reserved_ DWORD dwOptions,
                        _In_ REGSAM      samDesired,
                        _Out_ PHKEY      phkResult)
{
    std::ignore = hToken;
    std::ignore = dwOptions;
    std::ignore = samDesired;

    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegOpenCurrentUser(_In_ REGSAM samDesired, _Out_ PHKEY phkResult)
{
    std::ignore = samDesired;

    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegDisablePredefinedCache(VOID) { return ERROR_NOT_SUPPORTED; }

LSTATUS
APIENTRY
mRegDisablePredefinedCacheEx(VOID) { return ERROR_NOT_SUPPORTED; }

LSTATUS
APIENTRY
mRegConnectRegistryA(_In_opt_ LPCSTR lpMachineName, _In_ HKEY hKey, _Out_ PHKEY phkResult)
{
    std::ignore = lpMachineName;
    std::ignore = hKey;

    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegConnectRegistryW(_In_opt_ LPCWSTR lpMachineName, _In_ HKEY hKey, _Out_ PHKEY phkResult)
{
    std::ignore = lpMachineName;
    std::ignore = hKey;

    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegConnectRegistryExA(_In_opt_ LPCSTR lpMachineName,
                       _In_ HKEY       hKey,
                       _In_ ULONG      Flags,
                       _Out_ PHKEY     phkResult)
{
    std::ignore = lpMachineName;
    std::ignore = hKey;
    std::ignore = Flags;

    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegConnectRegistryExW(_In_opt_ LPCWSTR lpMachineName,
                       _In_ HKEY        hKey,
                       _In_ ULONG       Flags,
                       _Out_ PHKEY      phkResult)
{
    std::ignore = lpMachineName;
    std::ignore = hKey;
    std::ignore = Flags;

    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegCreateKeyA(_In_ HKEY hKey, _In_opt_ LPCSTR lpSubKey, _Out_ PHKEY phkResult)
{
    try
    {
        if (phkResult != nullptr)
            *phkResult = nullptr;

        auto h    = m::mwin32_impl::handle::from_HKEY(hKey);
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(h);

        auto result = ikey->create_key(m::pil::key_path(lpSubKey));
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegCreateKeyW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey, _Out_ PHKEY phkResult)
{
    try
    {
        if (phkResult != nullptr)
            *phkResult = nullptr;

        auto h    = m::mwin32_impl::handle::from_HKEY(hKey);
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(h);

        auto result = ikey->create_key(m::pil::key_path(lpSubKey));
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegCreateKeyExA(_In_ HKEY                            hKey,
                 _In_ LPCSTR                          lpSubKey,
                 _Reserved_ DWORD                     Reserved,
                 _In_opt_ LPSTR                       lpClass,
                 _In_ DWORD                           dwOptions,
                 _In_ REGSAM                          samDesired,
                 _In_opt_ CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                 _Out_ PHKEY                          phkResult,
                 _Out_opt_ LPDWORD                    lpdwDisposition)
{
    std::ignore = lpClass;
    std::ignore = Reserved;

    try
    {
        if (phkResult != nullptr)
            *phkResult = nullptr;

        if (lpdwDisposition != nullptr)
            *lpdwDisposition = 0;

        M_VALIDATE_PARAMETER(dwOptions, dwOptions == 0);

        auto h    = m::mwin32_impl::handle::from_HKEY(hKey);
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(h);

        auto                          sa = m::pil::to_security_attributes(lpSecurityAttributes);
        std::shared_ptr<m::pil::ikey> key;

        // TODO: add create_key flag for getting disposition regarding whether
        // new key was created or existing key opened: REG_CREATED_NEW_KEY vs.
        // REG_OPENED_EXISTING_KEY returned in *lpdwDisposition.
        auto                          disp = ikey->create_key(m::pil::ikey::create_key_flags{},
                                     m::pil::key_path(lpSubKey),
                                     m::pil::sam{samDesired},
                                     sa,
                                     key);

        *phkResult = g_handles.intern(key).as_HKEY();
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegCreateKeyExW(_In_ HKEY                            hKey,
                 _In_ LPCWSTR                         lpSubKey,
                 _Reserved_ DWORD                     Reserved,
                 _In_opt_ LPWSTR                      lpClass,
                 _In_ DWORD                           dwOptions,
                 _In_ REGSAM                          samDesired,
                 _In_opt_ CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                 _Out_ PHKEY                          phkResult,
                 _Out_opt_ LPDWORD                    lpdwDisposition)
{
    std::ignore = lpClass;
    std::ignore = Reserved;

    try
    {
        if (phkResult != nullptr)
            *phkResult = nullptr;

        if (lpdwDisposition != nullptr)
            *lpdwDisposition = 0;

        M_VALIDATE_PARAMETER(dwOptions, dwOptions == 0);

        auto h    = m::mwin32_impl::handle::from_HKEY(hKey);
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(h);

        auto                          sa = m::pil::to_security_attributes(lpSecurityAttributes);
        std::shared_ptr<m::pil::ikey> key;

        // TODO: add create_key flag for getting disposition regarding whether
        // new key was created or existing key opened: REG_CREATED_NEW_KEY vs.
        // REG_OPENED_EXISTING_KEY returned in *lpdwDisposition.
        auto disp = ikey->create_key(m::pil::ikey::create_key_flags{},
                                     m::pil::key_path(lpSubKey),
                                     m::pil::sam{samDesired},
                                     sa,
                                     key);

        *phkResult = g_handles.intern(key).as_HKEY();
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegCreateKeyTransactedA(_In_ HKEY                            hKey,
                         _In_ LPCSTR                          lpSubKey,
                         _Reserved_ DWORD                     Reserved,
                         _In_opt_ LPSTR                       lpClass,
                         _In_ DWORD                           dwOptions,
                         _In_ REGSAM                          samDesired,
                         _In_opt_ CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                         _Out_ PHKEY                          phkResult,
                         _Out_opt_ LPDWORD                    lpdwDisposition,
                         _In_ HANDLE                          hTransaction,
                         _Reserved_ PVOID                     pExtendedParemeter)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = Reserved;
    std::ignore = lpClass;
    std::ignore = dwOptions;
    std::ignore = samDesired;
    std::ignore = lpSecurityAttributes;
    std::ignore = hTransaction;
    std::ignore = pExtendedParemeter;

    if (lpdwDisposition != nullptr)
        *lpdwDisposition = 0;

    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegCreateKeyTransactedW(_In_ HKEY                            hKey,
                         _In_ LPCWSTR                         lpSubKey,
                         _Reserved_ DWORD                     Reserved,
                         _In_opt_ LPWSTR                      lpClass,
                         _In_ DWORD                           dwOptions,
                         _In_ REGSAM                          samDesired,
                         _In_opt_ CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                         _Out_ PHKEY                          phkResult,
                         _Out_opt_ LPDWORD                    lpdwDisposition,
                         _In_ HANDLE                          hTransaction,
                         _Reserved_ PVOID                     pExtendedParemeter)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = Reserved;
    std::ignore = lpClass;
    std::ignore = dwOptions;
    std::ignore = samDesired;
    std::ignore = lpSecurityAttributes;
    std::ignore = hTransaction;
    std::ignore = pExtendedParemeter;

    if (lpdwDisposition != nullptr)
        *lpdwDisposition = 0;

    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegDeleteKeyA(_In_ HKEY hKey, _In_ LPCSTR lpSubKey)
{
    try
    {
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        ikey->delete_key(lpSubKey);
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegDeleteKeyW(_In_ HKEY hKey, _In_ LPCWSTR lpSubKey)
{
    try
    {
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        ikey->delete_key(lpSubKey);
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegDeleteKeyExA(_In_ HKEY        hKey,
                 _In_ LPCSTR      lpSubKey,
                 _In_ REGSAM      samDesired,
                 _Reserved_ DWORD Reserved)
{
    std::ignore = Reserved;
    try
    {
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        ikey->delete_key(lpSubKey, m::pil::sam{samDesired});
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegDeleteKeyExW(_In_ HKEY        hKey,
                 _In_ LPCWSTR     lpSubKey,
                 _In_ REGSAM      samDesired,
                 _Reserved_ DWORD Reserved)
{
    std::ignore = Reserved;
    try
    {
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        ikey->delete_key(lpSubKey, m::pil::sam{samDesired});
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegDeleteKeyTransactedA(_In_ HKEY        hKey,
                         _In_ LPCSTR      lpSubKey,
                         _In_ REGSAM      samDesired,
                         _Reserved_ DWORD Reserved,
                         _In_ HANDLE      hTransaction,
                         _Reserved_ PVOID pExtendedParameter)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = samDesired;
    std::ignore = Reserved;
    std::ignore = hTransaction;
    std::ignore = pExtendedParameter;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegDeleteKeyTransactedW(_In_ HKEY        hKey,
                         _In_ LPCWSTR     lpSubKey,
                         _In_ REGSAM      samDesired,
                         _Reserved_ DWORD Reserved,
                         _In_ HANDLE      hTransaction,
                         _Reserved_ PVOID pExtendedParameter)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = samDesired;
    std::ignore = Reserved;
    std::ignore = hTransaction;
    std::ignore = pExtendedParameter;
    return ERROR_NOT_SUPPORTED;
}

LONG APIENTRY
mRegDisableReflectionKey(_In_ HKEY hBase)
{
    std::ignore = hBase;
    return ERROR_NOT_SUPPORTED;
}

LONG APIENTRY
mRegEnableReflectionKey(_In_ HKEY hBase)
{
    std::ignore = hBase;
    return ERROR_NOT_SUPPORTED;
}

LONG APIENTRY
mRegQueryReflectionKey(_In_ HKEY hBase, _Out_ BOOL* bIsReflectionDisabled)
{
    std::ignore = hBase;
    std::ignore = bIsReflectionDisabled;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegDeleteValueA(_In_ HKEY hKey, _In_opt_ LPCSTR lpValueName)
{
    try
    {
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        ikey->delete_value(lpValueName);
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegDeleteValueW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpValueName)
{
    try
    {
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        ikey->delete_value(lpValueName);
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegEnumKeyA(_In_ HKEY                       hKey,
             _In_ DWORD                      dwIndex,
             _Out_writes_opt_(cchName) LPSTR lpName,
             _In_ DWORD                      cchName)
{
    try
    {
        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        auto opk = ikey->enumerate_keys(dwIndex);

        if (!opk.has_value())
            return ERROR_NO_MORE_ITEMS;

        auto spn = m::make_span(lpName, cchName);
        std::error_code ec;
        m::to_span(m::multi_byte::cp_acp, opk.value().native().view(), spn, ec);
        if (ec)
            return ERROR_MORE_DATA;
    }
    catch (std::system_error const& se)
    {
        if (auto r = decode_win32_error(se); r.has_value())
            return r.value();

        throw;
    }

    return ERROR_SUCCESS;
}

LSTATUS
APIENTRY
mRegEnumKeyW(_In_ HKEY                        hKey,
             _In_ DWORD                       dwIndex,
             _Out_writes_opt_(cchName) LPWSTR lpName,
             _In_ DWORD                       cchName)
{
    std::ignore = hKey;
    std::ignore = dwIndex;
    std::ignore = lpName;
    std::ignore = cchName;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegEnumKeyExA(_In_ HKEY                                               hKey,
               _In_ DWORD                                              dwIndex,
               _Out_writes_to_opt_(*lpcchName, *lpcchName + 1) LPSTR   lpName,
               _Inout_ LPDWORD                                         lpcchName,
               _Reserved_ LPDWORD                                      lpReserved,
               _Out_writes_to_opt_(*lpcchClass, *lpcchClass + 1) LPSTR lpClass,
               _Inout_opt_ LPDWORD                                     lpcchClass,
               _Out_opt_ PFILETIME                                     lpftLastWriteTime)
{
    std::ignore = hKey;
    std::ignore = dwIndex;
    std::ignore = lpName;
    std::ignore = lpcchName;
    std::ignore = lpReserved;
    std::ignore = lpClass;
    std::ignore = lpcchClass;
    std::ignore = lpftLastWriteTime;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegEnumKeyExW(_In_ HKEY                                                hKey,
               _In_ DWORD                                               dwIndex,
               _Out_writes_to_opt_(*lpcchName, *lpcchName + 1) LPWSTR   lpName,
               _Inout_ LPDWORD                                          lpcchName,
               _Reserved_ LPDWORD                                       lpReserved,
               _Out_writes_to_opt_(*lpcchClass, *lpcchClass + 1) LPWSTR lpClass,
               _Inout_opt_ LPDWORD                                      lpcchClass,
               _Out_opt_ PFILETIME                                      lpftLastWriteTime)
{
    std::ignore = hKey;
    std::ignore = dwIndex;
    std::ignore = lpName;
    std::ignore = lpcchName;
    std::ignore = lpReserved;
    std::ignore = lpClass;
    std::ignore = lpcchClass;
    std::ignore = lpftLastWriteTime;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegEnumValueA(_In_ HKEY                                                       hKey,
               _In_ DWORD                                                      dwIndex,
               _Out_writes_to_opt_(*lpcchValueName, *lpcchValueName + 1) LPSTR lpValueName,
               _Inout_ LPDWORD                                                 lpcchValueName,
               _Reserved_ LPDWORD                                              lpReserved,
               _Out_opt_ LPDWORD                                               lpType,
               _Out_writes_bytes_to_opt_(*lpcbData, *lpcbData) __out_data_source(REGISTRY)
                   LPBYTE          lpData,
               _Inout_opt_ LPDWORD lpcbData)
{
    std::ignore = hKey;
    std::ignore = dwIndex;
    std::ignore = lpValueName;
    std::ignore = lpcchValueName;
    std::ignore = lpReserved;
    std::ignore = lpType;
    std::ignore = lpData;
    std::ignore = lpcbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegEnumValueW(_In_ HKEY                                                        hKey,
               _In_ DWORD                                                       dwIndex,
               _Out_writes_to_opt_(*lpcchValueName, *lpcchValueName + 1) LPWSTR lpValueName,
               _Inout_ LPDWORD                                                  lpcchValueName,
               _Reserved_ LPDWORD                                               lpReserved,
               _Out_opt_ LPDWORD                                                lpType,
               _Out_writes_bytes_to_opt_(*lpcbData, *lpcbData) __out_data_source(REGISTRY)
                   LPBYTE          lpData,
               _Inout_opt_ LPDWORD lpcbData)
{
    std::ignore = hKey;
    std::ignore = dwIndex;
    std::ignore = lpValueName;
    std::ignore = lpcchValueName;
    std::ignore = lpReserved;
    std::ignore = lpType;
    std::ignore = lpData;
    std::ignore = lpcbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegFlushKey(_In_ HKEY hKey)
{
    std::ignore = hKey;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegGetKeySecurity(_In_ HKEY                 hKey,
                   _In_ SECURITY_INFORMATION SecurityInformation,
                   _Out_writes_bytes_opt_(*lpcbSecurityDescriptor)
                       PSECURITY_DESCRIPTOR pSecurityDescriptor,
                   _Inout_ LPDWORD          lpcbSecurityDescriptor)
{
    std::ignore = hKey;
    std::ignore = SecurityInformation;
    std::ignore = lpcbSecurityDescriptor;
    std::ignore = pSecurityDescriptor;
    std::ignore = lpcbSecurityDescriptor;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegLoadKeyA(_In_ HKEY hKey, _In_opt_ LPCSTR lpSubKey, _In_ LPCSTR lpFile)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpFile;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegLoadKeyW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey, _In_ LPCWSTR lpFile)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpFile;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegNotifyChangeKeyValue(_In_ HKEY       hKey,
                         _In_ BOOL       bWatchSubtree,
                         _In_ DWORD      dwNotifyFilter,
                         _In_opt_ HANDLE hEvent,
                         _In_ BOOL       fAsynchronous)
{
    std::ignore = hKey;
    std::ignore = bWatchSubtree;
    std::ignore = dwNotifyFilter;
    std::ignore = hEvent;
    std::ignore = fAsynchronous;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegOpenKeyTransactedA(_In_ HKEY        hKey,
                       _In_opt_ LPCSTR  lpSubKey,
                       _In_opt_ DWORD   ulOptions,
                       _In_ REGSAM      samDesired,
                       _Out_ PHKEY      phkResult,
                       _In_ HANDLE      hTransaction,
                       _Reserved_ PVOID pExtendedParemeter)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = ulOptions;
    std::ignore = samDesired;
    std::ignore = phkResult;
    std::ignore = hTransaction;
    std::ignore = pExtendedParemeter;
    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegOpenKeyTransactedW(_In_ HKEY        hKey,
                       _In_opt_ LPCWSTR lpSubKey,
                       _In_opt_ DWORD   ulOptions,
                       _In_ REGSAM      samDesired,
                       _Out_ PHKEY      phkResult,
                       _In_ HANDLE      hTransaction,
                       _Reserved_ PVOID pExtendedParemeter)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = ulOptions;
    std::ignore = samDesired;
    std::ignore = phkResult;
    std::ignore = hTransaction;
    std::ignore = pExtendedParemeter;
    if (phkResult != nullptr)
        *phkResult = nullptr;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegQueryInfoKeyA(_In_ HKEY                                               hKey,
                  _Out_writes_to_opt_(*lpcchClass, *lpcchClass + 1) LPSTR lpClass,
                  _Inout_opt_ LPDWORD                                     lpcchClass,
                  _Reserved_ LPDWORD                                      lpReserved,
                  _Out_opt_ LPDWORD                                       lpcSubKeys,
                  _Out_opt_ LPDWORD                                       lpcbMaxSubKeyLen,
                  _Out_opt_ LPDWORD                                       lpcbMaxClassLen,
                  _Out_opt_ LPDWORD                                       lpcValues,
                  _Out_opt_ LPDWORD                                       lpcbMaxValueNameLen,
                  _Out_opt_ LPDWORD                                       lpcbMaxValueLen,
                  _Out_opt_ LPDWORD                                       lpcbSecurityDescriptor,
                  _Out_opt_ PFILETIME                                     lpftLastWriteTime)
{
    std::ignore = hKey;
    std::ignore = lpClass;
    std::ignore = lpcchClass;
    std::ignore = lpReserved;
    std::ignore = lpcSubKeys;
    std::ignore = lpcbMaxSubKeyLen;
    std::ignore = lpcbMaxClassLen;
    std::ignore = lpcValues;
    std::ignore = lpcbMaxValueNameLen;
    std::ignore = lpcbMaxValueLen;
    std::ignore = lpcbSecurityDescriptor;
    std::ignore = lpftLastWriteTime;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegQueryInfoKeyW(_In_ HKEY                                                hKey,
                  _Out_writes_to_opt_(*lpcchClass, *lpcchClass + 1) LPWSTR lpClass,
                  _Inout_opt_ LPDWORD                                      lpcchClass,
                  _Reserved_ LPDWORD                                       lpReserved,
                  _Out_opt_ LPDWORD                                        lpcSubKeys,
                  _Out_opt_ LPDWORD                                        lpcbMaxSubKeyLen,
                  _Out_opt_ LPDWORD                                        lpcbMaxClassLen,
                  _Out_opt_ LPDWORD                                        lpcValues,
                  _Out_opt_ LPDWORD                                        lpcbMaxValueNameLen,
                  _Out_opt_ LPDWORD                                        lpcbMaxValueLen,
                  _Out_opt_ LPDWORD                                        lpcbSecurityDescriptor,
                  _Out_opt_ PFILETIME                                      lpftLastWriteTime)
{
    std::ignore = hKey;
    std::ignore = lpClass;
    std::ignore = lpcchClass;
    std::ignore = lpReserved;
    std::ignore = lpcSubKeys;
    std::ignore = lpcbMaxSubKeyLen;
    std::ignore = lpcbMaxClassLen;
    std::ignore = lpcValues;
    std::ignore = lpcbMaxValueNameLen;
    std::ignore = lpcbMaxValueLen;
    std::ignore = lpcbSecurityDescriptor;
    std::ignore = lpftLastWriteTime;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegQueryValueA(_In_ HKEY       hKey,
                _In_opt_ LPCSTR lpSubKey,
                _Out_writes_bytes_to_opt_(*lpcbData, *lpcbData) __out_data_source(REGISTRY)
                    LPSTR         lpData,
                _Inout_opt_ PLONG lpcbData)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpData;
    std::ignore = lpcbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegQueryValueW(_In_ HKEY        hKey,
                _In_opt_ LPCWSTR lpSubKey,
                _Out_writes_bytes_to_opt_(*lpcbData, *lpcbData) __out_data_source(REGISTRY)
                    LPWSTR        lpData,
                _Inout_opt_ PLONG lpcbData)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpData;
    std::ignore = lpcbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegQueryMultipleValuesA(_In_ HKEY                       hKey,
                         _Out_writes_(num_vals) PVALENTA val_list,
                         _In_ DWORD                      num_vals,
                         _Out_writes_bytes_to_opt_(*ldwTotsize, *ldwTotsize)
                             __out_data_source(REGISTRY) LPSTR lpValueBuf,
                         _Inout_opt_ LPDWORD                   ldwTotsize)
{
    std::ignore = hKey;
    std::ignore = val_list;
    std::ignore = num_vals;
    std::ignore = lpValueBuf;
    std::ignore = ldwTotsize;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegQueryMultipleValuesW(_In_ HKEY                       hKey,
                         _Out_writes_(num_vals) PVALENTW val_list,
                         _In_ DWORD                      num_vals,
                         _Out_writes_bytes_to_opt_(*ldwTotsize, *ldwTotsize)
                             __out_data_source(REGISTRY) LPWSTR lpValueBuf,
                         _Inout_opt_ LPDWORD                    ldwTotsize)
{
    std::ignore = hKey;
    std::ignore = val_list;
    std::ignore = num_vals;
    std::ignore = lpValueBuf;
    std::ignore = ldwTotsize;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegQueryValueExA(_In_ HKEY          hKey,
                  _In_opt_ LPCSTR    lpValueName,
                  _Reserved_ LPDWORD lpReserved,
                  _Out_opt_ LPDWORD  lpType,
                  _Out_writes_bytes_to_opt_(*lpcbData, *lpcbData) __out_data_source(REGISTRY)
                      LPBYTE lpData,
                  _When_(lpData == NULL, _Out_opt_) _When_(lpData != NULL, _Inout_opt_)
                      LPDWORD lpcbData)
{
    std::ignore = hKey;
    std::ignore = lpValueName;
    std::ignore = lpReserved;
    std::ignore = lpType;
    std::ignore = lpData;
    std::ignore = lpcbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegQueryValueExW(_In_ HKEY          hKey,
                  _In_opt_ LPCWSTR   lpValueName,
                  _Reserved_ LPDWORD lpReserved,
                  _Out_opt_ LPDWORD  lpType,
                  _Out_writes_bytes_to_opt_(*lpcbData, *lpcbData) __out_data_source(REGISTRY)
                      LPBYTE lpData,
                  _When_(lpData == NULL, _Out_opt_) _When_(lpData != NULL, _Inout_opt_)
                      LPDWORD lpcbData)
{
    std::ignore = hKey;
    std::ignore = lpValueName;
    std::ignore = lpReserved;
    std::ignore = lpType;
    std::ignore = lpData;
    std::ignore = lpcbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegReplaceKeyA(_In_ HKEY       hKey,
                _In_opt_ LPCSTR lpSubKey,
                _In_ LPCSTR     lpNewFile,
                _In_ LPCSTR     lpOldFile)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpNewFile;
    std::ignore = lpOldFile;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegReplaceKeyW(_In_ HKEY        hKey,
                _In_opt_ LPCWSTR lpSubKey,
                _In_ LPCWSTR     lpNewFile,
                _In_ LPCWSTR     lpOldFile)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpNewFile;
    std::ignore = lpOldFile;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegRestoreKeyA(_In_ HKEY hKey, _In_ LPCSTR lpFile, _In_ DWORD dwFlags)
{
    std::ignore = hKey;
    std::ignore = lpFile;
    std::ignore = dwFlags;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegRestoreKeyW(_In_ HKEY hKey, _In_ LPCWSTR lpFile, _In_ DWORD dwFlags)
{
    std::ignore = hKey;
    std::ignore = lpFile;
    std::ignore = dwFlags;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegRenameKey(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKeyName, _In_ LPCWSTR lpNewKeyName)
{
    std::ignore = hKey;
    std::ignore = lpSubKeyName;
    std::ignore = lpNewKeyName;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSaveKeyA(_In_ HKEY                            hKey,
             _In_ LPCSTR                          lpFile,
             _In_opt_ CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    std::ignore = hKey;
    std::ignore = lpFile;
    std::ignore = lpSecurityAttributes;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSaveKeyW(_In_ HKEY                            hKey,
             _In_ LPCWSTR                         lpFile,
             _In_opt_ CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    std::ignore = hKey;
    std::ignore = lpFile;
    std::ignore = lpSecurityAttributes;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSetKeySecurity(_In_ HKEY                 hKey,
                   _In_ SECURITY_INFORMATION SecurityInformation,
                   _In_ PSECURITY_DESCRIPTOR pSecurityDescriptor)
{
    std::ignore = hKey;
    std::ignore = SecurityInformation;
    std::ignore = pSecurityDescriptor;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSetValueA(_In_ HKEY                           hKey,
              _In_opt_ LPCSTR                     lpSubKey,
              _In_ DWORD                          dwType,
              _In_reads_bytes_opt_(cbData) LPCSTR lpData,
              _In_ DWORD                          cbData)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = dwType;
    std::ignore = lpData;
    std::ignore = cbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSetValueW(_In_ HKEY                            hKey,
              _In_opt_ LPCWSTR                     lpSubKey,
              _In_ DWORD                           dwType,
              _In_reads_bytes_opt_(cbData) LPCWSTR lpData,
              _In_ DWORD                           cbData)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = dwType;
    std::ignore = lpData;
    std::ignore = cbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSetValueExA(_In_ HKEY                                hKey,
                _In_opt_ LPCSTR                          lpValueName,
                _Reserved_ DWORD                         Reserved,
                _In_ DWORD                               dwType,
                _In_reads_bytes_opt_(cbData) CONST BYTE* lpData,
                _In_ DWORD                               cbData)
{
    std::ignore = hKey;
    std::ignore = lpValueName;
    std::ignore = Reserved;
    std::ignore = dwType;
    std::ignore = lpData;
    std::ignore = cbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSetValueExW(_In_ HKEY                                hKey,
                _In_opt_ LPCWSTR                         lpValueName,
                _Reserved_ DWORD                         Reserved,
                _In_ DWORD                               dwType,
                _In_reads_bytes_opt_(cbData) CONST BYTE* lpData,
                _In_ DWORD                               cbData)
{
    std::ignore = hKey;
    std::ignore = lpValueName;
    std::ignore = Reserved;
    std::ignore = dwType;
    std::ignore = lpData;
    std::ignore = cbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegUnLoadKeyA(_In_ HKEY hKey, _In_opt_ LPCSTR lpSubKey)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegUnLoadKeyW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegDeleteKeyValueA(_In_ HKEY hKey, _In_opt_ LPCSTR lpSubKey, _In_opt_ LPCSTR lpValueName)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpValueName;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegDeleteKeyValueW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey, _In_opt_ LPCWSTR lpValueName)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpValueName;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSetKeyValueA(_In_ HKEY                            hKey,
                 _In_opt_ LPCSTR                      lpSubKey,
                 _In_opt_ LPCSTR                      lpValueName,
                 _In_ DWORD                           dwType,
                 _In_reads_bytes_opt_(cbData) LPCVOID lpData,
                 _In_ DWORD                           cbData)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpValueName;
    std::ignore = dwType;
    std::ignore = lpData;
    std::ignore = cbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSetKeyValueW(_In_ HKEY                            hKey,
                 _In_opt_ LPCWSTR                     lpSubKey,
                 _In_opt_ LPCWSTR                     lpValueName,
                 _In_ DWORD                           dwType,
                 _In_reads_bytes_opt_(cbData) LPCVOID lpData,
                 _In_ DWORD                           cbData)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    std::ignore = lpValueName;
    std::ignore = dwType;
    std::ignore = lpData;
    std::ignore = cbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegDeleteTreeA(_In_ HKEY hKey, _In_opt_ LPCSTR lpSubKey)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegDeleteTreeW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey)
{
    std::ignore = hKey;
    std::ignore = lpSubKey;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegCopyTreeA(_In_ HKEY hKeySrc, _In_opt_ LPCSTR lpSubKey, _In_ HKEY hKeyDest)
{
    std::ignore = hKeySrc;
    std::ignore = lpSubKey;
    std::ignore = hKeyDest;

    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegGetValueA(_In_ HKEY         hkey,
              _In_opt_ LPCSTR   lpSubKey,
              _In_opt_ LPCSTR   lpValue,
              _In_ DWORD        dwFlags,
              _Out_opt_ LPDWORD pdwType,
              _When_((dwFlags & 0x7F) == RRF_RT_REG_SZ ||
                         (dwFlags & 0x7F) == RRF_RT_REG_EXPAND_SZ ||
                         (dwFlags & 0x7F) == (RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ) ||
                         *pdwType == REG_SZ || *pdwType == REG_EXPAND_SZ,
                     _Post_z_)
                  _When_((dwFlags & 0x7F) == RRF_RT_REG_MULTI_SZ || *pdwType == REG_MULTI_SZ,
                         _Post_ _NullNull_terminated_) _Out_writes_bytes_to_opt_(*pcbData, *pcbData)
                      PVOID       pvData,
              _Inout_opt_ LPDWORD pcbData)
{
    std::ignore = hkey;
    std::ignore = lpSubKey;
    std::ignore = lpValue;
    std::ignore = dwFlags;
    std::ignore = pdwType;
    std::ignore = pvData;
    std::ignore = pcbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegGetValueW(_In_ HKEY         hkey,
              _In_opt_ LPCWSTR  lpSubKey,
              _In_opt_ LPCWSTR  lpValue,
              _In_ DWORD        dwFlags,
              _Out_opt_ LPDWORD pdwType,
              _When_((dwFlags & 0x7F) == RRF_RT_REG_SZ ||
                         (dwFlags & 0x7F) == RRF_RT_REG_EXPAND_SZ ||
                         (dwFlags & 0x7F) == (RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ) ||
                         *pdwType == REG_SZ || *pdwType == REG_EXPAND_SZ,
                     _Post_z_)
                  _When_((dwFlags & 0x7F) == RRF_RT_REG_MULTI_SZ || *pdwType == REG_MULTI_SZ,
                         _Post_ _NullNull_terminated_) _Out_writes_bytes_to_opt_(*pcbData, *pcbData)
                      PVOID       pvData,
              _Inout_opt_ LPDWORD pcbData)
{
    std::ignore = hkey;
    std::ignore = lpSubKey;
    std::ignore = lpValue;
    std::ignore = dwFlags;
    std::ignore = pdwType;
    std::ignore = pvData;
    std::ignore = pcbData;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegCopyTreeW(_In_ HKEY hKeySrc, _In_opt_ LPCWSTR lpSubKey, _In_ HKEY hKeyDest)
{
    std::ignore = hKeySrc;
    std::ignore = lpSubKey;
    std::ignore = hKeyDest;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegLoadMUIStringA(_In_ HKEY                              hKey,
                   _In_opt_ LPCSTR                        pszValue,
                   _Out_writes_bytes_opt_(cbOutBuf) LPSTR pszOutBuf,
                   _In_ DWORD                             cbOutBuf,
                   _Out_opt_ LPDWORD                      pcbData,
                   _In_ DWORD                             Flags,
                   _In_opt_ LPCSTR                        pszDirectory)
{
    std::ignore = hKey;
    std::ignore = pszValue;
    std::ignore = pszOutBuf;
    std::ignore = cbOutBuf;
    std::ignore = pcbData;
    std::ignore = Flags;
    std::ignore = pszDirectory;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegLoadMUIStringW(_In_ HKEY                               hKey,
                   _In_opt_ LPCWSTR                        pszValue,
                   _Out_writes_bytes_opt_(cbOutBuf) LPWSTR pszOutBuf,
                   _In_ DWORD                              cbOutBuf,
                   _Out_opt_ LPDWORD                       pcbData,
                   _In_ DWORD                              Flags,
                   _In_opt_ LPCWSTR                        pszDirectory)
{
    std::ignore = hKey;
    std::ignore = pszValue;
    std::ignore = pszOutBuf;
    std::ignore = cbOutBuf;
    std::ignore = pcbData;
    std::ignore = Flags;
    std::ignore = pszDirectory;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegLoadAppKeyA(_In_ LPCSTR      lpFile,
                _Out_ PHKEY      phkResult,
                _In_ REGSAM      samDesired,
                _In_ DWORD       dwOptions,
                _Reserved_ DWORD Reserved)
{
    std::ignore = lpFile;
    std::ignore = phkResult;
    std::ignore = samDesired;
    std::ignore = dwOptions;
    std::ignore = Reserved;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegLoadAppKeyW(_In_ LPCWSTR     lpFile,
                _Out_ PHKEY      phkResult,
                _In_ REGSAM      samDesired,
                _In_ DWORD       dwOptions,
                _Reserved_ DWORD Reserved)
{
    std::ignore = lpFile;
    std::ignore = phkResult;
    std::ignore = samDesired;
    std::ignore = dwOptions;
    std::ignore = Reserved;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSaveKeyExA(_In_ HKEY                            hKey,
               _In_ LPCSTR                          lpFile,
               _In_opt_ CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
               _In_ DWORD                           Flags)
{
    std::ignore = hKey;
    std::ignore = lpFile;
    std::ignore = lpSecurityAttributes;
    std::ignore = Flags;
    return ERROR_NOT_SUPPORTED;
}

LSTATUS
APIENTRY
mRegSaveKeyExW(_In_ HKEY                            hKey,
               _In_ LPCWSTR                         lpFile,
               _In_opt_ CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes,
               _In_ DWORD                           Flags)
{
    std::ignore = hKey;
    std::ignore = lpFile;
    std::ignore = lpSecurityAttributes;
    std::ignore = Flags;
    return ERROR_NOT_SUPPORTED;
}
