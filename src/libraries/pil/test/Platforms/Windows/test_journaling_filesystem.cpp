// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

#include <pugixml.hpp>

#include <Windows.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/platform_interfaces.h>

#include "journaling/journaling.h"

using namespace std::string_view_literals;

#ifdef WIN32

//
// M-FS-JOURNAL-1: ordered replay of the filesystem namespace verbs. A mutation
// sequence (including a rename/move and a delete_tree) is recorded through the
// journaling decorator over the live provider; the recorded <Journal> is then
// replayed onto a freshly emptied base at the same location, and the resulting
// namespace is asserted observably equivalent to the one the source produced.
// Per D14 only namespace mutations are journaled (no file content).
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

    class scoped_temp_dir
    {
    public:
        scoped_temp_dir()
        {
            auto const base = std::filesystem::temp_directory_path();
            m_path = base / (L"m_pil_fs_journal_" + std::to_wstring(::GetCurrentProcessId()) +
                             L"_" + std::to_wstring(s_counter++));
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

    m::pil::directory
    open_dir(m::pil::filesystem_class& fs, std::filesystem::path const& absolute)
    {
        auto const fp   = to_file_path(absolute);
        auto       root = fs.open_root(fp.root());
        return root.open_directory(m::pil::file_path(fp.relative_path()));
    }

    // Ground-truth namespace of a directory tree, captured directly through
    // std::filesystem (independent of the PIL). Each entry is the path relative
    // to base, with a trailing '/' marking a directory so kind is part of the
    // comparison.
    std::set<std::wstring>
    snapshot_namespace(std::filesystem::path const& base)
    {
        std::set<std::wstring> names;
        for (auto const& e: std::filesystem::recursive_directory_iterator(base))
        {
            auto rel = std::filesystem::relative(e.path(), base).generic_wstring();
            if (e.is_directory())
                rel.push_back(L'/');
            names.insert(rel);
        }
        return names;
    }
} // namespace

TEST(JournalingFilesystem, RecordReplayProducesObservableEquivalence)
{
    scoped_temp_dir const tmp;

    // --- SOURCE: journaling decorator over the live provider. ---
    auto journaling_plat = std::make_shared<m::pil::impl::journaling::platform>(
        m::pil::make_platform_interface());

    {
        m::pil::platform p{std::shared_ptr<m::pil::iplatform>(journaling_plat)};

        auto fs   = p.get_filesystem();
        auto base = open_dir(fs, tmp.path());

        // A directory with a file, then a rename/move of the whole subtree.
        auto moved_from = base.create_directory(u"moved_from"sv);
        moved_from.create_file(u"a.txt"sv);
        base.rename_entry(m::pil::file_path(u"moved_from"sv), m::pil::file_path(u"moved_to"sv));

        // A non-empty subtree deleted wholesale: replay must remove it entirely.
        auto doomed = base.create_directory(u"doomed"sv);
        doomed.create_file(u"x.txt"sv);
        base.delete_tree(m::pil::file_path(u"doomed"sv));

        // A surviving subtree.
        auto kept = base.create_directory(u"kept"sv);
        kept.create_file(u"k.txt"sv);
    }

    // The namespace the source produced, captured as ground truth.
    auto const expected = snapshot_namespace(tmp.path());
    EXPECT_EQ(expected,
              (std::set<std::wstring>{L"moved_to/", L"moved_to/a.txt", L"kept/", L"kept/k.txt"}));

    // Capture the recorded verb stream as a standalone <Journal> artifact.
    pugi::xml_document journal_doc;
    auto               journal_root = journal_doc.append_child(L"Journal");
    journaling_plat->save_journal(journal_root);

    // --- Reset to a fresh, empty base at the same location. ---
    std::filesystem::remove_all(tmp.path());
    std::filesystem::create_directories(tmp.path());
    ASSERT_TRUE(snapshot_namespace(tmp.path()).empty());

    // --- TARGET: replay onto a fresh live filesystem (no source state). ---
    auto target_platform = m::pil::make_platform_interface();

    std::shared_ptr<m::pil::ifilesystem> target_fs;
    target_platform->get_filesystem(m::pil::iplatform::get_filesystem_flags{}, target_fs);

    m::pil::impl::journaling::replay(journal_root, *target_fs);

    // --- Observable namespace equivalence after replay. ---
    EXPECT_EQ(snapshot_namespace(tmp.path()), expected);
}

#endif // WIN32
