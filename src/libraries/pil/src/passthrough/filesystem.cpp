// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>

#include "passthrough.h"

namespace m::pil::impl::passthrough
{
    //
    // file
    //

    file::file(std::shared_ptr<ifile> const& underlying_file): m_file(underlying_file) {}

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
        return m_file->read_content(flags, offset, buffer, bytes_read, ec);
    }

    ifile::write_content_disposition
    file::write_content(write_content_flags        flags,
                        std::uint64_t              offset,
                        std::span<std::byte const> buffer,
                        std::size_t&               bytes_written,
                        std::error_code&           ec)
    {
        return m_file->write_content(flags, offset, buffer, bytes_written, ec);
    }

    ifile::enumerate_streams_disposition
    file::enumerate_streams(enumerate_streams_flags                       flags,
                            std::size_t                                   starting_index,
                            std::span<stream_entry, std::dynamic_extent>& entries,
                            std::error_code&                              ec)
    {
        return m_file->enumerate_streams(flags, starting_index, entries, ec);
    }

    //
    // directory
    //

    directory::directory(std::shared_ptr<idirectory> const& underlying_directory):
        m_directory(underlying_directory)
    {}

    idirectory::create_directory_disposition
    directory::create_directory(create_directory_flags       flags,
                                file_path const&             path,
                                file_access                  access,
                                std::shared_ptr<idirectory>& returned_directory)
    {
        std::shared_ptr<idirectory> unwrapped;
        auto const                  d = m_directory->create_directory(flags, path, access, unwrapped);
        if (unwrapped)
            returned_directory = std::make_shared<directory>(unwrapped);
        return d;
    }

    idirectory::create_file_disposition
    directory::create_file(create_file_flags       flags,
                           file_path const&        path,
                           file_access             access,
                           std::shared_ptr<ifile>& returned_file)
    {
        std::shared_ptr<ifile> unwrapped;
        auto const             d = m_directory->create_file(flags, path, access, unwrapped);
        if (unwrapped)
            returned_file = std::make_shared<file>(unwrapped);
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
        auto const d = m_directory->open_directory(flags, path, access, unwrapped, ec);
        if (unwrapped)
            returned_directory = std::make_shared<directory>(unwrapped);
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
        auto const             d = m_directory->open_file(flags, path, access, unwrapped, ec);
        if (unwrapped)
            returned_file = std::make_shared<file>(unwrapped);
        return d;
    }

    idirectory::remove_entry_disposition
    directory::remove_entry(remove_entry_flags flags, file_path const& name)
    {
        return m_directory->remove_entry(flags, name);
    }

    idirectory::delete_tree_disposition
    directory::delete_tree(delete_tree_flags flags, std::optional<file_path> const& name)
    {
        return m_directory->delete_tree(flags, name);
    }

    idirectory::rename_entry_disposition
    directory::rename_entry(rename_entry_flags flags,
                            file_path const&   old_path,
                            file_path const&   new_path)
    {
        return m_directory->rename_entry(flags, old_path, new_path);
    }

    idirectory::enumerate_entries_disposition
    directory::enumerate_entries(enumerate_entries_flags                          flags,
                                 std::size_t                                      starting_index,
                                 std::span<directory_entry, std::dynamic_extent>& entries)
    {
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

    filesystem::filesystem(std::shared_ptr<ifilesystem> const& underlying_filesystem):
        m_filesystem(underlying_filesystem)
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
            returned_directory = std::make_shared<directory>(unwrapped);
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

        m_monitor = std::make_shared<filesystem_monitor>(m_filesystem->monitor());
    }

} // namespace m::pil::impl::passthrough
