// Copyright (c) Microsoft Corporation.
//
// M-SAMPLE-2/3/4: lifecycle harness for the mwin32 sample client.
//
// These tests drive the standalone `mwin32_sample_client` executable (an ordinary
// Win32 registry client that links `mwin32_alias`) as a subprocess under different
// `.pilcfg` sidecars, exercising the full shim lifecycle with no effect on the live
// registry:
//   * capture  (M-SAMPLE-2): buffered + capture_snapshot — writes land in an overlay
//              and are persisted to a snapshot file; the live registry is untouched.
//   * replay   (M-SAMPLE-3): persisted_state — the client runs against the captured
//              snapshot with no live underlying registry and sees the captured state.
//
// The harness locates the samples via a build-generated sidecar
// (`mwin32_sample_clients.dir`, written next to this test binary by
// test/CMakeLists.txt) so the path is correct under both single- and
// multi-config generators, writes each sample's `<exe>.pilcfg`, runs it
// capturing stdout, and asserts on the snapshot file and the client's reported
// observations.

#include <windows.h>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <pugixml.hpp>

#include <m/pil/pil.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

namespace
{
    // The registry key/values the sample writes (must match mwin32_sample_client.cpp).
    constexpr wchar_t k_sample_subkey[] = L"mwin32_sample_client";

    // The value the fault scenario targets (must match k_value_count in the sample).
    constexpr wchar_t k_count_value[] = L"count";

    // Full path of this test's own executable, used to locate sibling artifacts.
    std::filesystem::path
    self_path()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            DWORD const written =
                ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (written == 0)
                return {};
            if (written < buffer.size())
            {
                buffer.resize(written);
                return std::filesystem::path(buffer);
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    // The directory that holds the sample client executables, plumbed from the
    // build system. test/CMakeLists.txt generates a sidecar
    // (`mwin32_sample_clients.dir`) next to this test binary recording the
    // configuration-resolved sample output directory. This cannot be derived
    // from this test's own module path because under a multi-config generator
    // the samples live in a per-config subdirectory (sample/<Config>/) while the
    // test lives in test/<Config>/.
    std::filesystem::path
    sample_client_dir()
    {
        auto const sidecar = self_path().parent_path() / L"mwin32_sample_clients.dir";
        std::ifstream f(sidecar, std::ios::binary);
        std::string line;
        std::getline(f, line);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        return std::filesystem::path(line);
    }

    // The `.pilcfg` sidecar a sample reads lives next to its executable.
    std::filesystem::path
    pilcfg_path(std::filesystem::path const& exe)
    {
        auto p = exe;
        p += L".pilcfg";
        return p;
    }

    // The registry sample executable and its `.pilcfg` sidecar (M-SAMPLE-2/3/4).
    std::filesystem::path
    sample_exe_path()
    {
        return sample_client_dir() / L"mwin32_sample_client.exe";
    }

    std::filesystem::path
    sample_pilcfg_path()
    {
        return pilcfg_path(sample_exe_path());
    }

    // The filesystem sample executable and its `.pilcfg` sidecar (M-FS-SHIM-8).
    std::filesystem::path
    fs_sample_exe_path()
    {
        return sample_client_dir() / L"mwin32_fs_sample_client.exe";
    }

    std::filesystem::path
    fs_sample_pilcfg_path()
    {
        return pilcfg_path(fs_sample_exe_path());
    }

    // The notification sample executable and its `.pilcfg` sidecar
    // (M-FS-NOTIFY-REDIR-1).
    std::filesystem::path
    notify_sample_exe_path()
    {
        return sample_client_dir() / L"mwin32_notify_sample_client.exe";
    }

    std::filesystem::path
    notify_sample_pilcfg_path()
    {
        return pilcfg_path(notify_sample_exe_path());
    }

    void
    write_text_file(std::filesystem::path const& p, std::string_view text)
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::string
    read_text_file(std::filesystem::path const& p)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f)
            return {};
        return std::string((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    }

    struct sample_run
    {
        bool        launched = false;
        DWORD       exit_code = 0xffffffffu;
        std::string output;
    };

    // Run a sample executable, capturing its stdout. The sample reads the
    // `.pilcfg` already written next to it.
    sample_run
    run_client(std::filesystem::path const& exe)
    {
        sample_run result;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength        = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE read_end  = nullptr;
        HANDLE write_end = nullptr;
        if (!::CreatePipe(&read_end, &write_end, &sa, 0))
            return result;
        // The child must not inherit the read end.
        ::SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdOutput = write_end;
        si.hStdError  = write_end;
        si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION pi{};

        std::wstring cmdline = L"\"" + exe.wstring() + L"\"";
        std::wstring workdir = exe.parent_path().wstring();

        BOOL const ok = ::CreateProcessW(nullptr,
                                         cmdline.data(),
                                         nullptr,
                                         nullptr,
                                         TRUE,
                                         0,
                                         nullptr,
                                         workdir.c_str(),
                                         &si,
                                         &pi);
        // Close our copy of the write end so the read end sees EOF when the child exits.
        ::CloseHandle(write_end);

        if (!ok)
        {
            ::CloseHandle(read_end);
            return result;
        }

        // Drain the pipe until the child closes it.
        for (;;)
        {
            std::array<char, 4096> buf{};
            DWORD                  got = 0;
            if (!::ReadFile(read_end, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) ||
                got == 0)
                break;
            result.output.append(buf.data(), got);
        }
        ::CloseHandle(read_end);

        ::WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0xffffffffu;
        ::GetExitCodeProcess(pi.hProcess, &code);
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);

        result.launched   = true;
        result.exit_code  = code;
        return result;
    }

    // Run the registry sample client.
    sample_run
    run_sample()
    {
        return run_client(sample_exe_path());
    }

    // Run a sample executable with command-line arguments, capturing its stdout.
    // Does not wait for the process to exit; returns the process and read handles
    // for the caller to manage.
    struct async_sample_run
    {
        bool                launched = false;
        HANDLE              process  = nullptr;
        HANDLE              read_end = nullptr;
        std::filesystem::path workdir;
    };

    async_sample_run
    run_client_async(std::filesystem::path const& exe, std::wstring const& args)
    {
        async_sample_run result;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength        = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE read_end  = nullptr;
        HANDLE write_end = nullptr;
        if (!::CreatePipe(&read_end, &write_end, &sa, 0))
            return result;
        // The child must not inherit the read end.
        ::SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdOutput = write_end;
        si.hStdError  = write_end;
        si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION pi{};

        std::wstring cmdline = L"\"" + exe.wstring() + L"\" " + args;
        std::wstring workdir = exe.parent_path().wstring();

        BOOL const ok = ::CreateProcessW(nullptr,
                                         cmdline.data(),
                                         nullptr,
                                         nullptr,
                                         TRUE,
                                         0,
                                         nullptr,
                                         workdir.c_str(),
                                         &si,
                                         &pi);
        ::CloseHandle(write_end);

        if (!ok)
        {
            ::CloseHandle(read_end);
            return result;
        }

        ::CloseHandle(pi.hThread);

        result.launched = true;
        result.process  = pi.hProcess;
        result.read_end = read_end;
        result.workdir  = workdir;
        return result;
    }

    // Drain the output from an async sample run and wait for it to exit.
    sample_run
    finish_async_run(async_sample_run& ar)
    {
        sample_run result;
        if (!ar.launched)
            return result;

        // Drain the pipe until the child closes it.
        for (;;)
        {
            std::array<char, 4096> buf{};
            DWORD                  got = 0;
            if (!::ReadFile(ar.read_end, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) ||
                got == 0)
                break;
            result.output.append(buf.data(), got);
        }
        ::CloseHandle(ar.read_end);

        ::WaitForSingleObject(ar.process, INFINITE);
        DWORD code = 0xffffffffu;
        ::GetExitCodeProcess(ar.process, &code);
        ::CloseHandle(ar.process);

        result.launched  = true;
        result.exit_code = code;
        return result;
    }

    // Assert the live registry does not contain the sample's key, proving the
    // buffered run never reached the real OS. Uses genuine advapi32 obtained via
    // GetProcAddress so the call is not subject to any alias redirection.
    void
    expect_live_registry_clean()
    {
        HMODULE advapi = ::LoadLibraryW(L"advapi32.dll");
        ASSERT_NE(advapi, nullptr);

        using open_t  = LSTATUS(APIENTRY*)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
        auto real_open = reinterpret_cast<open_t>(
            reinterpret_cast<void*>(::GetProcAddress(advapi, "RegOpenKeyExW")));
        ASSERT_NE(real_open, nullptr);

        HKEY    live = nullptr;
        LSTATUS rc   = real_open(HKEY_CURRENT_USER, k_sample_subkey, 0, KEY_READ, &live);
        EXPECT_EQ(rc, static_cast<LSTATUS>(ERROR_FILE_NOT_FOUND))
            << "live registry contains the sample key; the run was not isolated";
        if (rc == ERROR_SUCCESS && live != nullptr)
        {
            if (auto real_close = reinterpret_cast<LSTATUS(APIENTRY*)(HKEY)>(
                    reinterpret_cast<void*>(::GetProcAddress(advapi, "RegCloseKey"))))
                real_close(live);
        }
        ::FreeLibrary(advapi);
    }

    // Discover the absolute path the sample's workload key reports under the same
    // buffered-over-live platform the sample runs on, so a fault-script rule can
    // target it exactly. The probe creates the key in a throwaway buffered overlay
    // (never committed), so the live registry is untouched and the path is purely
    // structural — identical to what the sample computes in its own process.
    m::pil::key_path
    probe_sample_key_path()
    {
        auto p  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r  = p.get_registry();
        auto hk = r.open_predefined_key(m::pil::predefined_key::current_user);
        return hk.create_key(k_sample_subkey).get_path();
    }

    // Write a single-rule <FaultScript> that fails set_value on value_name of
    // target with the given action on its first occurrence, and return its path.
    std::filesystem::path
    write_sample_fault_script(std::filesystem::path const& dir,
                              m::pil::key_path const&       target,
                              wchar_t const*                value_name,
                              wchar_t const*                action)
    {
        auto const out = dir / L"m_sample_fault_script.xml";

        pugi::xml_document doc;
        auto               root = doc.append_child(L"FaultScript");
        auto               rule = root.append_child(L"Rule");
        rule.append_attribute(L"operation").set_value(L"set_value");
        rule.append_attribute(L"path").set_value(
            m::to_wstring(target.native().view()).c_str());
        rule.append_attribute(L"valueName").set_value(value_name);
        rule.append_attribute(L"occurrence").set_value(L"1");
        rule.append_attribute(L"action").set_value(action);
        doc.save_file(out.native().c_str());

        return out;
    }
}

// M-SAMPLE-2: capture scenario. The sample runs buffered with a capture snapshot;
// its writes are persisted to the snapshot and never touch the live registry.
TEST(Mwin32SampleLifecycle, CapturePersistsWritesWithoutTouchingLiveRegistry)
{
    const std::filesystem::path snapshot =
        sample_exe_path().parent_path() / L"m_sample_capture.xml";

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);

    // Buffered + capture: writes land in the overlay and are saved on exit.
    const std::string cfg =
        "{\"buffer_updates\": true, \"capture_snapshot\": \"" + snapshot.generic_string() + "\"}";
    write_text_file(sample_pilcfg_path(), cfg);

    const sample_run run = run_sample();
    ASSERT_TRUE(run.launched) << "failed to launch the sample executable";
    EXPECT_EQ(run.exit_code, 0u);

    // The client should have created the key and read its own writes back.
    EXPECT_NE(run.output.find("create_rc=0"), std::string::npos);
    EXPECT_NE(run.output.find("name=sample-client"), std::string::npos);
    EXPECT_NE(run.output.find("count=42"), std::string::npos);

    // The snapshot must capture the key and the three value operations.
    ASSERT_TRUE(std::filesystem::exists(snapshot)) << "capture snapshot was not written";
    const std::string xml = read_text_file(snapshot);
    EXPECT_NE(xml.find("name=\"mwin32_sample_client\""), std::string::npos);
    // REG_DWORD 42 == 0x2a, little-endian.
    EXPECT_NE(xml.find("<Value name=\"count\" type=\"4\" data=\"2a000000\""), std::string::npos);
    // REG_SZ "sample-client" as UTF-16LE.
    EXPECT_NE(
        xml.find("<Value name=\"name\" type=\"1\" "
                 "data=\"730061006d0070006c0065002d0063006c00690065006e0074000000\""),
        std::string::npos);
    // The deleted blob value is recorded as a tombstone.
    EXPECT_NE(xml.find("<Value name=\"blob\" deleted=\"true\""), std::string::npos);

    expect_live_registry_clean();

    std::filesystem::remove(snapshot, ec);
    std::filesystem::remove(sample_pilcfg_path(), ec);
}

// M-SAMPLE-4: logging scenario. Run the sample under a record_modifications
// `.pilcfg` and assert the emitted diagnostic log reflects the client's writes
// and deletes in the order they were issued.
TEST(Mwin32SampleLifecycle, RecordModificationsLogsWritesAndDeletesInOrder)
{
    const std::filesystem::path log = sample_exe_path().parent_path() / L"m_sample_diag.xml";

    std::error_code ec;
    std::filesystem::remove(log, ec);

    // Buffered + record_modifications + diagnostic_log: writes are recorded in
    // order and emitted on exit, never touching the live registry.
    write_text_file(sample_pilcfg_path(),
                    "{\"buffer_updates\": true, \"record_modifications\": true, "
                    "\"diagnostic_log\": \"" +
                        log.generic_string() + "\"}");

    const sample_run run = run_sample();
    ASSERT_TRUE(run.launched) << "failed to launch the sample executable";
    EXPECT_EQ(run.exit_code, 0u);

    ASSERT_TRUE(std::filesystem::exists(log)) << "diagnostic log was not written";
    const std::string xml = read_text_file(log);

    // The five operations the client issues, in the order it issued them.
    const auto create_key = xml.find("<Registry.CreateKey");
    const auto set_name   = xml.find("<Registry.SetValue key=\"true\" valueName=\"name\"");
    const auto set_count  = xml.find("<Registry.SetValue key=\"true\" valueName=\"count\"");
    const auto set_blob   = xml.find("<Registry.SetValue key=\"true\" valueName=\"blob\"");
    const auto del_blob   = xml.find("<Registry.DeleteValue key=\"true\" valueName=\"blob\"");

    ASSERT_NE(create_key, std::string::npos);
    ASSERT_NE(set_name, std::string::npos);
    ASSERT_NE(set_count, std::string::npos);
    ASSERT_NE(set_blob, std::string::npos);
    ASSERT_NE(del_blob, std::string::npos);

    // Create precedes all value writes; the three writes are in issue order; the
    // delete of the blob value comes after it was written.
    EXPECT_LT(create_key, set_name);
    EXPECT_LT(set_name, set_count);
    EXPECT_LT(set_count, set_blob);
    EXPECT_LT(set_blob, del_blob);

    // The DWORD value's data is logged as 0x2a (42).
    EXPECT_NE(xml.find("reg_dword_data=\"0x2a\""), std::string::npos);

    expect_live_registry_clean();

    std::filesystem::remove(log, ec);
    std::filesystem::remove(sample_pilcfg_path(), ec);
}
// sample binary is run against that snapshot via `persisted_state` with no live
// underlying registry. The client runs identically — proving the shim can fully
// substitute for the OS from a persisted state.
TEST(Mwin32SampleLifecycle, ReplayAgainstPersistedSnapshotRunsWithoutLiveRegistry)
{
    const std::filesystem::path snapshot =
        sample_exe_path().parent_path() / L"m_sample_replay_seed.xml";

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);

    // Phase 1: capture a snapshot of the sample's writes (buffered + capture).
    write_text_file(
        sample_pilcfg_path(),
        "{\"buffer_updates\": true, \"capture_snapshot\": \"" + snapshot.generic_string() + "\"}");
    const sample_run capture = run_sample();
    ASSERT_TRUE(capture.launched) << "failed to launch the sample for the capture phase";
    ASSERT_EQ(capture.exit_code, 0u);
    ASSERT_TRUE(std::filesystem::exists(snapshot)) << "capture phase did not write a snapshot";

    // Phase 2: replay the same binary against the snapshot with no live registry.
    write_text_file(sample_pilcfg_path(),
                    "{\"persisted_state\": \"" + snapshot.generic_string() + "\"}");
    const sample_run replay = run_sample();
    ASSERT_TRUE(replay.launched) << "failed to launch the sample for the replay phase";
    EXPECT_EQ(replay.exit_code, 0u);

    // The client behaves identically against the persisted snapshot: it creates
    // the key, writes and reads its values, all without the real OS.
    EXPECT_NE(replay.output.find("create_rc=0"), std::string::npos);
    EXPECT_NE(replay.output.find("name=sample-client"), std::string::npos);
    EXPECT_NE(replay.output.find("count=42"), std::string::npos);

    expect_live_registry_clean();

    std::filesystem::remove(snapshot, ec);
    std::filesystem::remove(sample_pilcfg_path(), ec);
}

// M-FAULTCFG-3: fault scenario. The sample runs buffered under a `.pilcfg` whose
// fault script fails the set of the "count" value with ERROR_ACCESS_DENIED on its
// first occurrence. The client observes exactly that injected status on the
// targeted operation, mapped through the shim to the right LSTATUS, while every
// other operation (the create and the non-targeted sets/reads) passes through
// unaffected and the client runs to normal completion.
TEST(Mwin32SampleLifecycle, FaultScriptInjectsFailureWhileOtherOpsPassThrough)
{
    auto const dir    = sample_exe_path().parent_path();
    auto const target = probe_sample_key_path();
    auto const script = write_sample_fault_script(dir, target, k_count_value, L"access_denied");

    // Buffered + fault_script: the targeted set fails with the scripted status;
    // the run never touches the live registry.
    const std::string cfg =
        "{\"buffer_updates\": true, \"fault_script\": \"" + script.generic_string() + "\"}";
    write_text_file(sample_pilcfg_path(), cfg);

    const sample_run run = run_sample();
    ASSERT_TRUE(run.launched) << "failed to launch the sample executable";
    EXPECT_EQ(run.exit_code, 0u);

    // Pass-through: the create and the non-targeted value writes succeed, and a
    // later read still observes the data the client wrote.
    EXPECT_NE(run.output.find("create_rc=0"), std::string::npos);
    EXPECT_NE(run.output.find("set_name_rc=0"), std::string::npos);
    EXPECT_NE(run.output.find("set_blob_rc=0"), std::string::npos);
    EXPECT_NE(run.output.find("name=sample-client"), std::string::npos);

    // Injected failure: the targeted set of "count" returns the scripted
    // access-denied status (ERROR_ACCESS_DENIED == 5).
    EXPECT_NE(run.output.find("set_count_rc=5"), std::string::npos)
        << "expected the injected ERROR_ACCESS_DENIED on the targeted set; output:\n"
        << run.output;

    expect_live_registry_clean();

    std::error_code ec;
    std::filesystem::remove(script, ec);
    std::filesystem::remove(sample_pilcfg_path(), ec);
}

namespace
{
    // Assert a path does not exist on the real filesystem, proving a buffered +
    // redirecting run never reached the live disk. Uses genuine kernel32 obtained
    // via GetProcAddress so the call is not subject to any alias redirection (the
    // test executable does not link mwin32_alias, but this keeps the intent
    // explicit and matches expect_live_registry_clean).
    void
    expect_live_path_absent(wchar_t const* path)
    {
        HMODULE k32 = ::LoadLibraryW(L"kernel32.dll");
        ASSERT_NE(k32, nullptr);

        using getattr_t = DWORD(WINAPI*)(LPCWSTR);
        auto real_getattr = reinterpret_cast<getattr_t>(
            reinterpret_cast<void*>(::GetProcAddress(k32, "GetFileAttributesW")));
        ASSERT_NE(real_getattr, nullptr);

        DWORD const attrs = real_getattr(path);
        EXPECT_EQ(attrs, INVALID_FILE_ATTRIBUTES)
            << "live filesystem contains a sample path; the run was not isolated";
        ::FreeLibrary(k32);
    }
}

// M-FS-SHIM-8: integration scenario for the filesystem shim. The filesystem sample
// (an ordinary Win32 client that becomes redirectable purely by linking
// `mwin32_alias`) runs under a buffered + redirecting `.pilcfg` with a capture
// snapshot. It drives genuine CreateDirectoryW / CreateFileW / GetFileAttributesExW
// / FindFirstFileW / MoveFileExW through the shim ABI against a public path; the
// redirector remaps that path to a private prefix and the buffered overlay captures
// the writes into the snapshot. The test asserts the metadata round-trips, the
// snapshot records the captured nodes under the redirected (private) path and never
// under the public name, the live disk is untouched, and a non-file handle's
// CloseHandle still reaches the real API.
TEST(Mwin32FsSampleLifecycle, RedirectsAndCapturesThroughAliasAbi)
{
    const std::filesystem::path snapshot =
        fs_sample_exe_path().parent_path() / L"m_fs_sample_capture.xml";

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);

    // Buffered + redirecting + capture: the public prefix the client uses is mapped
    // to a private prefix, writes land in the overlay, and the overlay (registry +
    // filesystem) is persisted to the snapshot on exit. The from/to prefixes are
    // root-relative paths as the shim presents them to the redirector: the drive
    // root (and its terminating separator) is split off, so the keys are the
    // leading path component with no leading separator.
    const std::string cfg =
        "{\"buffer_updates\": true, \"capture_snapshot\": \"" + snapshot.generic_string() +
        "\", \"redirections\": [{\"from\": \"mwin32_pub_root\", \"to\": \"mwin32_priv_root\"}]}";
    write_text_file(fs_sample_pilcfg_path(), cfg);

    const sample_run run = run_client(fs_sample_exe_path());
    ASSERT_TRUE(run.launched) << "failed to launch the filesystem sample executable";
    EXPECT_EQ(run.exit_code, 0u) << run.output;

    // Client observations: the directory and file were created and their metadata
    // round-trips through the shim (the directory reports as a directory, the file
    // does not), the listing found the data file, the rename succeeded with the old
    // name gone and the new name present, and the kernel-object CloseHandle (which
    // is not aliased) reached the real API.
    EXPECT_NE(run.output.find("mkdir_rc=1"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("dir_is_directory=1"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("create_file_rc=1"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("file_getattr_rc=1"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("file_is_directory=0"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("find_name=data.bin"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("move_rc=1"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("old_after_move_rc=0"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("new_after_move_rc=1"), std::string::npos) << run.output;
    EXPECT_NE(run.output.find("event_close_rc=1"), std::string::npos)
        << "CloseHandle on a non-file handle must reach the real API; output:\n"
        << run.output;

    // The snapshot must capture the filesystem overlay under the REDIRECTED private
    // prefix, not the public one the client named. The renamed file resolves under
    // the private work directory.
    ASSERT_TRUE(std::filesystem::exists(snapshot)) << "capture snapshot was not written";
    const std::string xml = read_text_file(snapshot);
    EXPECT_NE(xml.find("name=\"mwin32_priv_root\""), std::string::npos)
        << "snapshot does not contain the redirected private root; xml:\n"
        << xml;
    EXPECT_NE(xml.find("name=\"data_renamed.bin\""), std::string::npos) << xml;
    // The public name must never appear materialized in the overlay — the
    // redirector mapped every reference away from it.
    EXPECT_EQ(xml.find("name=\"mwin32_pub_root\""), std::string::npos)
        << "public path leaked into the snapshot; redirection did not take effect";

    // The live filesystem must be untouched: neither the public path the client
    // used nor the private path the overlay captured exists on the real disk.
    expect_live_path_absent(L"C:\\mwin32_pub_root");
    expect_live_path_absent(L"C:\\mwin32_priv_root");

    std::filesystem::remove(snapshot, ec);
    std::filesystem::remove(fs_sample_pilcfg_path(), ec);
}

// M-FS-NOTIFY-REDIR-2: integration scenario for filesystem notification through a
// redirected path. The notification sample (an ordinary Win32 client that becomes
// redirectable purely by linking `mwin32_alias`) runs under a redirecting `.pilcfg`.
// It watches a "public" path that is redirected to a real "backing" directory.
// The test mutates the backing directory using the genuine Win32 API, and the
// notification sample receives the change notification, proving that change
// notifications work correctly through path redirection.
TEST(Mwin32NotifySampleLifecycle, ReceivesNotificationsThroughRedirectedPath)
{
    // Create a unique temporary directory structure.
    auto const base = std::filesystem::temp_directory_path() / (L"notify_redir_test_" +
                          std::to_wstring(::GetCurrentProcessId()) + L"_" +
                          std::to_wstring(::GetTickCount64()));
    auto const backing = base / L"backing" / L"watchdir";
    std::error_code ec;
    std::filesystem::create_directories(backing, ec);
    ASSERT_FALSE(ec) << "failed to create backing directory: " << ec.message();

    // The "public" path the sample will use. It doesn't need to exist as a real
    // directory; the redirection layer intercepts it.
    auto const public_path = base / L"pub" / L"watchdir";

    // Write a redirecting .pilcfg. The redirection prefixes are root-relative paths
    // (drive letter + colon stripped). We redirect pub→backing.
    //
    // Example: if base is C:\Users\test\AppData\Local\Temp\notify_redir_test_1234
    // then from = "Users\\test\\AppData\\Local\\Temp\\notify_redir_test_1234\\pub"
    //      to   = "Users\\test\\AppData\\Local\\Temp\\notify_redir_test_1234\\backing"
    //
    // The paths must use backslashes to match the PIL's file_path separator, and
    // backslashes are escaped as "\\\\" in JSON strings.
    auto strip_drive_and_escape = [](std::filesystem::path const& p) -> std::string {
        auto s = p.string();
        // Skip "X:\" prefix (3 chars) if present.
        if (s.size() > 3 && s[1] == ':' && (s[2] == '\\' || s[2] == '/'))
            s = s.substr(3);
        // Escape backslashes for JSON embedding.
        std::string escaped;
        escaped.reserve(s.size() * 2);
        for (char c: s)
        {
            if (c == '\\')
                escaped += "\\\\";
            else
                escaped += c;
        }
        return escaped;
    };

    auto const from_prefix = strip_drive_and_escape(base / L"pub");
    auto const to_prefix   = strip_drive_and_escape(base / L"backing");

    std::string const cfg =
        "{\"redirections\": [{\"from\": \"" + from_prefix + "\", \"to\": \"" + to_prefix + "\"}]}";
    write_text_file(notify_sample_pilcfg_path(), cfg);

    // Launch the notification sample watching the public path. It will arm a
    // watch and create a ready marker, then wait for the next notification.
    std::wstring const args = L"\"" + public_path.wstring() + L"\"";
    auto ar = run_client_async(notify_sample_exe_path(), args);
    ASSERT_TRUE(ar.launched) << "failed to launch the notification sample executable";

    // Wait for the ready marker to appear in the backing directory (the sample
    // creates it via the shim, which redirects to the backing path).
    auto const ready_marker = backing / L".watch_ready";
    constexpr int k_max_wait_ms = 5000;
    constexpr int k_poll_ms     = 50;
    int waited = 0;
    while (!std::filesystem::exists(ready_marker) && waited < k_max_wait_ms)
    {
        ::Sleep(k_poll_ms);
        waited += k_poll_ms;
    }
    ASSERT_TRUE(std::filesystem::exists(ready_marker))
        << "ready marker did not appear within timeout; backing=" << backing;

    // Brief pause to let the sample re-arm the watch after consuming the ready marker.
    ::Sleep(300);

    // Create a test file in the backing directory using genuine kernel32. The
    // sample should receive a notification for this file.
    constexpr wchar_t k_test_filename[] = L"test_notify_file.txt";
    auto const test_file_path = backing / k_test_filename;

    HMODULE k32 = ::LoadLibraryW(L"kernel32.dll");
    ASSERT_NE(k32, nullptr);

    using create_file_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    auto real_create_file = reinterpret_cast<create_file_t>(
        reinterpret_cast<void*>(::GetProcAddress(k32, "CreateFileW")));
    ASSERT_NE(real_create_file, nullptr);

    using close_handle_t = BOOL(WINAPI*)(HANDLE);
    auto real_close_handle = reinterpret_cast<close_handle_t>(
        reinterpret_cast<void*>(::GetProcAddress(k32, "CloseHandle")));
    ASSERT_NE(real_close_handle, nullptr);

    HANDLE hFile = real_create_file(test_file_path.c_str(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    ASSERT_NE(hFile, INVALID_HANDLE_VALUE)
        << "failed to create test file; GLE=" << ::GetLastError();
    real_close_handle(hFile);
    ::FreeLibrary(k32);

    // Collect the sample's output and wait for it to exit.
    sample_run run = finish_async_run(ar);
    ASSERT_TRUE(run.launched);

    // The sample should have received a notification and exited cleanly.
    EXPECT_EQ(run.exit_code, 0u) << "sample exited with error; output:\n" << run.output;

    // Verify the sample received a notification for the test file.
    EXPECT_NE(run.output.find("wait_notification_rc=1"), std::string::npos)
        << "sample did not receive notification; output:\n" << run.output;
    EXPECT_NE(run.output.find("notify_action="), std::string::npos)
        << "sample did not report notification action; output:\n" << run.output;

    // The notification should report the test filename.
    std::string expected_name = "notify_name=test_notify_file.txt";
    EXPECT_NE(run.output.find(expected_name), std::string::npos)
        << "sample did not report expected filename; output:\n" << run.output;

    // Cleanup.
    std::filesystem::remove(notify_sample_pilcfg_path(), ec);
    std::filesystem::remove_all(base, ec);
}
