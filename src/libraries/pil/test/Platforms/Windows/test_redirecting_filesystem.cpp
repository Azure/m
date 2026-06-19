// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>

#include "redirecting/redirecting.h"

//
// M-FS-REDIR-1: functional verification of the redirecting filesystem facet
// over a live provider. A redirecting::directory is placed over a live
// direct idirectory handle with a path-prefix redirection table; operations
// whose path lies under a redirected prefix land in the target subtree on
// disk, while non-matching paths pass through unchanged and the caller's
// original case is preserved in the created leaf names.
//

namespace
{
    using namespace std::string_view_literals;

    namespace redir = m::pil::impl::redirecting;

    m::pil::file_path
    to_file_path(std::filesystem::path const& p)
    {
        std::wstring const   ws = p.wstring();
        std::u16string const u16(ws.begin(), ws.end());
        return m::pil::file_path(m::pil::file_path::view_type(u16));
    }

    class scoped_temp_dir
    {
    public:
        scoped_temp_dir()
        {
            auto const base = std::filesystem::temp_directory_path();
            m_path = base / (L"m_pil_fs_redir_" + std::to_wstring(::GetCurrentProcessId()) + L"_" +
                             std::to_wstring(s_counter++));
            std::filesystem::remove_all(m_path);
            std::filesystem::create_directories(m_path);
        }

        scoped_temp_dir(scoped_temp_dir const&)            = delete;
        scoped_temp_dir& operator=(scoped_temp_dir const&) = delete;

        ~scoped_temp_dir()
        {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }

        std::filesystem::path const&
        path() const noexcept
        {
            return m_path;
        }

    private:
        static inline unsigned s_counter = 0;
        std::filesystem::path  m_path;
    };

    // A live (direct) idirectory handle for an absolute directory.
    std::shared_ptr<m::pil::idirectory>
    live_directory(std::filesystem::path const& absolute)
    {
        auto platform = m::pil::make_platform_interface();

        std::shared_ptr<m::pil::ifilesystem> fs;
        platform->get_filesystem(m::pil::iplatform::get_filesystem_flags{}, fs);

        auto const fp   = to_file_path(absolute);
        auto       root = fs->open_root(fp.root(), m::pil::file_access::default_open);
        return root->open_directory(m::pil::file_path(fp.relative_path()));
    }

    using P = std::pair<std::u16string_view, std::u16string_view>;

    // A redirecting directory whose "redir" prefix is sent to the "actual"
    // subtree; everything else passes through.
    std::shared_ptr<m::pil::idirectory>
    redirecting_directory(std::filesystem::path const& absolute)
    {
        std::array<P, 1> const table = {{P{u"redir"sv, u"actual"sv}}};
        auto const r = std::make_shared<redir::fs_redirector>(table);
        return std::make_shared<redir::directory>(live_directory(absolute), r);
    }

    TEST(RedirectingFilesystem, RedirectedPrefixLandsInTargetSubtree)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"actual");

        auto dir = redirecting_directory(tmp.path());

        // create_directory("redir\\child") must materialize "actual\\child".
        (void)dir->create_directory(m::pil::file_path(u"redir\\child"sv));

        EXPECT_TRUE(std::filesystem::exists(tmp.path() / L"actual" / L"child"));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"redir"));
    }

    TEST(RedirectingFilesystem, NonMatchingPathPassesThrough)
    {
        scoped_temp_dir const tmp;

        auto dir = redirecting_directory(tmp.path());

        (void)dir->create_directory(m::pil::file_path(u"plain"sv));

        EXPECT_TRUE(std::filesystem::exists(tmp.path() / L"plain"));
    }

    TEST(RedirectingFilesystem, OriginalCaseOfLeafPreserved)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"actual");

        auto dir = redirecting_directory(tmp.path());

        // Prefix matched case-insensitively; the leaf keeps its exact case.
        (void)dir->create_file(m::pil::file_path(u"REDIR\\MixedCase.TXT"sv));

        auto const expected = tmp.path() / L"actual" / L"MixedCase.TXT";
        ASSERT_TRUE(std::filesystem::exists(expected));

        // Confirm the on-disk leaf name has the exact case requested.
        bool found_exact = false;
        for (auto const& e: std::filesystem::directory_iterator(tmp.path() / L"actual"))
        {
            if (e.path().filename().wstring() == L"MixedCase.TXT")
                found_exact = true;
        }
        EXPECT_TRUE(found_exact);
    }

    TEST(RedirectingFilesystem, ReadContentForwardsThroughDecorator)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"actual");
        std::string const contents = "redirected-bytes";
        {
            std::ofstream f(tmp.path() / L"actual" / L"data.bin", std::ios::binary);
            f << contents;
        }

        auto dir  = redirecting_directory(tmp.path());
        auto file = dir->open_file(m::pil::file_path(u"redir\\data.bin"sv));

        std::array<std::byte, 32> buffer{};
        auto const                read = file->read_content(0, std::span<std::byte>(buffer));

        EXPECT_EQ(read, contents.size());
        std::string const got(reinterpret_cast<char const*>(buffer.data()), read);
        EXPECT_EQ(got, contents);
    }

    TEST(RedirectingFilesystem, WriteContentForwardsThroughDecorator)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"actual");

        auto dir  = redirecting_directory(tmp.path());
        auto file = dir->create_file(m::pil::file_path(u"redir\\out.bin"sv));

        std::string const contents = "decorator-write";
        auto const        span     = std::span<std::byte const>(
            reinterpret_cast<std::byte const*>(contents.data()), contents.size());
        auto const written = file->write_content(0, span);

        EXPECT_EQ(written, contents.size());

        // The bytes must have landed in the backing ("actual") subtree.
        std::ifstream     in(tmp.path() / L"actual" / L"out.bin", std::ios::binary);
        std::string const on_disk((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        EXPECT_EQ(on_disk, contents);
    }

    // M-FS-STREAMS-1.3: subtree redirection binding through the public
    // configuration path. This is the init-time binding D16 describes: a chosen
    // subtree is bound to an assembled real backing directory by handing
    // make_platform_interface a filesystem redirection. A file placed in the
    // backing directory is then read back through the bound public path, served
    // whole-file by the 1.1 content accessor (read_content) flowing down through
    // the redirecting layer the platform factory installs from its table.
    TEST(RedirectingFilesystem, SubtreeBindingReadsBackingFileThroughPublicPath)
    {
        scoped_temp_dir const tmp;

        // Assemble the backing directory and place a file in it.
        auto const backing_abs = tmp.path() / L"backing";
        std::filesystem::create_directory(backing_abs);
        std::string const contents = "subtree-bound-content";
        {
            std::ofstream f(backing_abs / L"hello.txt", std::ios::binary);
            f << contents;
        }

        // The public subtree the client will use; it need not exist on disk.
        auto const public_abs = tmp.path() / L"public";

        // Bind the public subtree -> backing directory as a path-prefix
        // redirection, keyed on the root-relative form the filesystem operations
        // carry below open_root. The file_path locals own the key/value storage
        // for the duration of the make_platform_interface call.
        auto const public_fp   = to_file_path(public_abs);
        auto const backing_fp  = to_file_path(backing_abs);
        auto const public_rel  = m::pil::file_path(public_fp.relative_path());
        auto const backing_rel = m::pil::file_path(backing_fp.relative_path());

        std::array<P, 1> const table = {
            {P{public_rel.native().view(), backing_rel.native().view()}}};

        auto platform = m::pil::make_platform_interface(m::pil::make_platform_flags{},
                                                        std::span<P const>(table));

        std::shared_ptr<m::pil::ifilesystem> fs;
        platform->get_filesystem(m::pil::iplatform::get_filesystem_flags{}, fs);

        auto root = fs->open_root(public_fp.root(), m::pil::file_access::default_open);
        auto dir  = root->open_directory(public_rel);
        auto file = dir->open_file(m::pil::file_path(u"hello.txt"sv));

        std::array<std::byte, 64> buffer{};
        auto const                read = file->read_content(0, std::span<std::byte>(buffer));

        EXPECT_EQ(read, contents.size());
        std::string const got(reinterpret_cast<char const*>(buffer.data()), read);
        EXPECT_EQ(got, contents);
    }

    // M-FS-STREAMS-1.4: namespace-mutation overlay / tombstones with content
    // read-through. A buffered overlay placed over the subtree binding (D16)
    // tracks create / delete / rename of entries as overlay state isolated from
    // the shared backing, while an unmodified backing file is still served
    // whole-file through read_content (the 1.1 accessor flowing into the
    // retained backing handle). The backing directory on disk is never mutated
    // by overlay namespace edits (D16: no byte-range/size mutation of backing).
    TEST(RedirectingFilesystem, BufferedOverlayIsolatesNamespaceMutationsWithReadThrough)
    {
        scoped_temp_dir const tmp;

        // Backing directory with three files: one to read through, one to
        // delete, one to rename.
        auto const backing_abs = tmp.path() / L"backing";
        std::filesystem::create_directory(backing_abs);
        std::string const keep_contents = "keep-me-bytes";
        {
            std::ofstream f(backing_abs / L"keep.txt", std::ios::binary);
            f << keep_contents;
        }
        {
            std::ofstream f(backing_abs / L"drop.txt", std::ios::binary);
            f << "doomed";
        }
        {
            std::ofstream f(backing_abs / L"old.txt", std::ios::binary);
            f << "rename-me";
        }

        // The public subtree the client uses; it need not exist on disk.
        auto const public_abs = tmp.path() / L"public";

        auto const public_fp   = to_file_path(public_abs);
        auto const backing_fp  = to_file_path(backing_abs);
        auto const public_rel  = m::pil::file_path(public_fp.relative_path());
        auto const backing_rel = m::pil::file_path(backing_fp.relative_path());

        std::array<P, 1> const table = {
            {P{public_rel.native().view(), backing_rel.native().view()}}};

        // buffer_updates installs a buffered overlay beneath the redirecting
        // binding: namespace mutations land in the overlay, never on the
        // backing, while reads of unmodified entries pass through to it.
        auto platform = m::pil::make_platform_interface(m::pil::make_platform_flags::buffer_updates,
                                                        std::span<P const>(table));

        std::shared_ptr<m::pil::ifilesystem> fs;
        platform->get_filesystem(m::pil::iplatform::get_filesystem_flags{}, fs);

        auto root = fs->open_root(public_fp.root(), m::pil::file_access::default_open);
        auto dir  = root->open_directory(public_rel);

        // (a) read-through: an unmodified backing file's bytes are served.
        {
            auto file = dir->open_file(m::pil::file_path(u"keep.txt"sv));

            std::array<std::byte, 64> buffer{};
            auto const                read = file->read_content(0, std::span<std::byte>(buffer));

            EXPECT_EQ(read, keep_contents.size());
            std::string const got(reinterpret_cast<char const*>(buffer.data()), read);
            EXPECT_EQ(got, keep_contents);
        }

        // (b) delete: tombstoned in the overlay; the entry no longer resolves,
        // yet the backing file remains untouched on disk.
        dir->remove_entry(m::pil::file_path(u"drop.txt"sv));
        EXPECT_EQ(dir->try_open_file(m::pil::file_path(u"drop.txt"sv)), nullptr);
        EXPECT_TRUE(std::filesystem::exists(backing_abs / L"drop.txt"));

        // (c) rename: the new name resolves, the old name is gone; the backing
        // keeps its original name and gains nothing.
        dir->rename_entry(m::pil::file_path(u"old.txt"sv), m::pil::file_path(u"new.txt"sv));
        EXPECT_NE(dir->try_open_file(m::pil::file_path(u"new.txt"sv)), nullptr);
        EXPECT_EQ(dir->try_open_file(m::pil::file_path(u"old.txt"sv)), nullptr);
        EXPECT_TRUE(std::filesystem::exists(backing_abs / L"old.txt"));
        EXPECT_FALSE(std::filesystem::exists(backing_abs / L"new.txt"));

        // (d) create: appears in the overlay; the backing gains nothing.
        (void)dir->create_file(m::pil::file_path(u"fresh.txt"sv));
        EXPECT_NE(dir->try_open_file(m::pil::file_path(u"fresh.txt"sv)), nullptr);
        EXPECT_FALSE(std::filesystem::exists(backing_abs / L"fresh.txt"));
    }

} // namespace
