// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>

#include "passthrough/passthrough.h"

//
// M-FS-PASS-1: observable equivalence of the pass-through filesystem facet to
// the underlying live provider. A transparent passthrough layer is placed over
// a live direct platform; driving the namespace through that layer must produce
// the same observations as std::filesystem ground truth (and, by construction,
// as the direct provider it forwards to).
//

namespace
{
    m::pil::file_path
    to_file_path(std::filesystem::path const& p)
    {
        std::wstring const   ws = p.wstring();
        std::u16string const u16(ws.begin(), ws.end());
        return m::pil::file_path(m::pil::file_path::view_type(u16));
    }

    std::u16string
    name_of(m::pil::directory_entry const& e)
    {
        return std::u16string(e.m_name.view());
    }

    class scoped_temp_dir
    {
    public:
        scoped_temp_dir()
        {
            auto const base = std::filesystem::temp_directory_path();
            m_path = base / (L"m_pil_fs_pass_" + std::to_wstring(::GetCurrentProcessId()) + L"_" +
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

    // A value platform whose filesystem resolves through a transparent
    // passthrough layer sitting over the live direct provider.
    m::pil::platform
    passthrough_over_live()
    {
        auto inner = m::pil::make_platform_interface();
        std::shared_ptr<m::pil::iplatform> layered =
            std::make_shared<m::pil::impl::passthrough::platform>(inner);
        return m::pil::platform(std::move(layered));
    }

    m::pil::directory
    open_through(m::pil::filesystem_class& fs, std::filesystem::path const& absolute)
    {
        auto const fp   = to_file_path(absolute);
        auto       root = fs.open_root(fp.root());
        auto const rel  = fp.relative_path();
        return root.open_directory(m::pil::file_path(rel));
    }

    TEST(PassthroughFilesystem, EnumerateMatchesGroundTruth)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"alpha");
        std::filesystem::create_directory(tmp.path() / L"beta");
        {
            std::ofstream f(tmp.path() / L"gamma.txt");
            f << "hello";
        }

        auto platform = passthrough_over_live();
        auto fs       = platform.get_filesystem();
        auto dir      = open_through(fs, tmp.path());

        std::set<std::u16string> names;
        std::set<std::u16string> directories;
        for (auto const& e: dir.list_entries())
        {
            names.insert(name_of(e));
            if (e.m_metadata.is_directory())
                directories.insert(name_of(e));
        }

        EXPECT_EQ(names, (std::set<std::u16string>{u"alpha", u"beta", u"gamma.txt"}));
        EXPECT_EQ(directories, (std::set<std::u16string>{u"alpha", u"beta"}));
    }

    TEST(PassthroughFilesystem, StatForwardsThrough)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"child");
        std::string const contents = "0123456789";
        {
            std::ofstream f(tmp.path() / L"data.bin", std::ios::binary);
            f << contents;
        }

        auto platform = passthrough_over_live();
        auto fs       = platform.get_filesystem();
        auto dir      = open_through(fs, tmp.path());

        auto child = dir.open_directory(std::u16string_view(u"child"));
        EXPECT_TRUE(child.query_information().is_directory());

        auto data = dir.open_file(std::u16string_view(u"data.bin"));
        auto md   = data.query_information();
        EXPECT_TRUE(md.is_file());
        EXPECT_EQ(md.m_size, contents.size());
    }

    TEST(PassthroughFilesystem, TentativeOpenForwardsThrough)
    {
        scoped_temp_dir const tmp;

        auto platform = passthrough_over_live();
        auto fs       = platform.get_filesystem();
        auto dir      = open_through(fs, tmp.path());

        EXPECT_FALSE(dir.try_open_directory(m::pil::file_path(std::u16string_view(u"nope"))));
        EXPECT_FALSE(dir.try_open_file(m::pil::file_path(std::u16string_view(u"nope.txt"))));
    }

    TEST(PassthroughFilesystem, MutationsForwardThrough)
    {
        scoped_temp_dir const tmp;

        auto platform = passthrough_over_live();
        auto fs       = platform.get_filesystem();
        auto dir      = open_through(fs, tmp.path());

        // Create a nested tree through the passthrough layer.
        auto sub = dir.create_directory(std::u16string_view(u"sub"));
        (void)sub.create_file(std::u16string_view(u"f.txt"));

        EXPECT_TRUE(std::filesystem::is_directory(tmp.path() / L"sub"));
        EXPECT_TRUE(std::filesystem::is_regular_file(tmp.path() / L"sub" / L"f.txt"));

        // Rename / move forwards through unchanged.
        dir.rename_entry(m::pil::file_path(std::u16string_view(u"sub")),
                         m::pil::file_path(std::u16string_view(u"renamed")));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"sub"));
        EXPECT_TRUE(std::filesystem::is_regular_file(tmp.path() / L"renamed" / L"f.txt"));

        // delete_tree forwards through unchanged.
        dir.delete_tree(std::optional<m::pil::file_path>(
            m::pil::file_path(std::u16string_view(u"renamed"))));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"renamed"));
        EXPECT_TRUE(dir.list_entries().empty());
    }

    TEST(PassthroughFilesystem, EquivalentToDirectProvider)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directories(tmp.path() / L"a" / L"b");
        {
            std::ofstream f(tmp.path() / L"a" / L"leaf.txt");
            f << "x";
        }

        // Direct provider view.
        std::set<std::u16string> direct_names;
        {
            auto fs  = m::pil::make_platform().get_filesystem();
            auto dir = open_through(fs, tmp.path() / L"a");
            for (auto const& e: dir.list_entries())
                direct_names.insert(name_of(e));
        }

        // Passthrough view.
        std::set<std::u16string> passthrough_names;
        {
            auto platform = passthrough_over_live();
            auto fs       = platform.get_filesystem();
            auto dir      = open_through(fs, tmp.path() / L"a");
            for (auto const& e: dir.list_entries())
                passthrough_names.insert(name_of(e));
        }

        EXPECT_EQ(direct_names, passthrough_names);
        EXPECT_EQ(passthrough_names, (std::set<std::u16string>{u"b", u"leaf.txt"}));
    }

} // namespace
