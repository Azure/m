// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include <Windows.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>

#include "logging/logging.h"
#include "passthrough/passthrough.h"

using namespace std::string_view_literals;

#ifdef WIN32

//
// M-FS-LOG-1: the logging tap records Filesystem.* mutation entries (with the
// requested-vs-done shape) into the floating diagnostic <Log>, while reads pass
// through and the trace never lands in the persisted <Platform> (D6). The tap is
// placed at varied depths and must capture identically without altering the
// observable filesystem behavior.
//

namespace
{
    std::string
    read_file_text(std::filesystem::path const& p)
    {
        std::ifstream      in(p, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

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
            m_path          = base / (L"m_pil_fs_log_" + std::to_wstring(::GetCurrentProcessId()) +
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
    open_root_dir(m::pil::filesystem_class& fs, std::filesystem::path const& absolute)
    {
        auto const fp   = to_file_path(absolute);
        auto       root = fs.open_root(fp.root());
        auto const rel  = fp.relative_path();
        return root.open_directory(m::pil::file_path(rel));
    }

    // Apply an identical sequence of mutations through whatever stack `top`
    // represents against the given live directory, exercising every Filesystem.*
    // log entry kind (CreateDirectory, CreateFile, Rename, Remove, DeleteTree).
    // Returns the number of entries remaining in the base directory afterward so
    // callers can prove the tap depth did not alter behavior.
    std::size_t
    exercise_fs(std::shared_ptr<m::pil::iplatform> top, std::filesystem::path const& base_path)
    {
        m::pil::platform p(std::move(top));

        auto fs   = p.get_filesystem();
        auto base = open_root_dir(fs, base_path);

        auto sub = base.create_directory(u"sub"sv);  // Filesystem.CreateDirectory
        sub.create_file(u"f.txt"sv);                 // Filesystem.CreateFile

        base.rename_entry(m::pil::file_path(u"sub"sv),
                          m::pil::file_path(u"sub2"sv)); // Filesystem.Rename

        base.create_file(u"g.txt"sv);              // Filesystem.CreateFile
        base.remove_entry(u"g.txt"sv);             // Filesystem.Remove

        base.delete_tree(m::pil::file_path(u"sub2"sv)); // Filesystem.DeleteTree

        std::size_t remaining = 0;
        for ([[maybe_unused]] auto const& e: base.list_entries())
            ++remaining;
        return remaining;
    }

    void
    expect_all_filesystem_entries(std::string const& diag_text)
    {
        EXPECT_NE(diag_text.find("<DiagnosticLog"), std::string::npos);
        EXPECT_NE(diag_text.find("Filesystem.CreateDirectory"), std::string::npos);
        EXPECT_NE(diag_text.find("Filesystem.CreateFile"), std::string::npos);
        EXPECT_NE(diag_text.find("Filesystem.Rename"), std::string::npos);
        EXPECT_NE(diag_text.find("Filesystem.Remove"), std::string::npos);
        EXPECT_NE(diag_text.find("Filesystem.DeleteTree"), std::string::npos);
    }
} // namespace

// The logging tap floats at any depth: the same filesystem operations are issued
// against two live directories through stacks that differ only in where the
// logging layer sits (directly above the live leaf, and beneath a transparent
// passthrough layer). The Filesystem.* trace is captured at both depths and the
// observable behavior is identical.
TEST(LoggingFilesystem, TapCapturesAtAnyDepthWithoutAlteringBehavior)
{
    scoped_temp_dir const tmp_top;
    scoped_temp_dir const tmp_mid;

    auto const diag_top = std::filesystem::temp_directory_path() / "mfslog_diag_top.xml";
    auto const diag_mid = std::filesystem::temp_directory_path() / "mfslog_diag_mid.xml";

    std::error_code ec;
    std::filesystem::remove(diag_top, ec);
    std::filesystem::remove(diag_mid, ec);

    // Stack A: logging tap directly above the live leaf.
    std::size_t remaining_a = 0;
    {
        auto leaf = m::pil::make_platform_interface();
        auto tap  = std::make_shared<m::pil::impl::logging::platform>(leaf);

        std::shared_ptr<m::pil::iplatform> top = tap;
        remaining_a                            = exercise_fs(top, tmp_top.path());

        m::pil::platform p(std::move(top));
        p.save_diagnostic_log(diag_top);
    }

    // Stack B: logging tap beneath a transparent passthrough layer.
    std::size_t remaining_b = 0;
    {
        auto leaf = m::pil::make_platform_interface();
        auto tap  = std::make_shared<m::pil::impl::logging::platform>(leaf);
        auto outer = std::make_shared<m::pil::impl::passthrough::platform>(
            std::static_pointer_cast<m::pil::iplatform>(tap));

        std::shared_ptr<m::pil::iplatform> top = outer;
        remaining_b                            = exercise_fs(top, tmp_mid.path());

        m::pil::platform p(std::move(top));
        p.save_diagnostic_log(diag_mid);
    }

    // Behavior is unaltered by tap depth: both directories are empty afterward,
    // matching the std::filesystem ground truth.
    EXPECT_EQ(remaining_a, 0u);
    EXPECT_EQ(remaining_b, 0u);
    EXPECT_TRUE(std::filesystem::is_empty(tmp_top.path()));
    EXPECT_TRUE(std::filesystem::is_empty(tmp_mid.path()));

    expect_all_filesystem_entries(read_file_text(diag_top));
    expect_all_filesystem_entries(read_file_text(diag_mid));

    std::filesystem::remove(diag_top, ec);
    std::filesystem::remove(diag_mid, ec);
}

// D6: the persisted <Platform> must not carry the diagnostic log; the
// requested-vs-done filesystem trace is only obtainable from the separate side
// artifact written by save_diagnostic_log.
TEST(LoggingFilesystem, DiagnosticLogIsSideArtifactNotInPersistedPlatform)
{
    scoped_temp_dir const tmp;

    auto const platform_out = std::filesystem::temp_directory_path() / "mfslog_platform.xml";
    auto const diag_out     = std::filesystem::temp_directory_path() / "mfslog_diag.xml";

    std::error_code ec;
    std::filesystem::remove(platform_out, ec);
    std::filesystem::remove(diag_out, ec);

    {
        auto leaf = m::pil::make_platform_interface();
        auto tap  = std::make_shared<m::pil::impl::logging::platform>(leaf);

        std::shared_ptr<m::pil::iplatform> top = tap;
        (void)exercise_fs(top, tmp.path());

        m::pil::platform p(std::move(top));
        p.save(platform_out);
        p.save_diagnostic_log(diag_out);
    }

    auto const platform_text = read_file_text(platform_out);
    auto const diag_text     = read_file_text(diag_out);

    // The persisted platform carries no log and no filesystem mutation trace.
    EXPECT_NE(platform_text.find("<Platform"), std::string::npos);
    EXPECT_EQ(platform_text.find("<Log"), std::string::npos);
    EXPECT_EQ(platform_text.find("Filesystem.CreateDirectory"), std::string::npos);
    EXPECT_EQ(platform_text.find("Filesystem.DeleteTree"), std::string::npos);

    // The side diagnostic artifact carries the requested-vs-done trace.
    EXPECT_NE(diag_text.find("<Log"), std::string::npos);
    expect_all_filesystem_entries(diag_text);

    std::filesystem::remove(platform_out, ec);
    std::filesystem::remove(diag_out, ec);
}

#endif // WIN32
