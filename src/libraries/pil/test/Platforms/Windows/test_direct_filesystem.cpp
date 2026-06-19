// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem.h>
#include <m/pil/pil.h>

//
// Read-side integration tests for the live Windows filesystem provider
// (M-FS-DIRECT-1). A directory tree is built with std::filesystem (the ground
// truth), then opened and inspected through the direct provider's value
// wrappers (open_root / open_directory / open_file / enumerate / stat). The
// mutating verbs are exercised in the M-FS-DIRECT-2 / M-FS-DIRECT-4 tests.
//

namespace
{
    // Converts a std::filesystem path to a pil::file_path. On Windows wchar_t
    // and char16_t share a representation, so the wide string's code units copy
    // straight across.
    m::pil::file_path
    to_file_path(std::filesystem::path const& p)
    {
        std::wstring const  ws = p.wstring();
        std::u16string const u16(ws.begin(), ws.end());
        return m::pil::file_path(m::pil::file_path::view_type(u16));
    }

    std::u16string
    name_of(m::pil::directory_entry const& e)
    {
        return std::u16string(e.m_name.view());
    }

    // The set of child leaf names a std::filesystem directory holds (the ground
    // truth the provider's enumeration is compared against).
    std::set<std::u16string>
    fs_child_names(std::filesystem::path const& dir)
    {
        std::set<std::u16string> names;
        for (auto const& entry: std::filesystem::directory_iterator(dir))
        {
            std::wstring const ws = entry.path().filename().wstring();
            names.insert(std::u16string(ws.begin(), ws.end()));
        }
        return names;
    }

    // A unique temporary directory that is removed on destruction.
    class scoped_temp_dir
    {
    public:
        scoped_temp_dir()
        {
            auto const base = std::filesystem::temp_directory_path();
            m_path = base / (L"m_pil_fs_direct_" + std::to_wstring(::GetCurrentProcessId()) + L"_" +
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

    // Opens the directory named by an absolute std::filesystem path through the
    // direct provider, by opening its drive root and then the relative remainder.
    m::pil::directory
    open_through_provider(m::pil::filesystem_class& fs, std::filesystem::path const& absolute)
    {
        auto const fp   = to_file_path(absolute);
        auto       root = fs.open_root(fp.root());
        auto const rel  = fp.relative_path();
        return root.open_directory(m::pil::file_path(rel));
    }

    TEST(DirectFilesystem, OpenRootResolvesDriveRoot)
    {
        auto fs = m::pil::make_platform().get_filesystem();

        scoped_temp_dir const tmp;
        auto const            fp   = to_file_path(tmp.path());
        auto                  root = fs.open_root(fp.root());

        auto const md = root.query_information();
        EXPECT_TRUE(md.is_directory());
    }

    TEST(DirectFilesystem, EnumerateMatchesGroundTruth)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"alpha");
        std::filesystem::create_directory(tmp.path() / L"beta");
        {
            std::ofstream f(tmp.path() / L"gamma.txt");
            f << "hello";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());

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

    TEST(DirectFilesystem, EnumerateEmptyDirectory)
    {
        scoped_temp_dir const tmp;

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());

        EXPECT_TRUE(dir.list_entries().empty());
    }

    TEST(DirectFilesystem, OpenSubdirectoryAndStat)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"child");

        auto fs    = m::pil::make_platform().get_filesystem();
        auto dir   = open_through_provider(fs, tmp.path());
        auto child = dir.open_directory(std::u16string_view(u"child"));

        EXPECT_TRUE(child.query_information().is_directory());
    }

    TEST(DirectFilesystem, OpenFileAndStatSize)
    {
        scoped_temp_dir const tmp;
        std::string const     contents = "0123456789"; // 10 bytes
        {
            std::ofstream f(tmp.path() / L"data.bin", std::ios::binary);
            f << contents;
        }

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto file = dir.open_file(std::u16string_view(u"data.bin"));

        auto const md = file.query_information();
        EXPECT_TRUE(md.is_file());
        EXPECT_EQ(md.m_size, contents.size());
    }

    TEST(DirectFilesystem, ReadContentReturnsFileBytes)
    {
        scoped_temp_dir const tmp;
        std::string const     contents = "0123456789"; // 10 bytes
        {
            std::ofstream f(tmp.path() / L"data.bin", std::ios::binary);
            f << contents;
        }

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto file = dir.open_file(std::u16string_view(u"data.bin"));

        std::array<std::byte, 16>  buffer{};
        auto const                 read = file.read_content(0, std::span<std::byte>(buffer));

        EXPECT_EQ(read, contents.size());
        std::string const got(reinterpret_cast<char const*>(buffer.data()), read);
        EXPECT_EQ(got, contents);
    }

    TEST(DirectFilesystem, ReadContentAtOffsetReturnsTail)
    {
        scoped_temp_dir const tmp;
        std::string const     contents = "0123456789"; // 10 bytes
        {
            std::ofstream f(tmp.path() / L"data.bin", std::ios::binary);
            f << contents;
        }

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto file = dir.open_file(std::u16string_view(u"data.bin"));

        std::array<std::byte, 16> buffer{};
        auto const                read = file.read_content(7, std::span<std::byte>(buffer));

        EXPECT_EQ(read, std::size_t{3});
        std::string const got(reinterpret_cast<char const*>(buffer.data()), read);
        EXPECT_EQ(got, "789");
    }

    TEST(DirectFilesystem, ReadContentAtEofReturnsZero)
    {
        scoped_temp_dir const tmp;
        std::string const     contents = "0123456789"; // 10 bytes
        {
            std::ofstream f(tmp.path() / L"data.bin", std::ios::binary);
            f << contents;
        }

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto file = dir.open_file(std::u16string_view(u"data.bin"));

        std::array<std::byte, 16> buffer{};
        auto const                read =
            file.read_content(contents.size(), std::span<std::byte>(buffer));

        EXPECT_EQ(read, std::size_t{0});
    }

    TEST(DirectFilesystem, ReadContentEmptyBufferReadsNothing)
    {
        scoped_temp_dir const tmp;
        {
            std::ofstream f(tmp.path() / L"data.bin", std::ios::binary);
            f << "0123456789";
        }

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto file = dir.open_file(std::u16string_view(u"data.bin"));

        auto const read = file.read_content(0, std::span<std::byte>());

        EXPECT_EQ(read, std::size_t{0});
    }

    TEST(DirectFilesystem, WriteContentReplacesFileBytes)
    {
        scoped_temp_dir const tmp;

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto file = dir.create_file(std::u16string_view(u"out.bin"));

        std::string const contents = "hello world"; // 11 bytes
        auto const        bytes    = reinterpret_cast<std::byte const*>(contents.data());
        auto const        written  =
            file.write_content(0, std::span<std::byte const>(bytes, contents.size()));

        EXPECT_EQ(written, contents.size());

        std::array<std::byte, 32> buffer{};
        auto const                read = file.read_content(0, std::span<std::byte>(buffer));
        EXPECT_EQ(read, contents.size());
        std::string const got(reinterpret_cast<char const*>(buffer.data()), read);
        EXPECT_EQ(got, contents);
    }

    TEST(DirectFilesystem, WriteContentTruncatesToWrittenLength)
    {
        scoped_temp_dir const tmp;

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto file = dir.create_file(std::u16string_view(u"out.bin"));

        std::string const big = "0123456789ABCDEF"; // 16 bytes
        file.write_content(
            0, std::span<std::byte const>(reinterpret_cast<std::byte const*>(big.data()), big.size()));

        std::string const shrunk = "xyz"; // 3 bytes - must shrink the file
        auto const        written = file.write_content(
            0,
            std::span<std::byte const>(reinterpret_cast<std::byte const*>(shrunk.data()), shrunk.size()));
        EXPECT_EQ(written, shrunk.size());

        EXPECT_EQ(file.query_information().m_size, shrunk.size());

        std::array<std::byte, 32> buffer{};
        auto const                read = file.read_content(0, std::span<std::byte>(buffer));
        EXPECT_EQ(read, shrunk.size());
        std::string const got(reinterpret_cast<char const*>(buffer.data()), read);
        EXPECT_EQ(got, shrunk);
    }

    TEST(DirectFilesystem, WriteContentNonZeroOffsetRejected)
    {
        scoped_temp_dir const tmp;

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto file = dir.create_file(std::u16string_view(u"out.bin"));

        std::string const contents = "abc";
        auto const        span     = std::span<std::byte const>(
            reinterpret_cast<std::byte const*>(contents.data()), contents.size());

        EXPECT_THROW(file.write_content(1, span), std::system_error);
    }

    TEST(DirectFilesystem, TryOpenMissingDirectoryReturnsNullopt)
    {
        scoped_temp_dir const tmp;

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());

        EXPECT_FALSE(dir.try_open_directory(m::pil::file_path(std::u16string_view(u"nope"))));
    }

    TEST(DirectFilesystem, TryOpenMissingFileReturnsNullopt)
    {
        scoped_temp_dir const tmp;

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());

        EXPECT_FALSE(dir.try_open_file(m::pil::file_path(std::u16string_view(u"nope.txt"))));
    }

    TEST(DirectFilesystem, TryOpenExistingFileReturnsNode)
    {
        scoped_temp_dir const tmp;
        {
            std::ofstream f(tmp.path() / L"present.txt");
            f << "x";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());

        auto opened = dir.try_open_file(m::pil::file_path(std::u16string_view(u"present.txt")));
        ASSERT_TRUE(opened);
        EXPECT_TRUE(opened->query_information().is_file());
    }

    //
    // M-FS-DIRECT-2: namespace mutations.
    //

    TEST(DirectFilesystem, CreateDirectoryCreatesOnDisk)
    {
        scoped_temp_dir const tmp;

        auto fs    = m::pil::make_platform().get_filesystem();
        auto dir   = open_through_provider(fs, tmp.path());
        auto child = dir.create_directory(std::u16string_view(u"made"));

        EXPECT_TRUE(std::filesystem::is_directory(tmp.path() / L"made"));
        EXPECT_TRUE(child.query_information().is_directory());
    }

    TEST(DirectFilesystem, CreateDirectoryIsCreateOrOpen)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"existing");

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());

        // Opening an already-present directory is not an error.
        auto child = dir.create_directory(std::u16string_view(u"existing"));
        EXPECT_TRUE(child.query_information().is_directory());
    }

    TEST(DirectFilesystem, CreateFileCreatesOnDisk)
    {
        scoped_temp_dir const tmp;

        auto fs   = m::pil::make_platform().get_filesystem();
        auto dir  = open_through_provider(fs, tmp.path());
        auto made = dir.create_file(std::u16string_view(u"made.txt"));

        EXPECT_TRUE(std::filesystem::is_regular_file(tmp.path() / L"made.txt"));
        EXPECT_TRUE(made.query_information().is_file());
    }

    TEST(DirectFilesystem, RemoveEntryDeletesFile)
    {
        scoped_temp_dir const tmp;
        {
            std::ofstream f(tmp.path() / L"victim.txt");
            f << "x";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());
        dir.remove_entry(m::pil::file_path(std::u16string_view(u"victim.txt")));

        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"victim.txt"));
    }

    TEST(DirectFilesystem, RemoveEntryDeletesEmptyDirectory)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"empty");

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());
        dir.remove_entry(m::pil::file_path(std::u16string_view(u"empty")));

        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"empty"));
    }

    TEST(DirectFilesystem, RemoveEntryNonEmptyDirectoryThrows)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"full");
        {
            std::ofstream f(tmp.path() / L"full" / L"inside.txt");
            f << "x";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());

        EXPECT_ANY_THROW(dir.remove_entry(m::pil::file_path(std::u16string_view(u"full"))));
        EXPECT_TRUE(std::filesystem::exists(tmp.path() / L"full" / L"inside.txt"));
    }

    TEST(DirectFilesystem, DeleteTreeNamedRemovesSubtree)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directories(tmp.path() / L"sub" / L"nested");
        {
            std::ofstream f(tmp.path() / L"sub" / L"a.txt");
            f << "a";
        }
        {
            std::ofstream f(tmp.path() / L"sub" / L"nested" / L"b.txt");
            f << "b";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());
        dir.delete_tree(std::optional<m::pil::file_path>(
            m::pil::file_path(std::u16string_view(u"sub"))));

        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"sub"));
    }

    TEST(DirectFilesystem, DeleteTreeContentsKeepsDirectory)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"keep");
        std::filesystem::create_directories(tmp.path() / L"child_dir" / L"deep");
        {
            std::ofstream f(tmp.path() / L"top.txt");
            f << "x";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());
        dir.delete_tree(std::optional<m::pil::file_path>{});

        // The directory itself survives; all of its contents are gone.
        EXPECT_TRUE(std::filesystem::is_directory(tmp.path()));
        EXPECT_TRUE(dir.list_entries().empty());
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"keep"));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"child_dir"));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"top.txt"));
    }

    TEST(DirectFilesystem, RenameEntryMovesFile)
    {
        scoped_temp_dir const tmp;
        {
            std::ofstream f(tmp.path() / L"before.txt");
            f << "x";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());
        dir.rename_entry(m::pil::file_path(std::u16string_view(u"before.txt")),
                         m::pil::file_path(std::u16string_view(u"after.txt")));

        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"before.txt"));
        EXPECT_TRUE(std::filesystem::is_regular_file(tmp.path() / L"after.txt"));
    }

    TEST(DirectFilesystem, RenameEntryMovesDirectoryIntoSubtree)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"src");
        std::filesystem::create_directory(tmp.path() / L"dst");
        {
            std::ofstream f(tmp.path() / L"src" / L"f.txt");
            f << "x";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto dir = open_through_provider(fs, tmp.path());
        dir.rename_entry(m::pil::file_path(std::u16string_view(u"src")),
                         m::pil::file_path(std::u16string_view(u"dst\\moved")));

        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"src"));
        EXPECT_TRUE(std::filesystem::is_regular_file(tmp.path() / L"dst" / L"moved" / L"f.txt"));
    }

    //
    // M-FS-DIRECT-4: end-to-end integration. A directory tree is built, moved,
    // and torn down entirely through the direct provider, and each step is
    // checked against std::filesystem ground truth.
    //

    // Collects the provider's view of a directory's child leaf names.
    std::set<std::u16string>
    provider_child_names(m::pil::directory& dir)
    {
        std::set<std::u16string> names;
        for (auto const& e: dir.list_entries())
            names.insert(name_of(e));
        return names;
    }

    TEST(DirectFilesystem, EndToEndLifecycle)
    {
        scoped_temp_dir const tmp;

        auto fs   = m::pil::make_platform().get_filesystem();
        auto root = open_through_provider(fs, tmp.path());

        // Build a nested tree through the provider.
        auto project = root.create_directory(std::u16string_view(u"project"));
        auto src     = project.create_directory(std::u16string_view(u"src"));
        auto docs    = project.create_directory(std::u16string_view(u"docs"));

        (void)src.create_file(std::u16string_view(u"main.cpp"));
        (void)src.create_file(std::u16string_view(u"util.cpp"));
        (void)docs.create_file(std::u16string_view(u"readme.md"));

        // The tree exists on disk exactly as built.
        ASSERT_TRUE(std::filesystem::is_directory(tmp.path() / L"project" / L"src"));
        ASSERT_TRUE(std::filesystem::is_directory(tmp.path() / L"project" / L"docs"));
        ASSERT_TRUE(
            std::filesystem::is_regular_file(tmp.path() / L"project" / L"src" / L"main.cpp"));

        // Enumeration through the provider matches std::filesystem ground truth.
        EXPECT_EQ(provider_child_names(project), fs_child_names(tmp.path() / L"project"));
        EXPECT_EQ(provider_child_names(src), fs_child_names(tmp.path() / L"project" / L"src"));
        EXPECT_EQ(provider_child_names(src),
                  (std::set<std::u16string>{u"main.cpp", u"util.cpp"}));

        // stat: a directory and a file report their kinds.
        EXPECT_TRUE(src.query_information().is_directory());
        {
            auto main_cpp = src.open_file(std::u16string_view(u"main.cpp"));
            EXPECT_TRUE(main_cpp.query_information().is_file());
        }

        // Rename a directory in place.
        project.rename_entry(m::pil::file_path(std::u16string_view(u"docs")),
                             m::pil::file_path(std::u16string_view(u"documentation")));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"project" / L"docs"));
        EXPECT_TRUE(std::filesystem::is_directory(tmp.path() / L"project" / L"documentation"));

        // Move a file across subdirectories.
        project.rename_entry(m::pil::file_path(std::u16string_view(u"src\\util.cpp")),
                             m::pil::file_path(std::u16string_view(u"documentation\\util.cpp")));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"project" / L"src" / L"util.cpp"));
        EXPECT_TRUE(std::filesystem::is_regular_file(tmp.path() / L"project" / L"documentation" /
                                                     L"util.cpp"));

        // Remove a single file.
        src.remove_entry(m::pil::file_path(std::u16string_view(u"main.cpp")));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"project" / L"src" / L"main.cpp"));
        EXPECT_TRUE(src.list_entries().empty());

        // delete_tree removes the whole project subtree.
        root.delete_tree(std::optional<m::pil::file_path>(
            m::pil::file_path(std::u16string_view(u"project"))));
        EXPECT_FALSE(std::filesystem::exists(tmp.path() / L"project"));
        EXPECT_TRUE(root.list_entries().empty());
    }

} // namespace
