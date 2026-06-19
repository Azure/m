// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include <m/pil/filesystem_base_types.h>

namespace
{
    using m::pil::directory_entry;
    using m::pil::file_access;
    using m::pil::file_attributes;
    using m::pil::file_metadata;
    using m::pil::node_kind;

    m::pil::file_name_string_type
    nm(m::pil::file_name_view_type v)
    {
        return m::pil::file_name_string_type(v);
    }

    TEST(TestFilesystemBaseTypes, NodeKindValues)
    {
        EXPECT_NE(node_kind::directory, node_kind::file);
    }

    TEST(TestFilesystemBaseTypes, MetadataDefaultsToFile)
    {
        file_metadata md;
        EXPECT_EQ(md.m_kind, node_kind::file);
        EXPECT_EQ(md.m_size, 0u);
        EXPECT_EQ(md.m_attributes, file_attributes::none);
        EXPECT_TRUE(md.is_file());
        EXPECT_FALSE(md.is_directory());
    }

    TEST(TestFilesystemBaseTypes, MetadataDirectoryPredicate)
    {
        file_metadata md;
        md.m_kind = node_kind::directory;
        EXPECT_TRUE(md.is_directory());
        EXPECT_FALSE(md.is_file());
    }

    TEST(TestFilesystemBaseTypes, AttributeFlagValues)
    {
        EXPECT_EQ(static_cast<std::uint32_t>(file_attributes::read_only), 0x00000001u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_attributes::hidden), 0x00000002u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_attributes::system), 0x00000004u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_attributes::directory), 0x00000010u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_attributes::archive), 0x00000020u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_attributes::normal), 0x00000080u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_attributes::reparse_point), 0x00000400u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_attributes::encrypted), 0x00004000u);
    }

    TEST(TestFilesystemBaseTypes, AttributeBitflagOps)
    {
        auto const combined = file_attributes::read_only | file_attributes::hidden;
        EXPECT_EQ(static_cast<std::uint32_t>(combined), 0x00000003u);
        EXPECT_NE((combined & file_attributes::read_only), file_attributes::none);
        EXPECT_NE((combined & file_attributes::hidden), file_attributes::none);
        EXPECT_EQ((combined & file_attributes::system), file_attributes::none);
    }

    TEST(TestFilesystemBaseTypes, AccessValues)
    {
        EXPECT_EQ(static_cast<std::uint32_t>(file_access::read), 0x00000001u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_access::write), 0x00000002u);
        EXPECT_EQ(static_cast<std::uint32_t>(file_access::read_write), 0x00000003u);
    }

    TEST(TestFilesystemBaseTypes, AccessReadWriteIsReadOrWrite)
    {
        EXPECT_EQ(file_access::read | file_access::write, file_access::read_write);
    }

    TEST(TestFilesystemBaseTypes, AccessDefaults)
    {
        EXPECT_EQ(file_access::default_open, file_access::read);
        EXPECT_EQ(file_access::default_create, file_access::read_write);
    }

    TEST(TestFilesystemBaseTypes, DirectoryEntryDefault)
    {
        directory_entry entry;
        EXPECT_TRUE(entry.m_name.empty());
        EXPECT_EQ(entry.m_kind, node_kind::file);
    }

    TEST(TestFilesystemBaseTypes, DirectoryEntryConstruction)
    {
        file_metadata md;
        md.m_kind = node_kind::directory;
        md.m_size = 0;

        directory_entry entry(nm(u"sub"), md);
        EXPECT_EQ(entry.m_name, u"sub");
        EXPECT_EQ(entry.m_kind, node_kind::directory);
        EXPECT_EQ(entry.m_metadata.m_kind, node_kind::directory);
    }

    TEST(TestFilesystemBaseTypes, DirectoryEntryKindMirrorsMetadata)
    {
        file_metadata md;
        md.m_kind = node_kind::file;
        md.m_size = 42;

        directory_entry entry(nm(u"leaf.txt"), md);
        EXPECT_EQ(entry.m_kind, node_kind::file);
        EXPECT_EQ(entry.m_metadata.m_size, 42u);
    }

    TEST(TestFilesystemBaseTypes, DirectoryEntrySwap)
    {
        file_metadata dir_md;
        dir_md.m_kind = node_kind::directory;

        file_metadata file_md;
        file_md.m_kind = node_kind::file;
        file_md.m_size = 7;

        directory_entry a(nm(u"alpha"), dir_md);
        directory_entry b(nm(u"beta"), file_md);

        swap(a, b);

        EXPECT_EQ(a.m_name, u"beta");
        EXPECT_EQ(a.m_kind, node_kind::file);
        EXPECT_EQ(a.m_metadata.m_size, 7u);

        EXPECT_EQ(b.m_name, u"alpha");
        EXPECT_EQ(b.m_kind, node_kind::directory);
    }

} // namespace
