// Copyright (c) Microsoft Corporation.
//
// Filesystem sample client for the mwin32 link-time alias object (M-FS-SHIM-8).
//
// This is an ORDINARY Win32 filesystem client: it includes only <windows.h> and
// calls the genuine filesystem entry points (CreateDirectoryW, CreateFileW,
// GetFileAttributesExW, FindFirstFileW / FindClose, MoveFileExW) plus an unrelated
// kernel object call (CreateEventW / CloseHandle). It has no knowledge of mwin32
// and includes none of its headers. The only thing that makes its filesystem calls
// redirectable is that its CMake target links the `mwin32_alias` object, whose
// __imp_ slots retarget those calls into the mwin32 shim. `CloseHandle` is left
// un-aliased by design (mwin32.def marks it `; noalias`), so the kernel-object
// CloseHandle below always reaches the real API.
//
// The mode (here: a redirecting + buffered filesystem with a capture snapshot) is
// chosen entirely outside this program by the `<executable>.pilcfg` sidecar the
// host environment places next to it. The same binary therefore exercises the
// whole filesystem shim ABI without ever touching the live disk: its writes land
// in the overlay under the redirected (private) path and are persisted to the
// snapshot, never reaching the public path on the real filesystem.
//
// It performs a small, representative workload and reports each observation as a
// machine-parseable line on stdout so a harness can assert what the client saw.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{
    // The single output site for the whole program. Per the repository's
    // architectural pre-step rule, all reporting is routed through one sink so the
    // formatting/destination concern is separable from the call sites. Lines are
    // emitted as `tag=value` so a harness can parse them without ambiguity.
    class reporter
    {
    public:
        explicit reporter(std::FILE* out) noexcept : m_out(out) {}

        void
        kv(std::string_view tag, std::string_view value) const
        {
            std::fprintf(m_out, "%.*s=%.*s\n",
                         static_cast<int>(tag.size()), tag.data(),
                         static_cast<int>(value.size()), value.data());
        }

        void
        kv(std::string_view tag, unsigned long value) const
        {
            std::fprintf(m_out, "%.*s=%lu\n",
                         static_cast<int>(tag.size()), tag.data(), value);
        }

    private:
        std::FILE* m_out;
    };

    // The public workload paths the sample reads and writes. The names are fixed
    // (not process-unique) so a capture run and a later inspection agree on exactly
    // which namespace to look for, and deliberately unusual so they cannot collide
    // with a real top-level entry on the live drive. The leading "C:" anchors the
    // path at a drive root that exists on the host (so the buffered overlay can
    // open it); the actual create/move all land in the overlay under the redirected
    // private prefix, never on the real disk.
    constexpr wchar_t k_public_dir[]   = L"C:\\mwin32_pub_root\\work";
    constexpr wchar_t k_public_file[]  = L"C:\\mwin32_pub_root\\work\\data.bin";
    constexpr wchar_t k_public_moved[] = L"C:\\mwin32_pub_root\\work\\data_renamed.bin";
    constexpr wchar_t k_find_pattern[] = L"C:\\mwin32_pub_root\\work\\*";

    // Convert a narrow ASCII view of a wide string for reporting. The sample's
    // payload is ASCII, so a straight narrowing is sufficient and avoids dragging
    // in locale conversion.
    std::string
    narrow(std::wstring_view w)
    {
        std::string s;
        s.reserve(w.size());
        for (wchar_t c: w)
            s.push_back(c <= 0x7f ? static_cast<char>(c) : '?');
        return s;
    }
}

int
wmain()
{
    const reporter report(stdout);

    // 1) Create the workload directory through the shim. The redirecting layer
    // maps the public prefix to the private prefix; the buffered layer creates the
    // (auto-vivified) intermediate components in the overlay.
    BOOL ok = ::CreateDirectoryW(k_public_dir, nullptr);
    report.kv("mkdir_rc", static_cast<unsigned long>(ok ? 1u : 0u));
    if (!ok)
        report.kv("mkdir_gle", ::GetLastError());

    // 2) Read the directory's metadata back and confirm it round-trips as a
    // directory node.
    WIN32_FILE_ATTRIBUTE_DATA dir_info{};
    ok = ::GetFileAttributesExW(k_public_dir, GetFileExInfoStandard, &dir_info);
    report.kv("dir_getattr_rc", static_cast<unsigned long>(ok ? 1u : 0u));
    if (ok)
        report.kv("dir_is_directory",
                  static_cast<unsigned long>(
                      (dir_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1u : 0u));

    // 3) Create a file in the workload directory through the shim. CREATE_ALWAYS
    // maps to the create-file verb; the returned HANDLE is a shim-minted pseudo
    // handle interned in the handle table.
    HANDLE file = ::CreateFileW(k_public_file,
                                GENERIC_WRITE,
                                0,
                                nullptr,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    report.kv("create_file_rc",
              static_cast<unsigned long>((file != INVALID_HANDLE_VALUE) ? 1u : 0u));
    if (file == INVALID_HANDLE_VALUE)
        report.kv("create_file_gle", ::GetLastError());

    // 4) Read the file's metadata back. It must be present and report as a file
    // (not a directory) — proving the create captured into the overlay and the
    // metadata round-trips through the shim ABI. Content is out of scope (D14), so
    // the size is expected to be zero.
    WIN32_FILE_ATTRIBUTE_DATA file_info{};
    ok = ::GetFileAttributesExW(k_public_file, GetFileExInfoStandard, &file_info);
    report.kv("file_getattr_rc", static_cast<unsigned long>(ok ? 1u : 0u));
    if (ok)
    {
        report.kv("file_is_directory",
                  static_cast<unsigned long>(
                      (file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1u : 0u));
        report.kv("file_size_low", static_cast<unsigned long>(file_info.nFileSizeLow));
    }

    // 5) Enumerate the directory and report the leaf the listing returned, proving
    // the created file is visible through the Find family.
    WIN32_FIND_DATAW find_data{};
    HANDLE           find = ::FindFirstFileW(k_find_pattern, &find_data);
    report.kv("find_rc", static_cast<unsigned long>((find != INVALID_HANDLE_VALUE) ? 1u : 0u));
    if (find != INVALID_HANDLE_VALUE)
    {
        // Report every non-dot entry the listing yields so the harness can confirm
        // the data file appears (the overlay may also surface "." / ".." which the
        // harness ignores).
        do
        {
            std::wstring_view const leaf(find_data.cFileName);
            if (leaf != L"." && leaf != L"..")
                report.kv("find_name", narrow(leaf));
        } while (::FindNextFileW(find, &find_data));
        ::FindClose(find);
    }

    // 6) Rename the file within the directory through the shim and confirm the old
    // name is gone and the new name resolves — exercising MoveFileExW end to end.
    ok = ::MoveFileExW(k_public_file, k_public_moved, MOVEFILE_REPLACE_EXISTING);
    report.kv("move_rc", static_cast<unsigned long>(ok ? 1u : 0u));
    if (!ok)
        report.kv("move_gle", ::GetLastError());

    WIN32_FILE_ATTRIBUTE_DATA probe{};
    ok = ::GetFileAttributesExW(k_public_file, GetFileExInfoStandard, &probe);
    report.kv("old_after_move_rc", static_cast<unsigned long>(ok ? 1u : 0u));

    ok = ::GetFileAttributesExW(k_public_moved, GetFileExInfoStandard, &probe);
    report.kv("new_after_move_rc", static_cast<unsigned long>(ok ? 1u : 0u));

    // 7) A kernel object whose CloseHandle must reach the real API. CreateEventW is
    // not aliased, so this is a genuine event handle; CloseHandle is `; noalias` in
    // mwin32.def, so closing it bypasses the shim and reaches ::CloseHandle. A
    // success here proves a non-file handle's CloseHandle is not captured by the
    // filesystem shim.
    HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    report.kv("event_create_rc", static_cast<unsigned long>((event != nullptr) ? 1u : 0u));
    if (event != nullptr)
    {
        BOOL const closed = ::CloseHandle(event);
        report.kv("event_close_rc", static_cast<unsigned long>(closed ? 1u : 0u));
    }

    return 0;
}
