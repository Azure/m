// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include <m/cp_acp/convert_acp_to.h>
#include <m/cp_acp/convert_to_acp.h>
#include <m/pil/pil.h>
#include <m/utility/exception.h>
#include <m/windows_strings/convert.h>

using namespace std::string_literals;

#include "handle_table.h"
#include "win32_error_mapping.h"

//
// Translate the in-flight C++ exception raised while servicing a registry entry
// point into a Win32 LSTATUS. This MUST be called from within a catch block: it
// rethrows the active exception (through map_known_pil_exception) so the dynamic
// type can be matched, then maps the recognized categories to their Win32
// status. An exception the shim does not recognize is rethrown so it continues
// to propagate exactly as it did before this helper existed.
//
static LSTATUS
registry_exception_to_lstatus()
{
    auto const code = m::mwin32_impl::map_known_pil_exception();
    if (code.has_value())
        return static_cast<LSTATUS>(code.value());

    throw;
}

//
// Shallow conversion from a Win32 SECURITY_ATTRIBUTES to the platform-neutral
// pil::security_attributes. This only copies the inherit-handle flag and the
// pointer/length of the security descriptor; it does not deep-copy the
// descriptor itself. Lives here (rather than in pil) so the platform-neutral
// pil headers don't have to take a dependency on <Windows.h>.
//
static std::optional<m::pil::security_attributes>
to_security_attributes(LPSECURITY_ATTRIBUTES sa)
{
    if (sa == nullptr)
        return std::nullopt;

    return m::pil::security_attributes{.m_security_descriptor        = sa->lpSecurityDescriptor,
                                       .m_security_descriptor_length = sa->nLength,
                                       .m_inherit_handle             = !!sa->bInheritHandle};
}

//
// Conversion helpers for the registry entry points. The wide (*W) overloads
// pass UTF-16 straight through; the ANSI (*A) overloads interpret their narrow
// strings in the process's ANSI code page (CP_ACP), matching the documented
// behavior of the Win32 *A registry APIs, rather than assuming UTF-8.
//
static m::pil::key_path
to_key_path(LPCWSTR p)
{
    return m::pil::key_path(p);
}

static m::pil::key_path
to_key_path(LPCSTR p)
{
    auto const u16 = m::acp_to_basic_string<char16_t>(p);
    return m::pil::key_path(std::u16string_view(u16));
}

static m::pil::value_name_string_type
to_value_name(LPCWSTR p)
{
    return m::pil::to_value_name_string_type(p);
}

static m::pil::value_name_string_type
to_value_name(LPCSTR p)
{
    auto const u16 = m::acp_to_basic_string<char16_t>(p);
    return m::pil::to_value_name_string_type(std::u16string_view(u16));
}

//
// Shared implementation of the "raw bytes" query path used by
// mRegQueryValueExW and by the non-string branch of mRegQueryValueExA. The
// returned bytes are exactly the bytes stored under the value (no encoding
// conversion). Follows the Win32 RegQueryValueEx contract:
//   * lpData == NULL          -> size/type query; *lpcbData receives the
//                                required size, *lpType the type, ERROR_SUCCESS.
//   * buffer too small        -> *lpcbData receives the required size,
//                                *lpType the type, ERROR_MORE_DATA.
//   * success                 -> data copied, *lpcbData the actual size,
//                                *lpType the type, ERROR_SUCCESS.
//
static LSTATUS
raw_query_value(std::shared_ptr<m::pil::ikey> const&  ikey,
                m::pil::value_name_string_type const& name,
                LPDWORD                               lpType,
                LPBYTE                                lpData,
                LPDWORD                               lpcbData)
{
    m::pil::reg_value_type     vt{};
    std::optional<std::size_t> new_bytes_required;

    std::size_t const capacity = (lpcbData != nullptr) ? *lpcbData : 0u;

    std::span<std::byte> value_span;
    if (lpData != nullptr)
        value_span = std::span<std::byte>(reinterpret_cast<std::byte*>(lpData), capacity);

    ikey->get_value(m::pil::ikey::get_value_flags{}, name, vt, value_span, new_bytes_required);

    if (new_bytes_required.has_value())
    {
        // Either the caller's buffer was too small, or this was a size query
        // (lpData == NULL) against a non-empty value. The more-data path does
        // not necessarily populate the type, so fetch it explicitly.
        if (lpType != nullptr)
            *lpType = static_cast<DWORD>(ikey->get_value_type(name));
        if (lpcbData != nullptr)
            *lpcbData = static_cast<DWORD>(new_bytes_required.value());

        return (lpData == nullptr) ? ERROR_SUCCESS : ERROR_MORE_DATA;
    }

    if (lpType != nullptr)
        *lpType = static_cast<DWORD>(vt);
    if (lpcbData != nullptr)
        *lpcbData = static_cast<DWORD>(value_span.size());

    return ERROR_SUCCESS;
}

//
// Registry value types whose DATA is textual. For these the ANSI (*A) entry
// points convert the value data between CP_ACP and the UTF-16 form that is
// stored in the registry; all other types carry their bytes through
// unchanged.
//
static bool
is_string_value_type(m::pil::reg_value_type type)
{
    using rvt = m::pil::reg_value_type;
    return type == rvt::string || type == rvt::expand_string || type == rvt::link ||
           type == rvt::multi_string;
}

//
// Reads the full UTF-16 value stored under `name` and returns it as a span of
// bytes backed by `storage`. Loops to tolerate the value growing concurrently
// between the size probe and the read, as the ikey::get_value contract warns.
//
static std::span<std::byte const>
read_full_value(std::shared_ptr<m::pil::ikey> const&  ikey,
                m::pil::value_name_string_type const& name,
                std::vector<std::byte>&               storage)
{
    std::size_t capacity = ikey->get_value_size(name);

    for (;;)
    {
        storage.resize(capacity);

        m::pil::reg_value_type     vt{};
        std::optional<std::size_t> new_bytes_required;
        std::span<std::byte>       span(storage);

        ikey->get_value(m::pil::ikey::get_value_flags{}, name, vt, span, new_bytes_required);

        if (!new_bytes_required.has_value())
            return std::span<std::byte const>(storage.data(), span.size());

        capacity = new_bytes_required.value();
    }
}

//
// Implements the ANSI string-data query path for mRegQueryValueExA. The value
// is stored as UTF-16; it is read in full, converted to CP_ACP, and then
// emitted into the caller's buffer following the Win32 RegQueryValueEx
// contract (size/type query, ERROR_MORE_DATA, success). Buffer sizes are
// expressed in ANSI bytes, matching what an ANSI caller expects.
//
static LSTATUS
string_query_value_a(std::shared_ptr<m::pil::ikey> const&  ikey,
                     m::pil::value_name_string_type const& name,
                     m::pil::reg_value_type                type,
                     LPDWORD                               lpType,
                     LPBYTE                                lpData,
                     LPDWORD                               lpcbData)
{
    std::vector<std::byte> storage;
    auto const             wide_bytes = read_full_value(ikey, name, storage);

    auto const wide_view = std::u16string_view(reinterpret_cast<char16_t const*>(wide_bytes.data()),
                                                wide_bytes.size() / sizeof(char16_t));
    auto const ansi      = m::to_acp_string(wide_view);

    if (lpType != nullptr)
        *lpType = static_cast<DWORD>(type);

    std::size_t const ansi_size = ansi.size();

    if (lpData == nullptr)
    {
        if (lpcbData != nullptr)
            *lpcbData = static_cast<DWORD>(ansi_size);
        return ERROR_SUCCESS;
    }

    std::size_t const capacity = (lpcbData != nullptr) ? *lpcbData : 0u;

    if (capacity < ansi_size)
    {
        if (lpcbData != nullptr)
            *lpcbData = static_cast<DWORD>(ansi_size);
        return ERROR_MORE_DATA;
    }

    if (ansi_size != 0)
        std::memcpy(lpData, ansi.data(), ansi_size);
    if (lpcbData != nullptr)
        *lpcbData = static_cast<DWORD>(ansi_size);

    return ERROR_SUCCESS;
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
    catch (...)
    {
        return registry_exception_to_lstatus();
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
            sub_key = to_key_path(lpSubKey);

        auto result = ikey->open_key(sub_key);
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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
            sub_key = to_key_path(lpSubKey);

        auto result = ikey->open_key(sub_key);
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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
            sub_key = to_key_path(lpSubKey);

        auto result = ikey->open_key(sub_key, static_cast<m::pil::sam>(samDesired));
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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
            sub_key = to_key_path(lpSubKey);

        auto result = ikey->open_key(sub_key, static_cast<m::pil::sam>(samDesired));
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        auto result = ikey->create_key(to_key_path(lpSubKey));
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        auto result = ikey->create_key(to_key_path(lpSubKey));
        *phkResult  = g_handles.intern(result).as_HKEY();
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        auto                          sa = to_security_attributes(lpSecurityAttributes);
        std::shared_ptr<m::pil::ikey> key;

        // TODO: add create_key flag for getting disposition regarding whether
        // new key was created or existing key opened: REG_CREATED_NEW_KEY vs.
        // REG_OPENED_EXISTING_KEY returned in *lpdwDisposition.
        auto                          disp = ikey->create_key(m::pil::ikey::create_key_flags{},
                                     to_key_path(lpSubKey),
                                     m::pil::sam{samDesired},
                                     sa,
                                     key);

        *phkResult = g_handles.intern(key).as_HKEY();
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        auto                          sa = to_security_attributes(lpSecurityAttributes);
        std::shared_ptr<m::pil::ikey> key;

        // TODO: add create_key flag for getting disposition regarding whether
        // new key was created or existing key opened: REG_CREATED_NEW_KEY vs.
        // REG_OPENED_EXISTING_KEY returned in *lpdwDisposition.
        auto disp = ikey->create_key(m::pil::ikey::create_key_flags{},
                                     to_key_path(lpSubKey),
                                     m::pil::sam{samDesired},
                                     sa,
                                     key);

        *phkResult = g_handles.intern(key).as_HKEY();
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        ikey->delete_key(to_key_path(lpSubKey));
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        ikey->delete_key(to_key_path(lpSubKey));
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        ikey->delete_key(to_key_path(lpSubKey), m::pil::sam{samDesired});
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        ikey->delete_key(to_key_path(lpSubKey), m::pil::sam{samDesired});
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        ikey->delete_value(to_value_name(lpValueName));
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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

        ikey->delete_value(to_value_name(lpValueName));
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
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
    catch (...)
    {
        return registry_exception_to_lstatus();
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
    try
    {
        if (lpReserved != nullptr)
            return ERROR_INVALID_PARAMETER;

        // The value-name buffer and its character count are mandatory.
        if (lpValueName == nullptr || lpcchValueName == nullptr)
            return ERROR_INVALID_PARAMETER;

        // Win32 requires a size pointer whenever a data buffer is supplied.
        if (lpData != nullptr && lpcbData == nullptr)
            return ERROR_INVALID_PARAMETER;

        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        auto const value = ikey->enumerate_value_names_and_types(dwIndex);
        if (!value.has_value())
            return ERROR_NO_MORE_ITEMS;

        auto const& name      = value.value().m_value_name;
        auto const  ansi_name = m::to_acp_string(name.view());

        // The name count is in characters and includes room for the
        // terminating null on input; on success it receives the character
        // count excluding the null.
        if (*lpcchValueName < ansi_name.size() + 1)
            return ERROR_MORE_DATA;

        if (!ansi_name.empty())
            std::memcpy(lpValueName, ansi_name.data(), ansi_name.size());
        lpValueName[ansi_name.size()] = '\0';
        *lpcchValueName               = static_cast<DWORD>(ansi_name.size());

        // The data and type follow the RegQueryValueExA contract: string-typed
        // values are converted from their stored UTF-16 form to CP_ACP; all
        // other types carry their bytes through unchanged.
        auto const type = ikey->get_value_type(name);

        if (is_string_value_type(type))
            return string_query_value_a(ikey, name, type, lpType, lpData, lpcbData);

        return raw_query_value(ikey, name, lpType, lpData, lpcbData);
    }
    catch (m::not_found const&)
    {
        return ERROR_NO_MORE_ITEMS;
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
    }
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
    try
    {
        if (lpReserved != nullptr)
            return ERROR_INVALID_PARAMETER;

        // The value-name buffer and its character count are mandatory.
        if (lpValueName == nullptr || lpcchValueName == nullptr)
            return ERROR_INVALID_PARAMETER;

        // Win32 requires a size pointer whenever a data buffer is supplied.
        if (lpData != nullptr && lpcbData == nullptr)
            return ERROR_INVALID_PARAMETER;

        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        auto const value = ikey->enumerate_value_names_and_types(dwIndex);
        if (!value.has_value())
            return ERROR_NO_MORE_ITEMS;

        auto const& name      = value.value().m_value_name;
        auto const  name_view = name.view();

        // The name count is in characters and includes room for the
        // terminating null on input; on success it receives the character
        // count excluding the null.
        if (*lpcchValueName < name_view.size() + 1)
            return ERROR_MORE_DATA;

        if (!name_view.empty())
            std::memcpy(lpValueName, name_view.data(), name_view.size() * sizeof(wchar_t));
        lpValueName[name_view.size()] = L'\0';
        *lpcchValueName               = static_cast<DWORD>(name_view.size());

        // The data and type follow the RegQueryValueEx raw-bytes contract; the
        // W path stores the value bytes verbatim (no encoding conversion).
        return raw_query_value(ikey, name, lpType, lpData, lpcbData);
    }
    catch (m::not_found const&)
    {
        return ERROR_NO_MORE_ITEMS;
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
    }
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
    try
    {
        if (lpReserved != nullptr)
            return ERROR_INVALID_PARAMETER;

        // Win32 requires a size pointer whenever a data buffer is supplied.
        if (lpData != nullptr && lpcbData == nullptr)
            return ERROR_INVALID_PARAMETER;

        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        auto const name = to_value_name(lpValueName);
        auto const type = ikey->get_value_type(name);

        if (is_string_value_type(type))
            return string_query_value_a(ikey, name, type, lpType, lpData, lpcbData);

        return raw_query_value(ikey, name, lpType, lpData, lpcbData);
    }
    catch (m::not_found const&)
    {
        return ERROR_FILE_NOT_FOUND;
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
    }
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
    try
    {
        if (lpReserved != nullptr)
            return ERROR_INVALID_PARAMETER;

        // Win32 requires a size pointer whenever a data buffer is supplied.
        if (lpData != nullptr && lpcbData == nullptr)
            return ERROR_INVALID_PARAMETER;

        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        return raw_query_value(ikey, to_value_name(lpValueName), lpType, lpData, lpcbData);
    }
    catch (m::not_found const&)
    {
        return ERROR_FILE_NOT_FOUND;
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
    }
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
    try
    {
        if (Reserved != 0)
            return ERROR_INVALID_PARAMETER;

        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        auto const name = to_value_name(lpValueName);
        auto const type = static_cast<m::pil::reg_value_type>(dwType);

        if (is_string_value_type(type) && lpData != nullptr)
        {
            // The *A registry APIs interpret string DATA in CP_ACP. Convert the
            // entire buffer (preserving any embedded and trailing NULs, which
            // matters for REG_MULTI_SZ) to UTF-16 and store the wide bytes, so
            // the stored representation matches what mRegSetValueExW would have
            // written for the equivalent wide string.
            auto const narrow = std::string_view(reinterpret_cast<char const*>(lpData), cbData);
            auto const wide   = m::acp_to_basic_string<char16_t>(narrow);
            auto const bytes  = std::as_bytes(std::span<char16_t const>(wide));

            ikey->set_value(m::pil::ikey::set_value_flags{}, name, type, bytes);
        }
        else
        {
            auto const value =
                (lpData != nullptr)
                    ? std::span<std::byte const>(reinterpret_cast<std::byte const*>(lpData), cbData)
                    : std::span<std::byte const>{};

            ikey->set_value(m::pil::ikey::set_value_flags{}, name, type, value);
        }
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
    }

    return ERROR_SUCCESS;
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
    try
    {
        if (Reserved != 0)
            return ERROR_INVALID_PARAMETER;

        auto ikey = g_handles.deref_handle<std::shared_ptr<m::pil::ikey>>(
            m::mwin32_impl::handle::from_HKEY(hKey));

        auto const value =
            (lpData != nullptr)
                ? std::span<std::byte const>(reinterpret_cast<std::byte const*>(lpData), cbData)
                : std::span<std::byte const>{};

        ikey->set_value(m::pil::ikey::set_value_flags{},
                        to_value_name(lpValueName),
                        static_cast<m::pil::reg_value_type>(dwType),
                        value);
    }
    catch (...)
    {
        return registry_exception_to_lstatus();
    }

    return ERROR_SUCCESS;
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
