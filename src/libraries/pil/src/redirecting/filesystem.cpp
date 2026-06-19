// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>

#include "redirecting.h"

namespace m::pil::impl::redirecting
{
    //
    // file
    //

    file::file(std::shared_ptr<ifile> const&         underlying_file,
               std::shared_ptr<fs_redirector> const& redir):
        m_file(underlying_file), m_redirector(redir)
    {}

    ifile::query_information_disposition
    file::query_information(query_information_flags flags, file_metadata& metadata)
    {
        return m_file->query_information(flags, metadata);
    }

    ifile::read_content_disposition
    file::read_content(read_content_flags   flags,
                       std::uint64_t        offset,
                       std::span<std::byte> buffer,
                       std::size_t&         bytes_read,
                       std::error_code&     ec)
    {
        // Content is whole-file bytes; redirection rewrites names, not bytes,
        // so the read simply forwards to the underlying (backing) file.
        return m_file->read_content(flags, offset, buffer, bytes_read, ec);
    }

    ifile::write_content_disposition
    file::write_content(write_content_flags        flags,
                        std::uint64_t              offset,
                        std::span<std::byte const> buffer,
                        std::size_t&               bytes_written,
                        std::error_code&           ec)
    {
        // Redirection rewrites names, not bytes, so the whole-file write simply
        // forwards to the underlying (backing) file.
        return m_file->write_content(flags, offset, buffer, bytes_written, ec);
    }

    ifile::enumerate_streams_disposition
    file::enumerate_streams(enumerate_streams_flags                       flags,
                            std::size_t                                   starting_index,
                            std::span<stream_entry, std::dynamic_extent>& entries,
                            std::error_code&                              ec)
    {
        // Redirection rewrites file/directory names, not stream names, so the
        // stream enumeration simply forwards to the underlying (backing) file.
        return m_file->enumerate_streams(flags, starting_index, entries, ec);
    }

    //
    // directory
    //

    directory::directory(std::shared_ptr<idirectory> const&    underlying_directory,
                         std::shared_ptr<fs_redirector> const& redir):
        m_directory(underlying_directory), m_redirector(redir)
    {}

    idirectory::create_directory_disposition
    directory::create_directory(create_directory_flags       flags,
                                file_path const&             path,
                                file_access                  access,
                                std::shared_ptr<idirectory>& returned_directory)
    {
        std::shared_ptr<idirectory> unwrapped;
        auto const                  d = m_directory->create_directory(
            flags, m_redirector->map_public_to_private(path), access, unwrapped);
        if (unwrapped)
            returned_directory = std::make_shared<directory>(unwrapped, m_redirector);
        return d;
    }

    idirectory::create_file_disposition
    directory::create_file(create_file_flags       flags,
                           file_path const&        path,
                           file_access             access,
                           std::shared_ptr<ifile>& returned_file)
    {
        std::shared_ptr<ifile> unwrapped;
        auto const             d = m_directory->create_file(
            flags, m_redirector->map_public_to_private(path), access, unwrapped);
        if (unwrapped)
            returned_file = std::make_shared<file>(unwrapped, m_redirector);
        return d;
    }

    idirectory::open_directory_disposition
    directory::open_directory(open_directory_flags         flags,
                              file_path const&             path,
                              file_access                  access,
                              std::shared_ptr<idirectory>& returned_directory,
                              std::error_code&             ec)
    {
        std::shared_ptr<idirectory> unwrapped;
        auto const                  d = m_directory->open_directory(
            flags, m_redirector->map_public_to_private(path), access, unwrapped, ec);
        if (unwrapped)
            returned_directory = std::make_shared<directory>(unwrapped, m_redirector);
        return d;
    }

    idirectory::open_file_disposition
    directory::open_file(open_file_flags         flags,
                         file_path const&        path,
                         file_access             access,
                         std::shared_ptr<ifile>& returned_file,
                         std::error_code&        ec)
    {
        std::shared_ptr<ifile> unwrapped;
        auto const             d = m_directory->open_file(
            flags, m_redirector->map_public_to_private(path), access, unwrapped, ec);
        if (unwrapped)
            returned_file = std::make_shared<file>(unwrapped, m_redirector);
        return d;
    }

    idirectory::remove_entry_disposition
    directory::remove_entry(remove_entry_flags flags, file_path const& name)
    {
        return m_directory->remove_entry(flags, m_redirector->map_public_to_private(name));
    }

    idirectory::delete_tree_disposition
    directory::delete_tree(delete_tree_flags flags, std::optional<file_path> const& name)
    {
        std::optional<file_path> mapped;
        if (name.has_value())
            mapped = m_redirector->map_public_to_private(*name);
        return m_directory->delete_tree(flags, mapped);
    }

    idirectory::rename_entry_disposition
    directory::rename_entry(rename_entry_flags flags,
                            file_path const&   old_path,
                            file_path const&   new_path)
    {
        return m_directory->rename_entry(flags,
                                         m_redirector->map_public_to_private(old_path),
                                         m_redirector->map_public_to_private(new_path));
    }

    idirectory::enumerate_entries_disposition
    directory::enumerate_entries(enumerate_entries_flags                          flags,
                                 std::size_t                                      starting_index,
                                 std::span<directory_entry, std::dynamic_extent>& entries)
    {
        // Enumerated entries are single leaf names, not full paths, so there is
        // nothing to reverse-map: they pass through with their original case.
        return m_directory->enumerate_entries(flags, starting_index, entries);
    }

    idirectory::query_information_disposition
    directory::query_information(query_information_flags flags, file_metadata& metadata)
    {
        return m_directory->query_information(flags, metadata);
    }

    //
    // filesystem
    //

    filesystem::filesystem(std::shared_ptr<ifilesystem> const&   underlying_filesystem,
                           std::shared_ptr<fs_redirector> const& redir):
        m_filesystem(underlying_filesystem), m_redirector(redir)
    {}

    ifilesystem::open_root_disposition
    filesystem::open_root(open_root_flags              flags,
                          file_root const&             root,
                          file_access                  access,
                          std::shared_ptr<idirectory>& returned_directory)
    {
        std::shared_ptr<idirectory> unwrapped;
        auto const                  d = m_filesystem->open_root(flags, root, access, unwrapped);
        if (unwrapped)
            returned_directory = std::make_shared<directory>(unwrapped, m_redirector);
        return d;
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

        auto underlying_monitor = m_filesystem->monitor();

        m_monitor =
            std::make_shared<filesystem_monitor>(std::move(underlying_monitor), m_redirector);
    }

} // namespace m::pil::impl::redirecting
