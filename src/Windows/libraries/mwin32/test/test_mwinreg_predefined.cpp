// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/mwin32/mWindows.h>

//
// These tests exercise the session bootstrap: a predefined HKEY
// (HKEY_CURRENT_USER) must resolve through the PIL passthrough session to a
// live key, an open subkey handle must round-trip through the shim's handle
// table, and closing a predefined pseudo-handle must be a success no-op.
//
// All operations are read-only against HKEY_CURRENT_USER\Software, which always
// exists, so the live registry is never modified.
//

TEST(TestPredefinedKeys, OpenAndCloseSoftwareSubkey)
{
    HKEY    hSubkey = nullptr;
    LSTATUS status  = mRegOpenKeyExW(
        HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &hSubkey);

    EXPECT_EQ(ERROR_SUCCESS, status);
    EXPECT_NE(nullptr, hSubkey);

    if (status == ERROR_SUCCESS)
    {
        EXPECT_EQ(ERROR_SUCCESS, mRegCloseKey(hSubkey));
    }
}

TEST(TestPredefinedKeys, ClosingPredefinedHandleIsNoOp)
{
    // RegCloseKey on a predefined pseudo-handle is documented to succeed and
    // leave the always-open key usable.
    EXPECT_EQ(ERROR_SUCCESS, mRegCloseKey(HKEY_CURRENT_USER));

    // The key must still be usable after the no-op close.
    HKEY    hSubkey = nullptr;
    LSTATUS status  = mRegOpenKeyExW(
        HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &hSubkey);

    EXPECT_EQ(ERROR_SUCCESS, status);
    if (status == ERROR_SUCCESS)
    {
        EXPECT_EQ(ERROR_SUCCESS, mRegCloseKey(hSubkey));
    }
}
