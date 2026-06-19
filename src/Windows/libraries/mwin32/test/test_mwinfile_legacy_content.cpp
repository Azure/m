// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Dusty-deck legacy content integration tests (M-FS-LEGACY-4). The executable
// links m_mwin32 and drives the 16-bit-era byte primitives (_lopen / _lcreat /
// _lread / _lwrite / _hread / _hwrite / _llseek / _lclose) and the LZ
// compress / expand family (LZOpenFile / LZRead / LZSeek / LZClose / LZCopy /
// LZInit / GetExpandedName) directly. Like the M-FS-CONTENT suite these need
// *writable* content, which the buffered overlay does not model, so the .pilcfg
// is authored at runtime by main() to select a redirecting-over-direct stack
// (buffer_updates = false): legacy writes land as real bytes in a private
// backing directory beside the executable, and a later legacy open reads those
// same bytes back. This proves the legacy HFILE translates exactly like a
// mCreateFile handle (D11) and that the LZ family is a passthrough (no
// decompression, D16).
//

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <m/mwin32/mwinfile.h>

namespace
{
    //
    // The public root the tests open. Set by main() before any test runs. A
    // file created under this prefix is redirected (per the runtime-authored
    // .pilcfg) to a sibling private backing directory on the real disk.
    //
    std::string g_public_root;

    //
    // The absolute private backing directory the public root maps to. Retained
    // so main() can remove it (and its contents) on exit.
    //
    std::filesystem::path g_private_dir;

    //
    // Compose a path under the public root, creating the public directory first
    // (its redirected private parent must exist before the direct provider can
    // materialize a file inside it).
    //
    std::string
    public_path(std::string const& leaf)
    {
        ::mCreateDirectoryA(g_public_root.c_str(), nullptr);
        return g_public_root + "\\" + leaf;
    }
} // namespace

TEST(MwinFileLegacyContent, OpenFileWriteReadRoundTrips)
{
    std::string const                   file    = public_path("roundtrip.bin");
    std::array<unsigned char, 6> const  payload = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};

    // OpenFile (OF_CREATE) -> _lwrite -> _lclose authors the whole-file content.
    OFSTRUCT    ofs{};
    HFILE const hw = ::mOpenFile(file.c_str(), &ofs, OF_CREATE | OF_READWRITE);
    ASSERT_NE(hw, HFILE_ERROR);
    EXPECT_EQ(::m_lwrite(hw, reinterpret_cast<LPCCH>(payload.data()),
                         static_cast<UINT>(payload.size())),
              static_cast<UINT>(payload.size()));
    EXPECT_EQ(::m_lclose(hw), 0);

    // OpenFile (OF_READ) -> _lread -> _lclose reads the same bytes back.
    OFSTRUCT    reopen{};
    HFILE const hr = ::mOpenFile(file.c_str(), &reopen, OF_READ);
    ASSERT_NE(hr, HFILE_ERROR);

    std::array<unsigned char, 6> readback{};
    EXPECT_EQ(::m_lread(hr, readback.data(), static_cast<UINT>(readback.size())),
              static_cast<UINT>(payload.size()));
    EXPECT_EQ(readback, payload);
    EXPECT_EQ(::m_lclose(hr), 0);
}

TEST(MwinFileLegacyContent, HReadHWriteRoundTrips)
{
    std::string const                  file    = public_path("huge.bin");
    std::array<unsigned char, 5> const payload = {0x11, 0x22, 0x33, 0x44, 0x55};

    HFILE const hw = ::m_lcreat(file.c_str(), 0);
    ASSERT_NE(hw, HFILE_ERROR);
    EXPECT_EQ(::m_hwrite(hw, reinterpret_cast<LPCCH>(payload.data()),
                         static_cast<LONG>(payload.size())),
              static_cast<LONG>(payload.size()));
    EXPECT_EQ(::m_lclose(hw), 0);

    HFILE const hr = ::m_lopen(file.c_str(), OF_READ);
    ASSERT_NE(hr, HFILE_ERROR);

    std::array<unsigned char, 5> readback{};
    EXPECT_EQ(::m_hread(hr, readback.data(), static_cast<LONG>(readback.size())),
              static_cast<LONG>(payload.size()));
    EXPECT_EQ(readback, payload);
    EXPECT_EQ(::m_lclose(hr), 0);
}

TEST(MwinFileLegacyContent, LLSeekRepositionsSequentialRead)
{
    std::string const                  file    = public_path("seek.bin");
    std::array<unsigned char, 8> const payload = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    HFILE const hw = ::m_lcreat(file.c_str(), 0);
    ASSERT_NE(hw, HFILE_ERROR);
    EXPECT_EQ(::m_lwrite(hw, reinterpret_cast<LPCCH>(payload.data()),
                         static_cast<UINT>(payload.size())),
              static_cast<UINT>(payload.size()));
    EXPECT_EQ(::m_lclose(hw), 0);

    HFILE const hr = ::m_lopen(file.c_str(), OF_READ);
    ASSERT_NE(hr, HFILE_ERROR);

    // Seek to offset 4 (FILE_BEGIN); the next sequential read starts there.
    EXPECT_EQ(::m_llseek(hr, 4, FILE_BEGIN), 4);

    std::array<unsigned char, 4> tail{};
    EXPECT_EQ(::m_lread(hr, tail.data(), static_cast<UINT>(tail.size())),
              static_cast<UINT>(tail.size()));
    std::array<unsigned char, 4> const expected = {0x04, 0x05, 0x06, 0x07};
    EXPECT_EQ(tail, expected);

    // Seek to end reports the file extent.
    EXPECT_EQ(::m_llseek(hr, 0, FILE_END), static_cast<LONG>(payload.size()));
    EXPECT_EQ(::m_lclose(hr), 0);
}

TEST(MwinFileLegacyContent, LzOpenReadIsPassthrough)
{
    std::string const                  file    = public_path("lz_read.bin");
    std::array<unsigned char, 4> const payload = {0xDE, 0xAD, 0xBE, 0xEF};

    HFILE const hw = ::m_lcreat(file.c_str(), 0);
    ASSERT_NE(hw, HFILE_ERROR);
    EXPECT_EQ(::m_lwrite(hw, reinterpret_cast<LPCCH>(payload.data()),
                         static_cast<UINT>(payload.size())),
              static_cast<UINT>(payload.size()));
    EXPECT_EQ(::m_lclose(hw), 0);

    // LZOpenFile reads the file back with no decompression: the bytes are
    // verbatim the ones written.
    OFSTRUCT  ofs{};
    INT const lz = ::mLZOpenFileA(const_cast<LPSTR>(file.c_str()), &ofs, OF_READ);
    ASSERT_GE(lz, 0);

    // LZInit on an already-open passthrough handle is the identity.
    EXPECT_EQ(::mLZInit(lz), lz);

    std::array<unsigned char, 4> readback{};
    EXPECT_EQ(::mLZRead(lz, reinterpret_cast<CHAR*>(readback.data()),
                        static_cast<INT>(readback.size())),
              static_cast<INT>(payload.size()));
    EXPECT_EQ(readback, payload);

    // LZSeek repositions the same way _llseek does.
    EXPECT_EQ(::mLZSeek(lz, 2, FILE_BEGIN), 2);

    ::mLZClose(lz);
}

TEST(MwinFileLegacyContent, LzCopyDuplicatesWholeFile)
{
    std::string const                  src     = public_path("lz_src.bin");
    std::string const                  dst     = public_path("lz_dst.bin");
    std::array<unsigned char, 7> const payload = {0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96};

    HFILE const hs = ::m_lcreat(src.c_str(), 0);
    ASSERT_NE(hs, HFILE_ERROR);
    EXPECT_EQ(::m_lwrite(hs, reinterpret_cast<LPCCH>(payload.data()),
                         static_cast<UINT>(payload.size())),
              static_cast<UINT>(payload.size()));
    EXPECT_EQ(::m_lclose(hs), 0);

    OFSTRUCT  ofs{};
    INT const lz_src = ::mLZOpenFileA(const_cast<LPSTR>(src.c_str()), &ofs, OF_READ);
    ASSERT_GE(lz_src, 0);

    // A writable destination handle (read/write from _lcreat).
    HFILE const hd = ::m_lcreat(dst.c_str(), 0);
    ASSERT_NE(hd, HFILE_ERROR);

    EXPECT_EQ(::mLZCopy(lz_src, static_cast<INT>(hd)), static_cast<LONG>(payload.size()));

    ::mLZClose(lz_src);
    EXPECT_EQ(::m_lclose(hd), 0);

    // The destination now carries the source's whole content.
    HFILE const hr = ::m_lopen(dst.c_str(), OF_READ);
    ASSERT_NE(hr, HFILE_ERROR);
    std::array<unsigned char, 7> readback{};
    EXPECT_EQ(::m_lread(hr, readback.data(), static_cast<UINT>(readback.size())),
              static_cast<UINT>(payload.size()));
    EXPECT_EQ(readback, payload);
    EXPECT_EQ(::m_lclose(hr), 0);
}

TEST(MwinFileLegacyContent, GetExpandedNameIsPassthrough)
{
    // PIL models no LZ name expansion, so the source name is copied unchanged.
    char        source[] = "subdir\\packed.ex_";
    char        buffer[MAX_PATH] = {};
    EXPECT_EQ(::mGetExpandedNameA(source, buffer), TRUE);
    EXPECT_STREQ(buffer, source);
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

    std::filesystem::path const public_dir  = exe_dir / L"fslegacycontent_pub";
    std::filesystem::path const private_dir = exe_dir / L"fslegacycontent_priv";
    g_public_root = public_dir.string();
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
