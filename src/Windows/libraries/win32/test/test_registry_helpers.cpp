// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <string_view>

#include <Windows.h>

#include <m/win32/registry.h>

using namespace std::string_view_literals;

TEST(Win32RegistryHelpers, PositivePredefinedKeyLookup)
{
    auto pk1 = m::win32::registry::try_map_string_to_predefined_key(L"HKLM"sv);
    EXPECT_EQ(pk1, m::win32::registry::predefined_key::local_machine);

    auto pk2 = m::win32::registry::try_map_string_to_predefined_key(L"hklm"sv);
    EXPECT_EQ(pk2, m::win32::registry::predefined_key::local_machine);

    auto pk3 = m::win32::registry::try_map_string_to_predefined_key(L"HkLm"sv);
    EXPECT_EQ(pk3, m::win32::registry::predefined_key::local_machine);
}

TEST(Win32RegistryHelpers, NegativePredefinedKeyLookup)
{
    auto pk1 = m::win32::registry::try_map_string_to_predefined_key(L"xHKLM"sv);
    EXPECT_FALSE(pk1.has_value());

    auto pk2 = m::win32::registry::try_map_string_to_predefined_key(L"hxlm"sv);
    EXPECT_FALSE(pk2.has_value());

    auto pk3 = m::win32::registry::try_map_string_to_predefined_key(L"HkLmxxxxa"sv);
    EXPECT_FALSE(pk3.has_value());
}

TEST(Win32RegistryHelpers, TestWin32HKeyMapping)
{
    auto pk1 = m::win32::registry::try_map_hkey_to_predefined_key(HKEY_CLASSES_ROOT);
    EXPECT_TRUE(pk1.has_value());
    EXPECT_EQ(pk1, m::win32::registry::predefined_key::classes_root);

    auto pk2 = m::win32::registry::try_map_hkey_to_predefined_key(
        reinterpret_cast<HKEY>(INVALID_HANDLE_VALUE));
    EXPECT_FALSE(pk2.has_value());

    auto k1 = m::win32::registry::map_predefined_key_to_hkey(pk1.value());
    EXPECT_EQ(k1, HKEY_CLASSES_ROOT);
}

TEST(Win32RegistryHelpers, TestIsPredefinedHKey)
{
    using namespace m::win32::registry;

    EXPECT_TRUE(is_predefined_hkey(HKEY_CLASSES_ROOT));
    EXPECT_TRUE(is_predefined_hkey(HKEY_LOCAL_MACHINE));
    EXPECT_TRUE(is_predefined_hkey(HKEY_CURRENT_USER));
    EXPECT_TRUE(is_predefined_hkey(HKEY_USERS));
    EXPECT_TRUE(is_predefined_hkey(HKEY_PERFORMANCE_DATA));
    EXPECT_TRUE(is_predefined_hkey(HKEY_CURRENT_CONFIG));
    EXPECT_TRUE(is_predefined_hkey(HKEY_CURRENT_USER_LOCAL_SETTINGS));
    EXPECT_TRUE(is_predefined_hkey(HKEY_PERFORMANCE_TEXT));
    EXPECT_TRUE(is_predefined_hkey(HKEY_PERFORMANCE_NLSTEXT));
}

TEST(Win32RegistryHelpers, ValidateHKeyToPredefinedKeyRoundTrip)
{
    using namespace m::win32::registry;

    auto pk1 = try_map_hkey_to_predefined_key(HKEY_CLASSES_ROOT);
    EXPECT_TRUE(pk1.has_value());
    EXPECT_EQ(pk1.value(), predefined_key::classes_root);
    auto k1 = map_predefined_key_to_hkey(pk1.value());
    EXPECT_EQ(k1, HKEY_CLASSES_ROOT);

    auto pk2 = try_map_hkey_to_predefined_key(HKEY_LOCAL_MACHINE);
    EXPECT_TRUE(pk2.has_value());
    EXPECT_EQ(pk2.value(), predefined_key::local_machine);
    auto k2 = map_predefined_key_to_hkey(pk2.value());
    EXPECT_EQ(k2, HKEY_LOCAL_MACHINE);

    auto pk3 = try_map_hkey_to_predefined_key(HKEY_CURRENT_USER);
    EXPECT_TRUE(pk3.has_value());
    EXPECT_EQ(pk3.value(), predefined_key::current_user);
    auto k3 = map_predefined_key_to_hkey(pk3.value());
    EXPECT_EQ(k3, HKEY_CURRENT_USER);

    auto pk4 = try_map_hkey_to_predefined_key(HKEY_USERS);
    EXPECT_TRUE(pk4.has_value());
    EXPECT_EQ(pk4.value(), predefined_key::users);
    auto k4 = map_predefined_key_to_hkey(pk4.value());
    EXPECT_EQ(k4, HKEY_USERS);

    auto pk5 = try_map_hkey_to_predefined_key(HKEY_PERFORMANCE_DATA);
    EXPECT_TRUE(pk5.has_value());
    EXPECT_EQ(pk5.value(), predefined_key::performance_data);
    auto k5 = map_predefined_key_to_hkey(pk5.value());
    EXPECT_EQ(k5, HKEY_PERFORMANCE_DATA);

    auto pk6 = try_map_hkey_to_predefined_key(HKEY_CURRENT_CONFIG);
    EXPECT_TRUE(pk6.has_value());
    EXPECT_EQ(pk6.value(), predefined_key::current_config);
    auto k6 = map_predefined_key_to_hkey(pk6.value());
    EXPECT_EQ(k6, HKEY_CURRENT_CONFIG);

    auto pk7 = try_map_hkey_to_predefined_key(HKEY_CURRENT_USER_LOCAL_SETTINGS);
    EXPECT_TRUE(pk7.has_value());
    EXPECT_EQ(pk7.value(), predefined_key::current_user_local_settings);
    auto k7 = map_predefined_key_to_hkey(pk7.value());
    EXPECT_EQ(k7, HKEY_CURRENT_USER_LOCAL_SETTINGS);

    auto pk8 = try_map_hkey_to_predefined_key(HKEY_PERFORMANCE_TEXT);
    EXPECT_TRUE(pk8.has_value());
    EXPECT_EQ(pk8.value(), predefined_key::performance_text);
    auto k8 = map_predefined_key_to_hkey(pk8.value());
    EXPECT_EQ(k8, HKEY_PERFORMANCE_TEXT);

    auto pk9 = try_map_hkey_to_predefined_key(HKEY_PERFORMANCE_NLSTEXT);
    EXPECT_TRUE(pk9.has_value());
    EXPECT_EQ(pk9.value(), predefined_key::performance_nlstext);
    auto k9 = map_predefined_key_to_hkey(pk9.value());
    EXPECT_EQ(k9, HKEY_PERFORMANCE_NLSTEXT);
}

TEST(Win32RegistryHelpers, TestClosingHKCU)
{
    using namespace m::win32::registry;

    hkey hkcu{HKEY_CURRENT_USER};
}

TEST(Win32RegistryHelpers, HkeyDefaultIsInvalid)
{
    m::win32::registry::hkey k;
    EXPECT_FALSE(k.is_valid());
    EXPECT_FALSE(static_cast<bool>(k));
}

TEST(Win32RegistryHelpers, HkeyPredefinedGet)
{
    // Wrapping a predefined HKEY stores the value and returns it via get().
    // is_valid() intentionally returns false for predefined keys because they
    // are not closable via RegCloseKey.
    m::win32::registry::hkey k{HKEY_CURRENT_USER};
    EXPECT_EQ(k.get(), HKEY_CURRENT_USER);
    // Destructor must not call RegCloseKey on predefined keys — just verify
    // construction + destruction does not crash.
}

TEST(Win32RegistryHelpers, HkeySwap)
{
    using namespace m::win32::registry;

    // Open a real key so is_valid() reflects a closable hkey.
    hkey a;
    ASSERT_FALSE(a.openq(predefined_key::current_user, L"Software", KEY_QUERY_VALUE));
    hkey b;

    EXPECT_TRUE(a.is_valid());
    EXPECT_FALSE(b.is_valid());

    a.swap(b);

    EXPECT_FALSE(a.is_valid());
    EXPECT_TRUE(b.is_valid());
}

TEST(Win32RegistryHelpers, OpenqHappyPath)
{
    // HKCU\Software exists on essentially all Windows machines.
    using namespace m::win32::registry;

    hkey k;
    auto ec = k.openq(predefined_key::current_user, L"Software", KEY_QUERY_VALUE);

    EXPECT_FALSE(ec) << ec.message();
    EXPECT_TRUE(k.is_valid());
}

TEST(Win32RegistryHelpers, OpenqFailurePath)
{
    using namespace m::win32::registry;

    hkey k;
    auto ec = k.openq(predefined_key::current_user,
                      L"Software\\__nonexistent_key_m_win32_test__",
                      KEY_QUERY_VALUE);

    EXPECT_TRUE(ec) << "Expected error for non-existent key";
    // hkey must remain invalid after a failed openq.
    EXPECT_FALSE(k.is_valid());
}

TEST(Win32RegistryHelpers, OpenThrowsOnSuccess)
{
    using namespace m::win32::registry;

    hkey k;
    ASSERT_NO_THROW(k.open(predefined_key::current_user, L"Software", KEY_QUERY_VALUE));
    EXPECT_TRUE(k.is_valid());
}

TEST(Win32RegistryHelpers, OpenThrowsOnFailure)
{
    using namespace m::win32::registry;

    hkey k;
    EXPECT_ANY_THROW(k.open(predefined_key::current_user,
                            L"Software\\__nonexistent_key_m_win32_test__",
                            KEY_QUERY_VALUE));
    EXPECT_FALSE(k.is_valid());
}

TEST(Win32RegistryHelpers, OpenqFromHkeyBase)
{
    // Open HKCU\Software, then open a subkey from the resulting hkey.
    using namespace m::win32::registry;

    hkey software;
    auto ec = software.openq(predefined_key::current_user, L"Software", KEY_QUERY_VALUE);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(software.is_valid());

    // Now open using the hkey_base overload (openq(hkey const&, ...)).
    hkey software2;
    ec = software2.openq(software, L"", KEY_QUERY_VALUE);
    EXPECT_FALSE(ec) << ec.message();
    EXPECT_TRUE(software2.is_valid());
}

TEST(Win32RegistryHelpers, TestResetKey)
{
    using namespace m::win32::registry;

    hkey somekey{HKEY_CURRENT_USER};

    somekey.reset(HKEY_LOCAL_MACHINE);
}

TEST(Win32RegistryHelpers, TestReopenKey)
{
    using namespace m::win32::registry;

    hkey somekey{HKEY_CURRENT_USER};

    hkey otherkey;

    auto status = ::RegOpenKeyExW(somekey, nullptr, 0, 0, otherkey.addressof());
    EXPECT_EQ(status, ERROR_SUCCESS);

    otherkey.reset();
}

#if 0
//
// This test is #if'd out by default because in the GitHub CI, it cannot run
// successfully and we don't have a way to differentiate at this time.
//
TEST(Win32RegistryHelpers, TestOpenFromReopenedKey)
{
    using namespace m::win32::registry;

    hkey somekey{HKEY_LOCAL_MACHINE};

    hkey otherkey;

    // For some reason, samDesired == 0 does work on predefined keys (bug?)
    auto status = ::RegOpenKeyExW(somekey, nullptr, 0, 0, otherkey.addressof());
    EXPECT_EQ(status, ERROR_SUCCESS);

    hkey yetanotherkey;

    // But for accessing any other keys, a non-zero samDesired is required. This does
    // not vary for whether the base key is the reopened HKEY_CURRENT_USER or the
    // actual HKEY_CURRENT_USER value.
    status = ::RegOpenKeyExW(otherkey, L"Microsoft", 0, KEY_QUERY_VALUE, yetanotherkey.addressof());
    EXPECT_EQ(status, ERROR_SUCCESS);
}
#endif
