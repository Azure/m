// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <system_error>

#include <m/error_handling/macros.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>

#include "buffered.h"

namespace m::pil::impl::buffered
{
    //
    // file
    //

    file::file(file_metadata const& metadata): m_metadata(metadata) {}

    file::file(file_metadata const& metadata, std::shared_ptr<ifile> underlying):
        m_metadata(metadata), m_underlying(std::move(underlying))
    {}

    ifile::query_information_disposition
    file::query_information(query_information_flags flags, file_metadata& metadata)
    {
        if (flags != query_information_flags{})
            throw m::invalid_parameter("ifile::query_information.flags");

        metadata = m_metadata;
        return query_information_disposition{};
    }

    ifile::read_content_disposition
    file::read_content(read_content_flags   flags,
                       std::uint64_t        offset,
                       std::span<std::byte> buffer,
                       std::size_t&         bytes_read,
                       std::error_code&     ec)
    {
        // D16/D17: a mirrored node retains the live backing handle and serves
        // real bytes; a node with no backing (sealed snapshot, created or
        // renamed entry) models no content. Writes are never forwarded, so the
        // backing directory is never mutated through the overlay.
        if (m_underlying)
            return m_underlying->read_content(flags, offset, buffer, bytes_read, ec);

        bytes_read = 0;
        ec         = std::make_error_code(std::errc::not_supported);
        return read_content_disposition{};
    }

    ifile::enumerate_streams_disposition
    file::enumerate_streams(enumerate_streams_flags                       flags,
                            std::size_t                                   starting_index,
                            std::span<stream_entry, std::dynamic_extent>& entries,
                            std::error_code&                              ec)
    {
        // Stream enumeration: a mirrored node forwards to the backing handle;
        // a sealed snapshot or created node reports not_supported.
        if (m_underlying)
            return m_underlying->enumerate_streams(flags, starting_index, entries, ec);

        entries = {};
        ec      = std::make_error_code(std::errc::not_supported);
        return enumerate_streams_disposition{};
    }

    //
    // filesystem
    //

    filesystem::filesystem(std::shared_ptr<ifilesystem> const& underlying_filesystem):
        m_underlying_filesystem(underlying_filesystem)
    {}

    ifilesystem::open_root_disposition
    filesystem::open_root(open_root_flags              flags,
                          file_root const&             root,
                          file_access                  access,
                          std::shared_ptr<idirectory>& returned_directory)
    {
        returned_directory.reset();

        if (flags != open_root_flags{})
            throw m::invalid_parameter("ifilesystem::open_root.flags");

        auto lock = std::unique_lock(m_mutex);

        auto const find_location = m_roots.find(root);
        if (find_location != m_roots.end())
        {
            returned_directory = find_location->second;
            return open_root_disposition{};
        }

        // Open the root through the underlying filesystem and capture it whole.
        // A snapshot filesystem (no underlying) serves only roots restored from
        // the persisted file; an unknown root in that mode is not found.
        if (!m_underlying_filesystem)
            throw m::not_found("ifilesystem::open_root: root not present in snapshot");

        auto underlying_root = m_underlying_filesystem->open_root(root, access);

        auto captured = std::make_shared<directory>(underlying_root);

        auto const [insertion_location, inserted] = m_roots.emplace(root, std::move(captured));
        M_INTERNAL_ERROR_CHECK(inserted);

        returned_directory = insertion_location->second;
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

        // A snapshot filesystem (no underlying) has no live changes to report,
        // so it still hands out a buffered monitor; register_watch on it raises
        // "not implemented" exactly as the live case does (sealed snapshots do
        // not observe change).
        std::shared_ptr<ifilesystem_monitor> underlying_monitor;
        if (m_underlying_filesystem)
            underlying_monitor = m_underlying_filesystem->monitor();

        m_monitor = std::make_shared<filesystem_monitor>(std::move(underlying_monitor));
    }

} // namespace m::pil::impl::buffered
