// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// winsock2.h must be included before Windows.h (which is included transitively
// through intercepting_webcore.h) to avoid conflicts with http.h.
#include <winsock2.h>

#include "intercepting_webcore.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/pil/file_path.h>
#include <m/strings/convert.h>

// Additional Windows headers
#include <winreg.h>
#include <winerror.h>
#include <Dbghelp.h>
#include <http.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "httpapi.lib")

namespace m::pil::impl::intercepting
{
    //--------------------------------------------------------------------------
    // Active interception context
    //--------------------------------------------------------------------------
    //
    // This is a plain process-global, NOT thread_local: hwebcore.dll services
    // requests and runs async/worker callbacks on its own threads, and the
    // hooks fire on those threads. A thread_local pointer would be null on every
    // thread except the one that called activate(), silently bypassing the
    // entire interceptor.
    //
    // It is a std::atomic read with acquire / written with release semantics.
    // Publication is ordered (activate() stores it before the engine starts its
    // threads; ~webcore_instance() clears it after the engine is shut down), but
    // because the pointer is read on every hooked call on every engine thread we
    // do not want that correctness to rest on an opaque "the engine joins all of
    // its threads before its destructor returns" assumption on a
    // security-sensitive surface. The acquire/release pair is a plain mov on
    // x64/ARM64, so the cost is negligible. Each hook reads it through the
    // active_context() accessor (declared in the header), which performs the
    // acquire load.
    std::atomic<interception_context*> g_active_context_cell{nullptr};

    //--------------------------------------------------------------------------
    // synthetic_http_queue implementation
    //--------------------------------------------------------------------------

    HTTP_REQUEST_ID
    synthetic_http_queue::enqueue_request(synthetic_http_request request)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        request.request_id = m_next_request_id++;
        HTTP_REQUEST_ID id = request.request_id;
        m_pending_requests.push_back(std::move(request));
        m_request_cv.notify_one();
        return id;
    }

    std::optional<synthetic_http_request>
    synthetic_http_queue::try_dequeue_request()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_pending_requests.empty())
            return std::nullopt;
        synthetic_http_request request = std::move(m_pending_requests.front());
        m_pending_requests.pop_front();
        return request;
    }

    void
    synthetic_http_queue::requeue_front(synthetic_http_request request)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_pending_requests.push_front(std::move(request));
        m_request_cv.notify_one();
    }

    std::optional<synthetic_http_request>
    synthetic_http_queue::dequeue_request(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (timeout.count() == 0)
        {
            // Non-blocking.
            if (m_pending_requests.empty())
                return std::nullopt;
        }
        else
        {
            // Wait with timeout.
            if (!m_request_cv.wait_for(lock, timeout, [this] { return !m_pending_requests.empty(); }))
                return std::nullopt;
        }
        synthetic_http_request request = std::move(m_pending_requests.front());
        m_pending_requests.pop_front();
        return request;
    }

    void
    synthetic_http_queue::capture_response(HTTP_REQUEST_ID request_id, captured_http_response response)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        response.request_id = request_id;
        m_responses[request_id] = std::move(response);
        m_response_cv.notify_all();
    }

    void
    synthetic_http_queue::append_response_body(HTTP_REQUEST_ID request_id, std::span<std::uint8_t const> data)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_responses.find(request_id);
        if (it != m_responses.end())
        {
            it->second.body.insert(it->second.body.end(), data.begin(), data.end());
        }
    }

    void
    synthetic_http_queue::complete_response(HTTP_REQUEST_ID request_id)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_responses.find(request_id);
        if (it != m_responses.end())
        {
            it->second.complete = true;
            m_response_cv.notify_all();
        }
    }

    std::optional<captured_http_response>
    synthetic_http_queue::get_response(HTTP_REQUEST_ID request_id) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_responses.find(request_id);
        if (it == m_responses.end())
            return std::nullopt;
        return it->second;
    }

    std::optional<captured_http_response>
    synthetic_http_queue::wait_for_response(HTTP_REQUEST_ID request_id, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto pred = [this, request_id] {
            auto it = m_responses.find(request_id);
            return it != m_responses.end() && it->second.complete;
        };
        if (!m_response_cv.wait_for(lock, timeout, pred))
            return std::nullopt;
        auto it = m_responses.find(request_id);
        if (it == m_responses.end())
            return std::nullopt;
        return it->second;
    }

    void
    synthetic_http_queue::clear()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_pending_requests.clear();
        m_responses.clear();
    }

    bool
    synthetic_http_queue::has_pending_requests() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return !m_pending_requests.empty();
    }

    //--------------------------------------------------------------------------
    // Helper: check if an HKEY is a predefined root (HKLM, HKCU, etc.)
    //--------------------------------------------------------------------------

    namespace
    {
        bool
        is_predefined_key(HKEY hkey)
        {
            // Predefined keys have values like 0x80000000, 0x80000001, etc.
            auto const v = reinterpret_cast<uintptr_t>(hkey);
            return (v >= 0x80000000 && v <= 0x800000FF);
        }

        // Map predefined HKEY to PIL predefined_key enum.
        std::optional<predefined_key>
        hkey_to_predefined_key(HKEY hkey)
        {
            if (hkey == HKEY_LOCAL_MACHINE)
                return predefined_key::local_machine;
            if (hkey == HKEY_CURRENT_USER)
                return predefined_key::current_user;
            if (hkey == HKEY_CLASSES_ROOT)
                return predefined_key::classes_root;
            if (hkey == HKEY_USERS)
                return predefined_key::users;
            if (hkey == HKEY_CURRENT_CONFIG)
                return predefined_key::current_config;
            return std::nullopt;
        }
    } // namespace

    //--------------------------------------------------------------------------
    // Hook implementations: Registry APIs
    //
    // For now, these hooks simply fall through to the original functions.
    // A complete implementation would translate calls to the PIL surfaces.
    // The infrastructure here demonstrates the IAT patching approach.
    //--------------------------------------------------------------------------

    namespace hooks
    {
        // Pull in http_endpoint from the parent pil namespace.
        using m::pil::http_endpoint;

        // Original function pointers (saved before patching).
        static LSTATUS(WINAPI* original_RegOpenKeyExW)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY) = nullptr;
        static LSTATUS(WINAPI* original_RegQueryValueExW)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) = nullptr;
        static LSTATUS(WINAPI* original_RegCloseKey)(HKEY) = nullptr;
        static LSTATUS(WINAPI* original_RegEnumKeyExW)(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, PFILETIME) = nullptr;
        static LSTATUS(WINAPI* original_RegEnumValueW)(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD) = nullptr;

        // Original function pointers for filesystem APIs.
        static HANDLE(WINAPI* original_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = nullptr;
        static HANDLE(WINAPI* original_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW) = nullptr;
        static BOOL(WINAPI* original_FindNextFileW)(HANDLE, LPWIN32_FIND_DATAW) = nullptr;
        static BOOL(WINAPI* original_FindClose)(HANDLE) = nullptr;
        static DWORD(WINAPI* original_GetFileAttributesW)(LPCWSTR) = nullptr;
        static BOOL(WINAPI* original_CloseHandle)(HANDLE) = nullptr;

        // Original function pointers for synthetic-file I/O APIs (M-HWC-REVIEW-2).
        // These make a handle returned by hook_CreateFileW actually usable: every
        // kernel32 call the engine makes on it is routed through the backing
        // PIL `ifile` instead of failing ERROR_INVALID_HANDLE.
        static BOOL(WINAPI* original_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) = nullptr;
        static BOOL(WINAPI* original_WriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED) = nullptr;
        static BOOL(WINAPI* original_GetFileSizeEx)(HANDLE, PLARGE_INTEGER) = nullptr;
        static DWORD(WINAPI* original_GetFileSize)(HANDLE, LPDWORD) = nullptr;
        static BOOL(WINAPI* original_SetFilePointerEx)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD) = nullptr;
        static DWORD(WINAPI* original_SetFilePointer)(HANDLE, LONG, PLONG, DWORD) = nullptr;
        static DWORD(WINAPI* original_GetFileType)(HANDLE) = nullptr;
        static BOOL(WINAPI* original_FlushFileBuffers)(HANDLE) = nullptr;
        static BOOL(WINAPI* original_SetEndOfFile)(HANDLE) = nullptr;

        // Original function pointers for HTTP Server APIs (D-HWC-6, Tier A).
        static ULONG(WINAPI* original_HttpAddUrl)(HANDLE, PCWSTR, PVOID) = nullptr;
        static ULONG(WINAPI* original_HttpAddUrlToUrlGroup)(HTTP_URL_GROUP_ID, PCWSTR, HTTP_URL_CONTEXT, ULONG) = nullptr;
        static ULONG(WINAPI* original_HttpRemoveUrl)(HANDLE, PCWSTR) = nullptr;
        static ULONG(WINAPI* original_HttpRemoveUrlFromUrlGroup)(HTTP_URL_GROUP_ID, PCWSTR, ULONG) = nullptr;

        // Original function pointers for HTTP Server APIs (D-HWC-6, Tier B).
        static ULONG(WINAPI* original_HttpReceiveHttpRequest)(HANDLE, HTTP_REQUEST_ID, ULONG, PHTTP_REQUEST, ULONG, PULONG, LPOVERLAPPED) = nullptr;
        static ULONG(WINAPI* original_HttpReceiveRequestEntityBody)(HANDLE, HTTP_REQUEST_ID, ULONG, PVOID, ULONG, PULONG, LPOVERLAPPED) = nullptr;
        static ULONG(WINAPI* original_HttpSendHttpResponse)(HANDLE, HTTP_REQUEST_ID, ULONG, PHTTP_RESPONSE, PHTTP_CACHE_POLICY, PULONG, PVOID, ULONG, LPOVERLAPPED, PHTTP_LOG_DATA) = nullptr;
        static ULONG(WINAPI* original_HttpSendResponseEntityBody)(HANDLE, HTTP_REQUEST_ID, ULONG, USHORT, PHTTP_DATA_CHUNK, PULONG, PVOID, ULONG, LPOVERLAPPED, PHTTP_LOG_DATA) = nullptr;

        //----------------------------------------------------------------------
        // RegOpenKeyExW hook
        //----------------------------------------------------------------------

        LSTATUS WINAPI
        hook_RegOpenKeyExW(HKEY    hKey,
                           LPCWSTR lpSubKey,
                           DWORD   ulOptions,
                           REGSAM  samDesired,
                           PHKEY   phkResult)
        {
            if (!active_context() || !active_context()->registry)
            {
                // No active context — fall through to original.
                return original_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
            }

            try
            {
                std::shared_ptr<ikey> base_key;

                // Resolve hKey to a PIL ikey.
                if (is_predefined_key(hKey))
                {
                    auto pk = hkey_to_predefined_key(hKey);
                    if (!pk)
                    {
                        // Unknown predefined key — fall through.
                        return original_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
                    }

                    auto d = active_context()->registry->open_predefined_key(
                        iregistry::open_predefined_key_flags{},
                        *pk,
                        pil::sam::maximum_allowed,
                        base_key);

                    if (d || !base_key)
                    {
                        return ERROR_FILE_NOT_FOUND;
                    }
                }
                else
                {
                    // Look up from handle table.
                    base_key = active_context()->lookup_key_handle(hKey);
                    if (!base_key)
                    {
                        // Unknown handle — fall through to original.
                        return original_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
                    }
                }

                // If lpSubKey is null or empty, we're opening the base key itself.
                if (!lpSubKey || lpSubKey[0] == L'\0')
                {
                    *phkResult = active_context()->allocate_key_handle(base_key);
                    return ERROR_SUCCESS;
                }

                // Convert the subkey path to a PIL key_path.
                std::wstring_view subkey_view(lpSubKey);
                std::u16string_view subkey_u16(
                    reinterpret_cast<char16_t const*>(subkey_view.data()),
                    subkey_view.size());
                key_path path(subkey_u16);

                // Open the subkey.
                std::shared_ptr<ikey> opened_key;
                std::error_code ec;
                auto d = base_key->open_key(
                    ikey::open_key_flags::tolerate_not_found,
                    path,
                    pil::sam::maximum_allowed,
                    opened_key,
                    ec);

                if (ec)
                {
                    // Map error code to LSTATUS.
                    if (ec == std::errc::no_such_file_or_directory)
                        return ERROR_FILE_NOT_FOUND;
                    return ERROR_ACCESS_DENIED;
                }

                if (d.code() == ikey::open_key_result_code::key_not_found)
                {
                    return ERROR_FILE_NOT_FOUND;
                }

                if (!opened_key)
                {
                    return ERROR_FILE_NOT_FOUND;
                }

                // Allocate a synthetic handle.
                *phkResult = active_context()->allocate_key_handle(opened_key);
                return ERROR_SUCCESS;
            }
            catch (...)
            {
                // Fall through to original on any exception.
                return original_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
            }
        }

        //----------------------------------------------------------------------
        // RegQueryValueExW hook
        //----------------------------------------------------------------------

        LSTATUS WINAPI
        hook_RegQueryValueExW(HKEY    hKey,
                              LPCWSTR lpValueName,
                              LPDWORD lpReserved,
                              LPDWORD lpType,
                              LPBYTE  lpData,
                              LPDWORD lpcbData)
        {
            if (!active_context() || !active_context()->registry)
            {
                return original_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
            }

            // Look up the ikey from the handle table.
            auto key = active_context()->lookup_key_handle(hKey);
            if (!key)
            {
                // Unknown handle — fall through to original.
                return original_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
            }

            try
            {
                // Convert value name to PIL format.
                std::wstring_view value_name_view(lpValueName ? lpValueName : L"");
                value_name_string_type value_name = to_value_name_string_type(value_name_view);

                // Query mode: lpData is null, caller is asking for size only.
                if (!lpData && lpcbData)
                {
                    // Get type and size.
                    reg_value_type vt;
                    auto d = key->get_value_type(ikey::get_value_type_flags{}, value_name, vt);
                    if (d)
                        return ERROR_FILE_NOT_FOUND;

                    std::size_t size{};
                    auto d2 = key->get_value_size(ikey::get_value_size_flags{}, value_name, size);
                    if (d2)
                        return ERROR_FILE_NOT_FOUND;

                    if (lpType)
                        *lpType = static_cast<DWORD>(vt);
                    *lpcbData = static_cast<DWORD>(size);
                    return ERROR_SUCCESS;
                }

                // Read the value.
                reg_value_type vt;
                std::vector<std::byte> buffer;
                if (lpcbData && *lpcbData > 0)
                    buffer.resize(*lpcbData);

                std::span<std::byte> buffer_span(buffer);
                std::optional<std::size_t> new_bytes_required;

                auto d = key->get_value(
                    ikey::get_value_flags{},
                    value_name,
                    vt,
                    buffer_span,
                    new_bytes_required);

                if (new_bytes_required)
                {
                    // Buffer too small.
                    if (lpType)
                        *lpType = static_cast<DWORD>(vt);
                    if (lpcbData)
                        *lpcbData = static_cast<DWORD>(*new_bytes_required);
                    return ERROR_MORE_DATA;
                }

                // Copy to output buffer.
                if (lpType)
                    *lpType = static_cast<DWORD>(vt);
                if (lpcbData)
                    *lpcbData = static_cast<DWORD>(buffer_span.size());
                if (lpData && !buffer_span.empty())
                    std::memcpy(lpData, buffer_span.data(), buffer_span.size());

                return ERROR_SUCCESS;
            }
            catch (std::system_error const& e)
            {
                auto code = e.code();
                if (code == std::errc::no_such_file_or_directory)
                    return ERROR_FILE_NOT_FOUND;
                return ERROR_ACCESS_DENIED;
            }
            catch (...)
            {
                return original_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
            }
        }

        //----------------------------------------------------------------------
        // RegCloseKey hook
        //----------------------------------------------------------------------

        LSTATUS WINAPI
        hook_RegCloseKey(HKEY hKey)
        {
            if (!active_context())
            {
                return original_RegCloseKey(hKey);
            }

            // If it's one of our synthetic handles, just release it.
            if (active_context()->release_key_handle(hKey))
            {
                return ERROR_SUCCESS;
            }

            // Fall through for real handles or predefined keys.
            return original_RegCloseKey(hKey);
        }

        //----------------------------------------------------------------------
        // RegEnumKeyExW hook
        //----------------------------------------------------------------------

        LSTATUS WINAPI
        hook_RegEnumKeyExW(HKEY      hKey,
                           DWORD     dwIndex,
                           LPWSTR    lpName,
                           LPDWORD   lpcchName,
                           LPDWORD   lpReserved,
                           LPWSTR    lpClass,
                           LPDWORD   lpcchClass,
                           PFILETIME lpftLastWriteTime)
        {
            if (!active_context() || !active_context()->registry)
            {
                return original_RegEnumKeyExW(hKey, dwIndex, lpName, lpcchName, lpReserved, lpClass, lpcchClass, lpftLastWriteTime);
            }

            // Look up the ikey from the handle table.
            auto key = active_context()->lookup_key_handle(hKey);
            if (!key)
            {
                // Unknown handle — fall through to original.
                return original_RegEnumKeyExW(hKey, dwIndex, lpName, lpcchName, lpReserved, lpClass, lpcchClass, lpftLastWriteTime);
            }

            try
            {
                // Enumerate keys at the given index.
                auto key_name_opt = key->enumerate_keys(static_cast<std::size_t>(dwIndex));

                if (!key_name_opt)
                {
                    // No more keys.
                    return ERROR_NO_MORE_ITEMS;
                }

                // Convert the key name to wide string.
                auto name_str = key_name_opt->native();
                std::u16string_view name_view = name_str.view();
                std::wstring_view name_wview(
                    reinterpret_cast<wchar_t const*>(name_view.data()),
                    name_view.size());

                // Check buffer size.
                DWORD required_chars = static_cast<DWORD>(name_wview.size()) + 1; // +1 for null
                if (*lpcchName < required_chars)
                {
                    *lpcchName = required_chars;
                    return ERROR_MORE_DATA;
                }

                // Copy name to output buffer.
                std::wmemcpy(lpName, name_wview.data(), name_wview.size());
                lpName[name_wview.size()] = L'\0';
                *lpcchName = static_cast<DWORD>(name_wview.size());

                // Class is not supported by PIL — return empty.
                if (lpClass && lpcchClass && *lpcchClass > 0)
                {
                    lpClass[0] = L'\0';
                    *lpcchClass = 0;
                }

                // Last write time — query the opened subkey if needed.
                if (lpftLastWriteTime)
                {
                    // For simplicity, return 0 (not available through PIL).
                    lpftLastWriteTime->dwLowDateTime = 0;
                    lpftLastWriteTime->dwHighDateTime = 0;
                }

                return ERROR_SUCCESS;
            }
            catch (...)
            {
                return original_RegEnumKeyExW(hKey, dwIndex, lpName, lpcchName, lpReserved, lpClass, lpcchClass, lpftLastWriteTime);
            }
        }

        //----------------------------------------------------------------------
        // RegEnumValueW hook
        //----------------------------------------------------------------------

        LSTATUS WINAPI
        hook_RegEnumValueW(HKEY    hKey,
                           DWORD   dwIndex,
                           LPWSTR  lpValueName,
                           LPDWORD lpcchValueName,
                           LPDWORD lpReserved,
                           LPDWORD lpType,
                           LPBYTE  lpData,
                           LPDWORD lpcbData)
        {
            if (!active_context() || !active_context()->registry)
            {
                return original_RegEnumValueW(hKey, dwIndex, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);
            }

            // Look up the ikey from the handle table.
            auto key = active_context()->lookup_key_handle(hKey);
            if (!key)
            {
                // Unknown handle — fall through to original.
                return original_RegEnumValueW(hKey, dwIndex, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);
            }

            try
            {
                // Enumerate value names and types at the given index.
                auto value_opt = key->enumerate_value_names_and_types(static_cast<std::size_t>(dwIndex));

                if (!value_opt)
                {
                    // No more values.
                    return ERROR_NO_MORE_ITEMS;
                }

                // Convert the value name to wide string.
                std::u16string_view name_view(value_opt->m_value_name);
                std::wstring_view name_wview(
                    reinterpret_cast<wchar_t const*>(name_view.data()),
                    name_view.size());

                // Check value name buffer size.
                DWORD required_chars = static_cast<DWORD>(name_wview.size()) + 1; // +1 for null
                if (*lpcchValueName < required_chars)
                {
                    *lpcchValueName = required_chars;
                    return ERROR_MORE_DATA;
                }

                // Copy value name to output buffer.
                std::wmemcpy(lpValueName, name_wview.data(), name_wview.size());
                lpValueName[name_wview.size()] = L'\0';
                *lpcchValueName = static_cast<DWORD>(name_wview.size());

                // Set type.
                if (lpType)
                    *lpType = static_cast<DWORD>(value_opt->m_reg_value_type);

                // If data buffer is provided, read the value data.
                if (lpData && lpcbData && *lpcbData > 0)
                {
                    reg_value_type vt;
                    std::vector<std::byte> buffer(*lpcbData);
                    std::span<std::byte> buffer_span(buffer);
                    std::optional<std::size_t> new_bytes_required;

                    auto d = key->get_value(
                        ikey::get_value_flags{},
                        value_opt->m_value_name,
                        vt,
                        buffer_span,
                        new_bytes_required);

                    if (new_bytes_required)
                    {
                        *lpcbData = static_cast<DWORD>(*new_bytes_required);
                        return ERROR_MORE_DATA;
                    }

                    *lpcbData = static_cast<DWORD>(buffer_span.size());
                    if (!buffer_span.empty())
                        std::memcpy(lpData, buffer_span.data(), buffer_span.size());
                }
                else if (lpcbData)
                {
                    // Caller wants size only.
                    std::size_t size{};
                    key->get_value_size(ikey::get_value_size_flags{}, value_opt->m_value_name, size);
                    *lpcbData = static_cast<DWORD>(size);
                }

                return ERROR_SUCCESS;
            }
            catch (...)
            {
                return original_RegEnumValueW(hKey, dwIndex, lpValueName, lpcchValueName, lpReserved, lpType, lpData, lpcbData);
            }
        }

        //----------------------------------------------------------------------
        // CreateFileW hook
        //----------------------------------------------------------------------

        // Helper: Parse a Windows path into a file_root and relative path portion.
        // Returns the PIL file_path if successful, nullopt if the path cannot be
        // handled by PIL (e.g., device paths that aren't files).
        std::optional<file_path>
        parse_windows_path(LPCWSTR lpFileName)
        {
            if (!lpFileName || lpFileName[0] == L'\0')
                return std::nullopt;

            // Convert to u16string_view.
            std::wstring_view wpath(lpFileName);
            std::u16string_view u16path(
                reinterpret_cast<char16_t const*>(wpath.data()),
                wpath.size());

            return file_path(u16path);
        }

        HANDLE WINAPI
        hook_CreateFileW(LPCWSTR               lpFileName,
                         DWORD                 dwDesiredAccess,
                         DWORD                 dwShareMode,
                         LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                         DWORD                 dwCreationDisposition,
                         DWORD                 dwFlagsAndAttributes,
                         HANDLE                hTemplateFile)
        {
            if (!active_context() || !active_context()->filesystem)
            {
                return original_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                    lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
            }

            try
            {
                auto path_opt = parse_windows_path(lpFileName);
                if (!path_opt)
                {
                    return original_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                        lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                }

                file_path const& path = *path_opt;
                auto root = path.root();

                // We only intercept drive-rooted paths (e.g., C:\...).
                if (root.kind() != file_root_kind::drive)
                {
                    return original_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                        lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                }

                // Map dwDesiredAccess to PIL file_access.
                file_access access = file_access::default_open;
                if (dwDesiredAccess & GENERIC_READ)
                    access = access | file_access::read;
                if (dwDesiredAccess & GENERIC_WRITE)
                    access = access | file_access::write;

                // Open the root directory.
                std::shared_ptr<idirectory> root_dir;
                auto d = active_context()->filesystem->open_root(
                    ifilesystem::open_root_flags{},
                    root,
                    file_access::default_open,
                    root_dir);

                if (d || !root_dir)
                {
                    ::SetLastError(ERROR_PATH_NOT_FOUND);
                    return INVALID_HANDLE_VALUE;
                }

                // Get the relative portion of the path.
                auto relative = path.relative_path();
                if (relative.empty())
                {
                    // Opening the root directory itself is not supported via file handle.
                    return original_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                        lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                }

                // Check if we're opening a directory.
                bool open_directory = (dwFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS) != 0;

                if (open_directory)
                {
                    // Open as directory.
                    std::shared_ptr<idirectory> dir;
                    std::error_code ec;
                    auto d2 = root_dir->open_directory(
                        idirectory::open_directory_flags::tolerate_not_found,
                        file_path{relative},
                        access,
                        dir,
                        ec);

                    if (ec)
                    {
                        ::SetLastError(ERROR_PATH_NOT_FOUND);
                        return INVALID_HANDLE_VALUE;
                    }

                    if (d2.code() == idirectory::open_directory_result_code::not_found)
                    {
                        ::SetLastError(ERROR_PATH_NOT_FOUND);
                        return INVALID_HANDLE_VALUE;
                    }

                    // Directory handles are not tracked — fall through for now.
                    return original_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                        lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
                }

                // Open as file.
                std::shared_ptr<ifile> file;
                std::error_code ec;

                if (dwCreationDisposition == CREATE_ALWAYS || dwCreationDisposition == CREATE_NEW)
                {
                    auto d2 = root_dir->create_file(
                        idirectory::create_file_flags{},
                        file_path{relative},
                        access,
                        file);
                    (void)d2;
                }
                else
                {
                    auto d2 = root_dir->open_file(
                        idirectory::open_file_flags::tolerate_not_found,
                        file_path{relative},
                        access,
                        file,
                        ec);

                    if (ec)
                    {
                        if (ec == std::errc::no_such_file_or_directory)
                            ::SetLastError(ERROR_FILE_NOT_FOUND);
                        else
                            ::SetLastError(ERROR_ACCESS_DENIED);
                        return INVALID_HANDLE_VALUE;
                    }

                    if (d2.code() == idirectory::open_file_result_code::not_found)
                    {
                        if (dwCreationDisposition == OPEN_ALWAYS)
                        {
                            // Create if not exists.
                            auto d3 = root_dir->create_file(
                                idirectory::create_file_flags{},
                                file_path{relative},
                                access,
                                file);
                            (void)d3;
                        }
                        else
                        {
                            ::SetLastError(ERROR_FILE_NOT_FOUND);
                            return INVALID_HANDLE_VALUE;
                        }
                    }
                }

                if (!file)
                {
                    ::SetLastError(ERROR_FILE_NOT_FOUND);
                    return INVALID_HANDLE_VALUE;
                }

                // Allocate a synthetic handle.
                return active_context()->allocate_file_handle(file);
            }
            catch (std::system_error const& e)
            {
                auto code = e.code();
                if (code == std::errc::no_such_file_or_directory)
                    ::SetLastError(ERROR_FILE_NOT_FOUND);
                else
                    ::SetLastError(ERROR_ACCESS_DENIED);
                return INVALID_HANDLE_VALUE;
            }
            catch (...)
            {
                return original_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                    lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
            }
        }

        //----------------------------------------------------------------------
        // FindFirstFileW hook
        //----------------------------------------------------------------------

        // Helper: populate WIN32_FIND_DATAW from a directory_entry.
        void
        populate_find_data(WIN32_FIND_DATAW& fd, directory_entry const& entry)
        {
            std::memset(&fd, 0, sizeof(fd));

            // Set file attributes.
            fd.dwFileAttributes = 0;
            if (entry.m_kind == node_kind::directory)
                fd.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            else
                fd.dwFileAttributes |= FILE_ATTRIBUTE_NORMAL;

            // Map PIL file_attributes to WIN32 attributes.
            auto attrs = entry.m_metadata.m_attributes;
            if ((attrs & file_attributes::read_only) != file_attributes::none)
                fd.dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
            if ((attrs & file_attributes::hidden) != file_attributes::none)
                fd.dwFileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            if ((attrs & file_attributes::system) != file_attributes::none)
                fd.dwFileAttributes |= FILE_ATTRIBUTE_SYSTEM;

            // Convert size to high/low 32-bit parts.
            fd.nFileSizeHigh = static_cast<DWORD>(entry.m_metadata.m_size >> 32);
            fd.nFileSizeLow = static_cast<DWORD>(entry.m_metadata.m_size & 0xFFFFFFFF);

            // Copy filename.
            auto name_view = entry.m_name.view();
            std::size_t copy_len = (std::min)(name_view.size(), static_cast<std::size_t>(MAX_PATH - 1));
            std::wmemcpy(fd.cFileName, reinterpret_cast<wchar_t const*>(name_view.data()), copy_len);
            fd.cFileName[copy_len] = L'\0';

            // Timestamps (zeroed for now as PIL may not provide all timestamps).
        }

        // Helper: simple wildcard match for *.ext and * patterns.
        bool
        wildcard_match(std::wstring_view pattern, std::wstring_view filename)
        {
            // Handle *.* and * as "match all".
            if (pattern == L"*.*" || pattern == L"*")
                return true;

            // Handle *.ext pattern.
            if (pattern.size() >= 2 && pattern[0] == L'*' && pattern[1] == L'.')
            {
                std::wstring_view ext_pattern = pattern.substr(1); // .ext
                if (filename.size() >= ext_pattern.size())
                {
                    std::wstring_view file_ext = filename.substr(filename.size() - ext_pattern.size());
                    // Case-insensitive comparison.
                    if (ext_pattern.size() == file_ext.size())
                    {
                        for (std::size_t i = 0; i < ext_pattern.size(); ++i)
                        {
                            if (std::towlower(ext_pattern[i]) != std::towlower(file_ext[i]))
                                return false;
                        }
                        return true;
                    }
                }
                return false;
            }

            // Exact match (case-insensitive).
            if (pattern.size() == filename.size())
            {
                for (std::size_t i = 0; i < pattern.size(); ++i)
                {
                    if (std::towlower(pattern[i]) != std::towlower(filename[i]))
                        return false;
                }
                return true;
            }

            return false;
        }

        HANDLE WINAPI
        hook_FindFirstFileW(LPCWSTR             lpFileName,
                            LPWIN32_FIND_DATAW  lpFindFileData)
        {
            if (!active_context() || !active_context()->filesystem)
            {
                return original_FindFirstFileW(lpFileName, lpFindFileData);
            }

            try
            {
                auto path_opt = parse_windows_path(lpFileName);
                if (!path_opt)
                {
                    return original_FindFirstFileW(lpFileName, lpFindFileData);
                }

                file_path const& path = *path_opt;
                auto root = path.root();

                // Only intercept drive-rooted paths.
                if (root.kind() != file_root_kind::drive)
                {
                    return original_FindFirstFileW(lpFileName, lpFindFileData);
                }

                // Split into directory path and filename pattern.
                // e.g., "C:\dir\*.txt" -> directory="C:\dir", pattern="*.txt"
                std::wstring wpath(lpFileName);
                std::size_t last_sep = wpath.find_last_of(L"\\/");
                std::wstring dir_path;
                std::wstring pattern;

                if (last_sep != std::wstring::npos)
                {
                    dir_path = wpath.substr(0, last_sep);
                    pattern = wpath.substr(last_sep + 1);
                }
                else
                {
                    // No directory separator — use current directory (not supported, fall through).
                    return original_FindFirstFileW(lpFileName, lpFindFileData);
                }

                // Parse directory path.
                auto dir_path_opt = parse_windows_path(dir_path.c_str());
                if (!dir_path_opt)
                {
                    return original_FindFirstFileW(lpFileName, lpFindFileData);
                }

                // Open the root.
                std::shared_ptr<idirectory> root_dir;
                auto d = active_context()->filesystem->open_root(
                    ifilesystem::open_root_flags{},
                    dir_path_opt->root(),
                    file_access::default_open,
                    root_dir);

                if (d || !root_dir)
                {
                    ::SetLastError(ERROR_PATH_NOT_FOUND);
                    return INVALID_HANDLE_VALUE;
                }

                // Navigate to the target directory.
                auto relative = dir_path_opt->relative_path();
                std::shared_ptr<idirectory> target_dir;

                if (relative.empty())
                {
                    target_dir = root_dir;
                }
                else
                {
                    std::error_code ec;
                    auto d2 = root_dir->open_directory(
                        idirectory::open_directory_flags::tolerate_not_found,
                        file_path{relative},
                        file_access::default_open,
                        target_dir,
                        ec);

                    if (ec || d2.code() == idirectory::open_directory_result_code::not_found)
                    {
                        ::SetLastError(ERROR_PATH_NOT_FOUND);
                        return INVALID_HANDLE_VALUE;
                    }
                }

                if (!target_dir)
                {
                    ::SetLastError(ERROR_PATH_NOT_FOUND);
                    return INVALID_HANDLE_VALUE;
                }

                // Enumerate all entries and filter by pattern.
                std::vector<directory_entry> matching_entries;

                std::size_t index = 0;
                while (true)
                {
                    auto entry_opt = target_dir->enumerate_entries(index);
                    if (!entry_opt)
                        break;

                    // Convert entry name to wstring for pattern matching.
                    auto name_view = entry_opt->m_name.view();
                    std::wstring_view name_wview(
                        reinterpret_cast<wchar_t const*>(name_view.data()),
                        name_view.size());

                    if (wildcard_match(pattern, name_wview))
                    {
                        matching_entries.push_back(std::move(*entry_opt));
                    }

                    ++index;
                }

                if (matching_entries.empty())
                {
                    ::SetLastError(ERROR_FILE_NOT_FOUND);
                    return INVALID_HANDLE_VALUE;
                }

                // Populate the first result.
                populate_find_data(*lpFindFileData, matching_entries[0]);

                // Create find state.
                interception_context::find_state state;
                state.directory = target_dir;
                state.entries = std::move(matching_entries);
                state.current_index = 1; // Next call returns index 1.

                return active_context()->allocate_find_handle(std::move(state));
            }
            catch (...)
            {
                return original_FindFirstFileW(lpFileName, lpFindFileData);
            }
        }

        //----------------------------------------------------------------------
        // FindNextFileW hook
        //----------------------------------------------------------------------

        BOOL WINAPI
        hook_FindNextFileW(HANDLE             hFindFile,
                           LPWIN32_FIND_DATAW lpFindFileData)
        {
            if (!active_context())
            {
                return original_FindNextFileW(hFindFile, lpFindFileData);
            }

            // Check if this is one of our synthetic find handles.
            auto* state = active_context()->lookup_find_handle(hFindFile);
            if (!state)
            {
                return original_FindNextFileW(hFindFile, lpFindFileData);
            }

            // Return next entry from the pre-filtered list.
            if (state->current_index >= state->entries.size())
            {
                ::SetLastError(ERROR_NO_MORE_FILES);
                return FALSE;
            }

            populate_find_data(*lpFindFileData, state->entries[state->current_index]);
            ++state->current_index;
            return TRUE;
        }

        //----------------------------------------------------------------------
        // FindClose hook
        //----------------------------------------------------------------------

        BOOL WINAPI
        hook_FindClose(HANDLE hFindFile)
        {
            if (!active_context())
            {
                return original_FindClose(hFindFile);
            }

            if (active_context()->release_find_handle(hFindFile))
            {
                return TRUE;
            }

            return original_FindClose(hFindFile);
        }

        //----------------------------------------------------------------------
        // CloseHandle hook
        //----------------------------------------------------------------------

        // Map a std::error_code from the PIL file surfaces onto a Win32 last-error
        // and return FALSE (defined below; forward-declared so CloseHandle can
        // surface a flush failure).
        BOOL
        fail_from_error_code(std::error_code const& ec);

        BOOL WINAPI
        hook_CloseHandle(HANDLE hObject)
        {
            if (!active_context())
            {
                return original_CloseHandle(hObject);
            }

            // Fast path: CloseHandle is one of the hottest calls in a server
            // (every event, mutex, thread, mapping, file, registry handle, ...).
            // Our synthetic handles are minted at or above synthetic_handle_floor,
            // far above any real kernel handle, so a value below the floor cannot
            // be ours — skip the lock and map probe entirely.
            if (reinterpret_cast<uintptr_t>(hObject) < synthetic_handle_floor)
            {
                return original_CloseHandle(hObject);
            }

            // If it is one of our synthetic file handles (allocated by the
            // CreateFileW hook), flush any buffered writes and release it so the
            // backing ifile is freed. Otherwise it is a real OS handle — pass
            // through.
            std::error_code ec;
            if (active_context()->close_file_handle(hObject, ec))
            {
                if (ec)
                    return fail_from_error_code(ec);
                return TRUE;
            }

            return original_CloseHandle(hObject);
        }

        //----------------------------------------------------------------------
        // Synthetic-file I/O hooks (M-HWC-REVIEW-2)
        //----------------------------------------------------------------------
        //
        // CreateFileW can hand the engine a synthetic handle backed by a PIL
        // `ifile`. These hooks make that handle usable: each routes its call to
        // the backing file when the handle is one of ours, and otherwise falls
        // through to the real kernel32 function. Every hook starts with the same
        // lock-free fast path used by CloseHandle: a value below
        // synthetic_handle_floor cannot be one of ours, so we skip the lock.

        // Map a std::error_code from the PIL file surfaces onto a Win32 last-error
        // and return FALSE, the standard failure convention for these APIs.
        BOOL
        fail_from_error_code(std::error_code const& ec)
        {
            if (ec == std::errc::not_supported)
                ::SetLastError(ERROR_NOT_SUPPORTED);
            else if (ec == std::errc::invalid_argument)
                ::SetLastError(ERROR_INVALID_PARAMETER);
            else if (ec == std::errc::bad_file_descriptor)
                ::SetLastError(ERROR_INVALID_HANDLE);
            else
                ::SetLastError(ERROR_READ_FAULT);
            return FALSE;
        }

        BOOL WINAPI
        hook_ReadFile(HANDLE       hFile,
                      LPVOID       lpBuffer,
                      DWORD        nNumberOfBytesToRead,
                      LPDWORD      lpNumberOfBytesRead,
                      LPOVERLAPPED lpOverlapped)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_ReadFile(hFile, lpBuffer, nNumberOfBytesToRead,
                                         lpNumberOfBytesRead, lpOverlapped);
            }

            // Overlapped (asynchronous) I/O on a synthetic handle is not
            // supported: our backing files are synchronous in-memory PIL nodes
            // and we do not own the OVERLAPPED/IOCP completion machinery. Reject
            // it explicitly rather than silently completing it, which could hang
            // a caller that waits for an IOCP packet that never arrives.
            if (lpOverlapped != nullptr
                && active_context()->is_synthetic_file_handle(hFile))
            {
                ::SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }

            std::size_t     bytes_read = 0;
            std::error_code ec;
            std::span<std::byte> buffer(static_cast<std::byte*>(lpBuffer),
                                        nNumberOfBytesToRead);
            if (!active_context()->read_file_handle(hFile, buffer, bytes_read, ec))
            {
                // Not one of ours after all — pass through.
                return original_ReadFile(hFile, lpBuffer, nNumberOfBytesToRead,
                                         lpNumberOfBytesRead, lpOverlapped);
            }

            if (ec)
                return fail_from_error_code(ec);

            if (lpNumberOfBytesRead != nullptr)
                *lpNumberOfBytesRead = static_cast<DWORD>(bytes_read);

            return TRUE;
        }

        BOOL WINAPI
        hook_WriteFile(HANDLE       hFile,
                       LPCVOID      lpBuffer,
                       DWORD        nNumberOfBytesToWrite,
                       LPDWORD      lpNumberOfBytesWritten,
                       LPOVERLAPPED lpOverlapped)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite,
                                          lpNumberOfBytesWritten, lpOverlapped);
            }

            // Overlapped (asynchronous) I/O on a synthetic handle is not
            // supported (see hook_ReadFile). Reject it rather than faking an
            // IOCP completion the caller's port will never receive.
            if (lpOverlapped != nullptr
                && active_context()->is_synthetic_file_handle(hFile))
            {
                ::SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }

            std::size_t     bytes_written = 0;
            std::error_code ec;
            std::span<std::byte const> buffer(static_cast<std::byte const*>(lpBuffer),
                                              nNumberOfBytesToWrite);
            if (!active_context()->write_file_handle(hFile, buffer, bytes_written, ec))
            {
                return original_WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite,
                                          lpNumberOfBytesWritten, lpOverlapped);
            }

            if (ec)
                return fail_from_error_code(ec);

            if (lpNumberOfBytesWritten != nullptr)
                *lpNumberOfBytesWritten = static_cast<DWORD>(bytes_written);

            return TRUE;
        }

        BOOL WINAPI
        hook_GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_GetFileSizeEx(hFile, lpFileSize);
            }

            std::uint64_t   size = 0;
            std::error_code ec;
            if (!active_context()->get_file_handle_size(hFile, size, ec))
                return original_GetFileSizeEx(hFile, lpFileSize);

            if (ec)
                return fail_from_error_code(ec);

            if (lpFileSize != nullptr)
                lpFileSize->QuadPart = static_cast<LONGLONG>(size);
            return TRUE;
        }

        DWORD WINAPI
        hook_GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_GetFileSize(hFile, lpFileSizeHigh);
            }

            std::uint64_t   size = 0;
            std::error_code ec;
            if (!active_context()->get_file_handle_size(hFile, size, ec))
                return original_GetFileSize(hFile, lpFileSizeHigh);

            if (ec)
            {
                fail_from_error_code(ec);
                return INVALID_FILE_SIZE;
            }

            if (lpFileSizeHigh != nullptr)
                *lpFileSizeHigh = static_cast<DWORD>(size >> 32);
            ::SetLastError(NO_ERROR);
            return static_cast<DWORD>(size & 0xFFFFFFFFu);
        }

        BOOL WINAPI
        hook_SetFilePointerEx(HANDLE         hFile,
                              LARGE_INTEGER  liDistanceToMove,
                              PLARGE_INTEGER lpNewFilePointer,
                              DWORD          dwMoveMethod)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_SetFilePointerEx(hFile, liDistanceToMove,
                                                 lpNewFilePointer, dwMoveMethod);
            }

            std::uint64_t   new_position = 0;
            std::error_code ec;
            if (!active_context()->set_file_handle_pointer(
                    hFile, liDistanceToMove.QuadPart, dwMoveMethod, new_position, ec))
            {
                return original_SetFilePointerEx(hFile, liDistanceToMove,
                                                 lpNewFilePointer, dwMoveMethod);
            }

            if (ec)
                return fail_from_error_code(ec);

            if (lpNewFilePointer != nullptr)
                lpNewFilePointer->QuadPart = static_cast<LONGLONG>(new_position);
            return TRUE;
        }

        DWORD WINAPI
        hook_SetFilePointer(HANDLE hFile,
                            LONG   lDistanceToMove,
                            PLONG  lpDistanceToMoveHigh,
                            DWORD  dwMoveMethod)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_SetFilePointer(hFile, lDistanceToMove,
                                               lpDistanceToMoveHigh, dwMoveMethod);
            }

            // Assemble the 64-bit distance from the low LONG and optional high LONG.
            std::int64_t distance = lDistanceToMove;
            if (lpDistanceToMoveHigh != nullptr)
            {
                distance = static_cast<std::int64_t>(
                    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(*lpDistanceToMoveHigh)) << 32)
                    | static_cast<std::uint32_t>(lDistanceToMove));
            }

            std::uint64_t   new_position = 0;
            std::error_code ec;
            if (!active_context()->set_file_handle_pointer(
                    hFile, distance, dwMoveMethod, new_position, ec))
            {
                return original_SetFilePointer(hFile, lDistanceToMove,
                                               lpDistanceToMoveHigh, dwMoveMethod);
            }

            if (ec)
            {
                fail_from_error_code(ec);
                return INVALID_SET_FILE_POINTER;
            }

            if (lpDistanceToMoveHigh != nullptr)
                *lpDistanceToMoveHigh = static_cast<LONG>(new_position >> 32);
            ::SetLastError(NO_ERROR);
            return static_cast<DWORD>(new_position & 0xFFFFFFFFu);
        }

        DWORD WINAPI
        hook_GetFileType(HANDLE hFile)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_GetFileType(hFile);
            }

            if (active_context()->is_synthetic_file_handle(hFile))
                return FILE_TYPE_DISK;

            return original_GetFileType(hFile);
        }

        BOOL WINAPI
        hook_FlushFileBuffers(HANDLE hFile)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_FlushFileBuffers(hFile);
            }

            // Flush any writes buffered by the WriteFile hook to the backing PIL
            // node. flush_file_handle returns false when the handle is not one of
            // ours, in which case we pass through to the real flush.
            std::error_code ec;
            if (active_context()->flush_file_handle(hFile, ec))
            {
                if (ec)
                    return fail_from_error_code(ec);
                return TRUE;
            }

            return original_FlushFileBuffers(hFile);
        }

        BOOL WINAPI
        hook_SetEndOfFile(HANDLE hFile)
        {
            if (!active_context()
                || reinterpret_cast<uintptr_t>(hFile) < synthetic_handle_floor)
            {
                return original_SetEndOfFile(hFile);
            }

            // Truncate (or zero-extend) the buffered content to the current file
            // position. set_end_of_file_handle returns false when the handle is
            // not one of ours, in which case we pass through.
            std::error_code ec;
            if (active_context()->set_end_of_file_handle(hFile, ec))
            {
                if (ec)
                    return fail_from_error_code(ec);
                return TRUE;
            }

            return original_SetEndOfFile(hFile);
        }

        //----------------------------------------------------------------------
        // GetFileAttributesW hook
        //----------------------------------------------------------------------

        DWORD WINAPI
        hook_GetFileAttributesW(LPCWSTR lpFileName)
        {
            if (!active_context() || !active_context()->filesystem)
            {
                return original_GetFileAttributesW(lpFileName);
            }

            try
            {
                auto path_opt = parse_windows_path(lpFileName);
                if (!path_opt)
                {
                    return original_GetFileAttributesW(lpFileName);
                }

                file_path const& path = *path_opt;
                auto root = path.root();

                // Only intercept drive-rooted paths.
                if (root.kind() != file_root_kind::drive)
                {
                    return original_GetFileAttributesW(lpFileName);
                }

                // Open the root.
                std::shared_ptr<idirectory> root_dir;
                auto d = active_context()->filesystem->open_root(
                    ifilesystem::open_root_flags{},
                    root,
                    file_access::default_open,
                    root_dir);

                if (d || !root_dir)
                {
                    ::SetLastError(ERROR_PATH_NOT_FOUND);
                    return INVALID_FILE_ATTRIBUTES;
                }

                // Get the relative portion.
                auto relative = path.relative_path();
                if (relative.empty())
                {
                    // Root directory — return directory attributes.
                    return FILE_ATTRIBUTE_DIRECTORY;
                }

                // Try to open as a directory first.
                std::shared_ptr<idirectory> dir;
                std::error_code ec;
                auto d2 = root_dir->open_directory(
                    idirectory::open_directory_flags::tolerate_not_found,
                    file_path{relative},
                    file_access::default_open,
                    dir,
                    ec);

                if (!ec && d2.code() != idirectory::open_directory_result_code::not_found && dir)
                {
                    // It's a directory.
                    DWORD attrs = FILE_ATTRIBUTE_DIRECTORY;
                    // A backing metadata-query failure must surface as a Win32
                    // error, not the process-fatal internal-error check that the
                    // no-argument query_information() convenience overload would
                    // raise inside this hook (catch(...) cannot recover a
                    // fail-fast abort).
                    file_metadata metadata;
                    if (dir->query_information(idirectory::query_information_flags{}, metadata))
                    {
                        ::SetLastError(ERROR_FILE_NOT_FOUND);
                        return INVALID_FILE_ATTRIBUTES;
                    }
                    if ((metadata.m_attributes & file_attributes::read_only) != file_attributes::none)
                        attrs |= FILE_ATTRIBUTE_READONLY;
                    if ((metadata.m_attributes & file_attributes::hidden) != file_attributes::none)
                        attrs |= FILE_ATTRIBUTE_HIDDEN;
                    if ((metadata.m_attributes & file_attributes::system) != file_attributes::none)
                        attrs |= FILE_ATTRIBUTE_SYSTEM;
                    return attrs;
                }

                // Try to open as a file.
                std::shared_ptr<ifile> file;
                auto d3 = root_dir->open_file(
                    idirectory::open_file_flags::tolerate_not_found,
                    file_path{relative},
                    file_access::read,
                    file,
                    ec);

                if (!ec && d3.code() != idirectory::open_file_result_code::not_found && file)
                {
                    // It's a file.
                    DWORD attrs = FILE_ATTRIBUTE_NORMAL;
                    // See the directory branch above: surface a query failure as
                    // a Win32 error rather than the fatal no-argument overload.
                    file_metadata metadata;
                    if (file->query_information(ifile::query_information_flags{}, metadata))
                    {
                        ::SetLastError(ERROR_FILE_NOT_FOUND);
                        return INVALID_FILE_ATTRIBUTES;
                    }
                    if ((metadata.m_attributes & file_attributes::read_only) != file_attributes::none)
                        attrs |= FILE_ATTRIBUTE_READONLY;
                    if ((metadata.m_attributes & file_attributes::hidden) != file_attributes::none)
                        attrs |= FILE_ATTRIBUTE_HIDDEN;
                    if ((metadata.m_attributes & file_attributes::system) != file_attributes::none)
                        attrs |= FILE_ATTRIBUTE_SYSTEM;
                    return attrs;
                }

                // Not found.
                ::SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_FILE_ATTRIBUTES;
            }
            catch (...)
            {
                return original_GetFileAttributesW(lpFileName);
            }
        }

        //----------------------------------------------------------------------
        // HTTP URL parsing and remapping helpers (D-HWC-6, Tier A)
        //----------------------------------------------------------------------

        // Parsed components of an HTTP Server API URL.
        // Format: scheme://host:port/path  (e.g., http://+:80/app/)
        struct parsed_http_url
        {
            std::wstring scheme;    // "http" or "https"
            std::wstring host;      // hostname, "+", or "*"
            uint16_t     port{0};   // port number
            std::wstring path;      // path including leading and trailing slashes
        };

        // Parse an HTTP Server API URL into components.
        // Returns true if parsing succeeded, false otherwise.
        bool
        parse_http_url(std::wstring_view url, parsed_http_url& out)
        {
            // Format: scheme://host:port/path
            // Examples:
            //   http://+:80/
            //   https://localhost:443/app/
            //   http://*:8080/api/v1/

            out = {};

            // Find "://"
            auto scheme_end = url.find(L"://");
            if (scheme_end == std::wstring_view::npos)
                return false;

            out.scheme = std::wstring(url.substr(0, scheme_end));

            // Skip past "://"
            auto host_start = scheme_end + 3;
            if (host_start >= url.size())
                return false;

            // Find the port separator ":"
            auto port_sep = url.find(L':', host_start);
            if (port_sep == std::wstring_view::npos)
                return false;

            out.host = std::wstring(url.substr(host_start, port_sep - host_start));

            // Find the path separator "/"
            auto path_start = url.find(L'/', port_sep);
            if (path_start == std::wstring_view::npos)
            {
                // No path - port runs to end
                auto port_str = url.substr(port_sep + 1);
                out.port = static_cast<uint16_t>(std::wcstoul(std::wstring(port_str).c_str(), nullptr, 10));
                out.path = L"/";
            }
            else
            {
                auto port_str = url.substr(port_sep + 1, path_start - port_sep - 1);
                out.port = static_cast<uint16_t>(std::wcstoul(std::wstring(port_str).c_str(), nullptr, 10));
                out.path = std::wstring(url.substr(path_start));
            }

            return true;
        }

        // Reconstruct an HTTP Server API URL from components.
        std::wstring
        reconstruct_http_url(parsed_http_url const& parsed)
        {
            std::wstring result;
            result.reserve(parsed.scheme.size() + 3 + parsed.host.size() + 6 + parsed.path.size());
            result += parsed.scheme;
            result += L"://";
            result += parsed.host;
            result += L':';
            result += std::to_wstring(parsed.port);
            result += parsed.path;
            return result;
        }

        // Convert wide string host to u16string for http_endpoint.
        std::u16string
        wstring_to_u16string(std::wstring const& ws)
        {
            // On Windows, wchar_t is 16-bit, so this is a direct reinterpret.
            static_assert(sizeof(wchar_t) == sizeof(char16_t), "wchar_t must be 16-bit");
            return std::u16string(reinterpret_cast<char16_t const*>(ws.data()), ws.size());
        }

        // Convert u16string to wide string.
        std::wstring
        u16string_to_wstring(std::u16string const& u16s)
        {
            static_assert(sizeof(wchar_t) == sizeof(char16_t), "wchar_t must be 16-bit");
            return std::wstring(reinterpret_cast<wchar_t const*>(u16s.data()), u16s.size());
        }

        // Try to remap a URL using the http_listener_session.
        // Returns the remapped URL if a mapping exists, or the original URL otherwise.
        // If remapping occurred, records it in the context for later reverse lookup.
        std::wstring
        try_remap_url(std::wstring const& original_url)
        {
            if (!active_context() || !active_context()->http_listener_session)
                return original_url;

            parsed_http_url parsed;
            if (!parse_http_url(original_url, parsed))
                return original_url;

            // Create an endpoint from the parsed host:port.
            http_endpoint public_ep(wstring_to_u16string(parsed.host), parsed.port);

            // Look up the mapping.
            auto private_ep_opt = active_context()->http_listener_session->lookup_private(public_ep);
            if (!private_ep_opt)
                return original_url;

            // Remap the URL with the private endpoint.
            http_endpoint const& private_ep = *private_ep_opt;
            parsed.host = u16string_to_wstring(private_ep.host);
            parsed.port = private_ep.port;

            std::wstring remapped_url = reconstruct_http_url(parsed);

            // Record the mapping for reverse lookup on removal.
            active_context()->record_url_mapping(original_url, remapped_url);

            return remapped_url;
        }

        // Try to reverse-map a URL (find the private URL for a public URL that was registered).
        // Used when removing a URL - the caller passes the public URL, but we registered the private one.
        std::wstring
        try_reverse_map_url(std::wstring const& public_url)
        {
            if (!active_context())
                return public_url;

            auto private_url_opt = active_context()->lookup_private_url(public_url);
            if (!private_url_opt)
                return public_url;

            // Remove the mapping since we're about to unregister.
            active_context()->remove_url_mapping_by_public(public_url);

            return *private_url_opt;
        }

        //----------------------------------------------------------------------
        // HttpAddUrl hook (D-HWC-6, Tier A)
        //----------------------------------------------------------------------

        ULONG WINAPI
        hook_HttpAddUrl(HANDLE ReqQueueHandle, PCWSTR pFullyQualifiedUrl, PVOID pReserved)
        {
            if (!active_context() || !active_context()->http_listener_session)
            {
                return original_HttpAddUrl(ReqQueueHandle, pFullyQualifiedUrl, pReserved);
            }

            // Remap the URL if we have a mapping for it.
            std::wstring original_url(pFullyQualifiedUrl);
            std::wstring remapped_url = try_remap_url(original_url);

            return original_HttpAddUrl(ReqQueueHandle, remapped_url.c_str(), pReserved);
        }

        //----------------------------------------------------------------------
        // HttpAddUrlToUrlGroup hook (D-HWC-6, Tier A)
        //----------------------------------------------------------------------

        ULONG WINAPI
        hook_HttpAddUrlToUrlGroup(HTTP_URL_GROUP_ID UrlGroupId,
                                   PCWSTR            pFullyQualifiedUrl,
                                   HTTP_URL_CONTEXT  UrlContext,
                                   ULONG             Reserved)
        {
            if (!active_context() || !active_context()->http_listener_session)
            {
                return original_HttpAddUrlToUrlGroup(UrlGroupId, pFullyQualifiedUrl, UrlContext, Reserved);
            }

            // Remap the URL if we have a mapping for it.
            std::wstring original_url(pFullyQualifiedUrl);
            std::wstring remapped_url = try_remap_url(original_url);

            return original_HttpAddUrlToUrlGroup(UrlGroupId, remapped_url.c_str(), UrlContext, Reserved);
        }

        //----------------------------------------------------------------------
        // HttpRemoveUrl hook (D-HWC-6, Tier A)
        //----------------------------------------------------------------------

        ULONG WINAPI
        hook_HttpRemoveUrl(HANDLE ReqQueueHandle, PCWSTR pFullyQualifiedUrl)
        {
            if (!active_context() || !active_context()->http_listener_session)
            {
                return original_HttpRemoveUrl(ReqQueueHandle, pFullyQualifiedUrl);
            }

            // Look up the remapped URL (we registered with the private URL).
            std::wstring public_url(pFullyQualifiedUrl);
            std::wstring private_url = try_reverse_map_url(public_url);

            return original_HttpRemoveUrl(ReqQueueHandle, private_url.c_str());
        }

        //----------------------------------------------------------------------
        // HttpRemoveUrlFromUrlGroup hook (D-HWC-6, Tier A)
        //----------------------------------------------------------------------

        ULONG WINAPI
        hook_HttpRemoveUrlFromUrlGroup(HTTP_URL_GROUP_ID UrlGroupId,
                                        PCWSTR            pFullyQualifiedUrl,
                                        ULONG             Flags)
        {
            if (!active_context() || !active_context()->http_listener_session)
            {
                return original_HttpRemoveUrlFromUrlGroup(UrlGroupId, pFullyQualifiedUrl, Flags);
            }

            // Look up the remapped URL (we registered with the private URL).
            std::wstring public_url(pFullyQualifiedUrl);
            std::wstring private_url = try_reverse_map_url(public_url);

            return original_HttpRemoveUrlFromUrlGroup(UrlGroupId, private_url.c_str(), Flags);
        }

        //----------------------------------------------------------------------
        // HttpReceiveHttpRequest hook (D-HWC-6, Tier B)
        //----------------------------------------------------------------------

        // Storage for synthetic request bodies, keyed by request_id. Populated
        // by hook_HttpReceiveHttpRequest when a dequeued request carries a body
        // and drained by hook_HttpReceiveRequestEntityBody.
        static std::unordered_map<HTTP_REQUEST_ID, std::pair<std::vector<std::uint8_t>, size_t>> s_synthetic_request_bodies;
        static std::mutex s_request_body_mutex;

        // Drop every stashed request body. Called on instance teardown so bodies
        // belonging to requests that were never drained (no entity-body read and
        // no completing response) do not survive past the activation that owned
        // them.
        static void
        clear_synthetic_request_bodies()
        {
            std::lock_guard<std::mutex> guard(s_request_body_mutex);
            s_synthetic_request_bodies.clear();
        }

        // Helper: Marshal a synthetic request into an HTTP_REQUEST buffer.
        // Returns the total bytes needed; if larger than RequestBufferLength the
        // caller returns ERROR_MORE_DATA and retries with a larger buffer.
        //
        // The engine routes on the cooked URL (host + abs-path) and decides
        // whether to call HttpReceiveRequestEntityBody from the Content-Length
        // header, so a base HTTP_REQUEST alone is not enough: we marshal the raw
        // URL, the cooked (wide) URL components, a computed Content-Length, the
        // Host header, and any remaining caller-supplied headers as unknown
        // headers. All variable-length data is laid out in the trailing region
        // of the caller's buffer with the struct's pointers referring into it.

        // A bump allocator over the trailing region of the request buffer.
        // reserve() always advances the running offset (so the true required
        // size is known even when the buffer is too small) but only returns a
        // pointer when the reservation actually fits.
        struct request_buffer_writer
        {
            std::byte* base;
            std::size_t capacity;
            std::size_t offset;

            void*
            reserve(std::size_t n, std::size_t align)
            {
                std::size_t const aligned = (offset + (align - 1)) & ~(align - 1);
                std::size_t const end     = aligned + n;
                void*             p        = (end <= capacity) ? (base + aligned) : nullptr;
                offset = end;
                return p;
            }
        };

        static PCSTR
        put_narrow(request_buffer_writer& w, std::string_view s)
        {
            void* p = w.reserve(s.size() + 1, 1);
            if (p != nullptr)
            {
                std::memcpy(p, s.data(), s.size());
                static_cast<char*>(p)[s.size()] = '\0';
            }
            return static_cast<PCSTR>(p);
        }

        static PCWSTR
        put_wide(request_buffer_writer& w, std::wstring_view s)
        {
            void* p = w.reserve((s.size() + 1) * sizeof(wchar_t), alignof(wchar_t));
            if (p != nullptr)
            {
                std::memcpy(p, s.data(), s.size() * sizeof(wchar_t));
                static_cast<wchar_t*>(p)[s.size()] = L'\0';
            }
            return static_cast<PCWSTR>(p);
        }

        // URLs and HTTP header values are ASCII on the wire; narrow each code
        // unit, substituting '?' for anything outside 7-bit ASCII.
        static std::string
        narrow_ascii(std::wstring_view w)
        {
            std::string s;
            s.reserve(w.size());
            for (wchar_t c : w)
                s.push_back(c <= 0x7F ? static_cast<char>(c) : '?');
            return s;
        }

        static bool
        iequals_ascii(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                char ca = a[i];
                char cb = b[i];
                if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
                if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
                if (ca != cb)
                    return false;
            }
            return true;
        }

        static ULONG
        marshal_synthetic_request(synthetic_http_request const& synth,
                                  PHTTP_REQUEST                 pRequestBuffer,
                                  ULONG                         RequestBufferLength,
                                  PULONG                        pBytesReceived)
        {
            // Parse the full URL (scheme://host[:port]/abs-path[?query]) into the
            // pieces HTTP_COOKED_URL exposes.
            std::wstring const& full = synth.url;
            std::size_t const   scheme_pos = full.find(L"://");
            std::size_t const   host_start = (scheme_pos == std::wstring::npos) ? 0 : scheme_pos + 3;
            std::size_t const   path_start = full.find(L'/', host_start);

            std::wstring const host_wide =
                (path_start == std::wstring::npos)
                    ? full.substr(host_start)
                    : full.substr(host_start, path_start - host_start);

            std::wstring const path_and_query =
                (path_start == std::wstring::npos) ? std::wstring(L"/") : full.substr(path_start);

            std::size_t const  q          = path_and_query.find(L'?');
            std::wstring const abspath_wide = (q == std::wstring::npos)
                                                  ? path_and_query
                                                  : path_and_query.substr(0, q);
            std::wstring const query_wide = (q == std::wstring::npos)
                                                ? std::wstring()
                                                : path_and_query.substr(q); // includes '?'

            // Narrow forms used for the raw URL and the Host known header.
            std::string const raw_url_narrow = narrow_ascii(path_and_query);

            // Prefer a caller-supplied Host header; otherwise derive it from the URL.
            std::string host_narrow = narrow_ascii(host_wide);
            for (auto const& [name, value] : synth.headers)
            {
                if (iequals_ascii(name, "Host"))
                {
                    host_narrow = value;
                    break;
                }
            }

            std::string const content_length = std::to_string(synth.body.size());

            // Caller-supplied headers other than Host / Content-Length (which we
            // marshal as known headers) become unknown headers.
            std::vector<std::pair<std::string, std::string>> unknown;
            unknown.reserve(synth.headers.size());
            for (auto const& [name, value] : synth.headers)
            {
                if (iequals_ascii(name, "Host") || iequals_ascii(name, "Content-Length"))
                    continue;
                unknown.emplace_back(name, value);
            }

            ULONG const base_size = sizeof(HTTP_REQUEST);

            request_buffer_writer w{reinterpret_cast<std::byte*>(pRequestBuffer),
                                    RequestBufferLength,
                                    base_size};

            // Only touch the fixed struct when it actually fits.
            bool const have_struct = (RequestBufferLength >= base_size);
            if (have_struct)
                ZeroMemory(pRequestBuffer, base_size);

            // Lay out the trailing variable-length data. When a reservation does
            // not fit, the returned pointer is null; the running offset still
            // advances so the caller learns the true required size and retries.
            PCSTR  raw_url_ptr = put_narrow(w, raw_url_narrow);
            PCWSTR full_ptr    = put_wide(w, full);
            PCWSTR host_ptr    = put_wide(w, host_wide);
            PCWSTR abspath_ptr = put_wide(w, abspath_wide);
            PCWSTR query_ptr   = query_wide.empty() ? nullptr : put_wide(w, query_wide);

            PCSTR content_length_ptr = put_narrow(w, content_length);
            PCSTR host_value_ptr     = put_narrow(w, host_narrow);

            // Unknown-header name/value strings, then the array that references them.
            std::vector<HTTP_UNKNOWN_HEADER> unknown_entries;
            unknown_entries.reserve(unknown.size());
            for (auto const& [name, value] : unknown)
            {
                HTTP_UNKNOWN_HEADER entry{};
                entry.NameLength     = static_cast<USHORT>(name.size());
                entry.pName          = put_narrow(w, name);
                entry.RawValueLength = static_cast<USHORT>(value.size());
                entry.pRawValue      = put_narrow(w, value);
                unknown_entries.push_back(entry);
            }

            HTTP_UNKNOWN_HEADER* unknown_array = nullptr;
            if (!unknown_entries.empty())
            {
                unknown_array = static_cast<HTTP_UNKNOWN_HEADER*>(w.reserve(
                    unknown_entries.size() * sizeof(HTTP_UNKNOWN_HEADER),
                    alignof(HTTP_UNKNOWN_HEADER)));
                if (unknown_array != nullptr)
                    std::memcpy(unknown_array,
                                unknown_entries.data(),
                                unknown_entries.size() * sizeof(HTTP_UNKNOWN_HEADER));
            }

            ULONG const needed = static_cast<ULONG>(w.offset);

            // If everything fits, fill in the fixed struct and its pointers.
            if (have_struct && needed <= RequestBufferLength)
            {
                pRequestBuffer->RequestId            = synth.request_id;
                pRequestBuffer->Version.MajorVersion = synth.http_version_major;
                pRequestBuffer->Version.MinorVersion = synth.http_version_minor;

                if (synth.method == "GET")
                    pRequestBuffer->Verb = HttpVerbGET;
                else if (synth.method == "POST")
                    pRequestBuffer->Verb = HttpVerbPOST;
                else if (synth.method == "PUT")
                    pRequestBuffer->Verb = HttpVerbPUT;
                else if (synth.method == "DELETE")
                    pRequestBuffer->Verb = HttpVerbDELETE;
                else if (synth.method == "HEAD")
                    pRequestBuffer->Verb = HttpVerbHEAD;
                else if (synth.method == "OPTIONS")
                    pRequestBuffer->Verb = HttpVerbOPTIONS;
                else if (synth.method == "TRACE")
                    pRequestBuffer->Verb = HttpVerbTRACE;
                else if (synth.method == "CONNECT")
                    pRequestBuffer->Verb = HttpVerbCONNECT;
                else
                    pRequestBuffer->Verb = HttpVerbUnknown;

                pRequestBuffer->pRawUrl     = raw_url_ptr;
                pRequestBuffer->RawUrlLength = static_cast<USHORT>(raw_url_narrow.size());

                auto& cu            = pRequestBuffer->CookedUrl;
                cu.pFullUrl         = full_ptr;
                cu.FullUrlLength    = static_cast<USHORT>(full.size() * sizeof(wchar_t));
                cu.pHost            = host_ptr;
                cu.HostLength       = static_cast<USHORT>(host_wide.size() * sizeof(wchar_t));
                cu.pAbsPath         = abspath_ptr;
                cu.AbsPathLength    = static_cast<USHORT>(abspath_wide.size() * sizeof(wchar_t));
                cu.pQueryString     = query_ptr;
                cu.QueryStringLength = static_cast<USHORT>(query_wide.size() * sizeof(wchar_t));

                auto& headers = pRequestBuffer->Headers;
                headers.KnownHeaders[HttpHeaderContentLength].pRawValue      = content_length_ptr;
                headers.KnownHeaders[HttpHeaderContentLength].RawValueLength = static_cast<USHORT>(content_length.size());
                headers.KnownHeaders[HttpHeaderHost].pRawValue               = host_value_ptr;
                headers.KnownHeaders[HttpHeaderHost].RawValueLength          = static_cast<USHORT>(host_narrow.size());
                headers.UnknownHeaderCount                                   = static_cast<USHORT>(unknown_entries.size());
                headers.pUnknownHeaders                                      = unknown_array;

                pRequestBuffer->BytesReceived = synth.body.size();

                if (pBytesReceived != nullptr)
                    *pBytesReceived = needed;
            }
            else
            {
                if (pBytesReceived != nullptr)
                    *pBytesReceived = 0;
            }

            return needed;
        }

        ULONG WINAPI
        hook_HttpReceiveHttpRequest(HANDLE          ReqQueueHandle,
                                     HTTP_REQUEST_ID RequestId,
                                     ULONG           Flags,
                                     PHTTP_REQUEST   pRequestBuffer,
                                     ULONG           RequestBufferLength,
                                     PULONG          pBytesReceived,
                                     LPOVERLAPPED    pOverlapped)
        {
            // Check for synthetic HTTP mode first.
            if (active_context() && active_context()->synthetic_http_enabled &&
                active_context()->synthetic_queue)
            {
                // Asynchronous mode with overlapped not supported for synthetic queue.
                if (pOverlapped != nullptr)
                {
                    // For now, return ERROR_INVALID_PARAMETER for async calls.
                    // A full implementation would support async via completion port.
                    return ERROR_INVALID_PARAMETER;
                }

                // Try to dequeue a synthetic request.
                auto synth_request = active_context()->synthetic_queue->try_dequeue_request();
                if (!synth_request)
                {
                    // No request available; return appropriate status.
                    // Real http.sys would block or return pending; we return immediately.
                    return ERROR_HANDLE_EOF;
                }

                // Marshal the synthetic request into the buffer.
                ULONG needed = marshal_synthetic_request(*synth_request,
                                                          pRequestBuffer,
                                                          RequestBufferLength,
                                                          pBytesReceived);
                if (needed > RequestBufferLength)
                {
                    // The caller's buffer is too small. try_dequeue_request has
                    // already removed the request, so put it back at the front
                    // (preserving its request_id and FIFO order) for the caller's
                    // retry with a larger buffer — otherwise the request is lost.
                    active_context()->synthetic_queue->requeue_front(std::move(*synth_request));
                    return ERROR_MORE_DATA;
                }

                // Stash the request body so a subsequent
                // HttpReceiveRequestEntityBody call can retrieve it. Without
                // this the engine would observe an empty body for POST/PUT.
                if (!synth_request->body.empty())
                {
                    std::lock_guard<std::mutex> guard(s_request_body_mutex);
                    s_synthetic_request_bodies[synth_request->request_id] =
                        std::make_pair(synth_request->body, static_cast<size_t>(0));
                }

                return NO_ERROR;
            }

            // Not synthetic mode; check for Tier A http_listener session.
            if (!active_context() || !active_context()->http_listener)
            {
                return original_HttpReceiveHttpRequest(
                    ReqQueueHandle, RequestId, Flags, pRequestBuffer,
                    RequestBufferLength, pBytesReceived, pOverlapped);
            }

            // Tier A mode: fall through to real http.sys.
            return original_HttpReceiveHttpRequest(
                ReqQueueHandle, RequestId, Flags, pRequestBuffer,
                RequestBufferLength, pBytesReceived, pOverlapped);
        }

        //----------------------------------------------------------------------
        // HttpReceiveRequestEntityBody hook (D-HWC-6, Tier B)
        //----------------------------------------------------------------------

        ULONG WINAPI
        hook_HttpReceiveRequestEntityBody(HANDLE          ReqQueueHandle,
                                           HTTP_REQUEST_ID RequestId,
                                           ULONG           Flags,
                                           PVOID           pBuffer,
                                           ULONG           BufferLength,
                                           PULONG          pBytesReceived,
                                           LPOVERLAPPED    pOverlapped)
        {
            // Check for synthetic HTTP mode.
            if (active_context() && active_context()->synthetic_http_enabled &&
                active_context()->synthetic_queue)
            {
                // Asynchronous mode not supported for synthetic queue.
                if (pOverlapped != nullptr)
                {
                    return ERROR_INVALID_PARAMETER;
                }

                // Look up the request body for this RequestId.
                std::lock_guard<std::mutex> guard(s_request_body_mutex);
                auto it = s_synthetic_request_bodies.find(RequestId);
                if (it == s_synthetic_request_bodies.end())
                {
                    // No body data for this request.
                    if (pBytesReceived)
                        *pBytesReceived = 0;
                    return ERROR_HANDLE_EOF;
                }

                auto& [body, offset] = it->second;
                size_t remaining = body.size() - offset;
                if (remaining == 0)
                {
                    // All body data consumed.
                    if (pBytesReceived)
                        *pBytesReceived = 0;
                    s_synthetic_request_bodies.erase(it);
                    return ERROR_HANDLE_EOF;
                }

                // Copy as much as fits in the buffer.
                size_t to_copy = (std::min)(static_cast<size_t>(BufferLength), remaining);
                memcpy(pBuffer, body.data() + offset, to_copy);
                offset += to_copy;

                if (pBytesReceived)
                    *pBytesReceived = static_cast<ULONG>(to_copy);

                // Check if there's more data.
                if (offset < body.size())
                {
                    return ERROR_MORE_DATA;
                }
                else
                {
                    s_synthetic_request_bodies.erase(it);
                    return NO_ERROR;
                }
            }

            // Not synthetic mode; fall through to real http.sys.
            if (!active_context() || !active_context()->http_listener)
            {
                return original_HttpReceiveRequestEntityBody(
                    ReqQueueHandle, RequestId, Flags, pBuffer,
                    BufferLength, pBytesReceived, pOverlapped);
            }

            return original_HttpReceiveRequestEntityBody(
                ReqQueueHandle, RequestId, Flags, pBuffer,
                BufferLength, pBytesReceived, pOverlapped);
        }

        //----------------------------------------------------------------------
        // HttpSendHttpResponse hook (D-HWC-6, Tier B)
        //----------------------------------------------------------------------

        // Helper: Extract response data from HTTP_RESPONSE and data chunks.
        static captured_http_response
        extract_response(HTTP_REQUEST_ID RequestId, PHTTP_RESPONSE pHttpResponse,
                         USHORT EntityChunkCount = 0, PHTTP_DATA_CHUNK pEntityChunks = nullptr)
        {
            captured_http_response response;
            response.request_id = RequestId;

            if (pHttpResponse)
            {
                response.status_code = pHttpResponse->StatusCode;

                // Copy reason phrase if present.
                if (pHttpResponse->pReason && pHttpResponse->ReasonLength > 0)
                {
                    response.reason_phrase.assign(
                        pHttpResponse->pReason,
                        pHttpResponse->ReasonLength);
                }

                // Extract known headers.
                for (int i = 0; i < HttpHeaderResponseMaximum; ++i)
                {
                    auto const& hdr = pHttpResponse->Headers.KnownHeaders[i];
                    if (hdr.pRawValue && hdr.RawValueLength > 0)
                    {
                        // Map header index to header name.
                        static char const* const known_header_names[] = {
                            "Cache-Control", "Connection", "Date", "Keep-Alive",
                            "Pragma", "Trailer", "Transfer-Encoding", "Upgrade",
                            "Via", "Warning", "Allow", "Content-Length",
                            "Content-Type", "Content-Encoding", "Content-Language",
                            "Content-Location", "Content-MD5", "Content-Range",
                            "Expires", "Last-Modified", "Accept-Ranges", "Age",
                            "ETag", "Location", "Proxy-Authenticate", "Retry-After",
                            "Server", "Set-Cookie", "Vary", "WWW-Authenticate"
                        };
                        if (i < static_cast<int>(std::size(known_header_names)))
                        {
                            response.headers.emplace_back(
                                known_header_names[i],
                                std::string(hdr.pRawValue, hdr.RawValueLength));
                        }
                    }
                }

                // Extract unknown headers.
                for (USHORT j = 0; j < pHttpResponse->Headers.UnknownHeaderCount; ++j)
                {
                    auto const& uhdr = pHttpResponse->Headers.pUnknownHeaders[j];
                    if (uhdr.pName && uhdr.NameLength > 0 &&
                        uhdr.pRawValue && uhdr.RawValueLength > 0)
                    {
                        response.headers.emplace_back(
                            std::string(uhdr.pName, uhdr.NameLength),
                            std::string(uhdr.pRawValue, uhdr.RawValueLength));
                    }
                }

                // Extract entity body from response if present.
                if (pHttpResponse->EntityChunkCount > 0 && pHttpResponse->pEntityChunks)
                {
                    for (USHORT k = 0; k < pHttpResponse->EntityChunkCount; ++k)
                    {
                        auto const& chunk = pHttpResponse->pEntityChunks[k];
                        if (chunk.DataChunkType == HttpDataChunkFromMemory &&
                            chunk.FromMemory.pBuffer && chunk.FromMemory.BufferLength > 0)
                        {
                            auto* data = static_cast<std::uint8_t const*>(chunk.FromMemory.pBuffer);
                            response.body.insert(response.body.end(),
                                                 data, data + chunk.FromMemory.BufferLength);
                        }
                    }
                }
            }

            // Extract additional entity chunks if provided.
            if (EntityChunkCount > 0 && pEntityChunks)
            {
                for (USHORT k = 0; k < EntityChunkCount; ++k)
                {
                    auto const& chunk = pEntityChunks[k];
                    if (chunk.DataChunkType == HttpDataChunkFromMemory &&
                        chunk.FromMemory.pBuffer && chunk.FromMemory.BufferLength > 0)
                    {
                        auto* data = static_cast<std::uint8_t const*>(chunk.FromMemory.pBuffer);
                        response.body.insert(response.body.end(),
                                             data, data + chunk.FromMemory.BufferLength);
                    }
                }
            }

            return response;
        }

        ULONG WINAPI
        hook_HttpSendHttpResponse(HANDLE            ReqQueueHandle,
                                   HTTP_REQUEST_ID   RequestId,
                                   ULONG             Flags,
                                   PHTTP_RESPONSE    pHttpResponse,
                                   PHTTP_CACHE_POLICY pCachePolicy,
                                   PULONG            pBytesSent,
                                   PVOID             pReserved1,
                                   ULONG             Reserved2,
                                   LPOVERLAPPED      pOverlapped,
                                   PHTTP_LOG_DATA    pLogData)
        {
            // Check for synthetic HTTP mode.
            if (active_context() && active_context()->synthetic_http_enabled &&
                active_context()->synthetic_queue)
            {
                // Capture the response.
                auto response = extract_response(RequestId, pHttpResponse);

                // Check if this is a complete response (no more data flag).
                bool is_complete = (Flags & HTTP_SEND_RESPONSE_FLAG_MORE_DATA) == 0;
                response.complete = is_complete;

                active_context()->synthetic_queue->capture_response(RequestId, std::move(response));

                // The request is finished once its complete response is sent.
                // Drop any leftover stashed request body so the map does not grow
                // unbounded when the engine never drained the entity body (e.g. a
                // GET, or a request the engine rejected without reading it).
                if (is_complete)
                {
                    std::lock_guard<std::mutex> guard(s_request_body_mutex);
                    s_synthetic_request_bodies.erase(RequestId);
                }

                // Calculate bytes sent (approximate).
                ULONG bytes_sent = sizeof(HTTP_RESPONSE);
                if (pHttpResponse)
                {
                    bytes_sent += pHttpResponse->ReasonLength;
                    // Add body size from entity chunks.
                    for (USHORT k = 0; k < pHttpResponse->EntityChunkCount; ++k)
                    {
                        auto const& chunk = pHttpResponse->pEntityChunks[k];
                        if (chunk.DataChunkType == HttpDataChunkFromMemory)
                        {
                            bytes_sent += chunk.FromMemory.BufferLength;
                        }
                    }
                }

                if (pBytesSent)
                    *pBytesSent = bytes_sent;

                return NO_ERROR;
            }

            // Not synthetic mode; fall through to real http.sys.
            if (!active_context() || !active_context()->http_listener)
            {
                return original_HttpSendHttpResponse(
                    ReqQueueHandle, RequestId, Flags, pHttpResponse, pCachePolicy,
                    pBytesSent, pReserved1, Reserved2, pOverlapped, pLogData);
            }

            return original_HttpSendHttpResponse(
                ReqQueueHandle, RequestId, Flags, pHttpResponse, pCachePolicy,
                pBytesSent, pReserved1, Reserved2, pOverlapped, pLogData);
        }

        //----------------------------------------------------------------------
        // HttpSendResponseEntityBody hook (D-HWC-6, Tier B)
        //----------------------------------------------------------------------

        ULONG WINAPI
        hook_HttpSendResponseEntityBody(HANDLE            ReqQueueHandle,
                                         HTTP_REQUEST_ID   RequestId,
                                         ULONG             Flags,
                                         USHORT            EntityChunkCount,
                                         PHTTP_DATA_CHUNK  pEntityChunks,
                                         PULONG            pBytesSent,
                                         PVOID             pReserved1,
                                         ULONG             Reserved2,
                                         LPOVERLAPPED      pOverlapped,
                                         PHTTP_LOG_DATA    pLogData)
        {
            // Check for synthetic HTTP mode.
            if (active_context() && active_context()->synthetic_http_enabled &&
                active_context()->synthetic_queue)
            {
                ULONG bytes_sent = 0;

                // Append body chunks to the response.
                for (USHORT k = 0; k < EntityChunkCount; ++k)
                {
                    auto const& chunk = pEntityChunks[k];
                    if (chunk.DataChunkType == HttpDataChunkFromMemory &&
                        chunk.FromMemory.pBuffer && chunk.FromMemory.BufferLength > 0)
                    {
                        auto* data = static_cast<std::uint8_t const*>(chunk.FromMemory.pBuffer);
                        std::span<std::uint8_t const> span(data, chunk.FromMemory.BufferLength);
                        active_context()->synthetic_queue->append_response_body(RequestId, span);
                        bytes_sent += chunk.FromMemory.BufferLength;
                    }
                }

                // Check if response is complete.
                bool is_complete = (Flags & HTTP_SEND_RESPONSE_FLAG_MORE_DATA) == 0;
                if (is_complete)
                {
                    active_context()->synthetic_queue->complete_response(RequestId);
                }

                if (pBytesSent)
                    *pBytesSent = bytes_sent;

                return NO_ERROR;
            }

            // Not synthetic mode; fall through to real http.sys.
            if (!active_context() || !active_context()->http_listener)
            {
                return original_HttpSendResponseEntityBody(
                    ReqQueueHandle, RequestId, Flags, EntityChunkCount, pEntityChunks,
                    pBytesSent, pReserved1, Reserved2, pOverlapped, pLogData);
            }

            return original_HttpSendResponseEntityBody(
                ReqQueueHandle, RequestId, Flags, EntityChunkCount, pEntityChunks,
                pBytesSent, pReserved1, Reserved2, pOverlapped, pLogData);
        }

    } // namespace hooks

    //--------------------------------------------------------------------------
    // IAT patching helpers
    //--------------------------------------------------------------------------

    namespace
    {
        // Patch a single IAT entry, returning the old value.
        bool
        patch_iat_entry(void** entry, void* new_func, void*& old_func)
        {
            DWORD old_protect = 0;
            if (!::VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old_protect))
            {
                return false;
            }

            old_func = *entry;
            *entry = new_func;

            ::VirtualProtect(entry, sizeof(void*), old_protect, &old_protect);
            return true;
        }

        // Restore an IAT entry.
        void
        restore_iat_entry(void** entry, void* old_func)
        {
            DWORD old_protect = 0;
            if (::VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old_protect))
            {
                *entry = old_func;
                ::VirtualProtect(entry, sizeof(void*), old_protect, &old_protect);
            }
        }

        // Structure defining which functions to hook.
        struct hook_definition
        {
            char const* dll_name;
            char const* function_name;
            void*       hook_func;
            void**      original_func_ptr;
        };

        hook_definition const k_hooks[] = {
            {"ADVAPI32.dll", "RegOpenKeyExW",    reinterpret_cast<void*>(hooks::hook_RegOpenKeyExW),    reinterpret_cast<void**>(&hooks::original_RegOpenKeyExW)},
            {"ADVAPI32.dll", "RegQueryValueExW", reinterpret_cast<void*>(hooks::hook_RegQueryValueExW), reinterpret_cast<void**>(&hooks::original_RegQueryValueExW)},
            {"ADVAPI32.dll", "RegCloseKey",      reinterpret_cast<void*>(hooks::hook_RegCloseKey),      reinterpret_cast<void**>(&hooks::original_RegCloseKey)},
            {"ADVAPI32.dll", "RegEnumKeyExW",    reinterpret_cast<void*>(hooks::hook_RegEnumKeyExW),    reinterpret_cast<void**>(&hooks::original_RegEnumKeyExW)},
            {"ADVAPI32.dll", "RegEnumValueW",    reinterpret_cast<void*>(hooks::hook_RegEnumValueW),    reinterpret_cast<void**>(&hooks::original_RegEnumValueW)},
            {"KERNEL32.dll", "CreateFileW",      reinterpret_cast<void*>(hooks::hook_CreateFileW),      reinterpret_cast<void**>(&hooks::original_CreateFileW)},
            {"KERNEL32.dll", "FindFirstFileW",   reinterpret_cast<void*>(hooks::hook_FindFirstFileW),   reinterpret_cast<void**>(&hooks::original_FindFirstFileW)},
            {"KERNEL32.dll", "FindNextFileW",    reinterpret_cast<void*>(hooks::hook_FindNextFileW),    reinterpret_cast<void**>(&hooks::original_FindNextFileW)},
            {"KERNEL32.dll", "FindClose",        reinterpret_cast<void*>(hooks::hook_FindClose),        reinterpret_cast<void**>(&hooks::original_FindClose)},
            {"KERNEL32.dll", "CloseHandle",      reinterpret_cast<void*>(hooks::hook_CloseHandle),      reinterpret_cast<void**>(&hooks::original_CloseHandle)},
            {"KERNEL32.dll", "GetFileAttributesW", reinterpret_cast<void*>(hooks::hook_GetFileAttributesW), reinterpret_cast<void**>(&hooks::original_GetFileAttributesW)},
            // Synthetic-file I/O hooks (M-HWC-REVIEW-2): make handles from CreateFileW usable.
            {"KERNEL32.dll", "ReadFile",         reinterpret_cast<void*>(hooks::hook_ReadFile),         reinterpret_cast<void**>(&hooks::original_ReadFile)},
            {"KERNEL32.dll", "WriteFile",        reinterpret_cast<void*>(hooks::hook_WriteFile),        reinterpret_cast<void**>(&hooks::original_WriteFile)},
            {"KERNEL32.dll", "GetFileSizeEx",    reinterpret_cast<void*>(hooks::hook_GetFileSizeEx),    reinterpret_cast<void**>(&hooks::original_GetFileSizeEx)},
            {"KERNEL32.dll", "GetFileSize",      reinterpret_cast<void*>(hooks::hook_GetFileSize),      reinterpret_cast<void**>(&hooks::original_GetFileSize)},
            {"KERNEL32.dll", "SetFilePointerEx", reinterpret_cast<void*>(hooks::hook_SetFilePointerEx), reinterpret_cast<void**>(&hooks::original_SetFilePointerEx)},
            {"KERNEL32.dll", "SetFilePointer",   reinterpret_cast<void*>(hooks::hook_SetFilePointer),   reinterpret_cast<void**>(&hooks::original_SetFilePointer)},
            {"KERNEL32.dll", "GetFileType",      reinterpret_cast<void*>(hooks::hook_GetFileType),      reinterpret_cast<void**>(&hooks::original_GetFileType)},
            {"KERNEL32.dll", "FlushFileBuffers", reinterpret_cast<void*>(hooks::hook_FlushFileBuffers), reinterpret_cast<void**>(&hooks::original_FlushFileBuffers)},
            {"KERNEL32.dll", "SetEndOfFile",     reinterpret_cast<void*>(hooks::hook_SetEndOfFile),     reinterpret_cast<void**>(&hooks::original_SetEndOfFile)},
            // HTTP Server API hooks (D-HWC-6, Tier A)
            {"httpapi.dll", "HttpAddUrl",               reinterpret_cast<void*>(hooks::hook_HttpAddUrl),               reinterpret_cast<void**>(&hooks::original_HttpAddUrl)},
            {"httpapi.dll", "HttpAddUrlToUrlGroup",     reinterpret_cast<void*>(hooks::hook_HttpAddUrlToUrlGroup),     reinterpret_cast<void**>(&hooks::original_HttpAddUrlToUrlGroup)},
            {"httpapi.dll", "HttpRemoveUrl",            reinterpret_cast<void*>(hooks::hook_HttpRemoveUrl),            reinterpret_cast<void**>(&hooks::original_HttpRemoveUrl)},
            {"httpapi.dll", "HttpRemoveUrlFromUrlGroup",reinterpret_cast<void*>(hooks::hook_HttpRemoveUrlFromUrlGroup),reinterpret_cast<void**>(&hooks::original_HttpRemoveUrlFromUrlGroup)},
            // HTTP Server API hooks (D-HWC-6, Tier B)
            {"httpapi.dll", "HttpReceiveHttpRequest",      reinterpret_cast<void*>(hooks::hook_HttpReceiveHttpRequest),      reinterpret_cast<void**>(&hooks::original_HttpReceiveHttpRequest)},
            {"httpapi.dll", "HttpReceiveRequestEntityBody",reinterpret_cast<void*>(hooks::hook_HttpReceiveRequestEntityBody),reinterpret_cast<void**>(&hooks::original_HttpReceiveRequestEntityBody)},
            {"httpapi.dll", "HttpSendHttpResponse",        reinterpret_cast<void*>(hooks::hook_HttpSendHttpResponse),        reinterpret_cast<void**>(&hooks::original_HttpSendHttpResponse)},
            {"httpapi.dll", "HttpSendResponseEntityBody",  reinterpret_cast<void*>(hooks::hook_HttpSendResponseEntityBody),  reinterpret_cast<void**>(&hooks::original_HttpSendResponseEntityBody)},
        };

    } // namespace

    //--------------------------------------------------------------------------
    // webcore_instance
    //--------------------------------------------------------------------------

    webcore_instance::webcore_instance(std::unique_ptr<iwebcore_instance>    underlying_instance,
                                       std::unique_ptr<interception_context> context,
                                       HMODULE                               target_module,
                                       std::vector<iat_hook>                 installed_hooks):
        m_underlying_instance(std::move(underlying_instance)),
        m_target_module(target_module),
        m_installed_hooks(std::move(installed_hooks)),
        m_context(std::move(context))
    {}

    webcore_instance::~webcore_instance()
    {
        // First shut down the underlying instance (which may still make hooked calls).
        m_underlying_instance.reset();

        // Then uninstall the hooks.
        if (m_target_module && !m_installed_hooks.empty())
        {
            for (auto const& hook : m_installed_hooks)
            {
                restore_iat_entry(hook.iat_entry, hook.original_func);
            }
        }

        // Clear the active context before m_context frees it: once the hooks are
        // uninstalled no further engine call can route through the context, so
        // it is safe to detach the global pointer and let m_context destroy the
        // owned interception_context.
        g_active_context_cell.store(nullptr, std::memory_order_release);

        // Drop any synthetic request bodies that outlived their request (never
        // drained, no completing response) so they do not survive this activation.
        hooks::clear_synthetic_request_bodies();
    }

    //--------------------------------------------------------------------------
    // webcore
    //--------------------------------------------------------------------------

    webcore::webcore(std::shared_ptr<iplatform> platform,
                     std::shared_ptr<iwebcore>  underlying_webcore):
        m_platform(std::move(platform)),
        m_underlying_webcore(std::move(underlying_webcore))
    {
        M_INTERNAL_ERROR_CHECK(m_platform != nullptr);
        M_INTERNAL_ERROR_CHECK(m_underlying_webcore != nullptr);
    }

    std::vector<iat_hook>
    webcore::install_iat_hooks(HMODULE target_module)
    {
        std::vector<iat_hook> installed;

        // Use the DbgHelp ImageDirectoryEntryToDataEx to locate the import directory.
        ULONG import_dir_size = 0;
        auto* import_desc = static_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            ::ImageDirectoryEntryToDataEx(
                target_module,
                TRUE, // mapped as image
                IMAGE_DIRECTORY_ENTRY_IMPORT,
                &import_dir_size,
                nullptr));

        if (!import_desc)
        {
            return installed;
        }

        auto const base_addr = reinterpret_cast<uintptr_t>(target_module);

        // Walk the import descriptors.
        for (; import_desc->Name != 0; ++import_desc)
        {
            auto const* dll_name = reinterpret_cast<char const*>(base_addr + import_desc->Name);

            // Walk the thunk arrays (IAT and INT).
            auto* iat_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                base_addr + import_desc->FirstThunk);
            auto* int_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                base_addr + import_desc->OriginalFirstThunk);

            for (; iat_thunk->u1.Function != 0; ++iat_thunk, ++int_thunk)
            {
                // Check if this is an ordinal import (skip).
                if (IMAGE_SNAP_BY_ORDINAL(int_thunk->u1.Ordinal))
                    continue;

                // Get the imported function name.
                auto const* import_by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                    base_addr + int_thunk->u1.AddressOfData);
                char const* func_name = reinterpret_cast<char const*>(import_by_name->Name);

                // Check if this is one of the functions we want to hook.
                for (auto const& def : k_hooks)
                {
                    if (_stricmp(dll_name, def.dll_name) == 0 &&
                        strcmp(func_name, def.function_name) == 0)
                    {
                        // Found a match — patch it.
                        void* old_func = nullptr;
                        void** entry = reinterpret_cast<void**>(&iat_thunk->u1.Function);

                        if (patch_iat_entry(entry, def.hook_func, old_func))
                        {
                            // Save the original for restoring and for the hook to call.
                            *def.original_func_ptr = old_func;

                            installed.push_back({
                                entry,
                                old_func,
                                def.hook_func,
                                def.function_name
                            });
                        }
                        break;
                    }
                }
            }
        }

        return installed;
    }

    void
    webcore::uninstall_iat_hooks(HMODULE /*target_module*/, std::vector<iat_hook> const& hooks)
    {
        for (auto const& hook : hooks)
        {
            restore_iat_entry(hook.iat_entry, hook.original_func);
        }
    }

    iwebcore::activate_disposition
    webcore::activate(activate_flags                      flags,
                      activation_request const&           request,
                      std::unique_ptr<iwebcore_instance>& returned_instance,
                      std::error_code&                    ec)
    {
        ec.clear();
        returned_instance.reset();

        std::lock_guard<std::mutex> guard(m_mutex);

        // Get the PIL surfaces from the platform.
        auto registry = m_platform->get_registry();
        auto filesystem = m_platform->get_filesystem();
        auto http_listener = m_platform->get_http_listener();

        // Set up the interception context. Ownership stays with this local
        // unique_ptr until it is either transferred to the webcore_instance on
        // success or released on a failure path; active_context() only borrows
        // the raw pointer for the duration of the hooks.
        auto ctx = std::make_unique<interception_context>();
        ctx->registry = registry;
        ctx->filesystem = filesystem;
        ctx->http_listener = http_listener;

        // Install the context as the active one for the hooks.
        g_active_context_cell.store(ctx.get(), std::memory_order_release);

        // TODO: We need the HMODULE of hwebcore.dll. The underlying webcore
        // doesn't expose it directly. For now, find it by name after load.
        HMODULE hwc_module = ::GetModuleHandleW(L"hwebcore.dll");
        if (!hwc_module)
        {
            // Engine not loaded yet — call activate to load it, then patch.
            std::unique_ptr<iwebcore_instance> underlying_instance;
            auto d = m_underlying_webcore->activate(flags, request, underlying_instance, ec);
            if (d || !underlying_instance)
            {
                g_active_context_cell.store(nullptr, std::memory_order_release);
                return d;
            }

            // Now the module should be loaded.
            hwc_module = ::GetModuleHandleW(L"hwebcore.dll");
            if (!hwc_module)
            {
                // Still not found — return the underlying instance without hooks.
                g_active_context_cell.store(nullptr, std::memory_order_release);
                returned_instance = std::move(underlying_instance);
                return d;
            }

            // Install hooks and wrap the instance.
            auto hooks = install_iat_hooks(hwc_module);

            returned_instance = std::make_unique<webcore_instance>(
                std::move(underlying_instance),
                std::move(ctx),
                hwc_module,
                std::move(hooks));

            return d;
        }

        // Module already loaded — install hooks first, then activate.
        auto hooks = install_iat_hooks(hwc_module);

        std::unique_ptr<iwebcore_instance> underlying_instance;
        auto d = m_underlying_webcore->activate(flags, request, underlying_instance, ec);
        if (d || !underlying_instance)
        {
            // Activation failed — uninstall hooks and return.
            uninstall_iat_hooks(hwc_module, hooks);
            g_active_context_cell.store(nullptr, std::memory_order_release);
            return d;
        }

        returned_instance = std::make_unique<webcore_instance>(
            std::move(underlying_instance),
            std::move(ctx),
            hwc_module,
            std::move(hooks));

        return d;
    }

    iwebcore::set_metadata_disposition
    webcore::set_metadata(set_metadata_flags  flags,
                          std::u16string_view type,
                          std::u16string_view value,
                          std::error_code&    ec)
    {
        // Pass through to the underlying webcore.
        return m_underlying_webcore->set_metadata(flags, type, value, ec);
    }

    //--------------------------------------------------------------------------
    // Factory function
    //--------------------------------------------------------------------------

    std::shared_ptr<iwebcore>
    create_intercepting_webcore(std::shared_ptr<iplatform> platform,
                                std::shared_ptr<iwebcore>  underlying_webcore)
    {
        return std::make_shared<webcore>(std::move(platform), std::move(underlying_webcore));
    }

} // namespace m::pil::impl::intercepting
