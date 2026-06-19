// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// test_mwinhwc.cpp — unit tests for the mWebCore* HWC shim entry points
// (M-HWC-SHIM-6). These tests exercise the shim ABI through a fake engine
// (the null webcore surface or a test-injectable fake), verifying that:
//
//   - The HRESULT shapes match the real hwebcore.dll contract
//   - Single-activation-per-process is enforced at the session level
//   - Shutdown with no active instance yields ERROR_SERVICE_NOT_ACTIVE
//   - Double-activate yields ERROR_SERVICE_ALREADY_RUNNING
//

#include <gtest/gtest.h>

#include <m/mwin32/mwinhwc.h>

//
// NOTE: These tests exercise the shim against the session's configured webcore
// surface. By default (with no .pilcfg or a passthrough config), that surfaces
// the null_webcore provider which throws "not implemented" on activation —
// which the session catch-all maps to E_FAIL. The real engine is not loaded.
//
// To properly test the HWC shim's session-level single-activation contract, we
// need to either:
//   (a) Use a .pilcfg that wires a fake/test webcore provider (future work), or
//   (b) Test only the error paths that don't depend on a real engine.
//
// For now, we test the contract that the shim itself enforces (shutdown without
// activation → ERROR_SERVICE_NOT_ACTIVE) and verify that the API surface is
// callable.
//

TEST(MWinHwcTest, ShutdownWithoutActivateReturnsNotActive)
{
    // Without any activation, shutdown must return ERROR_SERVICE_NOT_ACTIVE.
    HRESULT hr = mWebCoreShutdown(FALSE);
    EXPECT_EQ(hr, HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE));
}

TEST(MWinHwcTest, ShutdownImmediateWithoutActivateReturnsNotActive)
{
    HRESULT hr = mWebCoreShutdown(TRUE);
    EXPECT_EQ(hr, HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE));
}

TEST(MWinHwcTest, ActivateWithNullConfigFailsGracefully)
{
    // The shim should handle null pointers gracefully. The null webcore
    // provider (used when no real engine is configured) throws not_implemented,
    // which the session maps to E_NOTIMPL.
    HRESULT hr = mWebCoreActivate(nullptr, nullptr, nullptr);
    // The null webcore throws not_implemented → E_NOTIMPL.
    EXPECT_EQ(hr, E_NOTIMPL);
}

TEST(MWinHwcTest, SetMetadataReturnsNotImpl)
{
    // The set_metadata entry point against the null webcore returns E_NOTIMPL.
    HRESULT hr = mWebCoreSetMetadata(L"TestType", L"TestValue");
    EXPECT_EQ(hr, E_NOTIMPL);
}
