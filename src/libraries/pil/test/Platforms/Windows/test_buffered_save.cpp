// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <m/pil/file_path.h>
#include <m/pil/filesystem.h>
#include <m/pil/pil.h>
#include <m/pil/registry.h>

using namespace std::string_view_literals;

#ifdef WIN32

#include <Windows.h>

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

    // A throwaway directory tree under %TEMP%, removed on destruction.
    class scoped_temp_dir
    {
    public:
        scoped_temp_dir()
        {
            auto const base = std::filesystem::temp_directory_path();
            m_path = base / (L"m_pil_fs_buf3_" + std::to_wstring(::GetCurrentProcessId()) + L"_" +
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

    std::set<std::u16string>
    child_names(m::pil::directory& dir)
    {
        std::set<std::u16string> names;
        for (auto&& e: dir.list_entries())
            names.insert(std::u16string(e.m_name.view()));
        return names;
    }
} // namespace

// M4-3.1: a buffered overlay's created keys and set values are serialized to XML
// by the public save() path. Under the whole-key snapshot model (D2, D3) a
// touched key also serializes its observed subkey names and metadata, so this
// test asserts only the positive presence of the created key and values.
TEST(BufferedSave, OverlayKeysAndValuesAreSerialized)
{
    auto const      out = std::filesystem::temp_directory_path() /
                     "m4_3_1_overlay_keys_and_values.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    {
        auto p     = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r     = p.get_registry();
        auto k1    = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k_app = k1.create_key(L"M4_3_1_TestApp"sv);

        k_app.set_string_value(L"name", L"Joe");
        k_app.set_value(L"age", 24u);

        p.save(out);
    }

    auto const text = read_file_text(out);

    // The predefined key is stored under its canonical short name.
    EXPECT_NE(text.find("name=\"HKCU\""), std::string::npos);

    // The created subkey is materialized and therefore serialized.
    EXPECT_NE(text.find("name=\"M4_3_1_TestApp\""), std::string::npos);

    // The string value: REG_SZ == type 1.
    EXPECT_NE(text.find("name=\"name\""), std::string::npos);
    EXPECT_NE(text.find("type=\"1\""), std::string::npos);

    // The dword value: REG_DWORD == type 4, value 24 == 0x18 little-endian.
    EXPECT_NE(text.find("name=\"age\""), std::string::npos);
    EXPECT_NE(text.find("type=\"4\""), std::string::npos);
    EXPECT_NE(text.find("data=\"18000000\""), std::string::npos);

    std::filesystem::remove(out, ec);
}

// M4-3.1: a deleted value produces a tombstone element so that loading can
// faithfully reconstruct the overlay.
TEST(BufferedSave, DeletedValueIsSerializedAsTombstone)
{
    auto const      out = std::filesystem::temp_directory_path() /
                     "m4_3_1_deleted_value_tombstone.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    {
        auto p     = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r     = p.get_registry();
        auto k1    = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k_app = k1.create_key(L"M4_3_1_TombstoneApp"sv);

        k_app.set_value(L"doomed", 7u);
        k_app.delete_value(L"doomed"sv);

        p.save(out);
    }

    auto const text = read_file_text(out);

    EXPECT_NE(text.find("name=\"M4_3_1_TombstoneApp\""), std::string::npos);
    EXPECT_NE(text.find("name=\"doomed\""), std::string::npos);
    EXPECT_NE(text.find("deleted=\"true\""), std::string::npos);

    std::filesystem::remove(out, ec);
}

// M4-3.2: a saved overlay round-trips through load() into a snapshot platform
// that serves the keys and values without any live underlying registry.
TEST(BufferedSave, OverlayRoundTripsThroughLoad)
{
    auto const      out = std::filesystem::temp_directory_path() /
                     "m4_3_2_roundtrip.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    {
        auto p     = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r     = p.get_registry();
        auto k1    = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k_app = k1.create_key(L"M4_3_2_RoundTrip"sv);

        k_app.set_value(L"age", 24u);
        k_app.set_string_value(L"name", L"Joe");

        auto k_sub = k_app.create_key(L"Sub"sv);
        k_sub.set_value(L"answer", 42u);

        p.save(out);
    }

    auto snap  = m::pil::load_platform(out);
    auto r     = snap.get_registry();
    auto k1    = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k_app = k1.open_key(L"M4_3_2_RoundTrip"sv);

    EXPECT_EQ(k_app.get_uint32_value(L"age"sv), 24u);
    EXPECT_EQ(k_app.get_string_value(L"name"sv), L"Joe");

    auto k_sub = k_app.open_key(L"Sub"sv);
    EXPECT_EQ(k_sub.get_uint32_value(L"answer"sv), 42u);

    std::filesystem::remove(out, ec);
}

// M-PS-3: a key that was merely observed (touched, not modified) serializes as
// a whole-key snapshot — its own metadata (last_write_time) and its child
// subkey names appear in the artifact even though nothing was written to it
// through the buffered layer (D2, D3).
TEST(BufferedSave, ObservedKeyMetadataAndSubkeyNamesSerialized)
{
    static constexpr auto k_subkey = L"MPS3_Observed"sv;

    auto const      out = std::filesystem::temp_directory_path() /
                     "mps3_observed_whole_key.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    // Stage a real HKCU subkey through a direct platform so the buffered
    // capture has a deterministic child name to enumerate.
    {
        auto p    = m::pil::make_platform();
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        try
        {
            hkcu.delete_tree(k_subkey);
        }
        catch (...)
        {
        }
        auto k = hkcu.create_key(k_subkey);
        k.set_value(L"marker"sv, 1u);
    }

    {
        // Open (touch) HKCU through the buffered layer; eager whole-key capture
        // records HKCU's metadata and the names of its child subkeys. We never
        // open or modify MPS3_Observed.
        auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        (void)hkcu;

        p.save(out);
    }

    auto const text = read_file_text(out);

    // The observed subkey name is serialized even though it was never opened.
    EXPECT_NE(text.find("name=\"MPS3_Observed\""), std::string::npos);

    // The touched key's own metadata is serialized.
    EXPECT_NE(text.find("last_write_time="), std::string::npos);

    // Cleanup the staged key.
    {
        auto p    = m::pil::make_platform();
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        try
        {
            hkcu.delete_tree(k_subkey);
        }
        catch (...)
        {
        }
    }

    std::filesystem::remove(out, ec);
}

// M-PS-4: a key that was merely *observed* (captured from the real registry
// through the buffered layer, never explicitly written) is fully readable from
// the loaded snapshot with no live underlying registry. We prove the absence of
// fall-through by deleting the real key before loading: any read that reached a
// live registry would now fail.
TEST(BufferedSave, ObservedKeyReadableFromSealedSnapshot)
{
    static constexpr auto k_subkey = L"MPS4_Sealed"sv;

    auto const      out = std::filesystem::temp_directory_path() / "mps4_sealed.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    auto const remove_staged = []() {
        auto p    = m::pil::make_platform();
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        try
        {
            hkcu.delete_tree(k_subkey);
        }
        catch (...)
        {
        }
    };

    // Stage a real key with values and a child subkey through a direct platform.
    remove_staged();
    {
        auto p     = m::pil::make_platform();
        auto r     = p.get_registry();
        auto hkcu  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k     = hkcu.create_key(k_subkey);
        k.set_value(L"count"sv, 7u);
        k.set_string_value(L"label"sv, L"observed");
        auto child = k.create_key(L"Child"sv);
        child.set_value(L"x"sv, 1u);
    }

    // Run #1: observe the key through the buffered layer (eager whole-key
    // capture) and save the snapshot.
    {
        auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k    = hkcu.open_key(k_subkey); // touch -> capture whole key
        EXPECT_EQ(k.get_uint32_value(L"count"sv), 7u);

        p.save(out);
    }

    // Delete the real key entirely: the snapshot must now be the only source.
    remove_staged();

    // Run #2: load the sealed snapshot (no underlying) and read the observed
    // key's captured state.
    {
        auto snap = m::pil::load_platform(out);
        auto r    = snap.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k    = hkcu.open_key(k_subkey);

        EXPECT_EQ(k.get_uint32_value(L"count"sv), 7u);
        EXPECT_EQ(k.get_string_value(L"label"sv), L"observed");

        // The child subkey name survives into the sealed world's enumeration.
        // Its contents were never captured, so it is a name-only placeholder;
        // the open-time repair behavior for such placeholders is covered by
        // M-PS-5.
        bool found_child = false;
        for (auto&& n: k.list_subkey_names())
            if (n == m::pil::key_path(L"Child"sv))
                found_child = true;
        EXPECT_TRUE(found_child);
    }

    std::filesystem::remove(out, ec);
}

// M-PS-5: lazy consistency repair (D5). A name-only placeholder subkey
// enumerates in the sealed world but cannot be opened (its contents were never
// captured and there is no underlying registry). Opening it must drop it from
// the enumeration and advance the parent's last_write_time to T_load, leaving
// the snapshot self-consistent.
TEST(BufferedSave, NameOnlySubkeyRepairedAndRestampedOnOpen)
{
    static constexpr auto k_subkey = L"MPS5_Repair"sv;

    auto const      out = std::filesystem::temp_directory_path() / "mps5_repair.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    auto const remove_staged = []() {
        auto p    = m::pil::make_platform();
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        try
        {
            hkcu.delete_tree(k_subkey);
        }
        catch (...)
        {
        }
    };

    // Stage a real key with one value and a child subkey "Ghost".
    remove_staged();
    {
        auto p     = m::pil::make_platform();
        auto r     = p.get_registry();
        auto hkcu  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k     = hkcu.create_key(k_subkey);
        k.set_value(L"count"sv, 5u);
        auto ghost = k.create_key(L"Ghost"sv);
        ghost.set_value(L"y"sv, 2u);
    }

    // Run #1: observe MPS5_Repair (eager whole-key capture). This captures its
    // value and enumerates "Ghost" as a name-only placeholder, but Ghost itself
    // is never opened, so its contents are never captured. Save the snapshot.
    {
        auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k    = hkcu.open_key(k_subkey);
        EXPECT_EQ(k.get_uint32_value(L"count"sv), 5u);

        p.save(out);
    }

    remove_staged();

    // Run #2: load the sealed snapshot and exercise the repair.
    {
        auto snap = m::pil::load_platform(out);
        auto r    = snap.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k    = hkcu.open_key(k_subkey);

        auto const has_ghost = [&]() {
            for (auto&& n: k.list_subkey_names())
                if (n == m::pil::key_path(L"Ghost"sv))
                    return true;
            return false;
        };

        // Before the contradiction is exposed, "Ghost" enumerates.
        EXPECT_TRUE(has_ghost());
        auto const before_lwt = k.last_write_time();

        // Opening the unmaterializable placeholder triggers the repair.
        EXPECT_FALSE(k.try_open_key(L"Ghost"sv).has_value());

        // After repair, "Ghost" is gone from the enumeration and the parent's
        // stamp has advanced to T_load (strictly newer than its captured value).
        EXPECT_FALSE(has_ghost());
        EXPECT_GT(k.last_write_time(), before_lwt);
    }

    std::filesystem::remove(out, ec);
}

// M-PS-6: end-to-end integration. Build a buffered platform over the live
// registry, observe a representative tree (values of several types, nested
// subkeys) and apply overlay modifications, save, then reload as a sealed
// snapshot with the live key deleted. Assert the loaded world reproduces what
// run #1 observed (including overlay writes/deletes) and stays self-consistent
// under repeated reads and enumerations.
TEST(BufferedSave, EndToEndSealedSnapshotReproducesObservations)
{
    static constexpr auto k_root = L"MPS6_E2E"sv;

    auto const      out = std::filesystem::temp_directory_path() / "mps6_e2e.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    auto const remove_staged = []() {
        auto p    = m::pil::make_platform();
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        try
        {
            hkcu.delete_tree(k_root);
        }
        catch (...)
        {
        }
    };

    // Stage a representative tree through a direct platform.
    remove_staged();
    {
        auto p     = m::pil::make_platform();
        auto r     = p.get_registry();
        auto hkcu  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto root  = hkcu.create_key(k_root);
        root.set_value(L"vu"sv, 42u);
        root.set_string_value(L"vs"sv, L"hello");
        root.set_value(L"doomed"sv, 7u);

        auto alpha = root.create_key(L"Alpha"sv);
        alpha.set_value(L"a"sv, 1u);

        auto beta = root.create_key(L"Beta"sv);
        beta.set_value(L"b"sv, 2u);
        auto gamma = beta.create_key(L"Gamma"sv);
        gamma.set_value(L"g"sv, 3u);
    }

    // Run #1: observe through the buffered layer and apply overlay edits.
    {
        auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto root = hkcu.open_key(k_root); // capture root + enumerate subkeys

        EXPECT_EQ(root.get_uint32_value(L"vu"sv), 42u);

        auto alpha = root.open_key(L"Alpha"sv); // capture Alpha
        EXPECT_EQ(alpha.get_uint32_value(L"a"sv), 1u);

        auto beta = root.open_key(L"Beta"sv); // capture Beta + enumerate Gamma
        EXPECT_EQ(beta.get_uint32_value(L"b"sv), 2u);
        auto gamma = beta.open_key(L"Gamma"sv); // capture Gamma
        EXPECT_EQ(gamma.get_uint32_value(L"g"sv), 3u);

        // Overlay modifications: add a value, delete a value, create a subkey.
        root.set_value(L"vnew"sv, 99u);
        root.delete_value(L"doomed"sv);
        auto delta = root.create_key(L"Delta"sv);
        delta.set_value(L"d"sv, 4u);

        p.save(out);
    }

    // Delete the real tree: the snapshot is now the only source of truth.
    remove_staged();

    // Run #2: load the sealed snapshot and verify reproduction + consistency.
    {
        auto snap = m::pil::load_platform(out);
        auto r    = snap.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto root = hkcu.open_key(k_root);

        // Captured and overlay-written values reproduce.
        EXPECT_EQ(root.get_uint32_value(L"vu"sv), 42u);
        EXPECT_EQ(root.get_string_value(L"vs"sv), L"hello");
        EXPECT_EQ(root.get_uint32_value(L"vnew"sv), 99u);

        // The deleted value does not reappear.
        bool doomed_present = false;
        for (auto&& v: root.list_value_names_and_types())
            if (v.m_value_name == L"doomed")
                doomed_present = true;
        EXPECT_FALSE(doomed_present);

        // Nested captured subkeys reproduce.
        auto alpha = root.open_key(L"Alpha"sv);
        EXPECT_EQ(alpha.get_uint32_value(L"a"sv), 1u);
        auto beta = root.open_key(L"Beta"sv);
        EXPECT_EQ(beta.get_uint32_value(L"b"sv), 2u);
        auto gamma = beta.open_key(L"Gamma"sv);
        EXPECT_EQ(gamma.get_uint32_value(L"g"sv), 3u);

        // The overlay-created subkey reproduces.
        auto delta = root.open_key(L"Delta"sv);
        EXPECT_EQ(delta.get_uint32_value(L"d"sv), 4u);

        // Self-consistency under repeated enumerations: the subkey-name set is
        // stable across reads. Capture the names twice and compare.
        auto const collect_names = [&]() {
            std::vector<m::pil::key_path> names;
            for (auto&& n: root.list_subkey_names())
                names.push_back(n);
            std::sort(names.begin(),
                      names.end(),
                      [](auto const& l, auto const& rr) {
                          return l.native().view() < rr.native().view();
                      });
            return names;
        };

        auto const first  = collect_names();
        auto const second = collect_names();
        EXPECT_EQ(first, second);

        // The expected captured + created subkeys are all present.
        for (auto const* expected: {L"Alpha", L"Beta", L"Delta"})
        {
            bool found = false;
            for (auto&& n: first)
                if (n == m::pil::key_path(expected))
                    found = true;
            EXPECT_TRUE(found);
        }
    }

    std::filesystem::remove(out, ec);
}

// M-FS-BUF-3: an observed-but-unmodified directory node round-trips through the
// public save()/load() path. The overlay captures the node whole on touch
// (its own metadata plus its direct children's names + kinds + metadata, D2),
// serializes it into the <Filesystem> child of <Platform> (D3), and a fresh
// load reproduces the same namespace and metadata.
TEST(BufferedSave, FilesystemNamespaceRoundTrips)
{
    scoped_temp_dir const tmp;
    std::filesystem::create_directory(tmp.path() / L"alpha");
    std::filesystem::create_directory(tmp.path() / L"beta");
    {
        std::ofstream f(tmp.path() / L"gamma.txt");
        f << "hello";
    }

    auto const      out = std::filesystem::temp_directory_path() / "m_fs_buf3_roundtrip.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    m::pil::file_metadata captured{};
    {
        auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto fs   = p.get_filesystem();
        auto fp   = to_file_path(tmp.path());
        auto root = fs.open_root(fp.root());
        auto dir  = root.open_directory(m::pil::file_path(fp.relative_path()));

        captured = dir.query_information(); // touch -> capture whole node

        EXPECT_EQ(child_names(dir),
                  (std::set<std::u16string>{u"alpha", u"beta", u"gamma.txt"}));

        p.save(out);
    }

    auto snap  = m::pil::load_platform(out);
    auto fs    = snap.get_filesystem();
    auto fp    = to_file_path(tmp.path());
    auto root  = fs.open_root(fp.root());
    auto dir   = root.open_directory(m::pil::file_path(fp.relative_path()));

    EXPECT_EQ(dir.query_information().m_last_write_time, captured.m_last_write_time);
    EXPECT_EQ(child_names(dir),
              (std::set<std::u16string>{u"alpha", u"beta", u"gamma.txt"}));

    std::filesystem::remove(out, ec);
}

// Reproduces the CI-only failure where the host path carries an 8.3 short-name
// component. On hosted runners temp_directory_path() returns a path with a
// short component (e.g. C:\Users\RUNNER~1\AppData\Local\Temp), because the
// account name "runneradmin" exceeds 8 characters and Windows generates an 8.3
// alias. The buffered overlay captures each directory's children by enumeration
// -- which yields the LONG names -- but the requested path carries the SHORT
// name, so the exact-string map lookup misses and open_directory reports
// "no such file or directory". Dev machines whose user names are <= 8 chars
// never see an 8.3 component in %TEMP%, which is why this only failed on CI.
// Here we force the same condition deterministically with GetShortPathNameW so
// the failure is reproducible on any machine and the diagnostic traces show the
// captured-long vs requested-short name mismatch.
//
// The long-named directory is placed under a short (<= 8 char) parent so that
// the failing lookup happens in a directory with a single child, keeping the
// diagnostic MISS dump tiny instead of enumerating all of %TEMP%.
TEST(BufferedSave, FilesystemShortNamePathComponentReproducesCiFailure)
{
    auto const base = std::filesystem::temp_directory_path();
    // Parent name kept <= 8 chars so it never acquires its own 8.3 alias.
    std::filesystem::path const shortParentDir =
        base / (L"m8" + std::to_wstring(::GetCurrentProcessId() % 100000u));

    std::error_code ec;
    std::filesystem::remove_all(shortParentDir, ec);
    std::filesystem::create_directories(shortParentDir / L"longchildname" / L"alpha");

    struct cleanup
    {
        std::filesystem::path p;
        ~cleanup()
        {
            std::error_code e;
            std::filesystem::remove_all(p, e);
        }
    } const guard{shortParentDir};

    std::wstring const longPath = (shortParentDir / L"longchildname").wstring();

    DWORD const needed = ::GetShortPathNameW(longPath.c_str(), nullptr, 0);
    ASSERT_NE(needed, 0u) << "GetShortPathNameW size query failed: " << ::GetLastError();

    std::wstring shortPath(needed, L'\0');
    DWORD const got = ::GetShortPathNameW(longPath.c_str(), shortPath.data(), needed);
    ASSERT_NE(got, 0u) << "GetShortPathNameW failed: " << ::GetLastError();
    shortPath.resize(got);

    if (shortPath == longPath)
        GTEST_SKIP() << "8.3 short-name generation appears disabled on this volume";

    auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto fs   = p.get_filesystem();
    auto fp   = to_file_path(std::filesystem::path(shortPath));
    auto root = fs.open_root(fp.root());
    auto dir  = root.open_directory(m::pil::file_path(fp.relative_path()));

    EXPECT_EQ(child_names(dir), (std::set<std::u16string>{u"alpha"}));
}

// M-FS-BUF-3: a sealed snapshot serves the captured namespace with no
// underlying provider. We prove the absence of fall-through by deleting the
// real tree before loading: any read that reached a live filesystem would now
// fail. The loaded snapshot still enumerates the captured children and serves
// the node's metadata.
TEST(BufferedSave, FilesystemSealedSnapshotServesNamespace)
{
    scoped_temp_dir const tmp;
    std::filesystem::create_directory(tmp.path() / L"alpha");
    std::filesystem::create_directory(tmp.path() / L"beta");
    {
        std::ofstream f(tmp.path() / L"gamma.txt");
        f << "hello";
    }

    auto const      out = std::filesystem::temp_directory_path() / "m_fs_buf3_sealed.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    {
        auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto fs   = p.get_filesystem();
        auto fp   = to_file_path(tmp.path());
        auto root = fs.open_root(fp.root());
        auto dir  = root.open_directory(m::pil::file_path(fp.relative_path()));

        EXPECT_EQ(child_names(dir),
                  (std::set<std::u16string>{u"alpha", u"beta", u"gamma.txt"}));

        p.save(out);
    }

    // Delete the real tree: the sealed snapshot must now be the only source.
    std::filesystem::remove_all(tmp.path(), ec);

    auto snap  = m::pil::load_platform(out);
    auto fs    = snap.get_filesystem();
    auto fp    = to_file_path(tmp.path());
    auto root  = fs.open_root(fp.root());
    auto dir   = root.open_directory(m::pil::file_path(fp.relative_path()));

    // The captured namespace and metadata are served with no live provider.
    EXPECT_NO_THROW((void)dir.query_information());
    EXPECT_EQ(child_names(dir),
              (std::set<std::u16string>{u"alpha", u"beta", u"gamma.txt"}));

    std::filesystem::remove(out, ec);
}

// M-FS-BUF-5: end-to-end integration. Build a buffered filesystem over a live
// temp tree, observe a representative namespace (nested directories and files
// with metadata), apply overlay mutations (create / remove / rename), save, then
// **delete the live tree** and reload as a sealed snapshot. Assert the loaded
// world reproduces the observed + mutated namespace and metadata, stays
// self-consistent under repeated reads and enumerations, and that the D14
// limitation holds (a sealed file node serves its metadata but its *content* is
// out of scope — the `file` wrapper exposes no content-read API at all).
TEST(BufferedSave, FilesystemEndToEndSealedSnapshotReproducesNamespace)
{
    scoped_temp_dir const tmp;

    // Stage a representative live tree:
    //   alpha/            (dir, captured)
    //   beta/gamma/       (nested dirs, captured)
    //   keep.txt          (file, captured, later renamed)
    //   doomed.txt        (file, captured, later removed in the overlay)
    std::filesystem::create_directory(tmp.path() / L"alpha");
    std::filesystem::create_directory(tmp.path() / L"beta");
    std::filesystem::create_directory(tmp.path() / L"beta" / L"gamma");
    {
        std::ofstream f(tmp.path() / L"keep.txt");
        f << "keep-contents";
    }
    {
        std::ofstream f(tmp.path() / L"doomed.txt");
        f << "doomed-contents";
    }

    auto const      out = std::filesystem::temp_directory_path() / "m_fs_buf5_e2e.xml";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    m::pil::file_metadata captured_root{};

    // Run #1: observe through the buffered layer and apply overlay mutations.
    {
        auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto fs   = p.get_filesystem();
        auto fp   = to_file_path(tmp.path());
        auto root = fs.open_root(fp.root());
        auto dir  = root.open_directory(m::pil::file_path(fp.relative_path()));

        captured_root = dir.query_information(); // touch -> capture whole node

        EXPECT_EQ(child_names(dir),
                  (std::set<std::u16string>{u"alpha", u"beta", u"keep.txt", u"doomed.txt"}));

        // Capture nested nodes so they survive into the sealed world.
        auto beta  = dir.open_directory(m::pil::file_path(u"beta"sv));
        auto gamma = beta.open_directory(m::pil::file_path(u"gamma"sv));
        (void)gamma.query_information();

        // Touch keep.txt as a file to exercise the file-capture path before it
        // is renamed below.
        (void)dir.open_file(m::pil::file_path(u"keep.txt"sv)).query_information();

        // Overlay mutations: create a dir + file, remove a file, rename a file.
        auto delta = dir.create_directory(m::pil::file_path(u"delta"sv));
        (void)delta.create_file(m::pil::file_path(u"inner.txt"sv));
        dir.create_file(m::pil::file_path(u"added.txt"sv));
        dir.remove_entry(m::pil::file_path(u"doomed.txt"sv));
        dir.rename_entry(m::pil::file_path(u"keep.txt"sv),
                         m::pil::file_path(u"renamed.txt"sv));

        p.save(out);
    }

    // Delete the live tree: the sealed snapshot is now the only source of truth.
    std::filesystem::remove_all(tmp.path(), ec);

    // Run #2: load the sealed snapshot and verify reproduction + consistency.
    {
        auto snap = m::pil::load_platform(out);
        auto fs   = snap.get_filesystem();
        auto fp   = to_file_path(tmp.path());
        auto root = fs.open_root(fp.root());
        auto dir  = root.open_directory(m::pil::file_path(fp.relative_path()));

        // The captured node's own metadata reproduces with no live provider.
        EXPECT_EQ(dir.query_information().m_last_write_time, captured_root.m_last_write_time);

        // The namespace reproduces the captured set as mutated by the overlay:
        // doomed.txt removed, keep.txt renamed, delta/ + added.txt created.
        EXPECT_EQ(child_names(dir),
                  (std::set<std::u16string>{
                      u"alpha", u"beta", u"renamed.txt", u"delta", u"added.txt"}));

        // Nested captured directories reproduce.
        auto beta = dir.open_directory(m::pil::file_path(u"beta"sv));
        EXPECT_EQ(child_names(beta), (std::set<std::u16string>{u"gamma"}));
        EXPECT_NO_THROW((void)beta.open_directory(m::pil::file_path(u"gamma"sv)));

        // The overlay-created directory and its file reproduce.
        auto delta = dir.open_directory(m::pil::file_path(u"delta"sv));
        EXPECT_EQ(child_names(delta), (std::set<std::u16string>{u"inner.txt"}));

        // Self-consistency under repeated enumerations: the child-name set is
        // stable across reads.
        EXPECT_EQ(child_names(dir), child_names(dir));

        // The removed entry stays gone; the pre-rename name does not reappear.
        EXPECT_FALSE(dir.try_open_file(m::pil::file_path(u"doomed.txt"sv)).has_value());
        EXPECT_FALSE(dir.try_open_file(m::pil::file_path(u"keep.txt"sv)).has_value());

        // D14 boundary: a sealed file node serves its captured *metadata*, but
        // its *content* is out of scope — the `file` wrapper exposes no
        // content-read API, so no read can fall through to a (now-deleted) live
        // provider. We assert metadata is served and document the limitation.
        auto renamed = dir.open_file(m::pil::file_path(u"renamed.txt"sv));
        EXPECT_TRUE(static_cast<bool>(renamed));
        auto const renamed_md = renamed.query_information();
        EXPECT_EQ(renamed_md.m_kind, m::pil::node_kind::file);
        // (No content-read method exists on `file`; content capture is M-FS-STREAMS.)
    }

    std::filesystem::remove(out, ec);
}

#endif // WIN32
