// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Byte-content & positioning integration tests (M-FS-CONTENT-4). The executable
// links m_mwin32 and calls the m-prefixed filesystem entry points directly. Its
// .pilcfg sidecar is authored at runtime by main() (before any shim call) so it
// selects a *redirecting-over-direct* stack (buffer_updates = false): a write
// therefore lands as real bytes in a sibling private backing directory next to
// the test executable, and a later open reads those same real bytes back. This
// is the only configuration that exercises the whole-file content model end to
// end -- the buffered overlay models no writable content -- so the round-trip
// proves CreateFile -> WriteFile -> CloseHandle -> CreateFile -> ReadFile, and a
// mid-file overwrite proves the documented deferred-content error (D16 non-goal).
//
// Because the backing is real disk, main() creates the private directory under
// the executable's own directory (guaranteeing a writable, same-drive location)
// and removes it on exit.
//

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

#include <m/mwin32/mwinfile.h>

#include <winioctl.h>

namespace
{
    //
    // The public root the tests open. Set by main() before any test runs. A
    // file created under this prefix is redirected (per the runtime-authored
    // .pilcfg) to a sibling private backing directory on the real disk.
    //
    std::wstring g_public_root;

    //
    // The absolute private backing directory the public root maps to. Retained
    // so main() can remove it (and its contents) on exit.
    //
    std::filesystem::path g_private_dir;

    //
    // Open a handle under the public root, creating the public directory first
    // (its redirected private parent must exist before the direct provider can
    // materialize a file inside it).
    //
    HANDLE
    open_under_public_root(std::wstring const& leaf, DWORD access, DWORD disposition)
    {
        ::mCreateDirectoryW(g_public_root.c_str(), nullptr);
        std::wstring const path = g_public_root + L"\\" + leaf;
        return ::mCreateFileW(
            path.c_str(), access, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
} // namespace

TEST(MwinFileContent, WholeFileWriteThenReadRoundTrips)
{
    HANDLE const hw = open_under_public_root(L"roundtrip.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);

    std::array<unsigned char, 8> const payload = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    DWORD                              written = 0;
    ASSERT_TRUE(::mWriteFile(hw, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr));
    EXPECT_EQ(written, static_cast<DWORD>(payload.size()));
    ASSERT_TRUE(::mCloseHandle(hw));

    HANDLE const hr = open_under_public_root(L"roundtrip.bin", GENERIC_READ, OPEN_EXISTING);
    ASSERT_NE(hr, INVALID_HANDLE_VALUE);

    std::array<unsigned char, 8> readback{};
    DWORD                        read = 0;
    ASSERT_TRUE(::mReadFile(hr, readback.data(), static_cast<DWORD>(readback.size()), &read, nullptr));
    EXPECT_EQ(read, static_cast<DWORD>(payload.size()));
    EXPECT_EQ(readback, payload);
    EXPECT_TRUE(::mCloseHandle(hr));
}

TEST(MwinFileContent, EmptyWholeFileRoundTrips)
{
    HANDLE const hw = open_under_public_root(L"empty.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);

    DWORD written = 0;
    ASSERT_TRUE(::mWriteFile(hw, nullptr, 0, &written, nullptr));
    EXPECT_EQ(written, 0u);
    ASSERT_TRUE(::mCloseHandle(hw));

    HANDLE const hr = open_under_public_root(L"empty.bin", GENERIC_READ, OPEN_EXISTING);
    ASSERT_NE(hr, INVALID_HANDLE_VALUE);

    unsigned char byte = 0;
    DWORD         read = 1;
    EXPECT_TRUE(::mReadFile(hr, &byte, 1, &read, nullptr));
    EXPECT_EQ(read, 0u);
    EXPECT_TRUE(::mCloseHandle(hr));
}

TEST(MwinFileContent, WholeFileWriteReplacesPriorContents)
{
    HANDLE const h1 = open_under_public_root(L"replace.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(h1, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 6> const large = {1, 2, 3, 4, 5, 6};
    DWORD                              w1    = 0;
    ASSERT_TRUE(::mWriteFile(h1, large.data(), static_cast<DWORD>(large.size()), &w1, nullptr));
    ASSERT_TRUE(::mCloseHandle(h1));

    HANDLE const h2 = open_under_public_root(L"replace.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(h2, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 2> const small = {0xEE, 0xFF};
    DWORD                              w2    = 0;
    ASSERT_TRUE(::mWriteFile(h2, small.data(), static_cast<DWORD>(small.size()), &w2, nullptr));
    ASSERT_TRUE(::mCloseHandle(h2));

    HANDLE const hr = open_under_public_root(L"replace.bin", GENERIC_READ, OPEN_EXISTING);
    ASSERT_NE(hr, INVALID_HANDLE_VALUE);
    LARGE_INTEGER size{};
    ASSERT_TRUE(::mGetFileSizeEx(hr, &size));
    EXPECT_EQ(size.QuadPart, static_cast<LONGLONG>(small.size()));
    EXPECT_TRUE(::mCloseHandle(hr));
}

TEST(MwinFileContent, SequentialReadAdvancesPosition)
{
    HANDLE const hw = open_under_public_root(L"seq.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 8> const payload = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    DWORD                              w       = 0;
    ASSERT_TRUE(::mWriteFile(hw, payload.data(), static_cast<DWORD>(payload.size()), &w, nullptr));
    ASSERT_TRUE(::mCloseHandle(hw));

    HANDLE const hr = open_under_public_root(L"seq.bin", GENERIC_READ, OPEN_EXISTING);
    ASSERT_NE(hr, INVALID_HANDLE_VALUE);

    std::array<unsigned char, 4> first{};
    DWORD                        r1 = 0;
    ASSERT_TRUE(::mReadFile(hr, first.data(), 4, &r1, nullptr));
    EXPECT_EQ(r1, 4u);
    EXPECT_EQ((std::array<unsigned char, 4>{'A', 'B', 'C', 'D'}), first);

    std::array<unsigned char, 4> second{};
    DWORD                        r2 = 0;
    ASSERT_TRUE(::mReadFile(hr, second.data(), 4, &r2, nullptr));
    EXPECT_EQ(r2, 4u);
    EXPECT_EQ((std::array<unsigned char, 4>{'E', 'F', 'G', 'H'}), second);

    EXPECT_TRUE(::mCloseHandle(hr));
}

TEST(MwinFileContent, SetFilePointerSeeksWithinFile)
{
    HANDLE const hw = open_under_public_root(L"seek.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 8> const payload = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    DWORD                              w       = 0;
    ASSERT_TRUE(::mWriteFile(hw, payload.data(), static_cast<DWORD>(payload.size()), &w, nullptr));
    ASSERT_TRUE(::mCloseHandle(hw));

    HANDLE const hr = open_under_public_root(L"seek.bin", GENERIC_READ, OPEN_EXISTING);
    ASSERT_NE(hr, INVALID_HANDLE_VALUE);

    // Seek to offset 5 from the start, then read the trailing 3 bytes.
    EXPECT_EQ(::mSetFilePointer(hr, 5, nullptr, FILE_BEGIN), 5u);
    std::array<unsigned char, 3> tail{};
    DWORD                        r = 0;
    ASSERT_TRUE(::mReadFile(hr, tail.data(), 3, &r, nullptr));
    EXPECT_EQ(r, 3u);
    EXPECT_EQ((std::array<unsigned char, 3>{'f', 'g', 'h'}), tail);

    // Seek to end via mSetFilePointerEx and confirm the reported position.
    LARGE_INTEGER const zero{};
    LARGE_INTEGER       end{};
    ASSERT_TRUE(::mSetFilePointerEx(hr, zero, &end, FILE_END));
    EXPECT_EQ(end.QuadPart, static_cast<LONGLONG>(payload.size()));

    EXPECT_TRUE(::mCloseHandle(hr));
}

TEST(MwinFileContent, SetFilePointerNegativeSeekRejected)
{
    HANDLE const hr = open_under_public_root(L"neg.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hr, INVALID_HANDLE_VALUE);

    ::SetLastError(NO_ERROR);
    EXPECT_EQ(::mSetFilePointer(hr, -1, nullptr, FILE_BEGIN), INVALID_SET_FILE_POINTER);
    EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_NEGATIVE_SEEK));

    EXPECT_TRUE(::mCloseHandle(hr));
}

TEST(MwinFileContent, SetEndOfFileNoopAtCurrentExtent)
{
    HANDLE const hw = open_under_public_root(L"eofnoop.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 4> const payload = {1, 2, 3, 4};
    DWORD                              w       = 0;
    ASSERT_TRUE(::mWriteFile(hw, payload.data(), static_cast<DWORD>(payload.size()), &w, nullptr));

    // The position is at end-of-file after the write, so SetEndOfFile is a no-op.
    EXPECT_TRUE(::mSetEndOfFile(hw));

    LARGE_INTEGER size{};
    ASSERT_TRUE(::mGetFileSizeEx(hw, &size));
    EXPECT_EQ(size.QuadPart, static_cast<LONGLONG>(payload.size()));
    EXPECT_TRUE(::mCloseHandle(hw));
}

TEST(MwinFileContent, SetEndOfFileTruncatesToEmpty)
{
    HANDLE const hw = open_under_public_root(L"trunc.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 4> const payload = {9, 8, 7, 6};
    DWORD                              w       = 0;
    ASSERT_TRUE(::mWriteFile(hw, payload.data(), static_cast<DWORD>(payload.size()), &w, nullptr));

    // Rewind to the start and truncate: position 0 is the degenerate whole-file
    // replacement, which the content model honours.
    EXPECT_EQ(::mSetFilePointer(hw, 0, nullptr, FILE_BEGIN), 0u);
    EXPECT_TRUE(::mSetEndOfFile(hw));

    LARGE_INTEGER size{};
    ASSERT_TRUE(::mGetFileSizeEx(hw, &size));
    EXPECT_EQ(size.QuadPart, 0);
    EXPECT_TRUE(::mCloseHandle(hw));
}

TEST(MwinFileContent, SetEndOfFilePartialResizeUnsupported)
{
    HANDLE const hw = open_under_public_root(L"eofpart.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 8> const payload = {1, 2, 3, 4, 5, 6, 7, 8};
    DWORD                              w       = 0;
    ASSERT_TRUE(::mWriteFile(hw, payload.data(), static_cast<DWORD>(payload.size()), &w, nullptr));

    // Position in the middle: a resize that is neither a no-op nor a truncation
    // to empty is a partial size mutation the content model rejects (D16).
    EXPECT_EQ(::mSetFilePointer(hw, 3, nullptr, FILE_BEGIN), 3u);
    ::SetLastError(NO_ERROR);
    EXPECT_FALSE(::mSetEndOfFile(hw));
    EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_NOT_SUPPORTED));
    EXPECT_TRUE(::mCloseHandle(hw));
}

TEST(MwinFileContent, SetFileValidDataUnsupported)
{
    HANDLE const hw = open_under_public_root(L"valid.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);

    ::SetLastError(NO_ERROR);
    EXPECT_FALSE(::mSetFileValidData(hw, 16));
    EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_NOT_SUPPORTED));
    EXPECT_TRUE(::mCloseHandle(hw));
}

TEST(MwinFileContent, PartialOverwriteUnsupported)
{
    HANDLE const hw = open_under_public_root(L"partial.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 8> const seed = {0, 1, 2, 3, 4, 5, 6, 7};
    DWORD                              w    = 0;
    ASSERT_TRUE(::mWriteFile(hw, seed.data(), static_cast<DWORD>(seed.size()), &w, nullptr));
    ASSERT_TRUE(::mCloseHandle(hw));

    HANDLE const h = open_under_public_root(L"partial.bin", GENERIC_WRITE, OPEN_EXISTING);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    // A non-zero OVERLAPPED offset names a mid-file overwrite, which the
    // whole-file content model does not express.
    OVERLAPPED ov{};
    ov.Offset = 4;
    std::array<unsigned char, 2> const patch = {0xAA, 0xBB};
    DWORD                              w2    = 0;
    ::SetLastError(NO_ERROR);
    EXPECT_FALSE(::mWriteFile(h, patch.data(), static_cast<DWORD>(patch.size()), &w2, &ov));
    EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_NOT_SUPPORTED));
    EXPECT_TRUE(::mCloseHandle(h));
}

TEST(MwinFileContent, DuplicateHandleSharesSequentialPosition)
{
    HANDLE const hw = open_under_public_root(L"dup.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(hw, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 8> const payload = {'p', 'q', 'r', 's', 't', 'u', 'v', 'w'};
    DWORD                              w       = 0;
    ASSERT_TRUE(::mWriteFile(hw, payload.data(), static_cast<DWORD>(payload.size()), &w, nullptr));
    ASSERT_TRUE(::mCloseHandle(hw));

    HANDLE const hr = open_under_public_root(L"dup.bin", GENERIC_READ, OPEN_EXISTING);
    ASSERT_NE(hr, INVALID_HANDLE_VALUE);

    std::array<unsigned char, 4> head{};
    DWORD                        r1 = 0;
    ASSERT_TRUE(::mReadFile(hr, head.data(), 4, &r1, nullptr));
    EXPECT_EQ(r1, 4u);

    HANDLE dup = nullptr;
    ASSERT_TRUE(::mDuplicateHandle(::GetCurrentProcess(),
                                   hr,
                                   ::GetCurrentProcess(),
                                   &dup,
                                   0,
                                   FALSE,
                                   DUPLICATE_SAME_ACCESS));
    ASSERT_NE(dup, nullptr);

    // The duplicate shares the original's sequential position, so it reads the
    // trailing bytes the original left off at -- not a fresh read from offset 0.
    std::array<unsigned char, 4> tail{};
    DWORD                        r2 = 0;
    ASSERT_TRUE(::mReadFile(dup, tail.data(), 4, &r2, nullptr));
    EXPECT_EQ(r2, 4u);
    EXPECT_EQ((std::array<unsigned char, 4>{'t', 'u', 'v', 'w'}), tail);

    EXPECT_TRUE(::mCloseHandle(dup));
    EXPECT_TRUE(::mCloseHandle(hr));
}

TEST(MwinFileContent, FlushAndLockSucceed)
{
    HANDLE const h = open_under_public_root(L"flush.bin", GENERIC_WRITE, CREATE_ALWAYS);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    std::array<unsigned char, 4> const payload = {1, 2, 3, 4};
    DWORD                              w       = 0;
    ASSERT_TRUE(::mWriteFile(h, payload.data(), static_cast<DWORD>(payload.size()), &w, nullptr));

    EXPECT_TRUE(::mFlushFileBuffers(h));
    EXPECT_TRUE(::mLockFile(h, 0, 0, 4, 0));
    EXPECT_TRUE(::mUnlockFile(h, 0, 0, 4, 0));
    EXPECT_TRUE(::mCloseHandle(h));
}

TEST(MwinFileContent, DeviceIoControlUnsupported)
{
    HANDLE const h = open_under_public_root(L"ioctl.bin", GENERIC_READ, CREATE_ALWAYS);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    DWORD returned = 0;
    ::SetLastError(NO_ERROR);
    EXPECT_FALSE(::mDeviceIoControl(
        h, FSCTL_GET_COMPRESSION, nullptr, 0, nullptr, 0, &returned, nullptr));
    EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_NOT_SUPPORTED));
    EXPECT_TRUE(::mCloseHandle(h));
}

TEST(MwinFileContent, AsynchronousReadFormUnsupported)
{
    HANDLE const h = open_under_public_root(L"async.bin", GENERIC_READ, CREATE_ALWAYS);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    OVERLAPPED                   ov{};
    std::array<unsigned char, 4> buffer{};
    ::SetLastError(NO_ERROR);
    EXPECT_FALSE(::mReadFileEx(h, buffer.data(), 4, &ov, nullptr));
    EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_NOT_SUPPORTED));
    EXPECT_TRUE(::mCloseHandle(h));
}

namespace
{
    //
    // Append `value`'s characters to `out` as UTF-8, doubling each backslash so
    // the result is a valid JSON string body. The redirection prefixes are ASCII
    // filesystem paths, so a direct narrowing of each code unit is exact.
    //
    void
    append_json_escaped(std::string& out, std::wstring const& value)
    {
        for (wchar_t const c: value)
        {
            if (c == L'\\')
                out += "\\\\";
            else
                out += static_cast<char>(c);
        }
    }
} // namespace

int
main(int argc, char** argv)
{
    // Locate the test executable's directory; the backing lives beside it so the
    // redirected real-disk writes target a writable, same-drive location.
    std::wstring module_path(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD const written =
            ::GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
        if (written == 0)
            return 1;
        if (written < module_path.size())
        {
            module_path.resize(written);
            break;
        }
        module_path.resize(module_path.size() * 2);
    }

    std::filesystem::path const exe_path(module_path);
    std::filesystem::path const exe_dir = exe_path.parent_path();

    std::filesystem::path const public_dir  = exe_dir / L"fscontent_pub";
    std::filesystem::path const private_dir = exe_dir / L"fscontent_priv";
    g_public_root = public_dir.wstring();
    g_private_dir = private_dir;

    // The redirector matches a drive-relative prefix (the drive root is absorbed
    // by the provider's open_root), so strip the leading "<drive>:\" from each
    // absolute directory to form the from/to prefixes.
    auto drive_relative = [](std::filesystem::path const& p) -> std::wstring {
        std::wstring const native = p.wstring();
        std::size_t const  colon  = native.find(L':');
        std::size_t        start  = (colon == std::wstring::npos) ? 0 : colon + 1;
        while (start < native.size() && (native[start] == L'\\' || native[start] == L'/'))
            ++start;
        return native.substr(start);
    };

    std::wstring const from_prefix = drive_relative(public_dir);
    std::wstring const to_prefix   = drive_relative(private_dir);

    std::string json = "{\"buffer_updates\": false, \"redirections\": [{\"from\": \"";
    append_json_escaped(json, from_prefix);
    json += "\", \"to\": \"";
    append_json_escaped(json, to_prefix);
    json += "\"}]}\n";

    std::filesystem::path const sidecar = std::filesystem::path(module_path + L".pilcfg");
    {
        std::ofstream out(sidecar, std::ios::binary | std::ios::trunc);
        if (!out)
            return 1;
        out.write(json.data(), static_cast<std::streamsize>(json.size()));
    }

    // Start from a clean backing directory so prior runs never leak state.
    std::error_code ec;
    std::filesystem::remove_all(private_dir, ec);

    ::testing::InitGoogleTest(&argc, argv);
    int const result = RUN_ALL_TESTS();

    std::filesystem::remove_all(private_dir, ec);

    return result;
}
