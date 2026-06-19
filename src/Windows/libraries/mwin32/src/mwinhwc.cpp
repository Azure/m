// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <m/mwin32/mwinhwc.h>

#include "session.h"

//
// Win32 Hostable Web Core (HWC) shim (mwin32 M-HWC-SHIM). Each entry point
// mirrors the genuine hwebcore.dll signature and routes through the
// process-wide PIL session into iplatform::get_webcore(). The active mode
// (passthrough / logging / fault) is chosen by the .pilcfg sidecar.
//
// Contract (D-HWC-5): only a single activation is allowed per process. The
// session tracks the active iwebcore_instance; a second activation returns
// HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING). mWebCoreShutdown with no
// active instance returns HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE). These
// match the real hwebcore.dll contract.
//
// Failure model: the HWC ABI is pure HRESULT, so all failures (including OOM
// and any uncaught exceptions) flow through the return value; no exception is
// ever allowed to cross the C ABI.
//

HRESULT APIENTRY
mWebCoreActivate(_In_ PCWSTR pszAppHostConfigFile,
                 _In_opt_ PCWSTR pszRootWebConfigFile,
                 _In_ PCWSTR pszInstanceName)
{
    return m::mwin32_impl::session_webcore_activate(
        pszAppHostConfigFile, pszRootWebConfigFile, pszInstanceName);
}

HRESULT APIENTRY
mWebCoreShutdown(_In_ DWORD fImmediate)
{
    return m::mwin32_impl::session_webcore_shutdown(fImmediate);
}

HRESULT APIENTRY
mWebCoreSetMetadata(_In_ PCWSTR pszMetadataType, _In_ PCWSTR pszValue)
{
    return m::mwin32_impl::session_webcore_set_metadata(pszMetadataType, pszValue);
}
