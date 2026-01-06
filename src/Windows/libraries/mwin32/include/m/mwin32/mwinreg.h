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

