// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <Windows.h>

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <m/mwin32/mWindows.h>

//
// These tests exercise the registry value operations (mRegSetValueEx* /
// mRegQueryValueEx*) end-to-end through the C ABI.
//
// The test executable runs with a sibling `.pilcfg` that enables
// buffer_updates (see this directory's CMakeLists.txt), so every value written
// here lands in the in-memory buffered overlay and never reaches the live
// registry. Writes target the predefined HKEY_CURRENT_USER handle directly;
// the buffered overlay intercepts them. Each test uses a distinct value name so
// the process-wide buffered session does not leak state between tests.
//

namespace
{
    constexpr DWORD kDwordValue = 0xDEADBEEFul;

    // Byte size of a wide string literal including its terminating NUL.
    template <std::size_t N>
    constexpr DWORD
    wide_bytes_with_nul(wchar_t const (&)[N])
    {
        return static_cast<DWORD>(N * sizeof(wchar_t));
    }

    // Create (or open) a fresh single-component subkey under HKCU through the
    // shim and return its handle. Each enumeration test uses a distinct name so
    // the process-wide buffered session does not leak values between tests.
    HKEY
    create_subkey(wchar_t const* name)
    {
        HKEY subkey = nullptr;
        EXPECT_EQ(ERROR_SUCCESS,
                  mRegCreateKeyExW(HKEY_CURRENT_USER,
                                   name,
                                   0,
                                   nullptr,
                                   0,
                                   KEY_ALL_ACCESS,
                                   nullptr,
                                   &subkey,
                                   nullptr));
        return subkey;
    }
}

TEST(TestValueOps, DwordRoundTripW)
{
    auto const  name = L"mwin32_test_dword";
    DWORD const in   = kDwordValue;

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(HKEY_CURRENT_USER,
                              name,
                              0,
                              REG_DWORD,
                              reinterpret_cast<BYTE const*>(&in),
                              sizeof(in)));

    DWORD type = 0;
    DWORD out  = 0;
    DWORD cb   = sizeof(out);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegQueryValueExW(HKEY_CURRENT_USER,
                                name,
                                nullptr,
                                &type,
                                reinterpret_cast<BYTE*>(&out),
                                &cb));

    EXPECT_EQ(static_cast<DWORD>(REG_DWORD), type);
    EXPECT_EQ(sizeof(DWORD), cb);
    EXPECT_EQ(kDwordValue, out);
}

TEST(TestValueOps, StringRoundTripW)
{
    auto const name = L"mwin32_test_sz";
    wchar_t    data[] = L"hello world";
    DWORD const cb_in = wide_bytes_with_nul(data);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(HKEY_CURRENT_USER,
                              name,
                              0,
                              REG_SZ,
                              reinterpret_cast<BYTE const*>(data),
                              cb_in));

    DWORD   type = 0;
    wchar_t out[32]{};
    DWORD   cb = sizeof(out);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegQueryValueExW(HKEY_CURRENT_USER,
                                name,
                                nullptr,
                                &type,
                                reinterpret_cast<BYTE*>(out),
                                &cb));

    EXPECT_EQ(static_cast<DWORD>(REG_SZ), type);
    EXPECT_EQ(cb_in, cb);
    EXPECT_STREQ(data, out);
}

TEST(TestValueOps, BinaryRoundTripW)
{
    auto const                  name = L"mwin32_test_binary";
    std::array<BYTE, 5> const   data = {0x00, 0x11, 0x22, 0xFE, 0xFF};

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(HKEY_CURRENT_USER,
                              name,
                              0,
                              REG_BINARY,
                              data.data(),
                              static_cast<DWORD>(data.size())));

    DWORD               type = 0;
    std::array<BYTE, 5> out{};
    DWORD               cb = static_cast<DWORD>(out.size());

    ASSERT_EQ(ERROR_SUCCESS,
              mRegQueryValueExW(HKEY_CURRENT_USER,
                                name,
                                nullptr,
                                &type,
                                out.data(),
                                &cb));

    EXPECT_EQ(static_cast<DWORD>(REG_BINARY), type);
    EXPECT_EQ(data.size(), cb);
    EXPECT_EQ(data, out);
}

TEST(TestValueOps, MultiStringRoundTripW)
{
    auto const name = L"mwin32_test_multi_sz";

    // Two strings followed by the terminating empty string: "a\0bb\0\0".
    wchar_t const data[] = {L'a', L'\0', L'b', L'b', L'\0', L'\0'};
    DWORD const   cb_in  = static_cast<DWORD>(sizeof(data));

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(HKEY_CURRENT_USER,
                              name,
                              0,
                              REG_MULTI_SZ,
                              reinterpret_cast<BYTE const*>(data),
                              cb_in));

    DWORD   type = 0;
    wchar_t out[16]{};
    DWORD   cb = sizeof(out);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegQueryValueExW(HKEY_CURRENT_USER,
                                name,
                                nullptr,
                                &type,
                                reinterpret_cast<BYTE*>(out),
                                &cb));

    EXPECT_EQ(static_cast<DWORD>(REG_MULTI_SZ), type);
    EXPECT_EQ(cb_in, cb);
    EXPECT_EQ(0, std::memcmp(data, out, cb_in));
}

TEST(TestValueOps, SizeQueryWithNullBuffer)
{
    auto const  name = L"mwin32_test_size_query";
    DWORD const in   = kDwordValue;

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(HKEY_CURRENT_USER,
                              name,
                              0,
                              REG_DWORD,
                              reinterpret_cast<BYTE const*>(&in),
                              sizeof(in)));

    DWORD type = 0;
    DWORD cb   = 0;

    // lpData == nullptr is a size/type query: it must report the required size
    // and the type and succeed.
    ASSERT_EQ(ERROR_SUCCESS,
              mRegQueryValueExW(HKEY_CURRENT_USER, name, nullptr, &type, nullptr, &cb));

    EXPECT_EQ(static_cast<DWORD>(REG_DWORD), type);
    EXPECT_EQ(sizeof(DWORD), cb);
}

TEST(TestValueOps, BufferTooSmallReturnsMoreData)
{
    auto const name = L"mwin32_test_more_data";
    wchar_t    data[] = L"a longer string value";
    DWORD const cb_in = wide_bytes_with_nul(data);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(HKEY_CURRENT_USER,
                              name,
                              0,
                              REG_SZ,
                              reinterpret_cast<BYTE const*>(data),
                              cb_in));

    DWORD type = 0;
    BYTE  tiny[4]{};
    DWORD cb = sizeof(tiny);

    EXPECT_EQ(ERROR_MORE_DATA,
              mRegQueryValueExW(HKEY_CURRENT_USER, name, nullptr, &type, tiny, &cb));

    // On ERROR_MORE_DATA the required size must be reported.
    EXPECT_EQ(cb_in, cb);
}

TEST(TestValueOps, StringRoundTripA)
{
    auto const name = "mwin32_test_sz_a";
    char const data[] = "ansi value";
    DWORD const cb_in = static_cast<DWORD>(sizeof(data)); // includes NUL

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExA(HKEY_CURRENT_USER,
                              name,
                              0,
                              REG_SZ,
                              reinterpret_cast<BYTE const*>(data),
                              cb_in));

    DWORD type = 0;
    char  out[32]{};
    DWORD cb = sizeof(out);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegQueryValueExA(HKEY_CURRENT_USER,
                                name,
                                nullptr,
                                &type,
                                reinterpret_cast<BYTE*>(out),
                                &cb));

    EXPECT_EQ(static_cast<DWORD>(REG_SZ), type);
    EXPECT_EQ(cb_in, cb);
    EXPECT_STREQ(data, out);
}

TEST(TestValueOps, StringSetWQueryA)
{
    // A wide string stored via the *W setter must read back through the *A
    // query as the CP_ACP encoding of the same characters.
    auto const  wname = L"mwin32_test_cross";
    wchar_t     wdata[] = L"cross encoding";
    DWORD const cb_in   = wide_bytes_with_nul(wdata);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(HKEY_CURRENT_USER,
                              wname,
                              0,
                              REG_SZ,
                              reinterpret_cast<BYTE const*>(wdata),
                              cb_in));

    DWORD type = 0;
    char  out[32]{};
    DWORD cb = sizeof(out);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegQueryValueExA(HKEY_CURRENT_USER,
                                "mwin32_test_cross",
                                nullptr,
                                &type,
                                reinterpret_cast<BYTE*>(out),
                                &cb));

    EXPECT_EQ(static_cast<DWORD>(REG_SZ), type);
    EXPECT_STREQ("cross encoding", out);
    EXPECT_EQ(static_cast<DWORD>(sizeof("cross encoding")), cb);
}

TEST(TestValueOps, BinaryUnaffectedByAnsiVariant)
{
    // Non-string types must pass their bytes through the *A variants unchanged.
    auto const                name = "mwin32_test_binary_a";
    std::array<BYTE, 4> const data = {0xDE, 0xAD, 0xBE, 0xEF};

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExA(HKEY_CURRENT_USER,
                              name,
                              0,
                              REG_BINARY,
                              data.data(),
                              static_cast<DWORD>(data.size())));

    DWORD               type = 0;
    std::array<BYTE, 4> out{};
    DWORD               cb = static_cast<DWORD>(out.size());

    ASSERT_EQ(ERROR_SUCCESS,
              mRegQueryValueExA(HKEY_CURRENT_USER,
                                name,
                                nullptr,
                                &type,
                                out.data(),
                                &cb));

    EXPECT_EQ(static_cast<DWORD>(REG_BINARY), type);
    EXPECT_EQ(data.size(), cb);
    EXPECT_EQ(data, out);
}

TEST(TestValueOps, QueryMissingValueReturnsFileNotFound)
{
    DWORD type = 0;
    BYTE  buf[8]{};
    DWORD cb = sizeof(buf);

    EXPECT_EQ(ERROR_FILE_NOT_FOUND,
              mRegQueryValueExW(
                  HKEY_CURRENT_USER, L"mwin32_test_missing_value", nullptr, &type, buf, &cb));

    EXPECT_EQ(ERROR_FILE_NOT_FOUND,
              mRegQueryValueExA(
                  HKEY_CURRENT_USER, "mwin32_test_missing_value_a", nullptr, &type, buf, &cb));
}

TEST(TestValueOps, ReservedParameterRejected)
{
    DWORD const in = kDwordValue;

    EXPECT_EQ(ERROR_INVALID_PARAMETER,
              mRegSetValueExW(HKEY_CURRENT_USER,
                              L"mwin32_test_reserved",
                              1,
                              REG_DWORD,
                              reinterpret_cast<BYTE const*>(&in),
                              sizeof(in)));

    DWORD nonzero = 1;
    DWORD cb      = sizeof(in);
    EXPECT_EQ(ERROR_INVALID_PARAMETER,
              mRegQueryValueExW(HKEY_CURRENT_USER,
                                L"mwin32_test_reserved",
                                &nonzero,
                                nullptr,
                                nullptr,
                                &cb));
}

namespace
{
    // Upper bound on any value name / data used by the enumeration tests.
    constexpr DWORD kEnumBufferChars = 64;
}

TEST(TestValueOps, EnumerateValuesW)
{
    HKEY subkey = create_subkey(L"mwin32_enum_w");
    ASSERT_NE(subkey, nullptr);

    DWORD const              dword_in = 0x12345678ul;
    wchar_t const            sz_in[]  = L"enum-string";
    std::array<BYTE, 3> const bin_in  = {0x01, 0x02, 0x03};

    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(subkey,
                              L"alpha",
                              0,
                              REG_DWORD,
                              reinterpret_cast<BYTE const*>(&dword_in),
                              sizeof(dword_in)));
    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(subkey,
                              L"beta",
                              0,
                              REG_SZ,
                              reinterpret_cast<BYTE const*>(sz_in),
                              wide_bytes_with_nul(sz_in)));
    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(subkey,
                              L"gamma",
                              0,
                              REG_BINARY,
                              bin_in.data(),
                              static_cast<DWORD>(bin_in.size())));

    // Enumeration order is not part of the contract, so collect by name.
    std::map<std::wstring, std::pair<DWORD, std::vector<BYTE>>> found;
    for (DWORD index = 0;; ++index)
    {
        wchar_t name[kEnumBufferChars];
        BYTE    data[kEnumBufferChars];
        DWORD   cchName = kEnumBufferChars;
        DWORD   type    = 0;
        DWORD   cbData  = sizeof(data);

        LSTATUS const rc =
            mRegEnumValueW(subkey, index, name, &cchName, nullptr, &type, data, &cbData);
        if (rc == ERROR_NO_MORE_ITEMS)
            break;
        ASSERT_EQ(ERROR_SUCCESS, rc);

        found.emplace(std::wstring(name, cchName),
                      std::make_pair(type, std::vector<BYTE>(data, data + cbData)));
    }

    ASSERT_EQ(found.size(), 3u);

    ASSERT_TRUE(found.contains(L"alpha"));
    EXPECT_EQ(found[L"alpha"].first, static_cast<DWORD>(REG_DWORD));
    ASSERT_EQ(found[L"alpha"].second.size(), sizeof(DWORD));
    EXPECT_EQ(0, std::memcmp(found[L"alpha"].second.data(), &dword_in, sizeof(dword_in)));

    ASSERT_TRUE(found.contains(L"beta"));
    EXPECT_EQ(found[L"beta"].first, static_cast<DWORD>(REG_SZ));
    ASSERT_EQ(found[L"beta"].second.size(), wide_bytes_with_nul(sz_in));
    EXPECT_EQ(0, std::memcmp(found[L"beta"].second.data(), sz_in, wide_bytes_with_nul(sz_in)));

    ASSERT_TRUE(found.contains(L"gamma"));
    EXPECT_EQ(found[L"gamma"].first, static_cast<DWORD>(REG_BINARY));
    ASSERT_EQ(found[L"gamma"].second.size(), bin_in.size());
    EXPECT_EQ(0, std::memcmp(found[L"gamma"].second.data(), bin_in.data(), bin_in.size()));

    mRegCloseKey(subkey);
}

TEST(TestValueOps, EnumValuePastEndReturnsNoMoreItemsW)
{
    HKEY subkey = create_subkey(L"mwin32_enum_end");
    ASSERT_NE(subkey, nullptr);

    DWORD const in = kDwordValue;
    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(
                  subkey, L"only", 0, REG_DWORD, reinterpret_cast<BYTE const*>(&in), sizeof(in)));

    wchar_t name[kEnumBufferChars];
    BYTE    data[kEnumBufferChars];
    DWORD   type = 0;

    // Index 0 yields the one value.
    DWORD cchName = kEnumBufferChars;
    DWORD cbData  = sizeof(data);
    ASSERT_EQ(ERROR_SUCCESS,
              mRegEnumValueW(subkey, 0, name, &cchName, nullptr, &type, data, &cbData));

    // Index 1 is past the end.
    cchName = kEnumBufferChars;
    cbData  = sizeof(data);
    EXPECT_EQ(ERROR_NO_MORE_ITEMS,
              mRegEnumValueW(subkey, 1, name, &cchName, nullptr, &type, data, &cbData));

    mRegCloseKey(subkey);
}

TEST(TestValueOps, EnumValueNameBufferTooSmallW)
{
    HKEY subkey = create_subkey(L"mwin32_enum_name_small");
    ASSERT_NE(subkey, nullptr);

    DWORD const in = kDwordValue;
    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(subkey,
                              L"longvaluename",
                              0,
                              REG_DWORD,
                              reinterpret_cast<BYTE const*>(&in),
                              sizeof(in)));

    // A name buffer too small to hold the name plus its terminating NUL.
    wchar_t name[4];
    BYTE    data[kEnumBufferChars];
    DWORD   cchName = static_cast<DWORD>(std::size(name));
    DWORD   type    = 0;
    DWORD   cbData  = sizeof(data);

    EXPECT_EQ(ERROR_MORE_DATA,
              mRegEnumValueW(subkey, 0, name, &cchName, nullptr, &type, data, &cbData));

    mRegCloseKey(subkey);
}

TEST(TestValueOps, EnumValueDataBufferTooSmallW)
{
    HKEY subkey = create_subkey(L"mwin32_enum_data_small");
    ASSERT_NE(subkey, nullptr);

    wchar_t const sz_in[] = L"a sufficiently long value";
    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(subkey,
                              L"val",
                              0,
                              REG_SZ,
                              reinterpret_cast<BYTE const*>(sz_in),
                              wide_bytes_with_nul(sz_in)));

    // The name fits but the data buffer does not.
    wchar_t name[kEnumBufferChars];
    BYTE    tiny[4];
    DWORD   cchName = kEnumBufferChars;
    DWORD   type    = 0;
    DWORD   cbData  = sizeof(tiny);

    EXPECT_EQ(ERROR_MORE_DATA,
              mRegEnumValueW(subkey, 0, name, &cchName, nullptr, &type, tiny, &cbData));

    // The name was still produced and the required data size reported.
    EXPECT_STREQ(name, L"val");
    EXPECT_EQ(cchName, 3u);
    EXPECT_EQ(static_cast<DWORD>(REG_SZ), type);
    EXPECT_EQ(cbData, wide_bytes_with_nul(sz_in));

    mRegCloseKey(subkey);
}

TEST(TestValueOps, EnumerateValueA)
{
    HKEY subkey = create_subkey(L"mwin32_enum_a");
    ASSERT_NE(subkey, nullptr);

    wchar_t const sz_in[] = L"ansi-enum";
    ASSERT_EQ(ERROR_SUCCESS,
              mRegSetValueExW(subkey,
                              L"label",
                              0,
                              REG_SZ,
                              reinterpret_cast<BYTE const*>(sz_in),
                              wide_bytes_with_nul(sz_in)));

    char  name[kEnumBufferChars]{};
    char  data[kEnumBufferChars]{};
    DWORD cchName = kEnumBufferChars;
    DWORD type    = 0;
    DWORD cbData  = sizeof(data);

    ASSERT_EQ(ERROR_SUCCESS,
              mRegEnumValueA(subkey,
                             0,
                             name,
                             &cchName,
                             nullptr,
                             &type,
                             reinterpret_cast<BYTE*>(data),
                             &cbData));

    // The name comes back in CP_ACP, and the string data is converted from its
    // stored UTF-16 form to CP_ACP (matching mRegQueryValueExA).
    EXPECT_STREQ(name, "label");
    EXPECT_EQ(cchName, 5u);
    EXPECT_EQ(static_cast<DWORD>(REG_SZ), type);
    EXPECT_STREQ(data, "ansi-enum");
    EXPECT_EQ(cbData, static_cast<DWORD>(sizeof("ansi-enum")));

    mRegCloseKey(subkey);
}

TEST(TestValueOps, EnumValueReservedParameterRejectedW)
{
    HKEY subkey = create_subkey(L"mwin32_enum_reserved");
    ASSERT_NE(subkey, nullptr);

    wchar_t name[kEnumBufferChars];
    DWORD   cchName  = kEnumBufferChars;
    DWORD   reserved = 0;

    EXPECT_EQ(ERROR_INVALID_PARAMETER,
              mRegEnumValueW(subkey, 0, name, &cchName, &reserved, nullptr, nullptr, nullptr));

    mRegCloseKey(subkey);
}
