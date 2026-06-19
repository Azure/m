// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

LSTATUS
APIENTRY
mRegCloseKey(_In_ HKEY hKey);

LSTATUS
APIENTRY
mRegOpenKeyA(_In_ HKEY hKey, _In_opt_ LPCSTR lpSubKey, _Out_ PHKEY phkResult);

LSTATUS
APIENTRY
mRegOpenKeyW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey, _Out_ PHKEY phkResult);

#ifdef UNICODE
#define mRegOpenKey mRegOpenKeyW
#else
#define mRegOpenKey mRegOpenKeyA
#endif // !UNICODE

LSTATUS
APIENTRY
mRegOpenKeyExA(_In_ HKEY       hKey,
              _In_opt_ LPCSTR lpSubKey,
              _In_opt_ DWORD  ulOptions,
              _In_ REGSAM     samDesired,
              _Out_ PHKEY     phkResult);

LSTATUS
APIENTRY
mRegOpenKeyExW(_In_ HKEY        hKey,
              _In_opt_ LPCWSTR lpSubKey,
              _In_opt_ DWORD   ulOptions,
              _In_ REGSAM      samDesired,
              _Out_ PHKEY      phkResult);

#ifdef UNICODE
#define mRegOpenKeyEx mRegOpenKeyExW
#else
#define mRegOpenKeyEx mRegOpenKeyExA
#endif // !UNICODE

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
                 _Out_opt_ LPDWORD                    lpdwDisposition);

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
                 _Out_opt_ LPDWORD                    lpdwDisposition);

#ifdef UNICODE
#define mRegCreateKeyEx mRegCreateKeyExW
#else
#define mRegCreateKeyEx mRegCreateKeyExA
#endif // !UNICODE

LSTATUS
APIENTRY
mRegSetValueExA(_In_ HKEY                                hKey,
                _In_opt_ LPCSTR                          lpValueName,
                _Reserved_ DWORD                         Reserved,
                _In_ DWORD                               dwType,
                _In_reads_bytes_opt_(cbData) CONST BYTE* lpData,
                _In_ DWORD                               cbData);

LSTATUS
APIENTRY
mRegSetValueExW(_In_ HKEY                                hKey,
                _In_opt_ LPCWSTR                         lpValueName,
                _Reserved_ DWORD                         Reserved,
                _In_ DWORD                               dwType,
                _In_reads_bytes_opt_(cbData) CONST BYTE* lpData,
                _In_ DWORD                               cbData);

#ifdef UNICODE
#define mRegSetValueEx mRegSetValueExW
#else
#define mRegSetValueEx mRegSetValueExA
#endif // !UNICODE

LSTATUS
APIENTRY
mRegQueryValueExA(_In_ HKEY          hKey,
                  _In_opt_ LPCSTR    lpValueName,
                  _Reserved_ LPDWORD lpReserved,
                  _Out_opt_ LPDWORD  lpType,
                  _Out_writes_bytes_to_opt_(*lpcbData, *lpcbData) __out_data_source(REGISTRY)
                      LPBYTE lpData,
                  _When_(lpData == NULL, _Out_opt_) _When_(lpData != NULL, _Inout_opt_)
                      LPDWORD lpcbData);

LSTATUS
APIENTRY
mRegQueryValueExW(_In_ HKEY          hKey,
                  _In_opt_ LPCWSTR   lpValueName,
                  _Reserved_ LPDWORD lpReserved,
                  _Out_opt_ LPDWORD  lpType,
                  _Out_writes_bytes_to_opt_(*lpcbData, *lpcbData) __out_data_source(REGISTRY)
                      LPBYTE lpData,
                  _When_(lpData == NULL, _Out_opt_) _When_(lpData != NULL, _Inout_opt_)
                      LPDWORD lpcbData);

#ifdef UNICODE
#define mRegQueryValueEx mRegQueryValueExW
#else
#define mRegQueryValueEx mRegQueryValueExA
#endif // !UNICODE

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
               _Inout_opt_ LPDWORD lpcbData);

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
               _Inout_opt_ LPDWORD lpcbData);

#ifdef UNICODE
#define mRegEnumValue mRegEnumValueW
#else
#define mRegEnumValue mRegEnumValueA
#endif // !UNICODE


