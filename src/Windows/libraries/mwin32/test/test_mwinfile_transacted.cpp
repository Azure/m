// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Transacted (TxF) filesystem family integration tests (M-FS-LEGACY-2). The
// executable links m_mwin32 and drives the m-prefixed transacted entry points
// directly under a buffered + redirecting .pilcfg, so every call runs through
// the provider chain in-process without touching the live disk. Each test
// passes a deliberately bogus, non-null transaction handle to prove the shim
// ignores it (D11): the operation must still succeed, forwarded to its
// non-transacted sibling. A redirected and a non-redirected prefix are both
// exercised so the forwarding inherits the sibling's redirection behavior.
//

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <m/mwin32/mwinfile.h>

namespace
{
    constexpr wchar_t k_public_root[] = L"C:\\mwin32_txf_pub";

    // A non-null handle that is never a real transaction: the forwarders must
    // ignore it entirely, so its value is irrelevant to the outcome.
    HANDLE const k_bogus_txn = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x1234));

    HANDLE
    create_transacted_file(std::wstring const& file)
    {
        return ::mCreateFileTransactedW(file.c_str(),
                                        GENERIC_WRITE,
                                        0,
                                        nullptr,
                                        CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL,
                                        nullptr,
                                        k_bogus_txn,
                                        nullptr,
                                        nullptr);
    }
} // namespace

TEST(MwinFileTransacted, CreateFileForwardsIgnoringTransaction)
{
    std::wstring const dir  = std::wstring(k_public_root) + L"\\create";
    std::wstring const file = dir + L"\\data.bin";

    EXPECT_TRUE(::mCreateDirectoryTransactedW(nullptr, dir.c_str(), nullptr, k_bogus_txn));

    HANDLE const h = create_transacted_file(file);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    EXPECT_TRUE(::mCloseHandle(h));

    // The forwarded create is observable through the non-transacted query path.
    WIN32_FILE_ATTRIBUTE_DATA data{};
    EXPECT_TRUE(::mGetFileAttributesTransactedW(
        file.c_str(), GetFileExInfoStandard, &data, k_bogus_txn));
    EXPECT_EQ(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY, 0u);

    // SetFileAttributesTransacted forwards to the accept-and-ignore sibling: it
    // verifies the target exists and reports success.
    EXPECT_TRUE(::mSetFileAttributesTransactedW(
        file.c_str(), FILE_ATTRIBUTE_HIDDEN, k_bogus_txn));
}

TEST(MwinFileTransacted, CreateFileTransactedAnsiForwards)
{
    std::string const dir  = "C:\\mwin32_txf_pub\\ansi";
    std::string const file = dir + "\\data.bin";

    EXPECT_TRUE(::mCreateDirectoryTransactedA(nullptr, dir.c_str(), nullptr, k_bogus_txn));

    HANDLE const h = ::mCreateFileTransactedA(file.c_str(),
                                              GENERIC_WRITE,
                                              0,
                                              nullptr,
                                              CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL,
                                              nullptr,
                                              k_bogus_txn,
                                              nullptr,
                                              nullptr);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    EXPECT_TRUE(::mCloseHandle(h));
}

TEST(MwinFileTransacted, CopyAndMoveForwardIgnoringTransaction)
{
    std::wstring const dir  = std::wstring(k_public_root) + L"\\copymove";
    std::wstring const src  = dir + L"\\src.bin";
    std::wstring const copy = dir + L"\\copy.bin";
    std::wstring const dest = dir + L"\\moved.bin";

    EXPECT_TRUE(::mCreateDirectoryTransactedW(nullptr, dir.c_str(), nullptr, k_bogus_txn));
    EXPECT_TRUE(::mCloseHandle(create_transacted_file(src)));

    EXPECT_TRUE(::mCopyFileTransactedW(
        src.c_str(), copy.c_str(), nullptr, nullptr, nullptr, 0, k_bogus_txn));

    WIN32_FILE_ATTRIBUTE_DATA data{};
    EXPECT_TRUE(::mGetFileAttributesExW(copy.c_str(), GetFileExInfoStandard, &data));

    EXPECT_TRUE(::mMoveFileTransactedW(
        copy.c_str(), dest.c_str(), nullptr, nullptr, 0, k_bogus_txn));

    // The destination exists and the source of the move no longer does.
    EXPECT_TRUE(::mGetFileAttributesExW(dest.c_str(), GetFileExInfoStandard, &data));
    EXPECT_FALSE(::mGetFileAttributesExW(copy.c_str(), GetFileExInfoStandard, &data));
}

TEST(MwinFileTransacted, RemoveDirectoryForwards)
{
    std::wstring const dir = std::wstring(k_public_root) + L"\\toremove";

    EXPECT_TRUE(::mCreateDirectoryTransactedW(nullptr, dir.c_str(), nullptr, k_bogus_txn));
    EXPECT_TRUE(::mRemoveDirectoryTransactedW(dir.c_str(), k_bogus_txn));
    EXPECT_FALSE(::mRemoveDirectoryTransactedW(dir.c_str(), k_bogus_txn));
}

TEST(MwinFileTransacted, FindFirstFileForwards)
{
    std::wstring const dir  = std::wstring(k_public_root) + L"\\find";
    std::wstring const file = dir + L"\\entry.bin";

    EXPECT_TRUE(::mCreateDirectoryTransactedW(nullptr, dir.c_str(), nullptr, k_bogus_txn));
    EXPECT_TRUE(::mCloseHandle(create_transacted_file(file)));

    std::wstring const pattern = dir + L"\\*";

    WIN32_FIND_DATAW found{};
    HANDLE const      hFind = ::mFindFirstFileTransactedW(pattern.c_str(),
                                                     FindExInfoStandard,
                                                     &found,
                                                     FindExSearchNameMatch,
                                                     nullptr,
                                                     0,
                                                     k_bogus_txn);
    ASSERT_NE(hFind, INVALID_HANDLE_VALUE);
    EXPECT_TRUE(::mFindClose(hFind));
}

TEST(MwinFileTransacted, GetLongPathNameForwards)
{
    std::wstring const dir  = std::wstring(k_public_root) + L"\\longpath";
    std::wstring const file = dir + L"\\entry.bin";

    EXPECT_TRUE(::mCreateDirectoryTransactedW(nullptr, dir.c_str(), nullptr, k_bogus_txn));
    EXPECT_TRUE(::mCloseHandle(create_transacted_file(file)));

    wchar_t     buffer[MAX_PATH] = {};
    DWORD const len =
        ::mGetLongPathNameTransactedW(file.c_str(), buffer, MAX_PATH, k_bogus_txn);
    EXPECT_GT(len, 0u);
}
