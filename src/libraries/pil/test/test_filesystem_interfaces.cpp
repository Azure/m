// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>

//
// Exercises the filesystem interface contracts (ifilesystem / idirectory /
// ifile) against a minimal in-memory mock provider. The point is to validate
// that the disposition pattern — ec-form primitives, throwing wrappers, the
// enumerate/query_information convenience forms, and the tolerate_not_found
// tentative opens — composes correctly. The mock keeps a single-level child
// map keyed by the path text, which is enough to drive every wrapper.
//

namespace
{
    using m::pil::directory_entry;
    using m::pil::file_access;
    using m::pil::file_metadata;
    using m::pil::file_path;
    using m::pil::file_root;
    using m::pil::file_root_kind;
    using m::pil::idirectory;
    using m::pil::ifile;
    using m::pil::ifilesystem;
    using m::pil::node_kind;

    std::u16string
    key_of(file_path const& p)
    {
        return std::u16string(static_cast<std::u16string_view>(p.native()));
    }

    file_path
    fp(std::u16string_view v)
    {
        return file_path(v);
    }

    struct mock_file final : ifile
    {
        file_metadata m_metadata;

        explicit mock_file(file_metadata metadata): m_metadata(metadata) {}

        query_information_disposition
        query_information(query_information_flags, file_metadata& metadata) override
        {
            metadata = m_metadata;
            return {};
        }
    };

    struct mock_directory final : idirectory
    {
        struct node
        {
            node_kind                       m_kind = node_kind::file;
            std::shared_ptr<mock_directory> m_dir;
            std::shared_ptr<mock_file>      m_file;
            file_metadata                   m_metadata;
        };

        std::map<std::u16string, node> m_children;

        create_directory_disposition
        create_directory(create_directory_flags,
                         file_path const&             path,
                         file_access,
                         std::shared_ptr<idirectory>& returned_directory) override
        {
            auto          dir = std::make_shared<mock_directory>();
            file_metadata md;
            md.m_kind = node_kind::directory;

            node n;
            n.m_kind     = node_kind::directory;
            n.m_dir      = dir;
            n.m_metadata = md;

            m_children[key_of(path)] = n;
            returned_directory       = dir;
            return {};
        }

        create_file_disposition
        create_file(create_file_flags,
                    file_path const&        path,
                    file_access,
                    std::shared_ptr<ifile>& returned_file) override
        {
            file_metadata md;
            md.m_kind = node_kind::file;

            auto file = std::make_shared<mock_file>(md);

            node n;
            n.m_kind     = node_kind::file;
            n.m_file     = file;
            n.m_metadata = md;

            m_children[key_of(path)] = n;
            returned_file            = file;
            return {};
        }

        open_directory_disposition
        open_directory(open_directory_flags         flags,
                       file_path const&             path,
                       file_access,
                       std::shared_ptr<idirectory>& returned_directory,
                       std::error_code&             ec) override
        {
            auto const it = m_children.find(key_of(path));
            if (it == m_children.end() || it->second.m_kind != node_kind::directory)
            {
                if ((flags & open_directory_flags::tolerate_not_found) != open_directory_flags{})
                    return open_directory_result_code::not_found;

                ec = std::make_error_code(std::errc::no_such_file_or_directory);
                return {};
            }

            returned_directory = it->second.m_dir;
            return {};
        }

        open_file_disposition
        open_file(open_file_flags         flags,
                  file_path const&        path,
                  file_access,
                  std::shared_ptr<ifile>& returned_file,
                  std::error_code&        ec) override
        {
            auto const it = m_children.find(key_of(path));
            if (it == m_children.end() || it->second.m_kind != node_kind::file)
            {
                if ((flags & open_file_flags::tolerate_not_found) != open_file_flags{})
                    return open_file_result_code::not_found;

                ec = std::make_error_code(std::errc::no_such_file_or_directory);
                return {};
            }

            returned_file = it->second.m_file;
            return {};
        }

        remove_entry_disposition
        remove_entry(remove_entry_flags, file_path const& name) override
        {
            m_children.erase(key_of(name));
            return {};
        }

        delete_tree_disposition
        delete_tree(delete_tree_flags, std::optional<file_path> const& name) override
        {
            if (name.has_value())
                m_children.erase(key_of(name.value()));
            else
                m_children.clear();
            return {};
        }

        rename_entry_disposition
        rename_entry(rename_entry_flags, file_path const& old_path, file_path const& new_path) override
        {
            auto const it = m_children.find(key_of(old_path));
            if (it != m_children.end())
            {
                auto moved = it->second;
                m_children.erase(it);
                m_children[key_of(new_path)] = moved;
            }
            return {};
        }

        enumerate_entries_disposition
        enumerate_entries(enumerate_entries_flags,
                          std::size_t                                      starting_index,
                          std::span<directory_entry, std::dynamic_extent>& entries) override
        {
            std::size_t produced = 0;
            std::size_t index    = 0;
            for (auto const& [name, n]: m_children)
            {
                if (index >= starting_index && produced < entries.size())
                {
                    entries[produced] = directory_entry(
                        m::pil::file_name_string_type(m::pil::file_name_view_type(name)),
                        n.m_metadata);
                    ++produced;
                }
                ++index;
            }
            entries = entries.subspan(0, produced);
            return {};
        }

        query_information_disposition
        query_information(query_information_flags, file_metadata& metadata) override
        {
            metadata.m_kind = node_kind::directory;
            return {};
        }
    };

    struct mock_filesystem final : ifilesystem
    {
        std::shared_ptr<mock_directory> m_root = std::make_shared<mock_directory>();

        open_root_disposition
        open_root(open_root_flags,
                  file_root const&,
                  file_access,
                  std::shared_ptr<idirectory>& returned_directory) override
        {
            returned_directory = m_root;
            return {};
        }

        monitor_disposition
        monitor(monitor_flags,
                std::shared_ptr<m::pil::ifilesystem_monitor>& returned_filesystem_monitor) override
        {
            returned_filesystem_monitor.reset();
            return {};
        }
    };

    std::shared_ptr<idirectory>
    open_test_root(ifilesystem& fs)
    {
        return fs.open_root(file_root(file_root_kind::drive, std::u16string_view(u"C:")));
    }

    TEST(TestFilesystemInterfaces, OpenRootReturnsDirectory)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        ASSERT_NE(root, nullptr);
    }

    TEST(TestFilesystemInterfaces, CreateAndOpenDirectory)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);

        auto const created = root->create_directory(fp(u"sub"));
        ASSERT_NE(created, nullptr);

        auto const opened = root->open_directory(fp(u"sub"));
        ASSERT_NE(opened, nullptr);
    }

    TEST(TestFilesystemInterfaces, CreateAndOpenFile)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);

        auto const created = root->create_file(fp(u"leaf.txt"));
        ASSERT_NE(created, nullptr);

        auto const opened = root->open_file(fp(u"leaf.txt"));
        ASSERT_NE(opened, nullptr);
    }

    TEST(TestFilesystemInterfaces, OpenMissingDirectoryThrows)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        EXPECT_THROW((void)root->open_directory(fp(u"absent")), std::system_error);
    }

    TEST(TestFilesystemInterfaces, OpenMissingFileThrows)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        EXPECT_THROW((void)root->open_file(fp(u"absent.txt")), std::system_error);
    }

    TEST(TestFilesystemInterfaces, TryOpenMissingDirectoryReturnsNull)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        EXPECT_EQ(root->try_open_directory(fp(u"absent")), nullptr);
    }

    TEST(TestFilesystemInterfaces, TryOpenExistingDirectoryReturnsNode)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        root->create_directory(fp(u"sub"));
        EXPECT_NE(root->try_open_directory(fp(u"sub")), nullptr);
    }

    TEST(TestFilesystemInterfaces, TryOpenMissingFileReturnsNull)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        EXPECT_EQ(root->try_open_file(fp(u"absent.txt")), nullptr);
    }

    TEST(TestFilesystemInterfaces, RemoveEntry)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        root->create_file(fp(u"leaf.txt"));
        root->remove_entry(fp(u"leaf.txt"));
        EXPECT_EQ(root->try_open_file(fp(u"leaf.txt")), nullptr);
    }

    TEST(TestFilesystemInterfaces, DeleteTreeNamedChild)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        root->create_directory(fp(u"sub"));
        root->delete_tree(fp(u"sub"));
        EXPECT_EQ(root->try_open_directory(fp(u"sub")), nullptr);
    }

    TEST(TestFilesystemInterfaces, DeleteTreeWholeDirectory)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        root->create_directory(fp(u"a"));
        root->create_file(fp(u"b.txt"));
        root->delete_tree(std::nullopt);
        EXPECT_EQ(root->enumerate_entries(0), std::nullopt);
    }

    TEST(TestFilesystemInterfaces, RenameEntry)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        root->create_file(fp(u"old.txt"));
        root->rename_entry(fp(u"old.txt"), fp(u"new.txt"));
        EXPECT_EQ(root->try_open_file(fp(u"old.txt")), nullptr);
        EXPECT_NE(root->try_open_file(fp(u"new.txt")), nullptr);
    }

    TEST(TestFilesystemInterfaces, EnumerateEntriesWalksChildren)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        root->create_directory(fp(u"a"));
        root->create_file(fp(u"b.txt"));

        auto const e0 = root->enumerate_entries(0);
        auto const e1 = root->enumerate_entries(1);
        auto const e2 = root->enumerate_entries(2);

        ASSERT_TRUE(e0.has_value());
        ASSERT_TRUE(e1.has_value());
        EXPECT_FALSE(e2.has_value());

        // Children are stored sorted by name: "a" then "b.txt".
        EXPECT_EQ(e0->m_name, u"a");
        EXPECT_EQ(e0->m_kind, node_kind::directory);
        EXPECT_EQ(e1->m_name, u"b.txt");
        EXPECT_EQ(e1->m_kind, node_kind::file);
    }

    TEST(TestFilesystemInterfaces, EnumerateEmptyDirectory)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        EXPECT_EQ(root->enumerate_entries(0), std::nullopt);
    }

    TEST(TestFilesystemInterfaces, DirectoryQueryInformation)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        auto const      md   = root->query_information();
        EXPECT_EQ(md.m_kind, node_kind::directory);
    }

    TEST(TestFilesystemInterfaces, FileQueryInformation)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        auto const      file = root->create_file(fp(u"leaf.txt"));
        auto const      md   = file->query_information();
        EXPECT_EQ(md.m_kind, node_kind::file);
    }

    TEST(TestFilesystemInterfaces, FileReadContentDefaultsToNotSupported)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        auto const      file = root->create_file(fp(u"leaf.txt"));

        std::array<std::byte, 8> buffer{};
        std::size_t              bytes_read = 123;
        std::error_code          ec;
        auto const               d = file->read_content(
            ifile::read_content_flags{}, 0, std::span<std::byte>(buffer), bytes_read, ec);

        EXPECT_FALSE(d);
        EXPECT_EQ(bytes_read, std::size_t{0});
        EXPECT_EQ(ec, std::make_error_code(std::errc::not_supported));
    }

    TEST(TestFilesystemInterfaces, FileReadContentThrowingWrapperThrowsWhenUnsupported)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        auto const      file = root->create_file(fp(u"leaf.txt"));

        std::array<std::byte, 8> buffer{};
        EXPECT_THROW(file->read_content(0, std::span<std::byte>(buffer)), std::system_error);
    }

    TEST(TestFilesystemInterfaces, FileWriteContentDefaultsToNotSupported)
    {
        mock_filesystem fs;
        auto const      root = open_test_root(fs);
        auto const      file = root->create_file(fp(u"leaf.txt"));

        std::array<std::byte, 8> const buffer{};
        std::size_t                    bytes_written = 123;
        std::error_code                ec;
        auto const                     d = file->write_content(ifile::write_content_flags{},
                                          0,
                                          std::span<std::byte const>(buffer),
                                          bytes_written,
                                          ec);

        EXPECT_FALSE(d);
        EXPECT_EQ(bytes_written, std::size_t{0});
        EXPECT_EQ(ec, std::make_error_code(std::errc::not_supported));
    }

} // namespace
