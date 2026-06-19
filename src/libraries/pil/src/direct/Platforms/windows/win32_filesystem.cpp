// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/utility/enum_operations.h>
#include <m/utility/error_macros.h>
#include <m/windows_chrono/windows_chrono_casts.h>

#include "pcwstr.h"
#include "win32.h"

namespace m::pil::impl::win32
{
    // Maps a file_path to the string Win32 should see. Fully qualified
    // drive / UNC paths gain the extended-length ("\\?\") prefix so long
    // paths work (M-FS-DIRECT-1); paths that already carry an extended root
    // (D11) are passed through verbatim.
    std::u16string
    to_win32_path(file_path const& path)
    {
        std::u16string text(path.native().view());

        switch (path.root_kind())
        {
            using enum file_root_kind;

            case extended:
            case extended_unc: return text;

            case unc:
                // "\\server\share\..." -> "\\?\UNC\server\share\..."
                return std::u16string(u"\\\\?\\UNC") + text.substr(1);

            case drive:
                if (path.is_absolute())
                    return std::u16string(u"\\\\?\\") + text;
                return text;

            default: return text;
        }
    }

    namespace
    {
        // The bit width of a DWORD: a 64-bit file size is assembled from its
        // high and low 32-bit halves.
        inline constexpr unsigned dword_bit_width = 32;

        // The largest value representable in a DWORD: the low-32 mask for an
        // OVERLAPPED offset and the per-call clamp for a ReadFile transfer
        // count (which is itself a DWORD).
        inline constexpr std::uint64_t max_dword = 0xffffffffull;

        // The share mode used for every open. The PIL isolates one logical view
        // of the namespace; permitting concurrent read/write/delete sharing keeps
        // open node handles from blocking rename/delete of the same node.
        inline constexpr DWORD shared_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

        // Maps the surface access request onto a Win32 desired-access mask.
        DWORD
        to_desired_access(file_access access)
        {
            DWORD desired = 0;
            if (!!(access & file_access::read))
                desired |= GENERIC_READ;
            if (!!(access & file_access::write))
                desired |= GENERIC_WRITE;
            return desired;
        }

        // RAII guard for a FindFirstFile enumeration handle. A find handle must
        // be released with FindClose (not CloseHandle), so it cannot reuse the
        // generic m::win32::handle wrapper.
        class find_handle_guard
        {
        public:
            explicit find_handle_guard(HANDLE h) noexcept: m_handle(h) {}
            find_handle_guard(find_handle_guard const&)            = delete;
            find_handle_guard& operator=(find_handle_guard const&) = delete;

            ~find_handle_guard()
            {
                if (m_handle != INVALID_HANDLE_VALUE)
                    ::FindClose(m_handle);
            }

        private:
            HANDLE m_handle;
        };

        // True for the "." and ".." pseudo-entries that the unified namespace
        // (D13) does not surface as children.
        bool
        is_dot_or_dotdot(wchar_t const* name)
        {
            return (name[0] == L'.' && name[1] == L'\0') ||
                   (name[0] == L'.' && name[1] == L'.' && name[2] == L'\0');
        }

        // Builds an sstring leaf name from a null-terminated Win32 wide string.
        // On Windows wchar_t and char16_t share a representation.
        file_name_string_type
        leaf_name_from_wide(wchar_t const* name)
        {
            return file_name_string_type(
                file_name_view_type(reinterpret_cast<char16_t const*>(name)));
        }

        // Builds a single-component file_path from a Win32 wide leaf name, for
        // composing a child path with operator/.
        file_path
        leaf_path_from_wide(wchar_t const* name)
        {
            return file_path(file_path::view_type(reinterpret_cast<char16_t const*>(name)));
        }

        // Assembles surface metadata from any Win32 information structure whose
        // members follow the BY_HANDLE_FILE_INFORMATION / WIN32_FIND_DATAW naming
        // (the relevant fields are spelled identically in both).
        template <typename InfoT>
        file_metadata
        to_metadata(InfoT const& info)
        {
            file_metadata  md;
            auto const     attrs = info.dwFileAttributes;
            md.m_attributes      = static_cast<file_attributes>(attrs);
            md.m_kind = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? node_kind::directory : node_kind::file;

            if (md.m_kind == node_kind::file)
                md.m_size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << dword_bit_width) |
                            info.nFileSizeLow;

            md.m_creation_time    = m::clock_cast<m::pil::clock_type>(info.ftCreationTime);
            md.m_last_write_time  = m::clock_cast<m::pil::clock_type>(info.ftLastWriteTime);
            md.m_last_access_time = m::clock_cast<m::pil::clock_type>(info.ftLastAccessTime);
            return md;
        }

        // Invokes fn(child_path, is_directory) for every real child of `dir`
        // (the "." / ".." pseudo-entries are skipped). Used by the recursive
        // delete; a missing directory yields no children rather than an error.
        template <typename Fn>
        void
        for_each_child(file_path const& dir, Fn&& fn)
        {
            std::u16string pattern(to_win32_path(dir));
            if (!pattern.empty() && pattern.back() != file_preferred_separator)
                pattern.push_back(file_preferred_separator);
            pattern.push_back(u'*');

            auto const namez = pcwstr(std::u16string_view(pattern));

            WIN32_FIND_DATAW fd{};
            HANDLE           find = ::FindFirstFileExW(
                namez, FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
            if (find == INVALID_HANDLE_VALUE)
            {
                auto const status = ::GetLastError();
                if (status == ERROR_FILE_NOT_FOUND)
                    return;
                m::throw_win32_error_code(status);
            }

            find_handle_guard guard(find);

            do
            {
                if (is_dot_or_dotdot(fd.cFileName))
                    continue;

                bool const is_directory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                fn(dir / leaf_path_from_wide(fd.cFileName), is_directory);
            } while (::FindNextFileW(find, &fd));
        }

        // Removes the node at `node` and, when it is a directory, everything
        // beneath it. Mirrors RegDeleteTree's recursion for the unified
        // filesystem namespace (D13).
        void
        delete_node_recursive(file_path const& node)
        {
            auto const win_path = to_win32_path(node);
            auto const namez    = pcwstr(std::u16string_view(win_path));

            DWORD const attrs = ::GetFileAttributesW(namez);
            if (attrs == INVALID_FILE_ATTRIBUTES)
                m::throw_win32_error_code(::GetLastError());

            if (attrs & FILE_ATTRIBUTE_DIRECTORY)
            {
                for_each_child(node,
                               [](file_path const& child, bool) { delete_node_recursive(child); });
                if (!::RemoveDirectoryW(namez))
                    m::throw_win32_error_code(::GetLastError());
            }
            else
            {
                if (!::DeleteFileW(namez))
                    m::throw_win32_error_code(::GetLastError());
            }
        }
    } // namespace

    //
    // filesystem
    //

    filesystem::filesystem(std::shared_ptr<m::work_queue> wq): m_work_queue(std::move(wq)) {}

    ifilesystem::open_root_disposition
    filesystem::open_root(open_root_flags              flags,
                          file_root const&             root,
                          file_access                  access,
                          std::shared_ptr<idirectory>& returned_directory)
    {
        returned_directory.reset();

        M_VALIDATE_FLAGS_PARAMETER(flags, open_root_flags{});

        // A bare drive root ("C:") names the drive-relative current directory;
        // to open the drive's top-level directory it must be terminated by a
        // separator ("C:\").
        std::u16string root_text(root.text());
        if (root.kind() == file_root_kind::drive &&
            (root_text.empty() || root_text.back() != file_preferred_separator))
            root_text.push_back(file_preferred_separator);

        file_path root_path{file_path::view_type(root_text)};

        auto const win_path = to_win32_path(root_path);
        auto const namez    = pcwstr(std::u16string_view(win_path));

        HANDLE raw = ::CreateFileW(namez,
                                   to_desired_access(access),
                                   shared_all,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS,
                                   nullptr);
        if (raw == INVALID_HANDLE_VALUE)
            m::throw_win32_error_code(::GetLastError());

        returned_directory =
            std::make_shared<directory>(m::win32::handle(raw), std::move(root_path));

        return open_root_disposition{};
    }

    ifilesystem::monitor_disposition
    filesystem::monitor(monitor_flags                                 flags,
                        std::shared_ptr<m::pil::ifilesystem_monitor>& returned_filesystem_monitor)
    {
        if (flags != monitor_flags{})
            throw std::runtime_error("Invalid flags to call to ifilesystem::monitor()");

        auto lock = std::unique_lock(m_mutex);

        if (!m_monitor)
            initialize_monitor(m::locked);

        M_INTERNAL_ERROR_CHECK(m_monitor);

        returned_filesystem_monitor = m_monitor;
        return monitor_disposition{};
    }

    void
    filesystem::initialize_monitor(m::locked_t)
    {
        if (m_monitor)
            return;

        m_monitor = std::make_shared<filesystem_monitor>(m_work_queue);
    }

    //
    // directory
    //

    directory::directory(m::win32::handle&& h, file_path path):
        m_handle(std::move(h)), m_path(std::move(path))
    {}

    file_path
    directory::child_path(file_path const& name) const
    {
        return m_path / name;
    }

    idirectory::create_directory_disposition
    directory::create_directory(create_directory_flags       flags,
                                file_path const&             path,
                                file_access                  access,
                                std::shared_ptr<idirectory>& returned_directory)
    {
        returned_directory.reset();

        M_VALIDATE_FLAGS_PARAMETER(flags, create_directory_flags{});

        auto       child    = child_path(path);
        auto const win_path = to_win32_path(child);
        auto const namez    = pcwstr(std::u16string_view(win_path));

        // create-or-open semantics, mirroring RegCreateKeyEx: an already-present
        // directory is not an error. Any other failure propagates.
        if (!::CreateDirectoryW(namez, nullptr))
        {
            auto const status = ::GetLastError();
            if (status != ERROR_ALREADY_EXISTS)
                m::throw_win32_error_code(status);
        }

        HANDLE raw = ::CreateFileW(namez,
                                   to_desired_access(access),
                                   shared_all,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS,
                                   nullptr);
        if (raw == INVALID_HANDLE_VALUE)
            m::throw_win32_error_code(::GetLastError());

        m::win32::handle h(raw);

        BY_HANDLE_FILE_INFORMATION bhfi{};
        if (!::GetFileInformationByHandle(h, &bhfi))
            m::throw_win32_error_code(::GetLastError());

        // The unified namespace (D13) forbids a directory and a file sharing a
        // name; if the name already denoted a file, surface "already exists".
        if (!(bhfi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            m::throw_win32_error_code(ERROR_ALREADY_EXISTS);

        returned_directory = std::make_shared<directory>(std::move(h), std::move(child));
        return create_directory_disposition{};
    }

    idirectory::create_file_disposition
    directory::create_file(create_file_flags       flags,
                           file_path const&        path,
                           file_access             access,
                           std::shared_ptr<ifile>& returned_file)
    {
        returned_file.reset();

        M_VALIDATE_FLAGS_PARAMETER(flags, create_file_flags{});

        auto       child    = child_path(path);
        auto const win_path = to_win32_path(child);
        auto const namez    = pcwstr(std::u16string_view(win_path));

        // OPEN_ALWAYS gives create-or-open: created when absent, opened when
        // present. An existing directory of the same name fails the open
        // (ERROR_ACCESS_DENIED) and propagates, preserving the unified
        // namespace's one-name-one-kind rule (D13).
        HANDLE raw = ::CreateFileW(namez,
                                   to_desired_access(access),
                                   shared_all,
                                   nullptr,
                                   OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
        if (raw == INVALID_HANDLE_VALUE)
            m::throw_win32_error_code(::GetLastError());

        returned_file = std::make_shared<file>(m::win32::handle(raw), std::move(child));
        return create_file_disposition{};
    }

    idirectory::open_directory_disposition
    directory::open_directory(open_directory_flags         flags,
                              file_path const&             path,
                              file_access                  access,
                              std::shared_ptr<idirectory>& returned_directory,
                              std::error_code&             ec)
    {
        ec.clear();
        returned_directory.reset();

        if (m::excess_bits_set(flags, open_directory_flags::tolerate_not_found))
            throw std::runtime_error("Invalid flags to directory::open_directory() call");

        auto       child    = child_path(path);
        auto const win_path = to_win32_path(child);
        auto const namez    = pcwstr(std::u16string_view(win_path));

        HANDLE raw = ::CreateFileW(namez,
                                   to_desired_access(access),
                                   shared_all,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS,
                                   nullptr);
        if (raw == INVALID_HANDLE_VALUE)
        {
            auto const status = ::GetLastError();

            // When tentative-open is requested, a missing node is a (non-error)
            // disposition rather than an `ec`. Both ERROR_FILE_NOT_FOUND (the
            // leaf is absent) and ERROR_PATH_NOT_FOUND (an intermediate
            // component is absent) mean "the requested directory is not there".
            if (((status == ERROR_FILE_NOT_FOUND) || (status == ERROR_PATH_NOT_FOUND)) &&
                !!(flags & open_directory_flags::tolerate_not_found))
                return open_directory_disposition{open_directory_result_code::not_found};

            ec = m::make_win32_error_code(status);
            return open_directory_disposition{};
        }

        m::win32::handle h(raw);

        BY_HANDLE_FILE_INFORMATION bhfi{};
        if (!::GetFileInformationByHandle(h, &bhfi))
            m::throw_win32_error_code(::GetLastError());

        // The unified namespace (D13) keeps the verbs kind-specific: opening a
        // file through open_directory is rejected.
        if (!(bhfi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            ec = m::make_win32_error_code(ERROR_DIRECTORY);
            return open_directory_disposition{};
        }

        returned_directory = std::make_shared<directory>(std::move(h), std::move(child));
        return open_directory_disposition{};
    }

    idirectory::open_file_disposition
    directory::open_file(open_file_flags         flags,
                         file_path const&        path,
                         file_access             access,
                         std::shared_ptr<ifile>& returned_file,
                         std::error_code&        ec)
    {
        ec.clear();
        returned_file.reset();

        if (m::excess_bits_set(flags, open_file_flags::tolerate_not_found))
            throw std::runtime_error("Invalid flags to directory::open_file() call");

        auto       child    = child_path(path);
        auto const win_path = to_win32_path(child);
        auto const namez    = pcwstr(std::u16string_view(win_path));

        HANDLE raw = ::CreateFileW(namez,
                                   to_desired_access(access),
                                   shared_all,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
        if (raw == INVALID_HANDLE_VALUE)
        {
            auto const status = ::GetLastError();

            if (((status == ERROR_FILE_NOT_FOUND) || (status == ERROR_PATH_NOT_FOUND)) &&
                !!(flags & open_file_flags::tolerate_not_found))
                return open_file_disposition{open_file_result_code::not_found};

            ec = m::make_win32_error_code(status);
            return open_file_disposition{};
        }

        returned_file = std::make_shared<file>(m::win32::handle(raw), std::move(child));
        return open_file_disposition{};
    }

    idirectory::remove_entry_disposition
    directory::remove_entry(remove_entry_flags flags, file_path const& name)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, remove_entry_flags{});

        auto const child    = child_path(name);
        auto const win_path = to_win32_path(child);
        auto const namez    = pcwstr(std::u16string_view(win_path));

        DWORD const attrs = ::GetFileAttributesW(namez);
        if (attrs == INVALID_FILE_ATTRIBUTES)
            m::throw_win32_error_code(::GetLastError());

        // Unified namespace (D13): one verb removes whichever kind the name
        // denotes. A non-empty directory is rejected by RemoveDirectoryW
        // (ERROR_DIR_NOT_EMPTY); delete_tree is the recursive form.
        if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!::RemoveDirectoryW(namez))
                m::throw_win32_error_code(::GetLastError());
        }
        else
        {
            if (!::DeleteFileW(namez))
                m::throw_win32_error_code(::GetLastError());
        }

        return remove_entry_disposition{};
    }

    idirectory::delete_tree_disposition
    directory::delete_tree(delete_tree_flags flags, std::optional<file_path> const& name)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, delete_tree_flags{});

        if (name.has_value())
        {
            // Remove the named child and everything beneath it.
            delete_node_recursive(child_path(*name));
        }
        else
        {
            // Remove the contents of this directory, leaving the directory.
            for_each_child(m_path,
                           [](file_path const& child, bool) { delete_node_recursive(child); });
        }

        return delete_tree_disposition{};
    }

    idirectory::rename_entry_disposition
    directory::rename_entry(rename_entry_flags flags,
                            file_path const&   old_path,
                            file_path const&   new_path)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, rename_entry_flags{});

        auto const old_full = to_win32_path(child_path(old_path));
        auto const new_full = to_win32_path(child_path(new_path));

        auto const old_namez = pcwstr(std::u16string_view(old_full));
        auto const new_namez = pcwstr(std::u16string_view(new_full));

        // Rename/move within this directory's subtree. With no flags the move
        // does not replace an occupied destination (it fails with
        // ERROR_ALREADY_EXISTS).
        if (!::MoveFileExW(old_namez, new_namez, 0))
            m::throw_win32_error_code(::GetLastError());

        return rename_entry_disposition{};
    }

    idirectory::enumerate_entries_disposition
    directory::enumerate_entries(enumerate_entries_flags                          flags,
                                 std::size_t                                      starting_index,
                                 std::span<directory_entry, std::dynamic_extent>& entries)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, enumerate_entries_flags{});

        // The verb is stateless (it takes a starting index), so each call opens
        // a fresh enumeration and skips the already-reported prefix. Win32 yields
        // a stable order for an unchanged directory, so the skip is consistent
        // across the batched calls the wrapper makes.
        std::u16string pattern(to_win32_path(m_path));
        if (!pattern.empty() && pattern.back() != file_preferred_separator)
            pattern.push_back(file_preferred_separator);
        pattern.push_back(u'*');

        auto const namez = pcwstr(std::u16string_view(pattern));

        WIN32_FIND_DATAW fd{};
        // FindExInfoStandard (not FindExInfoBasic) is required so that
        // cAlternateFileName is populated; the buffered overlay captures that
        // 8.3 short name so a later lookup by the short alias resolves.
        HANDLE           find = ::FindFirstFileExW(
            namez, FindExInfoStandard, &fd, FindExSearchNameMatch, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE)
        {
            auto const status = ::GetLastError();
            if (status == ERROR_FILE_NOT_FOUND)
            {
                // An empty directory: no entries to report.
                entries = entries.subspan(0, 0);
                return enumerate_entries_disposition{};
            }
            m::throw_win32_error_code(status);
        }

        find_handle_guard guard(find);

        std::size_t skipped = 0;
        std::size_t filled  = 0;

        do
        {
            if (is_dot_or_dotdot(fd.cFileName))
                continue;

            if (skipped < starting_index)
            {
                ++skipped;
                continue;
            }

            if (filled >= entries.size())
                break;

            directory_entry entry(leaf_name_from_wide(fd.cFileName), to_metadata(fd));
            if (fd.cAlternateFileName[0] != L'\0')
                entry.m_short_name = leaf_name_from_wide(fd.cAlternateFileName);
            entries[filled] = std::move(entry);
            ++filled;
        } while (::FindNextFileW(find, &fd));

        entries = entries.subspan(0, filled);
        return enumerate_entries_disposition{};
    }

    idirectory::query_information_disposition
    directory::query_information(query_information_flags flags, file_metadata& metadata)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, query_information_flags{});

        BY_HANDLE_FILE_INFORMATION bhfi{};
        if (!::GetFileInformationByHandle(m_handle, &bhfi))
            m::throw_win32_error_code(::GetLastError());

        metadata = to_metadata(bhfi);
        return query_information_disposition{};
    }

    //
    // file
    //

    file::file(m::win32::handle&& h, file_path path): m_handle(std::move(h)), m_path(std::move(path))
    {}

    ifile::query_information_disposition
    file::query_information(query_information_flags flags, file_metadata& metadata)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, query_information_flags{});

        BY_HANDLE_FILE_INFORMATION bhfi{};
        if (!::GetFileInformationByHandle(m_handle, &bhfi))
            m::throw_win32_error_code(::GetLastError());

        metadata = to_metadata(bhfi);
        return query_information_disposition{};
    }

    ifile::read_content_disposition
    file::read_content(read_content_flags   flags,
                       std::uint64_t        offset,
                       std::span<std::byte> buffer,
                       std::size_t&         bytes_read,
                       std::error_code&     ec)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, read_content_flags{});

        bytes_read = 0;
        ec.clear();

        if (buffer.empty())
            return read_content_disposition{};

        // Positioned read on the node's (synchronous) handle: ReadFile honors
        // OVERLAPPED.Offset on a non-overlapped handle, so a single call reads
        // from the requested byte offset without an explicit seek and without
        // depending on a shared file pointer. The transfer count is a DWORD, so
        // one call is clamped to a DWORD and the caller loops for more.
        OVERLAPPED ov{};
        ov.Offset     = static_cast<DWORD>(offset & max_dword);
        ov.OffsetHigh = static_cast<DWORD>(offset >> dword_bit_width);

        DWORD const to_read =
            static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(), max_dword));

        DWORD read = 0;
        if (!::ReadFile(m_handle, buffer.data(), to_read, &read, &ov))
        {
            auto const status = ::GetLastError();

            // Reading at or past end-of-file is a zero-length short read, not a
            // hard error.
            if (status == ERROR_HANDLE_EOF)
                return read_content_disposition{};

            ec = m::make_win32_error_code(status);
            return read_content_disposition{};
        }

        bytes_read = read;
        return read_content_disposition{};
    }

    ifile::write_content_disposition
    file::write_content(write_content_flags        flags,
                        std::uint64_t              offset,
                        std::span<std::byte const> buffer,
                        std::size_t&               bytes_written,
                        std::error_code&           ec)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, write_content_flags{});

        bytes_written = 0;
        ec.clear();

        // Whole-file replacement only (D16): a write whose offset is non-zero is
        // a partial / mid-file overwrite, which is not modeled — report the
        // documented unsupported outcome.
        if (offset != 0)
        {
            ec = std::make_error_code(std::errc::not_supported);
            return write_content_disposition{};
        }

        // Positioned write at the head of the file: WriteFile honors
        // OVERLAPPED.Offset on the node's (synchronous) handle. The transfer
        // count is a DWORD, so one call is clamped to a DWORD and the caller
        // loops for more.
        OVERLAPPED ov{};
        ov.Offset     = 0;
        ov.OffsetHigh = 0;

        DWORD const to_write =
            static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(), max_dword));

        DWORD written = 0;
        if (to_write != 0 &&
            !::WriteFile(m_handle, buffer.data(), to_write, &written, &ov))
        {
            ec = m::make_win32_error_code(::GetLastError());
            return write_content_disposition{};
        }

        // Set the file's extent to the bytes just written, truncating any
        // trailing remainder so the result is a true whole-file replacement.
        // Use the 64-bit positioning API: a single WriteFile may transfer up to
        // max_dword (~4 GiB) bytes, which does not fit in the signed 32-bit
        // distance that SetFilePointer accepts (values above 2 GiB would be
        // misinterpreted as a negative offset).
        LARGE_INTEGER const new_extent{.QuadPart = static_cast<LONGLONG>(written)};
        if (!::SetFilePointerEx(m_handle, new_extent, nullptr, FILE_BEGIN))
        {
            ec = m::make_win32_error_code(::GetLastError());
            return write_content_disposition{};
        }

        if (!::SetEndOfFile(m_handle))
        {
            ec = m::make_win32_error_code(::GetLastError());
            return write_content_disposition{};
        }

        bytes_written = written;
        return write_content_disposition{};
    }

    ifile::enumerate_streams_disposition
    file::enumerate_streams(enumerate_streams_flags                       flags,
                            std::size_t                                   starting_index,
                            std::span<stream_entry, std::dynamic_extent>& entries,
                            std::error_code&                              ec)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, enumerate_streams_flags{});

        ec.clear();

        if (entries.empty())
            return enumerate_streams_disposition{};

        // Use the file path directly for FindFirstStreamW (no handle-based API).
        auto const win_path = to_win32_path(m_path);
        auto const namez    = pcwstr(std::u16string_view(win_path));

        WIN32_FIND_STREAM_DATA fsd{};
        HANDLE find = ::FindFirstStreamW(namez, FindStreamInfoStandard, &fsd, 0);
        if (find == INVALID_HANDLE_VALUE)
        {
            auto const status = ::GetLastError();
            // No streams is not an error - just return an empty span.
            if (status == ERROR_HANDLE_EOF)
            {
                entries = {};
                return enumerate_streams_disposition{};
            }
            ec = m::make_win32_error_code(status);
            return enumerate_streams_disposition{};
        }

        find_handle_guard guard(find);

        std::size_t skipped = 0;
        std::size_t filled  = 0;

        do
        {
            if (skipped < starting_index)
            {
                ++skipped;
                continue;
            }

            if (filled >= entries.size())
                break;

            // Stream name from Win32 is wchar_t; reinterpret as char16_t.
            file_name_string_type stream_name(
                file_name_view_type(reinterpret_cast<char16_t const*>(fsd.cStreamName)));

            // StreamSize is a LARGE_INTEGER; read its QuadPart.
            std::uint64_t stream_size = static_cast<std::uint64_t>(fsd.StreamSize.QuadPart);

            entries[filled] = stream_entry(std::move(stream_name), stream_size);
            ++filled;
        } while (::FindNextStreamW(find, &fsd));

        entries = entries.subspan(0, filled);
        return enumerate_streams_disposition{};
    }

} // namespace m::pil::impl::win32
