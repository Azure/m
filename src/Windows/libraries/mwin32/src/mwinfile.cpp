// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <m/cp_acp/convert_acp_to.h>
#include <m/cp_acp/convert_to_acp.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>

#include <m/mwin32/mwinfile.h>

#include <lzexpand.h>

#include "handle_table.h"
#include "session.h"
#include "win32_error_mapping.h"

//
// Win32 filesystem API shim (mwin32 D11), non-handle metadata / namespace
// family (M-FS-SHIM-2). Each entry point mirrors the genuine <Windows.h>
// signature, converts its arguments into the platform-neutral PIL filesystem
// surface, and routes through the process-wide session's ifilesystem (the
// active passthrough / buffered / redirecting / logging / fault provider chosen
// by the .pilcfg sidecar).
//
// D11 handle-translation boundary: these entry points deal only in *paths* and
// *metadata*; none of them mint or consume a HANDLE, so the handle table is not
// involved. The CreateFile / Find families (later items) are where the table
// participates.
//
// Failure model (M-FS-SHIM-3): unlike the registry shims (which return an
// LSTATUS), the filesystem APIs report failure the Win32 way — a BOOL / sentinel
// return plus ::SetLastError. Every entry point therefore has a catch-all that
// translates the in-flight PIL exception into a Win32 last-error code and
// returns the failure sentinel; no exception (including OOM) is ever allowed to
// cross the C ABI.
//

namespace
{
    //
    // Translate the in-flight C++ exception raised while servicing a filesystem
    // entry point into a Win32 last-error DWORD. MUST be called from within a
    // catch block: map_known_pil_exception rethrows the active exception to
    // match its dynamic type. Anything the shim does not recognize collapses to
    // ERROR_GEN_FAILURE so that nothing escapes across the C ABI.
    //
    DWORD
    filesystem_exception_to_win32()
    {
        auto const code = m::mwin32_impl::map_known_pil_exception();
        return code.has_value() ? code.value() : static_cast<DWORD>(ERROR_GEN_FAILURE);
    }

    //
    // Path conversion. The wide (*W) overload passes UTF-16 straight through;
    // the ANSI (*A) overload interprets its narrow string in the process ANSI
    // code page (CP_ACP), matching the documented behavior of the Win32 *A file
    // APIs (the same convention mwinreg.cpp's to_key_path uses).
    //
    m::pil::file_path
    to_file_path(LPCWSTR p)
    {
        return m::pil::file_path(p);
    }

    m::pil::file_path
    to_file_path(LPCSTR p)
    {
        auto const u16 = m::acp_to_basic_string<char16_t>(p);
        return m::pil::file_path(std::u16string_view(u16));
    }

    //
    // Open the PIL root directory that `path` is anchored at. A path with no
    // root (a process-CWD-relative path) is out of scope for this milestone:
    // open_root on a none root surfaces as a provider error, which the caller's
    // catch-all converts to a Win32 last-error.
    //
    std::shared_ptr<m::pil::idirectory>
    open_root_for(m::pil::file_path const& path)
    {
        auto const fs = m::mwin32_impl::session_filesystem();
        return fs->open_root(path.root());
    }

    //
    // Convert a PIL utc_clock time point into a Win32 FILETIME.
    //
    // The PIL filesystem surface stores timestamps as m::pil::time_point_type
    // (utc_clock). A provider produced this value, when it read a node, via
    // m::clock_cast<m::pil::clock_type>(FILETIME); per
    // m::win32::filetime_clock::to_utc that is simply the FILETIME 100ns tick
    // count re-expressed against the 1970 epoch (utc_100ns = ft_100ns minus the
    // 1601->1970 offset). This inverts that exactly, so a timestamp read from a
    // node round-trips back to the identical FILETIME.
    //
    // We deliberately do NOT use m::to<FILETIME>(time_point): that conversion
    // emits a *relative* (negative) FILETIME for threadpool timers, not the
    // absolute file-timestamp representation required here.
    //
    FILETIME
    to_filetime(m::pil::time_point_type tp)
    {
        using ft_ticks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;

        // 1601-01-01 -> 1970-01-01 expressed in 100ns units.
        constexpr std::int64_t filetime_epoch_offset_100ns = 116'444'736'000'000'000;

        auto const utc_100ns = std::chrono::duration_cast<ft_ticks>(tp.time_since_epoch()).count();
        std::int64_t const ticks = utc_100ns + filetime_epoch_offset_100ns;
        return std::bit_cast<FILETIME>(ticks);
    }

    //
    // Project PIL file metadata onto the Win32 attribute bitmask. file_attributes
    // values mirror FILE_ATTRIBUTE_* exactly, so the cast is direct; the
    // directory bit is forced on for a directory node, and an otherwise-empty
    // mask collapses to FILE_ATTRIBUTE_NORMAL (Win32 never reports 0 for an
    // existing file).
    //
    DWORD
    to_win32_attributes(m::pil::file_metadata const& md)
    {
        DWORD attrs = static_cast<DWORD>(std::to_underlying(md.m_attributes));

        if (md.is_directory())
            attrs |= FILE_ATTRIBUTE_DIRECTORY;

        if (attrs == 0)
            attrs = FILE_ATTRIBUTE_NORMAL;

        return attrs;
    }

    //
    // Fill a WIN32_FILE_ATTRIBUTE_DATA from PIL metadata (the payload of
    // GetFileExInfoStandard).
    //
    void
    fill_attribute_data(m::pil::file_metadata const& md, WIN32_FILE_ATTRIBUTE_DATA& out)
    {
        // High / low 32-bit halves of the 64-bit byte size.
        constexpr std::uint64_t low_dword_mask  = 0xFFFF'FFFFull;
        constexpr unsigned      high_dword_shift = 32;

        out.dwFileAttributes = to_win32_attributes(md);
        out.ftCreationTime   = to_filetime(md.m_creation_time);
        out.ftLastAccessTime = to_filetime(md.m_last_access_time);
        out.ftLastWriteTime  = to_filetime(md.m_last_write_time);
        out.nFileSizeHigh    = static_cast<DWORD>(md.m_size >> high_dword_shift);
        out.nFileSizeLow     = static_cast<DWORD>(md.m_size & low_dword_mask);
    }

    //
    // "Stat" an arbitrary path. The PIL surface has no stat-by-path verb, so a
    // path is probed as a directory first, then as a file, both with
    // tolerate_not_found so absence is a non-error (null) outcome rather than an
    // exception:
    //   * the empty relative path names the root itself -> query the root;
    //   * a directory open that yields a node -> directory metadata;
    //   * otherwise a file open that yields a node -> file metadata;
    //   * neither present (no hard error) -> nullopt (caller maps to not-found).
    // A genuine error from the file probe (e.g. access denied) propagates as an
    // exception for the entry point's catch-all to translate.
    //
    std::optional<m::pil::file_metadata>
    query_path_metadata(m::pil::file_path const& path)
    {
        auto const root = open_root_for(path);

        auto rel = path.relative_path();
        if (rel.empty())
            return root->query_information();

        m::pil::file_path const rel_path{std::move(rel)};

        {
            std::shared_ptr<m::pil::idirectory> dir;
            std::error_code                     ec;
            root->open_directory(m::pil::idirectory::open_directory_flags::tolerate_not_found,
                                 rel_path,
                                 m::pil::file_access::default_open,
                                 dir,
                                 ec);
            // A null directory means "not a directory at this path": it is
            // absent, or it is a file (the provider reports the wrong-kind case
            // through ec). Either way fall through to the file probe; a real
            // error, if any, resurfaces there.
            if (dir)
                return dir->query_information();
        }

        {
            std::shared_ptr<m::pil::ifile> file;
            std::error_code                ec;
            root->open_file(m::pil::idirectory::open_file_flags::tolerate_not_found,
                            rel_path,
                            m::pil::file_access::default_open,
                            file,
                            ec);
            if (file)
                return file->query_information();
            if (ec)
                throw std::system_error(ec);
        }

        return std::nullopt;
    }
} // namespace

BOOL APIENTRY
mCreateDirectoryW(_In_ LPCWSTR lpPathName, _In_opt_ LPSECURITY_ATTRIBUTES)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);

        auto const path = to_file_path(lpPathName);
        auto const root = open_root_for(path);
        root->create_directory(m::pil::file_path{path.relative_path()});
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mCreateDirectoryA(_In_ LPCSTR lpPathName, _In_opt_ LPSECURITY_ATTRIBUTES)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);

        auto const path = to_file_path(lpPathName);
        auto const root = open_root_for(path);
        root->create_directory(m::pil::file_path{path.relative_path()});
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

//
// mRemoveDirectory and mDeleteFile both delegate to the unified-namespace (D13)
// remove_entry verb, which removes whichever kind of node the name refers to.
// The genuine APIs enforce the directory/file distinction; the shim does not at
// this milestone (the provider removes the named node regardless of kind).
//
BOOL APIENTRY
mRemoveDirectoryW(_In_ LPCWSTR lpPathName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);

        auto const path = to_file_path(lpPathName);
        auto const root = open_root_for(path);
        root->remove_entry(m::pil::file_path{path.relative_path()});
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mRemoveDirectoryA(_In_ LPCSTR lpPathName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);

        auto const path = to_file_path(lpPathName);
        auto const root = open_root_for(path);
        root->remove_entry(m::pil::file_path{path.relative_path()});
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mDeleteFileW(_In_ LPCWSTR lpFileName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        auto const path = to_file_path(lpFileName);
        auto const root = open_root_for(path);
        root->remove_entry(m::pil::file_path{path.relative_path()});
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mDeleteFileA(_In_ LPCSTR lpFileName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        auto const path = to_file_path(lpFileName);
        auto const root = open_root_for(path);
        root->remove_entry(m::pil::file_path{path.relative_path()});
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

namespace
{
    //
    // Shared rename implementation for mMoveFile / mMoveFileEx. The move is
    // scoped to a single root (D11): both paths are interpreted relative to the
    // source root, so a cross-volume move is rejected with ERROR_NOT_SAME_DEVICE
    // rather than silently misrouted. dwFlags (MOVEFILE_REPLACE_EXISTING and the
    // rest) are not honored this milestone: rename_entry has no replace mode, so
    // moving onto an existing target fails the way a no-replace rename does.
    //
    BOOL
    move_file_impl(m::pil::file_path const& src, m::pil::file_path const& dst)
    {
        if (!(src.root() == dst.root()))
        {
            ::SetLastError(ERROR_NOT_SAME_DEVICE);
            return FALSE;
        }

        auto const root = open_root_for(src);
        root->rename_entry(m::pil::file_path{src.relative_path()},
                           m::pil::file_path{dst.relative_path()});
        return TRUE;
    }
} // namespace

BOOL APIENTRY
mMoveFileW(_In_ LPCWSTR lpExistingFileName, _In_ LPCWSTR lpNewFileName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpExistingFileName, lpExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(lpNewFileName, lpNewFileName != nullptr);

        return move_file_impl(to_file_path(lpExistingFileName), to_file_path(lpNewFileName));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mMoveFileA(_In_ LPCSTR lpExistingFileName, _In_ LPCSTR lpNewFileName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpExistingFileName, lpExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(lpNewFileName, lpNewFileName != nullptr);

        return move_file_impl(to_file_path(lpExistingFileName), to_file_path(lpNewFileName));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

//
// mMoveFileEx ignores dwFlags (see move_file_impl). A NULL lpNewFileName (the
// genuine API's delete-on-reboot request) is not supported and is rejected.
//
BOOL APIENTRY
mMoveFileExW(_In_ LPCWSTR lpExistingFileName, _In_opt_ LPCWSTR lpNewFileName, _In_ DWORD)
{
    try
    {
        M_VALIDATE_PARAMETER(lpExistingFileName, lpExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(lpNewFileName, lpNewFileName != nullptr);

        return move_file_impl(to_file_path(lpExistingFileName), to_file_path(lpNewFileName));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mMoveFileExA(_In_ LPCSTR lpExistingFileName, _In_opt_ LPCSTR lpNewFileName, _In_ DWORD)
{
    try
    {
        M_VALIDATE_PARAMETER(lpExistingFileName, lpExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(lpNewFileName, lpNewFileName != nullptr);

        return move_file_impl(to_file_path(lpExistingFileName), to_file_path(lpNewFileName));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

DWORD APIENTRY
mGetFileAttributesW(_In_ LPCWSTR lpFileName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        auto const md = query_path_metadata(to_file_path(lpFileName));
        if (!md.has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_FILE_ATTRIBUTES;
        }

        return to_win32_attributes(md.value());
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_FILE_ATTRIBUTES;
    }
}

DWORD APIENTRY
mGetFileAttributesA(_In_ LPCSTR lpFileName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        auto const md = query_path_metadata(to_file_path(lpFileName));
        if (!md.has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_FILE_ATTRIBUTES;
        }

        return to_win32_attributes(md.value());
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_FILE_ATTRIBUTES;
    }
}

//
// Only GetFileExInfoStandard is supported; any other level is rejected.
//
BOOL APIENTRY
mGetFileAttributesExW(_In_ LPCWSTR                lpFileName,
                      _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
                      _Out_writes_bytes_(sizeof(WIN32_FILE_ATTRIBUTE_DATA))
                          LPVOID lpFileInformation)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);
        M_VALIDATE_PARAMETER(lpFileInformation, lpFileInformation != nullptr);
        M_VALIDATE_PARAMETER(fInfoLevelId, fInfoLevelId == GetFileExInfoStandard);

        auto const md = query_path_metadata(to_file_path(lpFileName));
        if (!md.has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return FALSE;
        }

        fill_attribute_data(md.value(),
                            *static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(lpFileInformation));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mGetFileAttributesExA(_In_ LPCSTR                 lpFileName,
                      _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
                      _Out_writes_bytes_(sizeof(WIN32_FILE_ATTRIBUTE_DATA))
                          LPVOID lpFileInformation)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);
        M_VALIDATE_PARAMETER(lpFileInformation, lpFileInformation != nullptr);
        M_VALIDATE_PARAMETER(fInfoLevelId, fInfoLevelId == GetFileExInfoStandard);

        auto const md = query_path_metadata(to_file_path(lpFileName));
        if (!md.has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return FALSE;
        }

        fill_attribute_data(md.value(),
                            *static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(lpFileInformation));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

namespace
{
    //
    // Project the Win32 dwDesiredAccess bitmask onto a PIL file_access. Only the
    // GENERIC_READ / GENERIC_WRITE intent is interpreted; the finer-grained
    // FILE_* access rights are out of scope under PIL isolation. A request that
    // names neither (dwDesiredAccess == 0, the "query attributes only" form)
    // maps to read access so the open can still resolve metadata.
    //
    m::pil::file_access
    to_file_access(DWORD dwDesiredAccess)
    {
        bool const wants_read  = (dwDesiredAccess & GENERIC_READ) != 0;
        bool const wants_write = (dwDesiredAccess & GENERIC_WRITE) != 0;

        if (wants_write && wants_read)
            return m::pil::file_access::read_write;
        if (wants_write)
            return m::pil::file_access::write;
        return m::pil::file_access::read;
    }

    //
    // Shared body of mCreateFileW / mCreateFileA. Maps dwCreationDisposition
    // onto the PIL open_file vs create_file verbs and interns the resulting
    // ifile in the global handle table (D11), returning the minted HANDLE.
    //
    // Disposition mapping (D12 - each verb interprets its own disposition at the
    // call site; there is no shared disposition table):
    //   * OPEN_EXISTING / TRUNCATE_EXISTING -> open_file (must already exist);
    //   * CREATE_NEW / CREATE_ALWAYS        -> create_file (fail-if-exists vs
    //         replace fidelity is delegated to the provider's create_file and
    //         not separately enforced here);
    //   * OPEN_ALWAYS -> try_open_file, falling back to create_file when absent.
    // Any other value is rejected as an invalid parameter.
    //
    // Content is out of scope (D14): TRUNCATE_EXISTING does not truncate, and
    // the minted handle resolves metadata only.
    //
    // A handle opened with FILE_FLAG_BACKUP_SEMANTICS names a directory (the
    // form ReadDirectoryChangesW requires). Such a handle stores only the
    // directory's public path (m_file stays null); it is consumed solely by the
    // change-notification family (M-FS-NOTIFY), which registers a watch on the
    // stored path. The byte-content / handle-metadata families do not accept it.
    //
    HANDLE
    create_file_impl(m::pil::file_path const& path,
                     DWORD                    dwDesiredAccess,
                     DWORD                    dwCreationDisposition,
                     DWORD                    dwFlagsAndAttributes)
    {
        if ((dwFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS) != 0)
        {
            auto const md = query_path_metadata(path);
            if (!md.has_value() || !md->is_directory())
            {
                ::SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_HANDLE_VALUE;
            }

            auto state    = std::make_shared<m::mwin32_impl::file_handle_state>();
            state->m_path = path;
            return ::g_handles.intern(state).as_HANDLE();
        }

        auto const root   = open_root_for(path);
        auto const access = to_file_access(dwDesiredAccess);

        m::pil::file_path const rel{path.relative_path()};

        std::shared_ptr<m::pil::ifile> file;

        switch (dwCreationDisposition)
        {
        case OPEN_EXISTING:
        case TRUNCATE_EXISTING:
            file = root->open_file(rel, access);
            break;

        case CREATE_NEW:
        case CREATE_ALWAYS:
            file = root->create_file(rel, access);
            break;

        case OPEN_ALWAYS:
            file = root->try_open_file(rel, access);
            if (!file)
                file = root->create_file(rel, access);
            break;

        default:
            M_VALIDATE_PARAMETER(dwCreationDisposition, false);
            break;
        }

        auto state     = std::make_shared<m::mwin32_impl::file_handle_state>();
        state->m_file  = std::move(file);
        state->m_path  = path;
        return ::g_handles.intern(state).as_HANDLE();
    }
} // namespace

HANDLE APIENTRY
mCreateFileW(_In_ LPCWSTR                   lpFileName,
             _In_ DWORD                     dwDesiredAccess,
             _In_ DWORD                     dwShareMode,
             _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
             _In_ DWORD                     dwCreationDisposition,
             _In_ DWORD                     dwFlagsAndAttributes,
             _In_opt_ HANDLE                hTemplateFile)
{
    (void)dwShareMode;
    (void)lpSecurityAttributes;
    (void)hTemplateFile;

    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        return create_file_impl(
            to_file_path(lpFileName), dwDesiredAccess, dwCreationDisposition, dwFlagsAndAttributes);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_HANDLE_VALUE;
    }
}

HANDLE APIENTRY
mCreateFileA(_In_ LPCSTR                    lpFileName,
             _In_ DWORD                     dwDesiredAccess,
             _In_ DWORD                     dwShareMode,
             _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
             _In_ DWORD                     dwCreationDisposition,
             _In_ DWORD                     dwFlagsAndAttributes,
             _In_opt_ HANDLE                hTemplateFile)
{
    (void)dwShareMode;
    (void)lpSecurityAttributes;
    (void)hTemplateFile;

    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        return create_file_impl(
            to_file_path(lpFileName), dwDesiredAccess, dwCreationDisposition, dwFlagsAndAttributes);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_HANDLE_VALUE;
    }
}

namespace
{
    //
    // Dusty-deck legacy open / create family (M-FS-LEGACY-1). OpenFile / _lopen
    // / _lcreat predate the Win32 HANDLE world: they hand back an HFILE (a plain
    // int). We mint that HFILE from the *same* handle_table as mCreateFile, so a
    // legacy handle is just a minted pseudo-handle narrowed to int — the
    // reserved encoding keeps every minted value inside 31 bits (bit 30 set,
    // nothing at or above bit 31), so the narrowing is lossless and positive.
    //

    //
    // Non-error HFILE for the OpenFile modifier styles (OF_PARSE / OF_EXIST /
    // OF_DELETE) that complete without handing back a live handle. Any value
    // other than HFILE_ERROR signals success to a dusty-deck caller; zero
    // carries no live table entry, so the caller cannot translate it back to a
    // node.
    //
    constexpr HFILE k_openfile_ok = 0;

    HFILE
    handle_to_hfile(HANDLE h)
    {
        return static_cast<HFILE>(reinterpret_cast<std::uintptr_t>(h));
    }

    //
    // Map the OpenFile / _lopen access style (low bits OF_READ / OF_WRITE /
    // OF_READWRITE) onto the Win32 generic-access mask create_file_impl expects.
    // The share-mode styles (OF_SHARE_*) are ignored under PIL isolation.
    //
    DWORD
    openfile_access(UINT uStyle)
    {
        switch (uStyle & (OF_WRITE | OF_READWRITE))
        {
        case OF_WRITE:
            return GENERIC_WRITE;
        case OF_READWRITE:
            return GENERIC_READ | GENERIC_WRITE;
        default:
            return GENERIC_READ; // OF_READ == 0
        }
    }

    //
    // Fill the caller's OFSTRUCT with the public path it passed (ANSI, truncated
    // to OFS_MAXPATHNAME). The path stored is the caller's pre-redirection path,
    // matching how file_handle_state records the public path (D11 private->
    // public): a dusty-deck caller reading szPathName back sees what it asked
    // for, never the private backing path.
    //
    void
    fill_ofstruct(OFSTRUCT& ofs, LPCSTR name)
    {
        std::memset(&ofs, 0, sizeof(ofs));
        ofs.cBytes     = static_cast<BYTE>(sizeof(OFSTRUCT));
        ofs.fFixedDisk = TRUE;

        auto const n =
            (std::min)(std::strlen(name), static_cast<std::size_t>(OFS_MAXPATHNAME - 1));
        std::memcpy(ofs.szPathName, name, n);
        ofs.szPathName[n] = '\0';
    }

    //
    // Shared body of mOpenFile after the OFSTRUCT has been populated. May throw;
    // the entry point's catch-all maps the exception to a Win32 last-error and
    // returns HFILE_ERROR.
    //
    HFILE
    open_file_legacy(LPCSTR name, UINT uStyle)
    {
        // OF_PARSE: the structure is already filled; perform no open.
        if ((uStyle & OF_PARSE) != 0)
            return k_openfile_ok;

        // OF_DELETE: namespace delete. Reuse the metadata-family delete shim so
        // the provider routing and the last-error contract match mDeleteFile
        // exactly (it reports its own failure, so a false result maps straight
        // to HFILE_ERROR with last-error already set).
        if ((uStyle & OF_DELETE) != 0)
            return ::mDeleteFileA(name) ? k_openfile_ok : HFILE_ERROR;

        auto const  path        = to_file_path(name);
        DWORD const disposition = ((uStyle & OF_CREATE) != 0) ? CREATE_ALWAYS : OPEN_EXISTING;

        HANDLE const h = create_file_impl(path, openfile_access(uStyle), disposition, 0);

        // OF_EXIST: existence probe only. The open above already proved the file
        // is present (or threw); release the minted handle and report success
        // without leaking a live table entry.
        if ((uStyle & OF_EXIST) != 0)
        {
            ::g_handles.close(m::mwin32_impl::handle::from_HANDLE(h));
            return k_openfile_ok;
        }

        return handle_to_hfile(h);
    }
} // namespace

HFILE APIENTRY
mOpenFile(_In_ LPCSTR lpFileName, _Out_ LPOFSTRUCT lpReOpenBuff, _In_ UINT uStyle)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        if (lpReOpenBuff != nullptr)
            fill_ofstruct(*lpReOpenBuff, lpFileName);

        return open_file_legacy(lpFileName, uStyle);
    }
    catch (...)
    {
        DWORD const err = filesystem_exception_to_win32();
        if (lpReOpenBuff != nullptr)
            lpReOpenBuff->nErrCode = static_cast<WORD>(err);
        ::SetLastError(err);
        return HFILE_ERROR;
    }
}

HFILE APIENTRY
m_lopen(_In_ LPCSTR lpPathName, _In_ int iReadWrite)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);

        auto const   path = to_file_path(lpPathName);
        HANDLE const h =
            create_file_impl(path, openfile_access(static_cast<UINT>(iReadWrite)), OPEN_EXISTING, 0);
        return handle_to_hfile(h);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return HFILE_ERROR;
    }
}

HFILE APIENTRY
m_lcreat(_In_ LPCSTR lpPathName, _In_ int iAttribute)
{
    // iAttribute carries the legacy file-attribute byte (0 normal, 1 read-only,
    // 2 hidden, 4 system). Metadata-write is not modeled on the PIL surface, so
    // the attribute is accepted and ignored; the freshly created file is opened
    // for read/write as the genuine _lcreat returns a writable handle.
    (void)iAttribute;

    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);

        auto const   path = to_file_path(lpPathName);
        HANDLE const h =
            create_file_impl(path, GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS, 0);
        return handle_to_hfile(h);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return HFILE_ERROR;
    }
}

namespace
{
    //
    // Open the PIL directory named by `dir_path`. An empty relative path names
    // the root itself; otherwise the directory is opened relative to its root.
    //
    std::shared_ptr<m::pil::idirectory>
    open_directory_at(m::pil::file_path const& dir_path)
    {
        auto const root = open_root_for(dir_path);

        auto rel = dir_path.relative_path();
        if (rel.empty())
            return root;

        return root->open_directory(m::pil::file_path{std::move(rel)});
    }

    //
    // Buffer the full child listing of the directory named by `pattern`'s parent
    // into a fresh find-enumeration state. The pattern's leaf component (the
    // wildcard or literal name) is not applied this milestone: every child is
    // captured. A pattern with no parent (a rootless single component) is out of
    // scope and rejected as an invalid parameter.
    //
    std::shared_ptr<m::mwin32_impl::find_enumeration_state>
    capture_directory_listing(m::pil::file_path const& pattern)
    {
        auto const [parent, leaf] = pattern.split_parent_path_and_leaf_name();
        (void)leaf;
        M_VALIDATE_PARAMETER(pattern, parent.has_value());

        auto const dir = open_directory_at(parent.value());

        auto state = std::make_shared<m::mwin32_impl::find_enumeration_state>();
        for (std::size_t i = 0;; ++i)
        {
            auto entry = dir->enumerate_entries(i);
            if (!entry.has_value())
                break;
            state->m_entries.push_back(std::move(entry.value()));
        }

        return state;
    }

    //
    // Common WIN32_FIND_DATA scalar fields shared by the W and A forms (the only
    // difference between them is the cFileName character type, filled by the
    // caller). The output struct is zeroed first so cAlternateFileName and the
    // reserved fields are left clear (short names are not modeled).
    //
    template <typename TFindData>
    void
    fill_find_data_common(m::pil::directory_entry const& entry, TFindData& out)
    {
        constexpr std::uint64_t low_dword_mask   = 0xFFFF'FFFFull;
        constexpr unsigned      high_dword_shift = 32;

        out = TFindData{};

        out.dwFileAttributes = to_win32_attributes(entry.m_metadata);
        out.ftCreationTime   = to_filetime(entry.m_metadata.m_creation_time);
        out.ftLastAccessTime = to_filetime(entry.m_metadata.m_last_access_time);
        out.ftLastWriteTime  = to_filetime(entry.m_metadata.m_last_write_time);
        out.nFileSizeHigh    = static_cast<DWORD>(entry.m_metadata.m_size >> high_dword_shift);
        out.nFileSizeLow     = static_cast<DWORD>(entry.m_metadata.m_size & low_dword_mask);
    }

    //
    // Fill a WIN32_FIND_DATAW from a PIL directory entry. The UTF-16 name is
    // copied into the fixed cFileName buffer and truncated (with a guaranteed
    // null terminator) if it does not fit.
    //
    void
    fill_find_data(m::pil::directory_entry const& entry, WIN32_FIND_DATAW& out)
    {
        fill_find_data_common(entry, out);

        auto const        sv = entry.m_name.view();
        std::size_t const n  = std::min<std::size_t>(sv.size(), MAX_PATH - 1);
        for (std::size_t i = 0; i < n; ++i)
            out.cFileName[i] = static_cast<WCHAR>(sv[i]);
        out.cFileName[n] = L'\0';
    }

    //
    // Fill a WIN32_FIND_DATAA from a PIL directory entry. The name is converted
    // from UTF-16 to the process ANSI code page (matching the *A API contract)
    // and copied into the fixed cFileName buffer, truncated with a guaranteed
    // null terminator if it does not fit.
    //
    void
    fill_find_data(m::pil::directory_entry const& entry, WIN32_FIND_DATAA& out)
    {
        fill_find_data_common(entry, out);

        auto const        acp = m::to_acp_string(entry.m_name.view());
        std::size_t const n   = std::min<std::size_t>(acp.size(), MAX_PATH - 1);
        for (std::size_t i = 0; i < n; ++i)
            out.cFileName[i] = acp[i];
        out.cFileName[n] = '\0';
    }

    //
    // Shared body of mFindFirstFileW / mFindFirstFileA. Captures the directory
    // listing, fills the caller's find-data with the first entry, interns the
    // enumeration state, and returns the minted find handle. An empty listing is
    // reported as ERROR_FILE_NOT_FOUND with INVALID_HANDLE_VALUE, matching the
    // genuine API.
    //
    template <typename TFindData>
    HANDLE
    find_first_file_impl(m::pil::file_path const& pattern, TFindData& out)
    {
        auto state = capture_directory_listing(pattern);

        if (state->m_entries.empty())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }

        fill_find_data(state->m_entries[0], out);
        state->m_cursor = 1;

        return ::g_handles.intern(state).as_HANDLE();
    }

    //
    // Shared body of mFindNextFileW / mFindNextFileA. Advances the cursor of the
    // enumeration state behind `hFindFile`, filling the caller's find-data with
    // the next entry. ERROR_NO_MORE_FILES is reported (FALSE return) once the
    // listing is exhausted.
    //
    template <typename TFindData>
    BOOL
    find_next_file_impl(HANDLE hFindFile, TFindData& out)
    {
        auto const state =
            ::g_handles
                .deref_handle<std::shared_ptr<m::mwin32_impl::find_enumeration_state>>(
                    m::mwin32_impl::handle::from_HANDLE(hFindFile));

        if (state->m_cursor >= state->m_entries.size())
        {
            ::SetLastError(ERROR_NO_MORE_FILES);
            return FALSE;
        }

        fill_find_data(state->m_entries[state->m_cursor], out);
        ++state->m_cursor;
        return TRUE;
    }
} // namespace

HANDLE APIENTRY
mFindFirstFileW(_In_ LPCWSTR lpFileName, _Out_ LPWIN32_FIND_DATAW lpFindFileData)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);
        M_VALIDATE_PARAMETER(lpFindFileData, lpFindFileData != nullptr);

        return find_first_file_impl(to_file_path(lpFileName), *lpFindFileData);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_HANDLE_VALUE;
    }
}

HANDLE APIENTRY
mFindFirstFileA(_In_ LPCSTR lpFileName, _Out_ LPWIN32_FIND_DATAA lpFindFileData)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);
        M_VALIDATE_PARAMETER(lpFindFileData, lpFindFileData != nullptr);

        return find_first_file_impl(to_file_path(lpFileName), *lpFindFileData);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_HANDLE_VALUE;
    }
}

BOOL APIENTRY
mFindNextFileW(_In_ HANDLE hFindFile, _Out_ LPWIN32_FIND_DATAW lpFindFileData)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFindFileData, lpFindFileData != nullptr);

        return find_next_file_impl(hFindFile, *lpFindFileData);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mFindNextFileA(_In_ HANDLE hFindFile, _Out_ LPWIN32_FIND_DATAA lpFindFileData)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFindFileData, lpFindFileData != nullptr);

        return find_next_file_impl(hFindFile, *lpFindFileData);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

//
// mFindClose releases the find-enumeration state behind the handle. A find
// handle is always one the shim minted, so it is closed directly through the
// handle table (no routing decision is needed here, unlike mCloseHandle).
//
BOOL APIENTRY
mFindClose(_In_ HANDLE hFindFile)
{
    try
    {
        ::g_handles.close(m::mwin32_impl::handle::from_HANDLE(hFindFile));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

//
// mCloseHandle routes by the handle's bit pattern (M-FS-SHIM-6). A value the
// shim minted (recognizable by handle_table's reserved encoding) is released
// from the table; every other value is a genuine OS handle and is forwarded to
// the real ::CloseHandle untouched. This is broader than mRegCloseKey: it sees
// all CloseHandle traffic, so it must leave non-shim handles to the real API.
//
BOOL APIENTRY
mCloseHandle(_In_ HANDLE hObject)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hObject);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::CloseHandle(hObject);

    try
    {
        ::g_handles.close(h);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

//
// Handle-based metadata family (M-FS-HANDLE-META). Each entry point consumes a
// HANDLE the shim minted via mCreateFile, so the D11 handle-translation
// invariant requires it be aliased: it resolves the pseudo-handle back to its
// backing PIL ifile (a genuine ::GetFileInformationByHandle on a minted handle
// would otherwise reach the real API and fail ERROR_INVALID_HANDLE) and serves
// the answer from ifile::query_information. None of these touch byte content
// (D14): size is the metadata size, never a content length.
//
// Metadata is read-only on the PIL surface this milestone (ifile exposes only
// query_information; there is no metadata-write verb). The Set* entry points
// therefore resolve the handle, validate their request, and report success
// without persisting any change — the same accept-and-ignore stance the shim
// takes for parameters isolation cannot model (dwShareMode, MOVEFILE flags).
// A real metadata-mutation verb is deferred to a future PIL milestone.
//
namespace
{
    //
    // Resolve a minted file HANDLE to its file-handle state (the backing ifile
    // plus the path it was opened with). A handle that is not a live file handle
    // (a find handle, a stale value, or a non-shim value) surfaces as
    // ERROR_INVALID_HANDLE from deref_handle, which the caller's catch-all turns
    // into the entry point's failure sentinel.
    //
    std::shared_ptr<m::mwin32_impl::file_handle_state>
    resolve_file_handle(HANDLE hFile)
    {
        return ::g_handles
            .deref_handle<std::shared_ptr<m::mwin32_impl::file_handle_state>>(
                m::mwin32_impl::handle::from_HANDLE(hFile));
    }

    //
    // The 64-bit byte size split into its Win32 high / low DWORD halves.
    //
    constexpr std::uint64_t size_low_dword_mask   = 0xFFFF'FFFFull;
    constexpr unsigned      size_high_dword_shift = 32;

    //
    // Reinterpret a FILETIME as the LARGE_INTEGER (100ns tick count) form used
    // by the FILE_*_INFO structures. The two layouts carry the identical 64 bits.
    //
    LARGE_INTEGER
    filetime_to_large_integer(FILETIME ft) noexcept
    {
        LARGE_INTEGER li;
        li.LowPart  = ft.dwLowDateTime;
        li.HighPart = static_cast<LONG>(ft.dwHighDateTime);
        return li;
    }
} // namespace

BOOL APIENTRY
mGetFileInformationByHandle(_In_ HANDLE                       hFile,
                            _Out_ LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileInformation, lpFileInformation != nullptr);

        auto const state = resolve_file_handle(hFile);
        auto const md    = state->m_file->query_information();

        *lpFileInformation = BY_HANDLE_FILE_INFORMATION{};

        lpFileInformation->dwFileAttributes = to_win32_attributes(md);
        lpFileInformation->ftCreationTime   = to_filetime(md.m_creation_time);
        lpFileInformation->ftLastAccessTime = to_filetime(md.m_last_access_time);
        lpFileInformation->ftLastWriteTime  = to_filetime(md.m_last_write_time);
        lpFileInformation->nFileSizeHigh =
            static_cast<DWORD>(md.m_size >> size_high_dword_shift);
        lpFileInformation->nFileSizeLow = static_cast<DWORD>(md.m_size & size_low_dword_mask);

        // PIL models no volume serial, file id, or hard-link count; report the
        // benign defaults (single link, zero ids) Win32 callers tolerate.
        lpFileInformation->dwVolumeSerialNumber = 0;
        lpFileInformation->nNumberOfLinks       = 1;
        lpFileInformation->nFileIndexHigh       = 0;
        lpFileInformation->nFileIndexLow        = 0;
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

DWORD APIENTRY
mGetFileSize(_In_ HANDLE hFile, _Out_opt_ LPDWORD lpFileSizeHigh)
{
    try
    {
        auto const state = resolve_file_handle(hFile);
        auto const md    = state->m_file->query_information();

        if (lpFileSizeHigh != nullptr)
            *lpFileSizeHigh = static_cast<DWORD>(md.m_size >> size_high_dword_shift);

        // The genuine API reports a valid low DWORD of 0xFFFFFFFF by also
        // clearing the last error; set it to NO_ERROR so a caller that checks
        // GetLastError after an INVALID_FILE_SIZE-looking low word is not misled.
        ::SetLastError(NO_ERROR);
        return static_cast<DWORD>(md.m_size & size_low_dword_mask);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_FILE_SIZE;
    }
}

BOOL APIENTRY
mGetFileSizeEx(_In_ HANDLE hFile, _Out_ PLARGE_INTEGER lpFileSize)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileSize, lpFileSize != nullptr);

        auto const state = resolve_file_handle(hFile);
        auto const md    = state->m_file->query_information();

        lpFileSize->QuadPart = static_cast<LONGLONG>(md.m_size);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

namespace
{
    //
    // Populate the FileBasicInfo / FileStandardInfo projections of a node's
    // metadata. These mirror the BY_HANDLE_FILE_INFORMATION fields in the
    // FILE_INFO_BY_HANDLE_CLASS shapes; ChangeTime has no PIL analogue so it
    // tracks the last-write time, and allocation size is reported equal to the
    // logical end-of-file (PIL models no on-disk allocation granularity).
    //
    void
    fill_basic_info(m::pil::file_metadata const& md, FILE_BASIC_INFO& info) noexcept
    {
        info.CreationTime   = filetime_to_large_integer(to_filetime(md.m_creation_time));
        info.LastAccessTime = filetime_to_large_integer(to_filetime(md.m_last_access_time));
        info.LastWriteTime  = filetime_to_large_integer(to_filetime(md.m_last_write_time));
        info.ChangeTime     = info.LastWriteTime;
        info.FileAttributes = to_win32_attributes(md);
    }

    void
    fill_standard_info(m::pil::file_metadata const& md, FILE_STANDARD_INFO& info) noexcept
    {
        info.AllocationSize.QuadPart = static_cast<LONGLONG>(md.m_size);
        info.EndOfFile.QuadPart      = static_cast<LONGLONG>(md.m_size);
        info.NumberOfLinks           = 1;
        info.DeletePending           = FALSE;
        info.Directory               = md.is_directory() ? TRUE : FALSE;
    }

    //
    // Project the handle's open path into the volume-relative form Win32
    // reports for FileNameInfo / mGetFinalPathNameByHandle: the path with its
    // drive root removed and a single leading separator (e.g. C:\dir\f ->
    // \dir\f). The stored path is the caller's public path, which is what a
    // caller that opened a public path expects to read back.
    //
    std::u16string
    volume_relative_name(m::pil::file_path const& path)
    {
        auto const rel = path.relative_path().view();
        std::u16string name;
        name.reserve(rel.size() + 1);
        name.push_back(u'\\');
        name.append(rel.data(), rel.size());
        return name;
    }

    //
    // The Win32 error reported for FILE_INFO_BY_HANDLE_CLASS values whose
    // backing data is byte content or on-disk allocation (FileAllocationInfo,
    // FileEndOfFileInfo, stream / compression classes). Content is deferred to a
    // future milestone (M-FS-CONTENT); until then these classes are explicitly
    // unsupported rather than silently mis-served.
    //
    constexpr DWORD deferred_content_error = ERROR_NOT_SUPPORTED;

    //
    // The extended-length ("\\?\C:\dir\file") form mGetFinalPathNameByHandle
    // reports for the default VOLUME_NAME_DOS request: the stored public path
    // prefixed with the "\\?\" device-namespace escape, exactly as the genuine
    // API renders a DOS-volume final path.
    //
    std::u16string
    dos_extended_path(m::pil::file_path const& path)
    {
        auto const native = path.native().view();
        std::u16string full;
        full.reserve(native.size() + 4);
        full.append(u"\\\\?\\");
        full.append(native.data(), native.size());
        return full;
    }

    //
    // Copy a final path into the caller's wide buffer with the Win32
    // GetFinalPathNameByHandle length contract: on success return the character
    // count written excluding the null; if the buffer cannot hold the string and
    // its null, write nothing and return the required size including the null.
    //
    DWORD
    copy_final_path_w(std::u16string const& full, LPWSTR buf, DWORD cch) noexcept
    {
        auto const len = static_cast<DWORD>(full.size());
        if (buf == nullptr || cch < len + 1)
            return len + 1;
        std::memcpy(buf, full.data(), static_cast<std::size_t>(len) * sizeof(WCHAR));
        buf[len] = L'\0';
        return len;
    }

    //
    // The ANSI counterpart of copy_final_path_w: the final path is converted to
    // the process ANSI code page (matching the *A API contract) and copied with
    // the identical length contract.
    //
    DWORD
    copy_final_path_a(std::u16string const& full, LPSTR buf, DWORD cch)
    {
        auto const acp = m::to_acp_string(std::u16string_view{full});
        auto const len = static_cast<DWORD>(acp.size());
        if (buf == nullptr || cch < len + 1)
            return len + 1;
        std::memcpy(buf, acp.data(), len);
        buf[len] = '\0';
        return len;
    }
} // namespace

BOOL APIENTRY
mGetFileInformationByHandleEx(_In_ HANDLE                    hFile,
                              _In_ FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                              _Out_writes_bytes_(dwBufferSize) LPVOID lpFileInformation,
                              _In_ DWORD                              dwBufferSize)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileInformation, lpFileInformation != nullptr);

        auto const state = resolve_file_handle(hFile);
        auto const md    = state->m_file->query_information();

        switch (FileInformationClass)
        {
        case FileBasicInfo:
            M_VALIDATE_PARAMETER(dwBufferSize, dwBufferSize >= sizeof(FILE_BASIC_INFO));
            fill_basic_info(md, *static_cast<FILE_BASIC_INFO*>(lpFileInformation));
            break;

        case FileStandardInfo:
            M_VALIDATE_PARAMETER(dwBufferSize, dwBufferSize >= sizeof(FILE_STANDARD_INFO));
            fill_standard_info(md, *static_cast<FILE_STANDARD_INFO*>(lpFileInformation));
            break;

        case FileAttributeTagInfo:
        {
            M_VALIDATE_PARAMETER(dwBufferSize, dwBufferSize >= sizeof(FILE_ATTRIBUTE_TAG_INFO));
            auto& info         = *static_cast<FILE_ATTRIBUTE_TAG_INFO*>(lpFileInformation);
            info.FileAttributes = to_win32_attributes(md);
            info.ReparseTag     = 0;
            break;
        }

        case FileNameInfo:
        {
            auto const  name       = volume_relative_name(state->m_path);
            auto const  name_bytes = name.size() * sizeof(WCHAR);
            std::size_t needed      = offsetof(FILE_NAME_INFO, FileName) + name_bytes;
            if (dwBufferSize < needed)
            {
                // Win32 reports a short buffer for FileNameInfo as ERROR_MORE_DATA;
                // the caller retries with a larger allocation.
                ::SetLastError(ERROR_MORE_DATA);
                return FALSE;
            }
            auto& info          = *static_cast<FILE_NAME_INFO*>(lpFileInformation);
            info.FileNameLength = static_cast<DWORD>(name_bytes);
            std::memcpy(info.FileName, name.data(), name_bytes);
            break;
        }

        default:
            // Content / allocation / stream and other unmodelled classes are
            // deferred (M-FS-CONTENT) rather than mis-served from metadata.
            ::SetLastError(deferred_content_error);
            return FALSE;
        }
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mSetFileInformationByHandle(_In_ HANDLE                    hFile,
                            _In_ FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                            _In_reads_bytes_(dwBufferSize) LPVOID lpFileInformation,
                            _In_ DWORD                            dwBufferSize)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileInformation, lpFileInformation != nullptr);
        M_VALIDATE_PARAMETER(dwBufferSize, dwBufferSize != 0);

        // Translate the pseudo-handle first (D11): a Set on a stale or foreign
        // handle must fail ERROR_INVALID_HANDLE before any class dispatch.
        auto const state = resolve_file_handle(hFile);
        (void)state;

        switch (FileInformationClass)
        {
        case FileBasicInfo:
        case FileRenameInfo:
        case FileDispositionInfo:
            // Metadata-mutation classes. PIL exposes no metadata-write verb this
            // milestone, so the request is accepted and reported successful
            // without persisting (the shim's accept-and-ignore stance for state
            // it cannot model). A real mutation verb is deferred to a future PIL
            // milestone.
            break;

        case FileAllocationInfo:
        case FileEndOfFileInfo:
        default:
            // Allocation / EOF resize byte content; those and any other class are
            // deferred (M-FS-CONTENT).
            ::SetLastError(deferred_content_error);
            return FALSE;
        }
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mGetFileTime(_In_ HANDLE       hFile,
             _Out_opt_ LPFILETIME lpCreationTime,
             _Out_opt_ LPFILETIME lpLastAccessTime,
             _Out_opt_ LPFILETIME lpLastWriteTime)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    // A non-minted value is a genuine OS handle; forward it untouched (D11: the
    // alias must not break a real handle handed to GetFileTime).
    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::GetFileTime(hFile, lpCreationTime, lpLastAccessTime, lpLastWriteTime);

    try
    {
        auto const state = resolve_file_handle(hFile);
        auto const md    = state->m_file->query_information();

        if (lpCreationTime != nullptr)
            *lpCreationTime = to_filetime(md.m_creation_time);
        if (lpLastAccessTime != nullptr)
            *lpLastAccessTime = to_filetime(md.m_last_access_time);
        if (lpLastWriteTime != nullptr)
            *lpLastWriteTime = to_filetime(md.m_last_write_time);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mSetFileTime(_In_ HANDLE                hFile,
             _In_opt_ CONST FILETIME* lpCreationTime,
             _In_opt_ CONST FILETIME* lpLastAccessTime,
             _In_opt_ CONST FILETIME* lpLastWriteTime)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::SetFileTime(hFile, lpCreationTime, lpLastAccessTime, lpLastWriteTime);

    try
    {
        // Translate the pseudo-handle (D11), then accept the request without
        // persisting: PIL exposes no metadata-write verb this milestone, so a
        // timestamp set is a documented no-op (deferred to a future PIL
        // milestone). The handle must still be valid for success to be reported.
        auto const state = resolve_file_handle(hFile);
        (void)state;
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

DWORD APIENTRY
mGetFileType(_In_ HANDLE hFile)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::GetFileType(hFile);

    try
    {
        // Every minted file handle names a buffered / redirected filesystem node;
        // those are disk-backed, so the genuine API's FILE_TYPE_DISK is the
        // faithful answer. A minted value that is not a file handle (a find or
        // registry handle) fails resolution and reports FILE_TYPE_UNKNOWN.
        auto const state = resolve_file_handle(hFile);
        (void)state;
        ::SetLastError(NO_ERROR);
        return FILE_TYPE_DISK;
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FILE_TYPE_UNKNOWN;
    }
}

DWORD APIENTRY
mGetFinalPathNameByHandleW(_In_ HANDLE                            hFile,
                           _Out_writes_(cchFilePath) LPWSTR        lpszFilePath,
                           _In_ DWORD                              cchFilePath,
                           _In_ DWORD                              dwFlags)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::GetFinalPathNameByHandleW(hFile, lpszFilePath, cchFilePath, dwFlags);

    try
    {
        // GUID / NT volume forms have no PIL analogue (the shim has no real
        // volume to name); only the DOS and volume-less forms are served.
        if ((dwFlags & (VOLUME_NAME_GUID | VOLUME_NAME_NT)) != 0)
        {
            ::SetLastError(deferred_content_error);
            return 0;
        }

        auto const           state = resolve_file_handle(hFile);
        std::u16string const full =
            ((dwFlags & VOLUME_NAME_NONE) != 0) ? volume_relative_name(state->m_path)
                                                : dos_extended_path(state->m_path);
        return copy_final_path_w(full, lpszFilePath, cchFilePath);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

DWORD APIENTRY
mGetFinalPathNameByHandleA(_In_ HANDLE                           hFile,
                           _Out_writes_(cchFilePath) LPSTR        lpszFilePath,
                           _In_ DWORD                             cchFilePath,
                           _In_ DWORD                             dwFlags)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::GetFinalPathNameByHandleA(hFile, lpszFilePath, cchFilePath, dwFlags);

    try
    {
        if ((dwFlags & (VOLUME_NAME_GUID | VOLUME_NAME_NT)) != 0)
        {
            ::SetLastError(deferred_content_error);
            return 0;
        }

        auto const           state = resolve_file_handle(hFile);
        std::u16string const full =
            ((dwFlags & VOLUME_NAME_NONE) != 0) ? volume_relative_name(state->m_path)
                                                : dos_extended_path(state->m_path);
        return copy_final_path_a(full, lpszFilePath, cchFilePath);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

//
// Byte-content & positioning family (M-FS-CONTENT). These entry points consume
// a minted file HANDLE, translate it to its backing PIL ifile (D11), and serve
// the redirection-backed (D16) whole-file byte stream through
// ifile::read_content / write_content. A genuine OS handle (one this table never
// minted) is forwarded untouched to the real API so the alias never breaks a
// real handle handed to ReadFile / WriteFile.
//
// Content model (D16): reads resolve to real backing bytes; a write is a
// whole-file replacement at offset 0. Anything finer -- a mid-file (non-zero
// offset) overwrite, vectored scatter / gather, or completion-routine (APC)
// delivery -- is not modeled and reports the documented deferred-content error
// (ERROR_NOT_SUPPORTED). The buffered overlay models no writable content, so a
// write routed through it surfaces the same deferred-content error naturally.
//

namespace
{
    //
    // Translate an error_code returned by an ifile content accessor into a Win32
    // last-error. The modeled "deferred content" outcome (D16 non-goal) is
    // reported as ERROR_NOT_SUPPORTED; a genuine backing-store failure carries
    // its own Win32 code (the direct provider wraps those via
    // make_win32_error_code, recoverable through decode_win32_error). Anything
    // unrecognized collapses to ERROR_GEN_FAILURE.
    //
    DWORD
    content_ec_to_win32(std::error_code const& ec)
    {
        if (ec == std::errc::not_supported)
            return deferred_content_error;

        auto const w = m::mwin32_impl::decode_win32_error(std::system_error(ec));
        return w.value_or(static_cast<DWORD>(ERROR_GEN_FAILURE));
    }

    //
    // The byte offset a read / write should start at. A non-null OVERLAPPED
    // names an explicit offset (its Offset / OffsetHigh halves) and the
    // per-handle sequential position is left untouched; a null OVERLAPPED uses
    // (and the caller then advances) the handle's stored sequential position.
    //
    std::uint64_t
    overlapped_offset(LPOVERLAPPED ov, std::uint64_t sequential) noexcept
    {
        if (ov == nullptr)
            return sequential;
        return (static_cast<std::uint64_t>(ov->OffsetHigh) << size_high_dword_shift) |
               static_cast<std::uint64_t>(ov->Offset);
    }
} // namespace

BOOL APIENTRY
mReadFile(_In_ HANDLE                                                              hFile,
          _Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) LPVOID lpBuffer,
          _In_ DWORD                                                              nNumberOfBytesToRead,
          _Out_opt_ LPDWORD                                                       lpNumberOfBytesRead,
          _Inout_opt_ LPOVERLAPPED                                                lpOverlapped)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::ReadFile(
            hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);

    try
    {
        M_VALIDATE_PARAMETER(lpBuffer, lpBuffer != nullptr || nNumberOfBytesToRead == 0);

        // A backup-semantics (directory) handle carries no byte content; only a
        // file handle resolves an ifile to read from.
        auto const state = resolve_file_handle(hFile);
        M_VALIDATE_PARAMETER(hFile, state->m_file != nullptr);

        std::uint64_t const offset = overlapped_offset(lpOverlapped, state->m_position);

        std::error_code ec;
        std::size_t     bytes_read = 0;
        state->m_file->read_content(
            m::pil::ifile::read_content_flags{},
            offset,
            std::span<std::byte>(static_cast<std::byte*>(lpBuffer), nNumberOfBytesToRead),
            bytes_read,
            ec);
        if (ec)
        {
            ::SetLastError(content_ec_to_win32(ec));
            return FALSE;
        }

        // A sequential read advances the per-handle position; an explicit
        // OVERLAPPED offset leaves it undisturbed. A short (including zero) read
        // is end-of-file, reported as success with the partial count.
        if (lpOverlapped == nullptr)
            state->m_position = offset + bytes_read;

        if (lpNumberOfBytesRead != nullptr)
            *lpNumberOfBytesRead = static_cast<DWORD>(bytes_read);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mWriteFile(_In_ HANDLE                                            hFile,
           _In_reads_bytes_opt_(nNumberOfBytesToWrite) LPCVOID    lpBuffer,
           _In_ DWORD                                             nNumberOfBytesToWrite,
           _Out_opt_ LPDWORD                                      lpNumberOfBytesWritten,
           _Inout_opt_ LPOVERLAPPED                               lpOverlapped)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::WriteFile(
            hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);

    try
    {
        M_VALIDATE_PARAMETER(lpBuffer, lpBuffer != nullptr || nNumberOfBytesToWrite == 0);

        auto const state = resolve_file_handle(hFile);
        M_VALIDATE_PARAMETER(hFile, state->m_file != nullptr);

        std::uint64_t const offset = overlapped_offset(lpOverlapped, state->m_position);

        // Whole-file replacement (D16): a write at offset 0 sets the file's
        // extent to the supplied bytes; a non-zero offset is a partial /
        // mid-file overwrite, which write_content rejects with not_supported ->
        // the documented deferred-content error.
        std::error_code ec;
        std::size_t     bytes_written = 0;
        state->m_file->write_content(
            m::pil::ifile::write_content_flags{},
            offset,
            std::span<std::byte const>(
                static_cast<std::byte const*>(lpBuffer), nNumberOfBytesToWrite),
            bytes_written,
            ec);
        if (ec)
        {
            ::SetLastError(content_ec_to_win32(ec));
            return FALSE;
        }

        if (lpOverlapped == nullptr)
            state->m_position = offset + bytes_written;

        if (lpNumberOfBytesWritten != nullptr)
            *lpNumberOfBytesWritten = static_cast<DWORD>(bytes_written);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

namespace
{
    //
    // Shared body of the asynchronous / vectored content forms (mReadFileEx,
    // mWriteFileEx, mReadFileScatter, mWriteFileGather) for a minted handle.
    // Completion-routine (APC) delivery and page-aligned scatter / gather
    // vectored I/O have no analogue under PIL isolation, so these forms validate
    // the handle (a stale or non-file value still fails the Win32 way) and then
    // report the documented deferred-content error rather than mis-serving a
    // partial transfer.
    //
    BOOL
    deferred_content_for_minted_handle(HANDLE hFile)
    {
        try
        {
            auto const state = resolve_file_handle(hFile);
            (void)state;
        }
        catch (...)
        {
            ::SetLastError(filesystem_exception_to_win32());
            return FALSE;
        }

        ::SetLastError(deferred_content_error);
        return FALSE;
    }
} // namespace

BOOL APIENTRY
mReadFileEx(_In_ HANDLE                                            hFile,
            _Out_writes_bytes_opt_(nNumberOfBytesToRead) LPVOID    lpBuffer,
            _In_ DWORD                                             nNumberOfBytesToRead,
            _Inout_ LPOVERLAPPED                                   lpOverlapped,
            _In_ LPOVERLAPPED_COMPLETION_ROUTINE                   lpCompletionRoutine)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::ReadFileEx(
            hFile, lpBuffer, nNumberOfBytesToRead, lpOverlapped, lpCompletionRoutine);

    return deferred_content_for_minted_handle(hFile);
}

BOOL APIENTRY
mWriteFileEx(_In_ HANDLE                                            hFile,
             _In_reads_bytes_opt_(nNumberOfBytesToWrite) LPCVOID    lpBuffer,
             _In_ DWORD                                             nNumberOfBytesToWrite,
             _Inout_ LPOVERLAPPED                                   lpOverlapped,
             _In_ LPOVERLAPPED_COMPLETION_ROUTINE                   lpCompletionRoutine)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::WriteFileEx(
            hFile, lpBuffer, nNumberOfBytesToWrite, lpOverlapped, lpCompletionRoutine);

    return deferred_content_for_minted_handle(hFile);
}

BOOL APIENTRY
mReadFileScatter(_In_ HANDLE                       hFile,
                 _In_ FILE_SEGMENT_ELEMENT         aSegmentArray[],
                 _In_ DWORD                        nNumberOfBytesToRead,
                 _Reserved_ LPDWORD                lpReserved,
                 _Inout_ LPOVERLAPPED              lpOverlapped)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::ReadFileScatter(
            hFile, aSegmentArray, nNumberOfBytesToRead, lpReserved, lpOverlapped);

    return deferred_content_for_minted_handle(hFile);
}

BOOL APIENTRY
mWriteFileGather(_In_ HANDLE                       hFile,
                 _In_ FILE_SEGMENT_ELEMENT         aSegmentArray[],
                 _In_ DWORD                        nNumberOfBytesToWrite,
                 _Reserved_ LPDWORD                lpReserved,
                 _Inout_ LPOVERLAPPED              lpOverlapped)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::WriteFileGather(
            hFile, aSegmentArray, nNumberOfBytesToWrite, lpReserved, lpOverlapped);

    return deferred_content_for_minted_handle(hFile);
}

//
// Positioning + size family (M-FS-CONTENT-2). The handle's sequential position
// is the per-handle cursor mReadFile / mWriteFile advance; mSetFilePointer{,Ex}
// move it. Size mutation through mSetEndOfFile / mSetFileValidData is bounded by
// the whole-file content model (D16): a truncation to empty or a no-op resize to
// the current extent is honoured, but any other partial resize is reported as
// the documented deferred-content error.
//
namespace
{
    //
    // Resolve a seek request against a minted handle's sequential position and
    // the backing file's current size. Returns a Win32 error code (NO_ERROR on
    // success) and, on success, the absolute new position. A move that would
    // place the pointer before the start of the file is ERROR_NEGATIVE_SEEK; an
    // unrecognized method is ERROR_INVALID_PARAMETER. A pointer past end-of-file
    // is permitted (matching Win32), and only materializes content on a write.
    //
    DWORD
    resolve_seek(m::mwin32_impl::file_handle_state const& state,
                 std::int64_t                             distance,
                 DWORD                                    method,
                 std::uint64_t&                           new_position)
    {
        std::int64_t base = 0;
        switch (method)
        {
        case FILE_BEGIN:
            base = 0;
            break;
        case FILE_CURRENT:
            base = static_cast<std::int64_t>(state.m_position);
            break;
        case FILE_END:
            base = static_cast<std::int64_t>(state.m_file->query_information().m_size);
            break;
        default:
            return ERROR_INVALID_PARAMETER;
        }

        std::int64_t const target = base + distance;
        if (target < 0)
            return ERROR_NEGATIVE_SEEK;

        new_position = static_cast<std::uint64_t>(target);
        return NO_ERROR;
    }
} // namespace

DWORD APIENTRY
mSetFilePointer(_In_ HANDLE       hFile,
                _In_ LONG         lDistanceToMove,
                _Inout_opt_ PLONG lpDistanceToMoveHigh,
                _In_ DWORD        dwMoveMethod)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::SetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);

    try
    {
        auto const state = resolve_file_handle(hFile);
        M_VALIDATE_PARAMETER(hFile, state->m_file != nullptr);

        // The 64-bit distance is split across lDistanceToMove (low half) and the
        // optional lpDistanceToMoveHigh (high half); when the high half is
        // absent the low half is a signed 32-bit distance.
        std::int64_t distance = 0;
        if (lpDistanceToMoveHigh != nullptr)
            distance = static_cast<std::int64_t>(
                (static_cast<std::uint64_t>(static_cast<DWORD>(*lpDistanceToMoveHigh))
                 << size_high_dword_shift) |
                static_cast<std::uint64_t>(static_cast<DWORD>(lDistanceToMove)));
        else
            distance = lDistanceToMove;

        std::uint64_t new_position = 0;
        DWORD const   err = resolve_seek(*state, distance, dwMoveMethod, new_position);
        if (err != NO_ERROR)
        {
            ::SetLastError(err);
            return INVALID_SET_FILE_POINTER;
        }

        state->m_position = new_position;

        if (lpDistanceToMoveHigh != nullptr)
            *lpDistanceToMoveHigh =
                static_cast<LONG>(static_cast<DWORD>(new_position >> size_high_dword_shift));

        // INVALID_SET_FILE_POINTER is also a legitimate low word; clear the last
        // error so a caller that inspects it after that value is not misled.
        ::SetLastError(NO_ERROR);
        return static_cast<DWORD>(new_position & size_low_dword_mask);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_SET_FILE_POINTER;
    }
}

BOOL APIENTRY
mSetFilePointerEx(_In_ HANDLE              hFile,
                  _In_ LARGE_INTEGER       liDistanceToMove,
                  _Out_opt_ PLARGE_INTEGER lpNewFilePointer,
                  _In_ DWORD               dwMoveMethod)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::SetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);

    try
    {
        auto const state = resolve_file_handle(hFile);
        M_VALIDATE_PARAMETER(hFile, state->m_file != nullptr);

        std::uint64_t new_position = 0;
        DWORD const   err =
            resolve_seek(*state, liDistanceToMove.QuadPart, dwMoveMethod, new_position);
        if (err != NO_ERROR)
        {
            ::SetLastError(err);
            return FALSE;
        }

        state->m_position = new_position;

        if (lpNewFilePointer != nullptr)
            lpNewFilePointer->QuadPart = static_cast<LONGLONG>(new_position);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mSetEndOfFile(_In_ HANDLE hFile)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::SetEndOfFile(hFile);

    try
    {
        auto const state = resolve_file_handle(hFile);
        M_VALIDATE_PARAMETER(hFile, state->m_file != nullptr);

        std::uint64_t const current_size = state->m_file->query_information().m_size;

        // Resizing to the existing extent is a no-op. Truncating to empty is the
        // degenerate whole-file replacement (offset 0, no bytes). Any other
        // target is a partial size mutation the content model does not express
        // (D16) -> the documented deferred-content error.
        if (state->m_position == current_size)
            return TRUE;

        if (state->m_position != 0)
        {
            ::SetLastError(deferred_content_error);
            return FALSE;
        }

        std::error_code ec;
        std::size_t     bytes_written = 0;
        state->m_file->write_content(
            m::pil::ifile::write_content_flags{}, 0, std::span<std::byte const>{}, bytes_written, ec);
        if (ec)
        {
            ::SetLastError(content_ec_to_win32(ec));
            return FALSE;
        }
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mSetFileValidData(_In_ HANDLE hFile, _In_ LONGLONG ValidDataLength)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::SetFileValidData(hFile, ValidDataLength);

    // The valid-data-length is an allocated-but-uninitialized extent hint that
    // mutates a sub-range of the file without a whole-file replacement; the
    // content model (D16) does not express it, so a minted handle reports the
    // documented deferred-content error after validating the handle.
    return deferred_content_for_minted_handle(hFile);
}

//
// Flush / lock / control / duplicate family (M-FS-CONTENT-3). These translate a
// minted handle and either forward a harmless no-op (the durability and
// byte-range-locking verbs have no analogue under single-process PIL isolation,
// where a write is already durable and there are no competing openers) or, for
// device control, report the documented deferred-content error. mDuplicateHandle
// is the exception: it interns a second table entry over the *same*
// file_handle_state so the original and the duplicate share one ifile and one
// sequential position, exactly as two Win32 handles onto one file object do.
//
namespace
{
    //
    // Validate that a minted handle still names a live file-table entry and, if
    // so, report success without side effects. Used by the durability / locking
    // verbs whose modeled behaviour under isolation is a successful no-op.
    //
    BOOL
    noop_success_for_minted_handle(HANDLE hFile)
    {
        try
        {
            auto const state = resolve_file_handle(hFile);
            (void)state;
        }
        catch (...)
        {
            ::SetLastError(filesystem_exception_to_win32());
            return FALSE;
        }

        return TRUE;
    }
} // namespace

BOOL APIENTRY
mFlushFileBuffers(_In_ HANDLE hFile)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::FlushFileBuffers(hFile);

    // A write through the content model is already materialized in the backing
    // store, so there is nothing to flush; validate the handle and succeed.
    return noop_success_for_minted_handle(hFile);
}

BOOL APIENTRY
mLockFile(_In_ HANDLE hFile,
          _In_ DWORD  dwFileOffsetLow,
          _In_ DWORD  dwFileOffsetHigh,
          _In_ DWORD  nNumberOfBytesToLockLow,
          _In_ DWORD  nNumberOfBytesToLockHigh)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::LockFile(hFile,
                          dwFileOffsetLow,
                          dwFileOffsetHigh,
                          nNumberOfBytesToLockLow,
                          nNumberOfBytesToLockHigh);

    // No competing openers exist under single-process isolation, so a byte-range
    // lock is vacuously granted.
    return noop_success_for_minted_handle(hFile);
}

BOOL APIENTRY
mLockFileEx(_In_ HANDLE              hFile,
            _In_ DWORD               dwFlags,
            _Reserved_ DWORD         dwReserved,
            _In_ DWORD               nNumberOfBytesToLockLow,
            _In_ DWORD               nNumberOfBytesToLockHigh,
            _Inout_ LPOVERLAPPED     lpOverlapped)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::LockFileEx(hFile,
                           dwFlags,
                           dwReserved,
                           nNumberOfBytesToLockLow,
                           nNumberOfBytesToLockHigh,
                           lpOverlapped);

    return noop_success_for_minted_handle(hFile);
}

BOOL APIENTRY
mUnlockFile(_In_ HANDLE hFile,
            _In_ DWORD  dwFileOffsetLow,
            _In_ DWORD  dwFileOffsetHigh,
            _In_ DWORD  nNumberOfBytesToUnlockLow,
            _In_ DWORD  nNumberOfBytesToUnlockHigh)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::UnlockFile(hFile,
                           dwFileOffsetLow,
                           dwFileOffsetHigh,
                           nNumberOfBytesToUnlockLow,
                           nNumberOfBytesToUnlockHigh);

    return noop_success_for_minted_handle(hFile);
}

BOOL APIENTRY
mUnlockFileEx(_In_ HANDLE          hFile,
              _Reserved_ DWORD     dwReserved,
              _In_ DWORD           nNumberOfBytesToUnlockLow,
              _In_ DWORD           nNumberOfBytesToUnlockHigh,
              _Inout_ LPOVERLAPPED lpOverlapped)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hFile);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::UnlockFileEx(hFile,
                             dwReserved,
                             nNumberOfBytesToUnlockLow,
                             nNumberOfBytesToUnlockHigh,
                             lpOverlapped);

    return noop_success_for_minted_handle(hFile);
}

BOOL APIENTRY
mDeviceIoControl(_In_ HANDLE                                              hDevice,
                 _In_ DWORD                                               dwIoControlCode,
                 _In_reads_bytes_opt_(nInBufferSize) LPVOID               lpInBuffer,
                 _In_ DWORD                                               nInBufferSize,
                 _Out_writes_bytes_to_opt_(nOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
                 _In_ DWORD                                               nOutBufferSize,
                 _Out_opt_ LPDWORD                                        lpBytesReturned,
                 _Inout_opt_ LPOVERLAPPED                                 lpOverlapped)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hDevice);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::DeviceIoControl(hDevice,
                                dwIoControlCode,
                                lpInBuffer,
                                nInBufferSize,
                                lpOutBuffer,
                                nOutBufferSize,
                                lpBytesReturned,
                                lpOverlapped);

    // Device / filesystem control codes address the backing volume or driver,
    // which the content model does not surface; report the documented
    // deferred-content error after validating the handle.
    return deferred_content_for_minted_handle(hDevice);
}

BOOL APIENTRY
mDuplicateHandle(_In_ HANDLE   hSourceProcessHandle,
                 _In_ HANDLE   hSourceHandle,
                 _In_ HANDLE   hTargetProcessHandle,
                 _Out_ LPHANDLE lpTargetHandle,
                 _In_ DWORD    dwDesiredAccess,
                 _In_ BOOL     bInheritHandle,
                 _In_ DWORD    dwOptions)
{
    auto const h = m::mwin32_impl::handle::from_HANDLE(hSourceHandle);

    if (!m::mwin32_impl::handle_table::is_minted_handle_value(h))
        return ::DuplicateHandle(hSourceProcessHandle,
                                hSourceHandle,
                                hTargetProcessHandle,
                                lpTargetHandle,
                                dwDesiredAccess,
                                bInheritHandle,
                                dwOptions);

    try
    {
        M_VALIDATE_PARAMETER(lpTargetHandle, lpTargetHandle != nullptr);

        // A minted handle lives only in this process, so the source / target
        // process handles and the access / inheritance arguments have no effect;
        // interning the same file_handle_state yields a second handle that
        // shares the original's ifile and sequential position.
        auto const state      = resolve_file_handle(hSourceHandle);
        auto const duplicate  = g_handles.intern(state);
        *lpTargetHandle       = duplicate.as_HANDLE();

        if ((dwOptions & DUPLICATE_CLOSE_SOURCE) != 0)
            g_handles.close(h);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

//
// Dusty-deck legacy content family (M-FS-LEGACY-3). The 16-bit-era _l* / _h*
// primitives and the LZ (compress / expand) family all traffic in the same
// minted HFILE the legacy open family hands back (M-FS-LEGACY-1): an HFILE is a
// minted pseudo-handle narrowed to int. Each shim widens the HFILE back to its
// HANDLE and forwards to the corresponding content shim (mReadFile, mWriteFile,
// mSetFilePointer, mCloseHandle) — which itself routes a minted value through
// the PIL ifile and a genuine value to the real API. The LZ family is a
// *passthrough*: PIL does not model LZ decompression, so an LZ "handle" is just
// the plain-file HFILE and no expansion is performed (D11 / D16).
//
namespace
{
    //
    // Widen a minted HFILE back to the HANDLE it was narrowed from. The minted
    // encoding keeps every bit within the low 31 bits, so the unsigned round-trip
    // is lossless; a genuine HFILE widens to the same HANDLE the real API
    // expects.
    //
    HANDLE
    hfile_to_handle(HFILE hf) noexcept
    {
        return reinterpret_cast<HANDLE>(
            static_cast<std::uintptr_t>(static_cast<std::uint32_t>(hf)));
    }

    //
    // Shared read body for _lread / _hread / LZRead. Forwards to mReadFile
    // (minted -> PIL ifile, genuine -> ::ReadFile) and reports the bytes
    // transferred, or HFILE_ERROR on failure.
    //
    LONG
    legacy_read(HFILE hf, LPVOID buffer, LONG count)
    {
        DWORD read = 0;
        if (!::mReadFile(hfile_to_handle(hf), buffer, static_cast<DWORD>(count), &read, nullptr))
            return HFILE_ERROR;
        return static_cast<LONG>(read);
    }

    //
    // Shared write body for _lwrite / _hwrite. Forwards to mWriteFile (a minted
    // handle's offset-zero write is the D16 whole-file replacement).
    //
    LONG
    legacy_write(HFILE hf, LPCVOID buffer, LONG count)
    {
        DWORD written = 0;
        if (!::mWriteFile(
                hfile_to_handle(hf), buffer, static_cast<DWORD>(count), &written, nullptr))
            return HFILE_ERROR;
        return static_cast<LONG>(written);
    }

    //
    // Shared seek body for _llseek / LZSeek. The legacy origin values
    // (0 / 1 / 2) coincide with FILE_BEGIN / FILE_CURRENT / FILE_END, so the
    // origin passes straight through to mSetFilePointer. mSetFilePointer clears
    // the last error on success, so the INVALID_SET_FILE_POINTER sentinel is a
    // genuine failure only when the last error is non-zero.
    //
    LONG
    legacy_seek(HFILE hf, LONG offset, int origin)
    {
        ::SetLastError(NO_ERROR);
        DWORD const r =
            ::mSetFilePointer(hfile_to_handle(hf), offset, nullptr, static_cast<DWORD>(origin));
        if (r == INVALID_SET_FILE_POINTER && ::GetLastError() != NO_ERROR)
            return HFILE_ERROR;
        return static_cast<LONG>(r);
    }
} // namespace

UINT APIENTRY
m_lread(_In_ HFILE                                       hFile,
        _Out_writes_bytes_to_(uBytes, return) LPVOID     lpBuffer,
        _In_ UINT                                        uBytes)
{
    return static_cast<UINT>(legacy_read(hFile, lpBuffer, static_cast<LONG>(uBytes)));
}

UINT APIENTRY
m_lwrite(_In_ HFILE hFile, _In_reads_bytes_(uBytes) LPCCH lpBuffer, _In_ UINT uBytes)
{
    return static_cast<UINT>(legacy_write(hFile, lpBuffer, static_cast<LONG>(uBytes)));
}

LONG APIENTRY
m_hread(_In_ HFILE                                       hFile,
        _Out_writes_bytes_to_(lBytes, return) LPVOID     lpBuffer,
        _In_ LONG                                        lBytes)
{
    return legacy_read(hFile, lpBuffer, lBytes);
}

LONG APIENTRY
m_hwrite(_In_ HFILE hFile, _In_reads_bytes_(lBytes) LPCCH lpBuffer, _In_ LONG lBytes)
{
    return legacy_write(hFile, lpBuffer, lBytes);
}

LONG APIENTRY
m_llseek(_In_ HFILE hFile, _In_ LONG lOffset, _In_ int iOrigin)
{
    return legacy_seek(hFile, lOffset, iOrigin);
}

HFILE APIENTRY
m_lclose(_In_ HFILE hFile)
{
    // Genuine _lclose returns zero on success, HFILE_ERROR on failure.
    return ::mCloseHandle(hfile_to_handle(hFile)) ? 0 : HFILE_ERROR;
}

INT APIENTRY
mLZOpenFileA(_In_ LPSTR lpFileName, _Inout_ LPOFSTRUCT lpReOpenBuf, _In_ WORD wStyle)
{
    // Passthrough: PIL models no LZ decompression, so open the file as an
    // ordinary file via the legacy OpenFile shim and reuse the minted HFILE as
    // the LZ handle. HFILE_ERROR (-1) coincides with LZERROR_BADINHANDLE.
    HFILE const hf = ::mOpenFile(lpFileName, lpReOpenBuf, wStyle);
    return (hf == HFILE_ERROR) ? LZERROR_BADINHANDLE : static_cast<INT>(hf);
}

INT APIENTRY
mLZOpenFileW(_In_ LPWSTR lpFileName, _Inout_ LPOFSTRUCT lpReOpenBuf, _In_ WORD wStyle)
{
    // OpenFile / OFSTRUCT are ANSI-only, so the wide path is narrowed to the
    // active code page and serviced by the ANSI form (matching how the genuine
    // LZOpenFileW fills the ANSI OFSTRUCT).
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        std::string acp = m::to_acp_string(
            std::u16string_view{reinterpret_cast<char16_t const*>(lpFileName)});
        return ::mLZOpenFileA(acp.data(), lpReOpenBuf, wStyle);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return LZERROR_BADINHANDLE;
    }
}

INT APIENTRY
mLZRead(_In_ INT hFile, _Out_writes_bytes_to_(cbRead, return) CHAR* lpBuffer, _In_ INT cbRead)
{
    LONG const r = legacy_read(static_cast<HFILE>(hFile), lpBuffer, static_cast<LONG>(cbRead));
    return (r == HFILE_ERROR) ? LZERROR_READ : static_cast<INT>(r);
}

LONG APIENTRY
mLZSeek(_In_ INT hFile, _In_ LONG lOffset, _In_ INT iOrigin)
{
    LONG const r = legacy_seek(static_cast<HFILE>(hFile), lOffset, iOrigin);
    return (r == HFILE_ERROR) ? LZERROR_BADVALUE : r;
}

VOID APIENTRY
mLZClose(_In_ INT hFile)
{
    (void)::m_lclose(static_cast<HFILE>(hFile));
}

INT APIENTRY
mLZInit(_In_ INT hfSource)
{
    // Passthrough: with no decompression modeled the source is already a
    // plain-file handle, so initialization is the identity. Probe the handle
    // (a no-op seek) so a bad value still surfaces as LZERROR_BADINHANDLE.
    if (legacy_seek(static_cast<HFILE>(hfSource), 0, FILE_CURRENT) == HFILE_ERROR)
        return LZERROR_BADINHANDLE;
    return hfSource;
}

LONG APIENTRY
mLZCopy(_In_ INT hfSource, _In_ INT hfDest)
{
    // Passthrough whole-file copy (D16): no decompression. Measure the source
    // extent, rewind, read its full content, and write it as the destination's
    // whole content. Returns the byte count copied or a negative LZ error.
    HFILE const src = static_cast<HFILE>(hfSource);
    HFILE const dst = static_cast<HFILE>(hfDest);

    LONG const size = legacy_seek(src, 0, FILE_END);
    if (size == HFILE_ERROR)
        return LZERROR_BADINHANDLE;
    if (legacy_seek(src, 0, FILE_BEGIN) == HFILE_ERROR)
        return LZERROR_BADINHANDLE;

    std::vector<std::byte> buffer(static_cast<std::size_t>(size));

    LONG const read = legacy_read(src, buffer.data(), size);
    if (read == HFILE_ERROR)
        return LZERROR_READ;

    LONG const written = legacy_write(dst, buffer.data(), read);
    if (written == HFILE_ERROR)
        return LZERROR_WRITE;

    return written;
}

namespace
{
    //
    // Shared body of GetExpandedNameA / W. Passthrough: PIL models no LZ name
    // expansion, so the source name is copied through unchanged. Returns TRUE
    // (1) on success, matching the genuine non-error return.
    //
    template <typename Char>
    INT
    get_expanded_name(Char const* source, Char* buffer)
    {
        if (source == nullptr || buffer == nullptr)
            return LZERROR_BADVALUE;

        std::size_t i = 0;
        for (; i + 1 < static_cast<std::size_t>(MAX_PATH) && source[i] != Char{}; ++i)
            buffer[i] = source[i];
        buffer[i] = Char{};
        return TRUE;
    }
} // namespace

INT APIENTRY
mGetExpandedNameA(_In_ LPSTR lpszSource, _Out_writes_(MAX_PATH) LPSTR lpszBuffer)
{
    return get_expanded_name<CHAR>(lpszSource, lpszBuffer);
}

INT APIENTRY
mGetExpandedNameW(_In_ LPWSTR lpszSource, _Out_writes_(MAX_PATH) LPWSTR lpszBuffer)
{
    return get_expanded_name<WCHAR>(lpszSource, lpszBuffer);
}

//
// Copy / replace / extended namespace & path family (M-FS-COPY). These are the
// path-based namespace and metadata APIs the D11 inventory marks S / S-ns that
// the earlier metadata milestones did not include. None of them mint or consume
// a file HANDLE for byte content; they operate on the unified namespace (D13)
// through the same session ifilesystem the rest of mwinfile.cpp routes through.
//

namespace
{
    //
    // Shared body of the mCopyFile family. This is a *namespace* copy (D11): it
    // verifies the source node exists, optionally rejects an already-present
    // destination, and materializes the destination node. Byte content is not
    // modeled this milestone (D14) -- the whole-file content copy lights up with
    // M-FS-CONTENT -- so the destination is created as an empty node regardless
    // of the source's size. Source and destination may live under different
    // roots; each path is resolved against its own root.
    //
    BOOL
    copy_file_impl(m::pil::file_path const& src, m::pil::file_path const& dst, bool fail_if_exists)
    {
        auto const src_md = query_path_metadata(src);
        if (!src_md.has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return FALSE;
        }

        // CopyFile copies a file; a directory source is rejected the way the
        // genuine API rejects it (ERROR_ACCESS_DENIED).
        if (src_md->is_directory())
        {
            ::SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }

        if (fail_if_exists && query_path_metadata(dst).has_value())
        {
            ::SetLastError(ERROR_FILE_EXISTS);
            return FALSE;
        }

        auto const dst_root = open_root_for(dst);
        dst_root->create_file(m::pil::file_path{dst.relative_path()});
        return TRUE;
    }
} // namespace

BOOL APIENTRY
mCopyFileW(_In_ LPCWSTR lpExistingFileName, _In_ LPCWSTR lpNewFileName, _In_ BOOL bFailIfExists)
{
    try
    {
        M_VALIDATE_PARAMETER(lpExistingFileName, lpExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(lpNewFileName, lpNewFileName != nullptr);

        return copy_file_impl(to_file_path(lpExistingFileName),
                              to_file_path(lpNewFileName),
                              bFailIfExists != FALSE);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mCopyFileA(_In_ LPCSTR lpExistingFileName, _In_ LPCSTR lpNewFileName, _In_ BOOL bFailIfExists)
{
    try
    {
        M_VALIDATE_PARAMETER(lpExistingFileName, lpExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(lpNewFileName, lpNewFileName != nullptr);

        return copy_file_impl(to_file_path(lpExistingFileName),
                              to_file_path(lpNewFileName),
                              bFailIfExists != FALSE);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

//
// mCopyFileEx ignores the progress routine, callback data and cancel flag under
// isolation (there is no long-running byte copy to report on or cancel); only
// the COPY_FILE_FAIL_IF_EXISTS bit of dwCopyFlags is interpreted.
//
BOOL APIENTRY
mCopyFileExW(_In_ LPCWSTR               lpExistingFileName,
             _In_ LPCWSTR               lpNewFileName,
             _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
             _In_opt_ LPVOID            lpData,
             _In_opt_ LPBOOL            pbCancel,
             _In_ DWORD                 dwCopyFlags)
{
    (void)lpProgressRoutine;
    (void)lpData;
    (void)pbCancel;

    try
    {
        M_VALIDATE_PARAMETER(lpExistingFileName, lpExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(lpNewFileName, lpNewFileName != nullptr);

        return copy_file_impl(to_file_path(lpExistingFileName),
                              to_file_path(lpNewFileName),
                              (dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS) != 0);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mCopyFileExA(_In_ LPCSTR                lpExistingFileName,
             _In_ LPCSTR                lpNewFileName,
             _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
             _In_opt_ LPVOID            lpData,
             _In_opt_ LPBOOL            pbCancel,
             _In_ DWORD                 dwCopyFlags)
{
    (void)lpProgressRoutine;
    (void)lpData;
    (void)pbCancel;

    try
    {
        M_VALIDATE_PARAMETER(lpExistingFileName, lpExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(lpNewFileName, lpNewFileName != nullptr);

        return copy_file_impl(to_file_path(lpExistingFileName),
                              to_file_path(lpNewFileName),
                              (dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS) != 0);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

//
// mCopyFile2 reports failure as an HRESULT rather than a BOOL + last-error. The
// fail-if-exists intent is the COPY_FILE_FAIL_IF_EXISTS bit of the extended
// parameters' dwCopyFlags; the progress callback and cancel pointer are ignored
// as in mCopyFileEx. A NULL pExtendedParameters means "no special flags".
//
HRESULT APIENTRY
mCopyFile2(_In_ PCWSTR                                  pwszExistingFileName,
           _In_ PCWSTR                                  pwszNewFileName,
           _In_opt_ COPYFILE2_EXTENDED_PARAMETERS*      pExtendedParameters)
{
    try
    {
        M_VALIDATE_PARAMETER(pwszExistingFileName, pwszExistingFileName != nullptr);
        M_VALIDATE_PARAMETER(pwszNewFileName, pwszNewFileName != nullptr);

        bool const fail_if_exists =
            pExtendedParameters != nullptr &&
            (pExtendedParameters->dwCopyFlags & COPY_FILE_FAIL_IF_EXISTS) != 0;

        if (!copy_file_impl(to_file_path(pwszExistingFileName),
                            to_file_path(pwszNewFileName),
                            fail_if_exists))
            return HRESULT_FROM_WIN32(::GetLastError());
    }
    catch (...)
    {
        return HRESULT_FROM_WIN32(filesystem_exception_to_win32());
    }

    return S_OK;
}

namespace
{
    //
    // Shared body of mReplaceFile. A replace is a namespace re-key (D13): the
    // replacement node takes the replaced node's name, and the replaced node's
    // original is optionally preserved under the backup name. Because the
    // operation is confined to a single volume (D11), all three paths must share
    // a root; a cross-root request is rejected with ERROR_NOT_SAME_DEVICE. Byte
    // content is not modeled this milestone (D14) -- only the namespace links
    // move; the content carried by the replacement node is whatever
    // M-FS-CONTENT will later attach.
    //
    BOOL
    replace_file_impl(m::pil::file_path const&                replaced,
                      m::pil::file_path const&                replacement,
                      std::optional<m::pil::file_path> const& backup)
    {
        if (!(replaced.root() == replacement.root()) ||
            (backup.has_value() && !(backup->root() == replaced.root())))
        {
            ::SetLastError(ERROR_NOT_SAME_DEVICE);
            return FALSE;
        }

        if (!query_path_metadata(replaced).has_value() ||
            !query_path_metadata(replacement).has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return FALSE;
        }

        auto const              root = open_root_for(replaced);
        m::pil::file_path const replaced_rel{replaced.relative_path()};
        m::pil::file_path const replacement_rel{replacement.relative_path()};

        if (backup.has_value())
        {
            m::pil::file_path const backup_rel{backup->relative_path()};

            // The genuine API overwrites an existing backup; rename_entry has no
            // replace mode, so an already-present backup node is removed first,
            // then the replaced node is moved aside into the backup name.
            if (query_path_metadata(*backup).has_value())
                root->remove_entry(backup_rel);
            root->rename_entry(replaced_rel, backup_rel);
        }
        else
        {
            // No backup requested: the replaced node is discarded outright.
            root->remove_entry(replaced_rel);
        }

        root->rename_entry(replacement_rel, replaced_rel);
        return TRUE;
    }
} // namespace

BOOL APIENTRY
mReplaceFileW(_In_ LPCWSTR      lpReplacedFileName,
              _In_ LPCWSTR      lpReplacementFileName,
              _In_opt_ LPCWSTR  lpBackupFileName,
              _In_ DWORD        dwReplaceFlags,
              _Reserved_ LPVOID lpExclude,
              _Reserved_ LPVOID lpReserved)
{
    (void)dwReplaceFlags;
    (void)lpExclude;
    (void)lpReserved;

    try
    {
        M_VALIDATE_PARAMETER(lpReplacedFileName, lpReplacedFileName != nullptr);
        M_VALIDATE_PARAMETER(lpReplacementFileName, lpReplacementFileName != nullptr);

        std::optional<m::pil::file_path> backup;
        if (lpBackupFileName != nullptr)
            backup = to_file_path(lpBackupFileName);

        return replace_file_impl(to_file_path(lpReplacedFileName),
                                 to_file_path(lpReplacementFileName),
                                 backup);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mReplaceFileA(_In_ LPCSTR       lpReplacedFileName,
              _In_ LPCSTR       lpReplacementFileName,
              _In_opt_ LPCSTR   lpBackupFileName,
              _In_ DWORD        dwReplaceFlags,
              _Reserved_ LPVOID lpExclude,
              _Reserved_ LPVOID lpReserved)
{
    (void)dwReplaceFlags;
    (void)lpExclude;
    (void)lpReserved;

    try
    {
        M_VALIDATE_PARAMETER(lpReplacedFileName, lpReplacedFileName != nullptr);
        M_VALIDATE_PARAMETER(lpReplacementFileName, lpReplacementFileName != nullptr);

        std::optional<m::pil::file_path> backup;
        if (lpBackupFileName != nullptr)
            backup = to_file_path(lpBackupFileName);

        return replace_file_impl(to_file_path(lpReplacedFileName),
                                 to_file_path(lpReplacementFileName),
                                 backup);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mCreateDirectoryExW(_In_ LPCWSTR lpTemplateDirectory,
                    _In_ LPCWSTR lpNewDirectory,
                    _In_opt_ LPSECURITY_ATTRIBUTES)
{
    // The template directory supplies attributes / streams to clone on the real
    // API; under isolation there is no metadata to clone (D14), so it is ignored
    // and the new directory is created the same way mCreateDirectory creates it.
    (void)lpTemplateDirectory;

    try
    {
        M_VALIDATE_PARAMETER(lpNewDirectory, lpNewDirectory != nullptr);

        auto const path = to_file_path(lpNewDirectory);
        auto const root = open_root_for(path);
        root->create_directory(m::pil::file_path{path.relative_path()});
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY
mCreateDirectoryExA(_In_ LPCSTR lpTemplateDirectory,
                    _In_ LPCSTR lpNewDirectory,
                    _In_opt_ LPSECURITY_ATTRIBUTES)
{
    (void)lpTemplateDirectory;

    try
    {
        M_VALIDATE_PARAMETER(lpNewDirectory, lpNewDirectory != nullptr);

        auto const path = to_file_path(lpNewDirectory);
        auto const root = open_root_for(path);
        root->create_directory(m::pil::file_path{path.relative_path()});
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }

    return TRUE;
}

namespace
{
    //
    // Compose the temporary-file leaf name the GetTempFileName family produces:
    // up to the first three characters of the prefix, four lowercase hex digits
    // of the 16-bit unique value, and the ".tmp" extension.
    //
    std::u16string
    make_temp_leaf_name(std::u16string_view prefix, unsigned value16)
    {
        constexpr std::size_t max_prefix_chars = 3;
        constexpr unsigned    hex_digit_count  = 4;

        std::u16string name;
        name.append(prefix.substr(0, std::min<std::size_t>(prefix.size(), max_prefix_chars)));

        constexpr std::u16string_view hex_digits = u"0123456789abcdef";
        for (unsigned i = 0; i < hex_digit_count; ++i)
        {
            unsigned const shift = (hex_digit_count - 1 - i) * 4;
            name.push_back(hex_digits[(value16 >> shift) & 0xFu]);
        }

        name.append(u".tmp");
        return name;
    }

    //
    // Shared body of mGetTempFileName. With uUnique != 0 the value is used
    // verbatim and the file is *not* created (matching the genuine API). With
    // uUnique == 0 a node that does not yet exist is found -- deterministically,
    // by scanning upward from 1 under isolation rather than seeding from the
    // system clock -- and created empty. The full path of the chosen name is
    // returned in out_full and the 16-bit unique value is the function result
    // (0 on failure, with the last-error set).
    //
    UINT
    get_temp_file_name_impl(m::pil::file_path const& dir,
                            std::u16string_view      prefix,
                            UINT                     uUnique,
                            std::u16string&          out_full)
    {
        if (uUnique != 0)
        {
            unsigned const          value16 = uUnique & 0xFFFFu;
            auto const              leaf    = make_temp_leaf_name(prefix, value16);
            m::pil::file_path const candidate =
                dir / m::pil::file_path{std::u16string_view{leaf}};
            out_full.assign(candidate.native().view());
            return value16;
        }

        for (unsigned value16 = 1; value16 <= 0xFFFFu; ++value16)
        {
            auto const              leaf = make_temp_leaf_name(prefix, value16);
            m::pil::file_path const candidate =
                dir / m::pil::file_path{std::u16string_view{leaf}};

            if (query_path_metadata(candidate).has_value())
                continue;

            auto const cand_root = open_root_for(candidate);
            cand_root->create_file(m::pil::file_path{candidate.relative_path()});
            out_full.assign(candidate.native().view());
            return value16;
        }

        // Every name in the 16-bit space was taken.
        ::SetLastError(ERROR_FILE_EXISTS);
        return 0;
    }
} // namespace

UINT APIENTRY
mGetTempFileNameW(_In_ LPCWSTR                  lpPathName,
                  _In_ LPCWSTR                  lpPrefixString,
                  _In_ UINT                     uUnique,
                  _Out_writes_(MAX_PATH) LPWSTR lpTempFileName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);
        M_VALIDATE_PARAMETER(lpPrefixString, lpPrefixString != nullptr);
        M_VALIDATE_PARAMETER(lpTempFileName, lpTempFileName != nullptr);

        std::u16string full;
        UINT const     result =
            get_temp_file_name_impl(to_file_path(lpPathName),
                                    std::u16string_view{reinterpret_cast<char16_t const*>(lpPrefixString)},
                                    uUnique,
                                    full);
        if (result == 0)
            return 0;

        if (full.size() + 1 > MAX_PATH)
        {
            ::SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }

        std::memcpy(lpTempFileName, full.data(), full.size() * sizeof(WCHAR));
        lpTempFileName[full.size()] = L'\0';
        return result;
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

UINT APIENTRY
mGetTempFileNameA(_In_ LPCSTR                  lpPathName,
                  _In_ LPCSTR                  lpPrefixString,
                  _In_ UINT                    uUnique,
                  _Out_writes_(MAX_PATH) LPSTR lpTempFileName)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);
        M_VALIDATE_PARAMETER(lpPrefixString, lpPrefixString != nullptr);
        M_VALIDATE_PARAMETER(lpTempFileName, lpTempFileName != nullptr);

        auto const     prefix = m::acp_to_basic_string<char16_t>(lpPrefixString);
        std::u16string full;
        UINT const     result =
            get_temp_file_name_impl(to_file_path(lpPathName), std::u16string_view{prefix}, uUnique, full);
        if (result == 0)
            return 0;

        auto const acp = m::to_acp_string(std::u16string_view{full});
        if (acp.size() + 1 > MAX_PATH)
        {
            ::SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }

        std::memcpy(lpTempFileName, acp.data(), acp.size());
        lpTempFileName[acp.size()] = '\0';
        return result;
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

namespace
{
    //
    // Shared body of mSetFileAttributes. PIL exposes no metadata-write verb this
    // milestone, so a successful set is a documented no-op: the target is
    // verified to exist (a missing target fails ERROR_FILE_NOT_FOUND the way the
    // genuine API does) and the new attribute mask is accepted and discarded
    // (the shim's accept-and-ignore stance for state isolation cannot model).
    //
    BOOL
    set_file_attributes_impl(m::pil::file_path const& path)
    {
        if (!query_path_metadata(path).has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return FALSE;
        }
        return TRUE;
    }
} // namespace

BOOL APIENTRY
mSetFileAttributesW(_In_ LPCWSTR lpFileName, _In_ DWORD dwFileAttributes)
{
    (void)dwFileAttributes;

    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);
        return set_file_attributes_impl(to_file_path(lpFileName));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mSetFileAttributesA(_In_ LPCSTR lpFileName, _In_ DWORD dwFileAttributes)
{
    (void)dwFileAttributes;

    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);
        return set_file_attributes_impl(to_file_path(lpFileName));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

namespace
{
    //
    // Emit a canonicalized path into a caller buffer using the Win32 path-name
    // length contract shared by GetFullPathName / GetLongPathName / SearchPath:
    // on success the count of characters written excluding the terminator is
    // returned; when the buffer is absent or too small the required size
    // including the terminator is returned and nothing is written. When
    // file_part is supplied it is pointed at the final component within buf, or
    // set null when there is no distinct final component (empty leaf).
    //
    DWORD
    copy_full_path_w(std::u16string_view full,
                     std::u16string_view leaf,
                     LPWSTR              buf,
                     DWORD               cch,
                     LPWSTR*             file_part)
    {
        auto const len = static_cast<DWORD>(full.size());
        if (buf == nullptr || cch < len + 1)
        {
            if (file_part != nullptr)
                *file_part = nullptr;
            return len + 1;
        }

        std::memcpy(buf, full.data(), static_cast<std::size_t>(len) * sizeof(WCHAR));
        buf[len] = L'\0';
        if (file_part != nullptr)
            *file_part = (!leaf.empty() && leaf.size() <= full.size())
                             ? buf + (full.size() - leaf.size())
                             : nullptr;
        return len;
    }

    //
    // ANSI counterpart of copy_full_path_w. The active-code-page transcoding is
    // applied to the whole path and, for the file-part offset, to the parent
    // prefix separately so the returned pointer indexes ACP bytes rather than
    // UTF-16 code units.
    //
    DWORD
    copy_full_path_a(std::u16string_view full,
                     std::u16string_view leaf,
                     LPSTR               buf,
                     DWORD               cch,
                     LPSTR*              file_part)
    {
        auto const acp = m::to_acp_string(full);
        auto const len = static_cast<DWORD>(acp.size());
        if (buf == nullptr || cch < len + 1)
        {
            if (file_part != nullptr)
                *file_part = nullptr;
            return len + 1;
        }

        std::memcpy(buf, acp.data(), len);
        buf[len] = '\0';
        if (file_part != nullptr)
        {
            if (!leaf.empty() && leaf.size() <= full.size())
            {
                auto const acp_prefix = m::to_acp_string(full.substr(0, full.size() - leaf.size()));
                *file_part           = buf + acp_prefix.size();
            }
            else
            {
                *file_part = nullptr;
            }
        }
        return len;
    }

    //
    // Canonicalize a path with the Windows surface and return its native text
    // together with the leaf (final component). A path that normalizes to a bare
    // root or that ends in a separator yields an empty leaf, signalling callers
    // that there is no distinct file component.
    //
    std::u16string
    canonical_full_path(m::pil::file_path const& path, std::u16string& out_leaf)
    {
        auto const     norm = path.lexically_normal(m::pil::path_surface::windows);
        std::u16string full{norm.native().view()};

        auto const split = norm.split_parent_path_and_leaf_name();
        out_leaf.assign(split.second.native().view());

        // A trailing separator (or a leaf that is not actually a suffix of the
        // normalized text) denotes a directory target with no file component.
        if (!full.empty() && (full.back() == u'\\' || full.back() == u'/'))
            out_leaf.clear();

        return full;
    }
} // namespace

DWORD APIENTRY
mGetFullPathNameW(_In_ LPCWSTR                            lpFileName,
                  _In_ DWORD                              nBufferLength,
                  _Out_writes_opt_(nBufferLength) LPWSTR  lpBuffer,
                  _Outptr_opt_ LPWSTR*                    lpFilePart)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        std::u16string       leaf;
        std::u16string const full = canonical_full_path(to_file_path(lpFileName), leaf);
        return copy_full_path_w(full, leaf, lpBuffer, nBufferLength, lpFilePart);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

DWORD APIENTRY
mGetFullPathNameA(_In_ LPCSTR                            lpFileName,
                  _In_ DWORD                             nBufferLength,
                  _Out_writes_opt_(nBufferLength) LPSTR  lpBuffer,
                  _Outptr_opt_ LPSTR*                    lpFilePart)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        std::u16string       leaf;
        std::u16string const full = canonical_full_path(to_file_path(lpFileName), leaf);
        return copy_full_path_a(full, leaf, lpBuffer, nBufferLength, lpFilePart);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

DWORD APIENTRY
mGetLongPathNameW(_In_ LPCWSTR                          lpszShortPath,
                  _Out_writes_opt_(cchBuffer) LPWSTR    lpszLongPath,
                  _In_ DWORD                            cchBuffer)
{
    try
    {
        M_VALIDATE_PARAMETER(lpszShortPath, lpszShortPath != nullptr);

        auto const path = to_file_path(lpszShortPath);

        // The genuine API resolves each component against the filesystem, so a
        // path that does not exist fails. There is no short/long distinction to
        // expand under isolation; the canonical form is returned unchanged.
        if (!query_path_metadata(path).has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return 0;
        }

        std::u16string       leaf;
        std::u16string const full = canonical_full_path(path, leaf);
        return copy_full_path_w(full, leaf, lpszLongPath, cchBuffer, nullptr);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

DWORD APIENTRY
mGetLongPathNameA(_In_ LPCSTR                           lpszShortPath,
                  _Out_writes_opt_(cchBuffer) LPSTR     lpszLongPath,
                  _In_ DWORD                            cchBuffer)
{
    try
    {
        M_VALIDATE_PARAMETER(lpszShortPath, lpszShortPath != nullptr);

        auto const path = to_file_path(lpszShortPath);
        if (!query_path_metadata(path).has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return 0;
        }

        std::u16string       leaf;
        std::u16string const full = canonical_full_path(path, leaf);
        return copy_full_path_a(full, leaf, lpszLongPath, cchBuffer, nullptr);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

namespace
{
    //
    // Compose the leaf SearchPath looks for: the supplied file name, with the
    // default extension appended only when the name itself carries no extension
    // (no '.' in the name). The extension text includes its leading dot.
    //
    std::u16string
    compose_search_leaf(std::u16string_view name, std::u16string_view extension)
    {
        std::u16string leaf{name};
        if (!extension.empty() && name.find(u'.') == std::u16string_view::npos)
            leaf.append(extension);
        return leaf;
    }

    //
    // Walk the semicolon-separated search directories and return the native path
    // of the first directory under which the composed leaf exists, or nullopt
    // when no directory yields a match. An empty directory entry is skipped.
    //
    std::optional<std::u16string>
    search_path_impl(std::u16string_view search_dirs,
                     std::u16string_view file_name,
                     std::u16string_view extension)
    {
        auto const leaf = compose_search_leaf(file_name, extension);

        std::size_t start = 0;
        while (start <= search_dirs.size())
        {
            auto const  semi = search_dirs.find(u';', start);
            auto const  dir  = search_dirs.substr(
                start, semi == std::u16string_view::npos ? std::u16string_view::npos : semi - start);

            if (!dir.empty())
            {
                m::pil::file_path const candidate =
                    m::pil::file_path{dir} / m::pil::file_path{std::u16string_view{leaf}};
                if (query_path_metadata(candidate).has_value())
                    return std::u16string{candidate.native().view()};
            }

            if (semi == std::u16string_view::npos)
                break;
            start = semi + 1;
        }

        return std::nullopt;
    }
} // namespace

DWORD APIENTRY
mSearchPathW(_In_opt_ LPCWSTR                        lpPath,
             _In_ LPCWSTR                            lpFileName,
             _In_opt_ LPCWSTR                        lpExtension,
             _In_ DWORD                              nBufferLength,
             _Out_writes_opt_(nBufferLength) LPWSTR  lpBuffer,
             _Outptr_opt_ LPWSTR*                    lpFilePart)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        // The default search order (NULL lpPath) draws on the real process and
        // system directories, which have no meaning under isolation; only an
        // explicit search path is honoured here.
        if (lpPath == nullptr)
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return 0;
        }

        auto const found =
            search_path_impl(std::u16string_view{reinterpret_cast<char16_t const*>(lpPath)},
                             std::u16string_view{reinterpret_cast<char16_t const*>(lpFileName)},
                             lpExtension != nullptr
                                 ? std::u16string_view{reinterpret_cast<char16_t const*>(lpExtension)}
                                 : std::u16string_view{});
        if (!found.has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return 0;
        }

        std::u16string       leaf;
        std::u16string const full = canonical_full_path(m::pil::file_path{std::u16string_view{*found}}, leaf);
        return copy_full_path_w(full, leaf, lpBuffer, nBufferLength, lpFilePart);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

DWORD APIENTRY
mSearchPathA(_In_opt_ LPCSTR                         lpPath,
             _In_ LPCSTR                             lpFileName,
             _In_opt_ LPCSTR                         lpExtension,
             _In_ DWORD                              nBufferLength,
             _Out_writes_opt_(nBufferLength) LPSTR   lpBuffer,
             _Outptr_opt_ LPSTR*                     lpFilePart)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);

        if (lpPath == nullptr)
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return 0;
        }

        auto const path_u16 = m::acp_to_basic_string<char16_t>(lpPath);
        auto const name_u16 = m::acp_to_basic_string<char16_t>(lpFileName);
        m::basic_sstring<char16_t> ext_u16;
        if (lpExtension != nullptr)
            ext_u16 = m::acp_to_basic_string<char16_t>(lpExtension);

        auto const found = search_path_impl(std::u16string_view{path_u16},
                                            std::u16string_view{name_u16},
                                            lpExtension != nullptr ? std::u16string_view{ext_u16}
                                                                   : std::u16string_view{});
        if (!found.has_value())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return 0;
        }

        std::u16string       leaf;
        std::u16string const full = canonical_full_path(m::pil::file_path{std::u16string_view{*found}}, leaf);
        return copy_full_path_a(full, leaf, lpBuffer, nBufferLength, lpFilePart);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return 0;
    }
}

//
// Directory change-notification family (M-FS-NOTIFY-1). mReadDirectoryChangesW /
// mReadDirectoryChangesExW surface the Win32 detailed change-notification
// contract onto the PIL filesystem monitor (ifilesystem::monitor()). The watch
// is registered on the directory handle's public path; the provider delivers
// detailed on_change(kind, entry_name) records on a background thread, which the
// shim funnels into a per-handle queue and decodes into the caller's
// FILE_NOTIFY_INFORMATION chain.
//
// Provider support, not buffering, decides whether notifications fire: the live
// (passthrough / direct) provider implements the watch via ReadDirectoryChangesW
// and reports real mutations, whereas the buffered provider models a sealed
// snapshot and does not observe live change (its register_watch is not
// implemented). A redirecting layer forwards the watch to its underlying
// provider. (mwin32 D15.)
//
namespace m::mwin32_impl
{
    struct directory_watch_context;

    //
    // The change-notification sink handed to ifilesystem_monitor::register_watch.
    // The provider invokes it on a threadpool thread for each detailed change;
    // it appends the (kind, entry-name) record to the owning context's queue and
    // satisfies any pending overlapped read or wakes a blocked synchronous
    // reader. It holds a bare reference to its context (no ownership cycle): the
    // context owns the sink and, after it, the token; the token quiesces in-
    // flight callbacks on destruction, so the reference is always valid while a
    // callback can run. The directory- and notification-attempt-failure hooks
    // decline to requeue (nullopt): under the shim there is no retry policy to
    // express, and a persistent failure simply stops delivery.
    //
    class notify_change_sink final : public m::pil::ifilesystem_monitor_change_notification
    {
    public:
        explicit notify_change_sink(directory_watch_context& ctx) noexcept: m_ctx(ctx) {}

        ~notify_change_sink() override = default;

        void
        on_begin(m::utc_time_point_type const&) override
        {}

        std::optional<requeue_directory_access_attempt>
        on_directory_access_failure(m::utc_time_point_type const&,
                                    m::pil::file_path const&,
                                    std::system_error const&) override
        {
            return std::nullopt;
        }

        std::optional<requeue_change_notification_attempt>
        on_change_notification_attempt_failure(m::utc_time_point_type const&,
                                               m::pil::file_path const&,
                                               std::system_error const&) override
        {
            return std::nullopt;
        }

        void
        on_change(m::utc_time_point_type const&,
                  m::pil::file_path const&,
                  m::pil::filesystem_change_kind kind,
                  m::pil::file_path const&       entry_name) override;

        void
        on_cancelled(m::utc_time_point_type const&) override;

    private:
        directory_watch_context& m_ctx;
    };

    //
    // An overlapped mReadDirectoryChangesW call that arrived before any change
    // was queued: its buffer is filled, *m_bytes_returned is set and m_event is
    // signaled by the sink when the next change lands.
    //
    struct pending_overlapped_read
    {
        LPVOID  m_buffer;
        DWORD   m_buffer_length;
        LPDWORD m_bytes_returned;
        HANDLE  m_event;
    };

    //
    // Per-directory-handle watch state. Member order is load-bearing for
    // teardown: members destroy in reverse declaration order, so m_token (last)
    // is destroyed first -- the PIL token's destructor cancels the in-flight
    // read and waits for any running callback to finish -- and only then is
    // m_sink destroyed, guaranteeing no callback touches the queue / mutex after
    // they begin to die.
    //
    struct directory_watch_context
    {
        std::mutex                                                              m_mutex;
        std::condition_variable                                                 m_cv;
        std::deque<std::pair<m::pil::filesystem_change_kind, std::u16string>>   m_changes;
        bool                                                                    m_cancelled = false;
        std::optional<pending_overlapped_read>                                  m_pending;
        std::unique_ptr<notify_change_sink>                                     m_sink;
        std::unique_ptr<m::pil::ifilesystem_monitor_token>                      m_token;
    };
} // namespace m::mwin32_impl

namespace
{
    //
    // FILE_ACTION_* wire code for a PIL change kind. This is the inverse of the
    // direct provider's action_to_change_kind; the two together preserve the
    // Win32 action across the PIL surface. Changing any mapping is a breaking
    // change tied to the FILE_NOTIFY_INFORMATION format.
    //
    DWORD
    change_kind_to_action(m::pil::filesystem_change_kind kind) noexcept
    {
        using enum m::pil::filesystem_change_kind;
        switch (kind)
        {
        case added: return FILE_ACTION_ADDED;
        case removed: return FILE_ACTION_REMOVED;
        case modified: return FILE_ACTION_MODIFIED;
        case renamed_old_name: return FILE_ACTION_RENAMED_OLD_NAME;
        case renamed_new_name: return FILE_ACTION_RENAMED_NEW_NAME;
        }
        return FILE_ACTION_MODIFIED;
    }

    //
    // Project the Win32 dwNotifyFilter mask and bWatchSubtree flag onto the PIL
    // register_watch_flags. Each FILE_NOTIFY_CHANGE_* bit has a one-to-one
    // counterpart; the provider re-expands them onto its own notify filter.
    //
    m::pil::ifilesystem_monitor::register_watch_flags
    notify_filter_to_watch_flags(DWORD dwNotifyFilter, BOOL bWatchSubtree) noexcept
    {
        using enum m::pil::ifilesystem_monitor::register_watch_flags;

        auto flags = m::pil::ifilesystem_monitor::register_watch_flags{};

        if (bWatchSubtree)
            flags |= watch_subtree;
        if (dwNotifyFilter & FILE_NOTIFY_CHANGE_FILE_NAME)
            flags |= file_name_changes;
        if (dwNotifyFilter & FILE_NOTIFY_CHANGE_DIR_NAME)
            flags |= directory_name_changes;
        if (dwNotifyFilter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
            flags |= attribute_changes;
        if (dwNotifyFilter & FILE_NOTIFY_CHANGE_SIZE)
            flags |= size_changes;
        if (dwNotifyFilter & FILE_NOTIFY_CHANGE_LAST_WRITE)
            flags |= last_write_changes;
        if (dwNotifyFilter & FILE_NOTIFY_CHANGE_LAST_ACCESS)
            flags |= last_access_changes;
        if (dwNotifyFilter & FILE_NOTIFY_CHANGE_CREATION)
            flags |= creation_changes;
        if (dwNotifyFilter & FILE_NOTIFY_CHANGE_SECURITY)
            flags |= security_changes;

        return flags;
    }

    //
    // Decode queued change records into a FILE_NOTIFY_INFORMATION chain in
    // [buffer, buffer+len). The caller must hold ctx.m_mutex. Records are 4-byte
    // (DWORD) aligned and linked through NextEntryOffset (0 terminates the
    // chain); FileNameLength is in bytes and FileName is not NUL-terminated.
    // Returns the number of bytes written (the end of the final record, without
    // trailing alignment padding). A buffer too small for even the first record
    // yields 0 with the changes left queued -- the genuine API reports an
    // overflow as zero bytes returned, but leaving them queued lets a subsequent
    // larger read still observe them rather than dropping the events.
    //
    DWORD
    drain_changes_locked(m::mwin32_impl::directory_watch_context& ctx, LPVOID buffer, DWORD len)
    {
        constexpr DWORD record_alignment = sizeof(DWORD);
        constexpr DWORD header_bytes     = offsetof(FILE_NOTIFY_INFORMATION, FileName);

        auto* const base       = static_cast<std::byte*>(buffer);
        DWORD       cursor     = 0; // offset of the next record to write
        DWORD       prev_off   = 0; // offset of the previously written record
        DWORD       end        = 0; // one past the last byte written
        bool        wrote_any  = false;

        while (!ctx.m_changes.empty())
        {
            auto const& front      = ctx.m_changes.front();
            auto const  name_bytes = static_cast<DWORD>(front.second.size() * sizeof(WCHAR));
            auto const  record     = header_bytes + name_bytes;

            if (cursor + record > len)
            {
                if (!wrote_any)
                    return 0;
                break;
            }

            auto* const info      = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(base + cursor);
            info->NextEntryOffset = 0;
            info->Action          = change_kind_to_action(front.first);
            info->FileNameLength  = name_bytes;
            std::memcpy(info->FileName, front.second.data(), name_bytes);

            if (wrote_any)
            {
                auto* const prev      = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(base + prev_off);
                prev->NextEntryOffset = cursor - prev_off;
            }

            prev_off  = cursor;
            end       = cursor + record;
            wrote_any = true;
            ctx.m_changes.pop_front();

            cursor = (end + record_alignment - 1) & ~(record_alignment - 1);
            if (cursor >= len)
                break;
        }

        return end;
    }
} // namespace

namespace m::mwin32_impl
{
    void
    notify_change_sink::on_change(m::utc_time_point_type const&,
                                  m::pil::file_path const&,
                                  m::pil::filesystem_change_kind kind,
                                  m::pil::file_path const&       entry_name)
    {
        auto l = std::unique_lock(m_ctx.m_mutex);

        m_ctx.m_changes.emplace_back(kind, std::u16string(entry_name.native().view()));

        if (m_ctx.m_pending.has_value())
        {
            auto const  p     = *m_ctx.m_pending;
            DWORD const bytes = drain_changes_locked(m_ctx, p.m_buffer, p.m_buffer_length);
            if (p.m_bytes_returned != nullptr)
                *p.m_bytes_returned = bytes;
            m_ctx.m_pending.reset();
            if (p.m_event != nullptr)
                ::SetEvent(p.m_event);
        }

        m_ctx.m_cv.notify_all();
    }

    void
    notify_change_sink::on_cancelled(m::utc_time_point_type const&)
    {
        auto l = std::unique_lock(m_ctx.m_mutex);
        m_ctx.m_cancelled = true;
        m_ctx.m_cv.notify_all();
    }
} // namespace m::mwin32_impl

namespace
{
    //
    // Resolve the directory handle to its watch context, installing the watch on
    // first use. The watch is registered on the handle's stored public path with
    // the requested filter; the per-handle context (and the PIL token it owns)
    // then lives in file_handle_state, torn down by RAII when the handle closes.
    // The install is serialized by a process-wide mutex because the handle's
    // watch slot is shared mutable state.
    //
    std::shared_ptr<m::mwin32_impl::directory_watch_context>
    ensure_watch(std::shared_ptr<m::mwin32_impl::file_handle_state> const& state,
                 DWORD                                                     dwNotifyFilter,
                 BOOL                                                      bWatchSubtree)
    {
        static std::mutex install_mutex;

        auto l = std::unique_lock(install_mutex);

        if (!state->m_watch)
        {
            auto context    = std::make_shared<m::mwin32_impl::directory_watch_context>();
            context->m_sink = std::make_unique<m::mwin32_impl::notify_change_sink>(*context);

            auto const flags   = notify_filter_to_watch_flags(dwNotifyFilter, bWatchSubtree);
            auto const monitor = m::mwin32_impl::session_filesystem()->monitor();
            context->m_token =
                monitor->register_watch(flags, state->m_path, context->m_sink.get());

            state->m_watch = std::move(context);
        }

        return state->m_watch;
    }

    //
    // Shared body of mReadDirectoryChangesW / mReadDirectoryChangesExW. The first
    // call on a handle installs the watch; thereafter:
    //   * synchronous (lpOverlapped == nullptr): block until a change is queued
    //         (or the watch is cancelled), then decode the queue into lpBuffer;
    //   * overlapped (lpOverlapped != nullptr): if changes are already queued,
    //         complete immediately and signal hEvent; otherwise record the read
    //         as pending so the sink fills lpBuffer, sets *lpBytesReturned and
    //         signals hEvent when the next change lands. *lpBytesReturned is set
    //         on completion (not at call time); the caller observes it after
    //         waiting on hEvent. Completion-routine (APC) delivery is not
    //         modeled -- lpCompletionRoutine is ignored; an event-bearing
    //         OVERLAPPED is the supported asynchronous form.
    //
    BOOL
    read_directory_changes_impl(HANDLE       hDirectory,
                                LPVOID       lpBuffer,
                                DWORD        nBufferLength,
                                BOOL         bWatchSubtree,
                                DWORD        dwNotifyFilter,
                                LPDWORD      lpBytesReturned,
                                LPOVERLAPPED lpOverlapped)
    {
        M_VALIDATE_PARAMETER(lpBuffer, lpBuffer != nullptr);

        auto const state = resolve_file_handle(hDirectory);
        auto const ctx   = ensure_watch(state, dwNotifyFilter, bWatchSubtree);

        auto l = std::unique_lock(ctx->m_mutex);

        if (lpOverlapped == nullptr)
        {
            ctx->m_cv.wait(l, [&] { return !ctx->m_changes.empty() || ctx->m_cancelled; });

            DWORD const bytes = drain_changes_locked(*ctx, lpBuffer, nBufferLength);
            if (lpBytesReturned != nullptr)
                *lpBytesReturned = bytes;
            return TRUE;
        }

        if (!ctx->m_changes.empty())
        {
            DWORD const bytes = drain_changes_locked(*ctx, lpBuffer, nBufferLength);
            if (lpBytesReturned != nullptr)
                *lpBytesReturned = bytes;
            if (lpOverlapped->hEvent != nullptr)
                ::SetEvent(lpOverlapped->hEvent);
            return TRUE;
        }

        if (lpOverlapped->hEvent != nullptr)
            ::ResetEvent(lpOverlapped->hEvent);

        ctx->m_pending = m::mwin32_impl::pending_overlapped_read{
            lpBuffer, nBufferLength, lpBytesReturned, lpOverlapped->hEvent};
        return TRUE;
    }
} // namespace

BOOL APIENTRY
mReadDirectoryChangesW(_In_ HANDLE                                  hDirectory,
                       _Out_writes_bytes_(nBufferLength) LPVOID     lpBuffer,
                       _In_ DWORD                                   nBufferLength,
                       _In_ BOOL                                    bWatchSubtree,
                       _In_ DWORD                                   dwNotifyFilter,
                       _Out_opt_ LPDWORD                            lpBytesReturned,
                       _Inout_opt_ LPOVERLAPPED                     lpOverlapped,
                       _In_opt_ LPOVERLAPPED_COMPLETION_ROUTINE     lpCompletionRoutine)
{
    (void)lpCompletionRoutine;

    try
    {
        return read_directory_changes_impl(hDirectory,
                                           lpBuffer,
                                           nBufferLength,
                                           bWatchSubtree,
                                           dwNotifyFilter,
                                           lpBytesReturned,
                                           lpOverlapped);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

//
// mReadDirectoryChangesExW differs only by the trailing information-class
// selector. Only ReadDirectoryNotifyInformation (the basic FILE_NOTIFY_-
// INFORMATION record) is modeled; the extended class carries timestamps and
// sizes the PIL surface does not deliver, so it is rejected with
// ERROR_INVALID_PARAMETER rather than silently returning basic records.
//
BOOL APIENTRY
mReadDirectoryChangesExW(_In_ HANDLE                              hDirectory,
                         _Out_writes_bytes_(nBufferLength) LPVOID lpBuffer,
                         _In_ DWORD                               nBufferLength,
                         _In_ BOOL                                bWatchSubtree,
                         _In_ DWORD                               dwNotifyFilter,
                         _Out_opt_ LPDWORD                        lpBytesReturned,
                         _Inout_opt_ LPOVERLAPPED                 lpOverlapped,
                         _In_opt_ LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
                         _In_ READ_DIRECTORY_NOTIFY_INFORMATION_CLASS
                             ReadDirectoryNotifyInformationClass)
{
    (void)lpCompletionRoutine;

    try
    {
        if (ReadDirectoryNotifyInformationClass != ReadDirectoryNotifyInformation)
        {
            ::SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        return read_directory_changes_impl(hDirectory,
                                           lpBuffer,
                                           nBufferLength,
                                           bWatchSubtree,
                                           dwNotifyFilter,
                                           lpBytesReturned,
                                           lpOverlapped);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

//
// Coarse change-notification family (M-FS-NOTIFY-2). FindFirstChangeNotification
// historically predates ReadDirectoryChangesW and carries no per-change detail:
// the caller learns only that *something* under the directory changed. The shim
// realizes this on the same PIL monitor surface, but -- because it does not
// intercept WaitForSingleObject -- the handle it returns must be a genuine
// OS-waitable object, so a real manual-reset Win32 event is created and the
// monitor's change callback signals it. The event-only sink discards the change
// detail the detailed path decodes. (mwin32 D15.)
//
namespace m::mwin32_impl
{
    //
    // The sink behind a FindFirstChangeNotification watch. Any matching change
    // signals the owning event; the per-change detail (kind, entry name) is
    // discarded because this family reports none. Like notify_change_sink it
    // declines to requeue on failure (no shim-level retry policy) and holds only
    // the raw event handle -- the context owns the event and outlives the token
    // that can invoke this sink.
    //
    class change_event_sink final : public m::pil::ifilesystem_monitor_change_notification
    {
    public:
        explicit change_event_sink(HANDLE event) noexcept: m_event(event) {}

        ~change_event_sink() override = default;

        void
        on_begin(m::utc_time_point_type const&) override
        {}

        std::optional<requeue_directory_access_attempt>
        on_directory_access_failure(m::utc_time_point_type const&,
                                    m::pil::file_path const&,
                                    std::system_error const&) override
        {
            return std::nullopt;
        }

        std::optional<requeue_change_notification_attempt>
        on_change_notification_attempt_failure(m::utc_time_point_type const&,
                                               m::pil::file_path const&,
                                               std::system_error const&) override
        {
            return std::nullopt;
        }

        void
        on_change(m::utc_time_point_type const&,
                  m::pil::file_path const&,
                  m::pil::filesystem_change_kind,
                  m::pil::file_path const&) override
        {
            if (m_event != nullptr)
                ::SetEvent(m_event);
        }

        void
        on_cancelled(m::utc_time_point_type const&) override
        {}

    private:
        HANDLE m_event;
    };

    //
    // The state behind a change-notification handle: the owned Win32 event, the
    // sink that signals it, and the PIL watch token. The explicit destructor
    // makes the teardown order load-bearing: the token is released first (its
    // destructor cancels the watch and waits for any in-flight callback to
    // finish), then the sink, and only then is the event closed -- guaranteeing
    // no callback touches the event after it is closed.
    //
    struct change_notification_context
    {
        HANDLE                                             m_event = nullptr;
        std::unique_ptr<change_event_sink>                 m_sink;
        std::unique_ptr<m::pil::ifilesystem_monitor_token> m_token;

        ~change_notification_context()
        {
            m_token.reset();
            m_sink.reset();
            if (m_event != nullptr)
                ::CloseHandle(m_event);
        }
    };
} // namespace m::mwin32_impl

namespace
{
    //
    // Process-wide registry of live change-notification handles. The returned
    // event handle is a real OS handle (outside the minted-handle namespace), so
    // it cannot live in g_handles; this side table maps it back to its context
    // for re-arm (FindNextChangeNotification) and teardown (FindClose-
    // ChangeNotification). A Meyers singleton sidesteps static-destruction-order
    // hazards.
    //
    struct change_notify_registry
    {
        std::mutex m_mutex;
        std::map<HANDLE, std::shared_ptr<m::mwin32_impl::change_notification_context>> m_table;
    };

    change_notify_registry&
    change_notify_registry_instance()
    {
        static change_notify_registry instance;
        return instance;
    }

    //
    // Shared body of mFindFirstChangeNotificationW / ...A. Validates that the
    // path names an existing directory, creates the manual-reset event, registers
    // an event-only watch on the monitor, and records the context under the event
    // handle. On any failure returns INVALID_HANDLE_VALUE with the last-error set.
    //
    HANDLE
    find_first_change_notification_impl(m::pil::file_path const& path,
                                        BOOL                     bWatchSubtree,
                                        DWORD                    dwNotifyFilter)
    {
        auto const md = query_path_metadata(path);
        if (!md.has_value() || !md->is_directory())
        {
            ::SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }

        auto ctx = std::make_shared<m::mwin32_impl::change_notification_context>();

        ctx->m_event =
            ::CreateEventW(nullptr, TRUE /* manual reset */, FALSE /* non-signaled */, nullptr);
        if (ctx->m_event == nullptr)
            return INVALID_HANDLE_VALUE; // last error set by CreateEventW

        ctx->m_sink = std::make_unique<m::mwin32_impl::change_event_sink>(ctx->m_event);

        auto const flags   = notify_filter_to_watch_flags(dwNotifyFilter, bWatchSubtree);
        auto const monitor = m::mwin32_impl::session_filesystem()->monitor();
        ctx->m_token       = monitor->register_watch(flags, path, ctx->m_sink.get());

        HANDLE const h = ctx->m_event;

        auto& reg = change_notify_registry_instance();
        {
            auto l = std::unique_lock(reg.m_mutex);
            reg.m_table.emplace(h, std::move(ctx));
        }

        return h;
    }
} // namespace

HANDLE APIENTRY
mFindFirstChangeNotificationW(_In_ LPCWSTR lpPathName,
                              _In_ BOOL    bWatchSubtree,
                              _In_ DWORD   dwNotifyFilter)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);
        return find_first_change_notification_impl(
            to_file_path(lpPathName), bWatchSubtree, dwNotifyFilter);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_HANDLE_VALUE;
    }
}

HANDLE APIENTRY
mFindFirstChangeNotificationA(_In_ LPCSTR lpPathName,
                              _In_ BOOL   bWatchSubtree,
                              _In_ DWORD  dwNotifyFilter)
{
    try
    {
        M_VALIDATE_PARAMETER(lpPathName, lpPathName != nullptr);
        return find_first_change_notification_impl(
            to_file_path(lpPathName), bWatchSubtree, dwNotifyFilter);
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_HANDLE_VALUE;
    }
}

BOOL APIENTRY
mFindNextChangeNotification(_In_ HANDLE hChangeHandle)
{
    try
    {
        auto& reg = change_notify_registry_instance();

        auto l = std::unique_lock(reg.m_mutex);

        auto const it = reg.m_table.find(hChangeHandle);
        if (it == reg.m_table.end())
        {
            ::SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }

        //
        // Re-arm: clear the signal so the next matching change re-signals the
        // event. The watch itself stays registered, so a change that arrives
        // after this reset re-signals the event as expected.
        //
        ::ResetEvent(it->second->m_event);
        return TRUE;
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

BOOL APIENTRY
mFindCloseChangeNotification(_In_ HANDLE hChangeHandle)
{
    try
    {
        std::shared_ptr<m::mwin32_impl::change_notification_context> doomed;

        {
            auto& reg = change_notify_registry_instance();

            auto l = std::unique_lock(reg.m_mutex);

            auto const it = reg.m_table.find(hChangeHandle);
            if (it == reg.m_table.end())
            {
                ::SetLastError(ERROR_INVALID_HANDLE);
                return FALSE;
            }

            //
            // Move the context out and erase the map entry under the lock, then
            // let it die *after* releasing the lock: its destructor cancels the
            // watch (which may block until an in-flight callback finishes), and
            // that callback path must not contend on the registry mutex.
            //
            doomed = std::move(it->second);
            reg.m_table.erase(it);
        }

        return TRUE;
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}

//
// Transacted (TxF) filesystem family (M-FS-LEGACY-2). The Transactional NTFS
// entry points layer a transaction handle -- and, for a few forms, extra
// TxF-only parameters -- on top of an otherwise ordinary filesystem operation.
// TxF is deprecated on Windows and has no analogue on the PIL surface, so each
// shim forwards to its non-transacted m* sibling and *ignores* the transaction
// handle (D11): the operation simply runs un-transacted. The redirection,
// buffering, and last-error contract are therefore exactly those of the
// forwarded non-transacted op; these forwarders add no try/catch of their own
// because the sibling already maps any in-flight exception to a Win32
// last-error.
//

HANDLE APIENTRY
mCreateFileTransactedW(_In_ LPCWSTR                   lpFileName,
                       _In_ DWORD                     dwDesiredAccess,
                       _In_ DWORD                     dwShareMode,
                       _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                       _In_ DWORD                     dwCreationDisposition,
                       _In_ DWORD                     dwFlagsAndAttributes,
                       _In_opt_ HANDLE                hTemplateFile,
                       _In_ HANDLE                    hTransaction,
                       _In_opt_ PUSHORT               pusMiniVersion,
                       _In_opt_ PVOID                 lpExtendedParameter)
{
    (void)hTransaction;
    (void)pusMiniVersion;
    (void)lpExtendedParameter;
    return ::mCreateFileW(lpFileName,
                          dwDesiredAccess,
                          dwShareMode,
                          lpSecurityAttributes,
                          dwCreationDisposition,
                          dwFlagsAndAttributes,
                          hTemplateFile);
}

HANDLE APIENTRY
mCreateFileTransactedA(_In_ LPCSTR                    lpFileName,
                       _In_ DWORD                     dwDesiredAccess,
                       _In_ DWORD                     dwShareMode,
                       _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                       _In_ DWORD                     dwCreationDisposition,
                       _In_ DWORD                     dwFlagsAndAttributes,
                       _In_opt_ HANDLE                hTemplateFile,
                       _In_ HANDLE                    hTransaction,
                       _In_opt_ PUSHORT               pusMiniVersion,
                       _In_opt_ PVOID                 lpExtendedParameter)
{
    (void)hTransaction;
    (void)pusMiniVersion;
    (void)lpExtendedParameter;
    return ::mCreateFileA(lpFileName,
                          dwDesiredAccess,
                          dwShareMode,
                          lpSecurityAttributes,
                          dwCreationDisposition,
                          dwFlagsAndAttributes,
                          hTemplateFile);
}

BOOL APIENTRY
mCreateDirectoryTransactedW(_In_ LPCWSTR                   lpTemplateDirectory,
                            _In_ LPCWSTR                   lpNewDirectory,
                            _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                            _In_ HANDLE                    hTransaction)
{
    (void)hTransaction;
    return ::mCreateDirectoryExW(lpTemplateDirectory, lpNewDirectory, lpSecurityAttributes);
}

BOOL APIENTRY
mCreateDirectoryTransactedA(_In_ LPCSTR                    lpTemplateDirectory,
                            _In_ LPCSTR                    lpNewDirectory,
                            _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                            _In_ HANDLE                    hTransaction)
{
    (void)hTransaction;
    return ::mCreateDirectoryExA(lpTemplateDirectory, lpNewDirectory, lpSecurityAttributes);
}

BOOL APIENTRY
mRemoveDirectoryTransactedW(_In_ LPCWSTR lpPathName, _In_ HANDLE hTransaction)
{
    (void)hTransaction;
    return ::mRemoveDirectoryW(lpPathName);
}

BOOL APIENTRY
mRemoveDirectoryTransactedA(_In_ LPCSTR lpPathName, _In_ HANDLE hTransaction)
{
    (void)hTransaction;
    return ::mRemoveDirectoryA(lpPathName);
}

BOOL APIENTRY
mMoveFileTransactedW(_In_ LPCWSTR                lpExistingFileName,
                     _In_opt_ LPCWSTR            lpNewFileName,
                     _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
                     _In_opt_ LPVOID             lpData,
                     _In_ DWORD                  dwFlags,
                     _In_ HANDLE                 hTransaction)
{
    (void)lpProgressRoutine;
    (void)lpData;
    (void)hTransaction;
    return ::mMoveFileExW(lpExistingFileName, lpNewFileName, dwFlags);
}

BOOL APIENTRY
mMoveFileTransactedA(_In_ LPCSTR                 lpExistingFileName,
                     _In_opt_ LPCSTR             lpNewFileName,
                     _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
                     _In_opt_ LPVOID             lpData,
                     _In_ DWORD                  dwFlags,
                     _In_ HANDLE                 hTransaction)
{
    (void)lpProgressRoutine;
    (void)lpData;
    (void)hTransaction;
    return ::mMoveFileExA(lpExistingFileName, lpNewFileName, dwFlags);
}

BOOL APIENTRY
mCopyFileTransactedW(_In_ LPCWSTR                lpExistingFileName,
                     _In_ LPCWSTR                lpNewFileName,
                     _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
                     _In_opt_ LPVOID             lpData,
                     _In_opt_ LPBOOL             pbCancel,
                     _In_ DWORD                  dwCopyFlags,
                     _In_ HANDLE                 hTransaction)
{
    (void)hTransaction;
    return ::mCopyFileExW(
        lpExistingFileName, lpNewFileName, lpProgressRoutine, lpData, pbCancel, dwCopyFlags);
}

BOOL APIENTRY
mCopyFileTransactedA(_In_ LPCSTR                 lpExistingFileName,
                     _In_ LPCSTR                 lpNewFileName,
                     _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
                     _In_opt_ LPVOID             lpData,
                     _In_opt_ LPBOOL             pbCancel,
                     _In_ DWORD                  dwCopyFlags,
                     _In_ HANDLE                 hTransaction)
{
    (void)hTransaction;
    return ::mCopyFileExA(
        lpExistingFileName, lpNewFileName, lpProgressRoutine, lpData, pbCancel, dwCopyFlags);
}

BOOL APIENTRY
mGetFileAttributesTransactedW(_In_ LPCWSTR                lpFileName,
                              _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
                              _Out_ LPVOID                lpFileInformation,
                              _In_ HANDLE                 hTransaction)
{
    (void)hTransaction;
    return ::mGetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
}

BOOL APIENTRY
mGetFileAttributesTransactedA(_In_ LPCSTR                 lpFileName,
                              _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
                              _Out_ LPVOID                lpFileInformation,
                              _In_ HANDLE                 hTransaction)
{
    (void)hTransaction;
    return ::mGetFileAttributesExA(lpFileName, fInfoLevelId, lpFileInformation);
}

BOOL APIENTRY
mSetFileAttributesTransactedW(_In_ LPCWSTR lpFileName,
                              _In_ DWORD   dwFileAttributes,
                              _In_ HANDLE  hTransaction)
{
    (void)hTransaction;
    return ::mSetFileAttributesW(lpFileName, dwFileAttributes);
}

BOOL APIENTRY
mSetFileAttributesTransactedA(_In_ LPCSTR lpFileName,
                              _In_ DWORD  dwFileAttributes,
                              _In_ HANDLE hTransaction)
{
    (void)hTransaction;
    return ::mSetFileAttributesA(lpFileName, dwFileAttributes);
}

HANDLE APIENTRY
mFindFirstFileTransactedW(_In_ LPCWSTR            lpFileName,
                          _In_ FINDEX_INFO_LEVELS fInfoLevelId,
                          _Out_ LPVOID            lpFindFileData,
                          _In_ FINDEX_SEARCH_OPS  fSearchOp,
                          _Reserved_ LPVOID       lpSearchFilter,
                          _In_ DWORD              dwAdditionalFlags,
                          _In_ HANDLE             hTransaction)
{
    (void)fInfoLevelId;
    (void)fSearchOp;
    (void)lpSearchFilter;
    (void)dwAdditionalFlags;
    (void)hTransaction;
    return ::mFindFirstFileW(lpFileName, static_cast<LPWIN32_FIND_DATAW>(lpFindFileData));
}

HANDLE APIENTRY
mFindFirstFileTransactedA(_In_ LPCSTR             lpFileName,
                          _In_ FINDEX_INFO_LEVELS fInfoLevelId,
                          _Out_ LPVOID            lpFindFileData,
                          _In_ FINDEX_SEARCH_OPS  fSearchOp,
                          _Reserved_ LPVOID       lpSearchFilter,
                          _In_ DWORD              dwAdditionalFlags,
                          _In_ HANDLE             hTransaction)
{
    (void)fInfoLevelId;
    (void)fSearchOp;
    (void)lpSearchFilter;
    (void)dwAdditionalFlags;
    (void)hTransaction;
    return ::mFindFirstFileA(lpFileName, static_cast<LPWIN32_FIND_DATAA>(lpFindFileData));
}

DWORD APIENTRY
mGetLongPathNameTransactedW(_In_ LPCWSTR                        lpszShortPath,
                            _Out_writes_opt_(cchBuffer) LPWSTR  lpszLongPath,
                            _In_ DWORD                          cchBuffer,
                            _In_ HANDLE                         hTransaction)
{
    (void)hTransaction;
    return ::mGetLongPathNameW(lpszShortPath, lpszLongPath, cchBuffer);
}

DWORD APIENTRY
mGetLongPathNameTransactedA(_In_ LPCSTR                        lpszShortPath,
                            _Out_writes_opt_(cchBuffer) LPSTR  lpszLongPath,
                            _In_ DWORD                         cchBuffer,
                            _In_ HANDLE                        hTransaction)
{
    (void)hTransaction;
    return ::mGetLongPathNameA(lpszShortPath, lpszLongPath, cchBuffer);
}


//
// Alternate-data-stream enumeration family (M-FS-STREAMS-2). mFindFirstStreamW
// captures the stream listing via ifile::enumerate_streams and mints a pseudo-
// handle over the iteration state; mFindNextStreamW advances the cursor.
//

namespace
{
    //
    // Open the target file and capture its full stream listing into a fresh
    // stream-enumeration state. Returns the state (for interning) or throws on
    // any PIL exception (caller's catch-all converts to a Win32 last-error).
    //
    std::shared_ptr<m::mwin32_impl::stream_enumeration_state>
    capture_stream_listing(m::pil::file_path const& file_path)
    {
        auto const root = open_root_for(file_path);
        auto const rel  = m::pil::file_path{file_path.relative_path()};

        // Open the file (read access to enumerate its streams).
        auto file = root->open_file(rel, m::pil::file_access::read);

        auto state = std::make_shared<m::mwin32_impl::stream_enumeration_state>();
        for (std::size_t i = 0;; ++i)
        {
            auto entry = file->enumerate_streams(i);
            if (!entry.has_value())
                break;
            state->m_entries.push_back(std::move(entry.value()));
        }

        return state;
    }

    //
    // Fill a WIN32_FIND_STREAM_DATA from a PIL stream_entry. The stream name is
    // copied into the fixed cStreamName buffer (MAX_PATH wchars) and truncated
    // with a guaranteed null terminator if it does not fit.
    //
    void
    fill_stream_data(m::pil::stream_entry const& entry, WIN32_FIND_STREAM_DATA& out)
    {
        out = WIN32_FIND_STREAM_DATA{};

        out.StreamSize.QuadPart = static_cast<LONGLONG>(entry.m_size);

        auto const        sv = entry.m_name.view();
        std::size_t const n  = std::min<std::size_t>(sv.size(), MAX_PATH - 1);
        for (std::size_t i = 0; i < n; ++i)
            out.cStreamName[i] = static_cast<WCHAR>(sv[i]);
        out.cStreamName[n] = L'\0';
    }

    //
    // Shared body of mFindFirstStreamW. Captures the stream listing, fills the
    // caller's find-stream-data with the first entry, interns the enumeration
    // state, and returns the minted pseudo-handle. An empty listing is reported
    // as ERROR_HANDLE_EOF with INVALID_HANDLE_VALUE, matching the genuine API.
    //
    HANDLE
    find_first_stream_impl(m::pil::file_path const& file_path, WIN32_FIND_STREAM_DATA& out)
    {
        auto state = capture_stream_listing(file_path);

        if (state->m_entries.empty())
        {
            ::SetLastError(ERROR_HANDLE_EOF);
            return INVALID_HANDLE_VALUE;
        }

        fill_stream_data(state->m_entries[0], out);
        state->m_cursor = 1;

        return ::g_handles.intern(state).as_HANDLE();
    }

    //
    // Shared body of mFindNextStreamW. Advances the cursor behind hFindStream,
    // filling out the caller's find-stream-data with the next entry. Returns
    // FALSE / ERROR_HANDLE_EOF when no entries remain.
    //
    BOOL
    find_next_stream_impl(HANDLE hFindStream, WIN32_FIND_STREAM_DATA& out)
    {
        auto const state =
            ::g_handles.deref_handle<std::shared_ptr<m::mwin32_impl::stream_enumeration_state>>(
                m::mwin32_impl::handle::from_HANDLE(hFindStream));

        if (state->m_cursor >= state->m_entries.size())
        {
            ::SetLastError(ERROR_HANDLE_EOF);
            return FALSE;
        }

        fill_stream_data(state->m_entries[state->m_cursor], out);
        ++state->m_cursor;
        return TRUE;
    }
} // namespace

//
// mFindFirstStreamW: open the file, capture its stream listing, fill the first
// entry, and return a pseudo-handle over the iteration state.
//
HANDLE APIENTRY
mFindFirstStreamW(_In_ LPCWSTR            lpFileName,
                  _In_ STREAM_INFO_LEVELS InfoLevel,
                  _Out_ LPVOID            lpFindStreamData,
                  _Reserved_ DWORD        dwFlags)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFileName, lpFileName != nullptr);
        M_VALIDATE_PARAMETER(lpFindStreamData, lpFindStreamData != nullptr);
        M_VALIDATE_PARAMETER(InfoLevel, InfoLevel == FindStreamInfoStandard);
        (void)dwFlags; // reserved, ignored

        return find_first_stream_impl(to_file_path(lpFileName),
                                      *static_cast<WIN32_FIND_STREAM_DATA*>(lpFindStreamData));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return INVALID_HANDLE_VALUE;
    }
}

//
// mFindNextStreamW: advance the cursor behind the pseudo-handle and fill the
// next entry. Returns FALSE / ERROR_HANDLE_EOF when no entries remain. The
// handle is released by mFindClose (shared with the file-find family).
//
BOOL APIENTRY
mFindNextStreamW(_In_ HANDLE hFindStream, _Out_ LPVOID lpFindStreamData)
{
    try
    {
        M_VALIDATE_PARAMETER(lpFindStreamData, lpFindStreamData != nullptr);

        return find_next_stream_impl(hFindStream,
                                     *static_cast<WIN32_FIND_STREAM_DATA*>(lpFindStreamData));
    }
    catch (...)
    {
        ::SetLastError(filesystem_exception_to_win32());
        return FALSE;
    }
}
