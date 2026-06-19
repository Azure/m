// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/exception/exception.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>

#include "buffered/buffered.h"

//
// M-FS-BUF-1: the buffered filesystem overlay's node model and read path. The
// overlay captures a directory whole on touch (names + kinds + metadata of its
// direct children, plus its own metadata) and serves those reads without
// re-reading the underlying provider — so an entry survives deletion of the
// live node after capture. These tests construct a buffered::directory directly
// over a live temp-tree handle (cheap, deterministic) and exercise enumerate,
// query_information, and open_directory/open_file.
//

namespace
{
    namespace bufimpl = m::pil::impl::buffered;

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
            m_path = base / (L"m_pil_fs_buf_" + std::to_wstring(::GetCurrentProcessId()) + L"_" +
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

    // A live (direct) idirectory handle for an absolute directory, obtained
    // without any buffering so capturing it is cheap and deterministic.
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

    std::set<std::u16string>
    overlay_child_names(m::pil::idirectory& dir)
    {
        std::set<std::u16string> names;
        for (std::size_t index = 0;; ++index)
        {
            auto const entry = dir.enumerate_entries(index);
            if (!entry.has_value())
                break;
            names.insert(name_of(entry.value()));
        }
        return names;
    }

    TEST(BufferedFilesystem, CaptureEnumerateMatchesGroundTruth)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"alpha");
        std::filesystem::create_directory(tmp.path() / L"beta");
        {
            std::ofstream f(tmp.path() / L"gamma.txt");
            f << "hello";
        }

        auto overlay = std::shared_ptr<m::pil::idirectory>(
            std::make_shared<bufimpl::directory>(live_directory(tmp.path())));

        EXPECT_EQ(overlay_child_names(*overlay),
                  (std::set<std::u16string>{u"alpha", u"beta", u"gamma.txt"}));
    }

    TEST(BufferedFilesystem, OwnAndChildMetadataCaptured)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"child");
        std::string const contents = "0123456789";
        {
            std::ofstream f(tmp.path() / L"data.bin", std::ios::binary);
            f << contents;
        }

        auto overlay = std::shared_ptr<m::pil::idirectory>(
            std::make_shared<bufimpl::directory>(live_directory(tmp.path())));

        EXPECT_TRUE(overlay->query_information().is_directory());

        // A captured subdirectory entry opens as a directory; a captured file
        // entry opens as a file carrying its captured size.
        auto child = overlay->open_directory(std::u16string_view(u"child"));
        EXPECT_TRUE(child->query_information().is_directory());

        auto data = overlay->open_file(std::u16string_view(u"data.bin"));
        auto md   = data->query_information();
        EXPECT_TRUE(md.is_file());
        EXPECT_EQ(md.m_size, contents.size());
    }

    TEST(BufferedFilesystem, WrongKindRejected)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"adir");
        {
            std::ofstream f(tmp.path() / L"afile");
            f << "x";
        }

        auto overlay = std::shared_ptr<m::pil::idirectory>(
            std::make_shared<bufimpl::directory>(live_directory(tmp.path())));

        // Opening a file through open_directory (and a directory through
        // open_file) is rejected by the unified namespace, even tentatively.
        std::error_code             ec;
        std::shared_ptr<m::pil::idirectory> as_dir;
        overlay->open_directory(m::pil::idirectory::open_directory_flags::tolerate_not_found,
                                m::pil::file_path(std::u16string_view(u"afile")),
                                m::pil::file_access::default_open,
                                as_dir,
                                ec);
        EXPECT_TRUE(static_cast<bool>(ec));
        EXPECT_FALSE(as_dir);

        ec.clear();
        std::shared_ptr<m::pil::ifile> as_file;
        overlay->open_file(m::pil::idirectory::open_file_flags::tolerate_not_found,
                           m::pil::file_path(std::u16string_view(u"adir")),
                           m::pil::file_access::default_open,
                           as_file,
                           ec);
        EXPECT_TRUE(static_cast<bool>(ec));
        EXPECT_FALSE(as_file);
    }

    TEST(BufferedFilesystem, TentativeOpenMissing)
    {
        scoped_temp_dir const tmp;

        auto overlay = std::shared_ptr<m::pil::idirectory>(
            std::make_shared<bufimpl::directory>(live_directory(tmp.path())));

        EXPECT_FALSE(overlay->try_open_directory(m::pil::file_path(std::u16string_view(u"nope"))));
        EXPECT_FALSE(overlay->try_open_file(m::pil::file_path(std::u16string_view(u"nope.txt"))));
    }

    TEST(BufferedFilesystem, CaptureSurvivesUnderlyingDeletion)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"keepdir");
        std::string const contents = "abcdef";
        {
            std::ofstream f(tmp.path() / L"vanish.bin", std::ios::binary);
            f << contents;
        }

        // Capture on touch.
        auto overlay = std::shared_ptr<m::pil::idirectory>(
            std::make_shared<bufimpl::directory>(live_directory(tmp.path())));

        // Now mutate the live tree out from under the overlay.
        std::filesystem::remove(tmp.path() / L"vanish.bin");

        // The captured namespace still reports the vanished file with its
        // captured metadata; the overlay never re-reads the underlying.
        EXPECT_EQ(overlay_child_names(*overlay),
                  (std::set<std::u16string>{u"keepdir", u"vanish.bin"}));

        auto data = overlay->open_file(std::u16string_view(u"vanish.bin"));
        EXPECT_EQ(data->query_information().m_size, contents.size());
    }

    TEST(BufferedFilesystem, MultiSegmentOpenWalksOverlay)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directories(tmp.path() / L"a" / L"b");
        {
            std::ofstream f(tmp.path() / L"a" / L"b" / L"leaf.txt");
            f << "z";
        }

        auto overlay = std::shared_ptr<m::pil::idirectory>(
            std::make_shared<bufimpl::directory>(live_directory(tmp.path())));

        auto leaf = overlay->open_file(m::pil::file_path(std::u16string_view(u"a\\b\\leaf.txt")));
        EXPECT_TRUE(leaf->query_information().is_file());

        auto b = overlay->open_directory(m::pil::file_path(std::u16string_view(u"a\\b")));
        EXPECT_EQ(overlay_child_names(*b), (std::set<std::u16string>{u"leaf.txt"}));
    }

    // Validates the platform -> get_filesystem -> open_root wiring of the
    // buffered facet over a live platform (capture is non-recursive, so this
    // only enumerates the drive root's direct children).
    TEST(BufferedFilesystem, OpenRootWiringOverLivePlatform)
    {
        auto inner = m::pil::make_platform_interface();
        std::shared_ptr<m::pil::iplatform> layered =
            std::make_shared<bufimpl::platform>(inner);

        std::shared_ptr<m::pil::ifilesystem> fs;
        layered->get_filesystem(m::pil::iplatform::get_filesystem_flags{}, fs);
        ASSERT_TRUE(static_cast<bool>(fs));

        scoped_temp_dir const tmp;
        auto const            fp = to_file_path(tmp.path());

        auto root = fs->open_root(fp.root(), m::pil::file_access::default_open);
        ASSERT_TRUE(static_cast<bool>(root));

        // The drive root captured a non-empty namespace.
        EXPECT_FALSE(overlay_child_names(*root).empty());

        // Opening the same root again returns the cached overlay directory.
        auto root_again = fs->open_root(fp.root(), m::pil::file_access::default_open);
        EXPECT_EQ(root.get(), root_again.get());
    }

    //
    // M-FS-BUF-2: namespace mutations in the overlay. These exercise the
    // mutation verbs directly against an empty (underlying-less) overlay
    // directory so the behavior is purely in-overlay and deterministic:
    // create directory/file (single- and multi-segment, create-or-open),
    // remove (file, empty directory, non-empty rejected), delete_tree (named
    // subtree and whole contents), and rename/move (re-keying the entry).
    //

    m::pil::file_path
    rel(std::u16string_view s)
    {
        return m::pil::file_path(s);
    }

    std::shared_ptr<m::pil::idirectory>
    empty_overlay()
    {
        m::pil::file_metadata md;
        md.m_kind       = m::pil::node_kind::directory;
        md.m_attributes = m::pil::file_attributes::directory;
        return std::make_shared<bufimpl::directory>(md, nullptr);
    }

    TEST(BufferedFilesystem, CreateDirectorySingleAndMultiSegment)
    {
        auto root = empty_overlay();

        auto a = root->create_directory(rel(u"alpha"));
        ASSERT_TRUE(static_cast<bool>(a));
        EXPECT_TRUE(a->query_information().is_directory());

        // A multi-segment create auto-creates the intermediate components.
        auto deep = root->create_directory(rel(u"x\\y\\z"));
        ASSERT_TRUE(static_cast<bool>(deep));

        EXPECT_EQ(overlay_child_names(*root), (std::set<std::u16string>{u"alpha", u"x"}));
        EXPECT_TRUE(static_cast<bool>(root->try_open_directory(rel(u"x\\y\\z"))));
    }

    TEST(BufferedFilesystem, CreateFileSingleAndMultiSegment)
    {
        auto root = empty_overlay();

        auto f = root->create_file(rel(u"note.txt"));
        ASSERT_TRUE(static_cast<bool>(f));
        m::pil::file_metadata md;
        f->query_information(m::pil::ifile::query_information_flags{}, md);
        EXPECT_TRUE(md.is_file());

        // A multi-segment create makes the leading directories then the leaf file.
        auto deep = root->create_file(rel(u"docs\\sub\\readme.md"));
        ASSERT_TRUE(static_cast<bool>(deep));

        EXPECT_EQ(overlay_child_names(*root), (std::set<std::u16string>{u"note.txt", u"docs"}));
        EXPECT_TRUE(static_cast<bool>(root->try_open_file(rel(u"docs\\sub\\readme.md"))));
    }

    TEST(BufferedFilesystem, CreateOrOpenIdempotentAndKindConflicts)
    {
        auto root = empty_overlay();

        auto d1 = root->create_directory(rel(u"d"));
        auto d2 = root->create_directory(rel(u"d")); // create-or-open: no throw
        EXPECT_TRUE(static_cast<bool>(d1));
        EXPECT_TRUE(static_cast<bool>(d2));

        auto f1 = root->create_file(rel(u"f"));
        auto f2 = root->create_file(rel(u"f")); // create-or-open: no throw
        EXPECT_TRUE(static_cast<bool>(f1));
        EXPECT_TRUE(static_cast<bool>(f2));

        // Unified namespace (D13): a name taken by one kind rejects the other.
        EXPECT_THROW(static_cast<void>(root->create_file(rel(u"d"))), m::already_exists);
        EXPECT_THROW(static_cast<void>(root->create_directory(rel(u"f"))), m::already_exists);
    }

    TEST(BufferedFilesystem, RemoveEntryFileEmptyDirAndNonEmptyRejected)
    {
        auto root = empty_overlay();

        root->create_file(rel(u"f"));
        root->remove_entry(rel(u"f"));
        EXPECT_FALSE(static_cast<bool>(root->try_open_file(rel(u"f"))));

        root->create_directory(rel(u"empty"));
        root->remove_entry(rel(u"empty"));
        EXPECT_FALSE(static_cast<bool>(root->try_open_directory(rel(u"empty"))));

        // A non-empty directory is rejected by remove_entry.
        root->create_file(rel(u"p\\q"));
        EXPECT_THROW(root->remove_entry(rel(u"p")), m::not_empty);
        EXPECT_TRUE(static_cast<bool>(root->try_open_directory(rel(u"p"))));

        // Removing a missing entry throws not_found.
        EXPECT_THROW(root->remove_entry(rel(u"missing")), m::not_found);
    }

    TEST(BufferedFilesystem, DeleteTreeNamedAndWholeContents)
    {
        auto root = empty_overlay();

        // A single tombstone hides a whole subtree (M-BUFTREE).
        root->create_file(rel(u"a\\b\\c\\leaf.txt"));
        root->delete_tree(std::optional<m::pil::file_path>(rel(u"a")));
        EXPECT_FALSE(static_cast<bool>(root->try_open_directory(rel(u"a"))));
        EXPECT_FALSE(static_cast<bool>(root->try_open_file(rel(u"a\\b\\c\\leaf.txt"))));

        // Empty the directory's whole contents but keep the directory itself.
        root->create_directory(rel(u"one"));
        root->create_file(rel(u"two"));
        root->delete_tree(std::optional<m::pil::file_path>{});
        EXPECT_TRUE(overlay_child_names(*root).empty());
    }

    TEST(BufferedFilesystem, RenameWithinDirectoryRekeysEntry)
    {
        auto root = empty_overlay();

        root->create_file(rel(u"old"));
        root->rename_entry(rel(u"old"), rel(u"new"));

        EXPECT_EQ(overlay_child_names(*root), (std::set<std::u16string>{u"new"}));
        EXPECT_TRUE(static_cast<bool>(root->try_open_file(rel(u"new"))));
        EXPECT_FALSE(static_cast<bool>(root->try_open_file(rel(u"old"))));
    }

    TEST(BufferedFilesystem, RenameAcrossDirectoriesMovesEntry)
    {
        auto root = empty_overlay();

        root->create_directory(rel(u"src"));
        root->create_file(rel(u"src\\f"));
        root->create_directory(rel(u"dst"));

        root->rename_entry(rel(u"src\\f"), rel(u"dst\\f"));

        EXPECT_TRUE(static_cast<bool>(root->try_open_file(rel(u"dst\\f"))));

        // The source slot is now empty.
        auto src = root->try_open_directory(rel(u"src"));
        ASSERT_TRUE(static_cast<bool>(src));
        EXPECT_TRUE(overlay_child_names(*src).empty());
    }

} // namespace

