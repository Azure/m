// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

//
// Win32 Hostable Web Core (HWC) shim (mwin32 M-HWC-SHIM). These entry points
// mirror the shape of the genuine hwebcore.dll exports (WebCoreActivate,
// WebCoreShutdown, WebCoreSetMetadata) — all returning HRESULT — so an unmodified
// client redirects through the generated mwin32_alias object with no source
// change. Each routes through the process-wide PIL session into
// iplatform::get_webcore(); the active mode (passthrough / logging / fault) is
// chosen by the .pilcfg sidecar.
//
// Contract (D-HWC-5): only a single activation is allowed per process. A second
// mWebCoreActivate while the engine is active returns
// HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING). mWebCoreShutdown with no
// active instance returns HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE). These
// match the real hwebcore.dll contract.
//
// Unlike the registry shims, the HWC ABI is pure HRESULT — not LSTATUS and not
// BOOL+GetLastError — so all failures flow through the return value.
//

HRESULT APIENTRY
mWebCoreActivate(_In_ PCWSTR pszAppHostConfigFile,
                 _In_opt_ PCWSTR pszRootWebConfigFile,
                 _In_ PCWSTR pszInstanceName);

HRESULT APIENTRY
mWebCoreShutdown(_In_ DWORD fImmediate);

HRESULT APIENTRY
mWebCoreSetMetadata(_In_ PCWSTR pszMetadataType, _In_ PCWSTR pszValue);
