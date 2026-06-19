// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/utility/enum_operations.h>

#include "buffered.h"

namespace m::pil::impl::buffered
{
    //
    // construction / whole-node capture
    //

    directory::directory(file_metadata const& metadata, std::nullptr_t): m_metadata(metadata) {}

    directory::directory(std::shared_ptr<idirectory> const& underlying_directory):
        m_underlying_directory(underlying_directory)
    {
        initialize_overlay();
    }

    file_metadata
    directory::query_underlying_metadata() const
    {
        file_metadata md;
        auto const    d =
            m_underlying_directory->query_information(idirectory::query_information_flags{}, md);
        M_INTERNAL_ERROR_CHECK(!d);
        return md;
    }

    void
    directory::initialize_children_overlay()
    {
        if (!m_underlying_directory)
            return;

        static constexpr std::size_t k_enumeration_batch = 32;

        std::array<directory_entry, k_enumeration_batch> entry_array;
        std::size_t                                      index{};

        for (;;)
        {
            std::span<directory_entry, std::dynamic_extent> entry_span{entry_array};

            auto const d = m_underlying_directory->enumerate_entries(
                enumerate_entries_flags{}, index, entry_span);
            M_INTERNAL_ERROR_CHECK(!d); // no flags in, no disposition out

            for (auto& e: entry_span)
                m_entries.emplace(e.m_name,
                                  entry_node{.m_kind      = e.m_kind,
                                             .m_directory = {},
                                             .m_file      = {},
                                             .m_metadata  = e.m_metadata,
                                             .m_deleted   = false,
                                             .m_mirrored  = true,
                                             .m_short_name = std::move(e.m_short_name)});

            if (entry_span.size() != entry_array.size())
                break;

            index += entry_array.size();
        }
    }

    void
    directory::initialize_overlay()
    {
        // Capture the whole directory at materialization ("touch"): mirror its
        // child-entry names/kinds/metadata and snapshot its own metadata so the
        // overlay becomes a self-contained snapshot of the directory (D2-D4).
        //
        // Best-effort consistency: the filesystem can change underneath us with
        // no synchronization, so we bracket the capture with last_write_time
        // reads and re-capture if the directory changed during enumeration. A
        // bounded retry keeps this from spinning; this is best-effort, not
        // transactional (D4). Non-recursive: children are mirrored placeholders,
        // each captured whole on its own first touch.
        if (!m_underlying_directory)
            return;

        static constexpr unsigned k_max_capture_attempts = 3;

        for (unsigned attempt = 1;; ++attempt)
        {
            m_entries.clear();

            auto const before = query_underlying_metadata();

            initialize_children_overlay();

            auto const after = query_underlying_metadata();

            m_metadata = after;

            if (before.m_last_write_time == after.m_last_write_time ||
                attempt >= k_max_capture_attempts)
                break;
        }
    }

    //
    // materialization of mirrored placeholders
    //

    std::shared_ptr<directory>
    directory::materialize_subdirectory(m::u16sstring const& name,
                                        entry_node&          node,
                                        file_access          access)
    {
        // Caller holds m_mutex.
        if (node.m_directory)
            return node.m_directory;

        if (m_underlying_directory)
        {
            auto underlying_child =
                m_underlying_directory->open_directory(file_path(name.view()), access);
            node.m_directory = std::make_shared<directory>(underlying_child);
            node.m_mirrored  = false;
            return node.m_directory;
        }

        // Sealed snapshot: a mirrored placeholder has no underlying to
        // materialize from. The node is reported absent here; the caller drops
        // it and restamps this directory as lazy consistency repair (D5).
        return nullptr;
    }

    std::shared_ptr<file>
    directory::materialize_file(m::u16sstring const& name, entry_node& node, file_access access)
    {
        // Caller holds m_mutex.
        if (node.m_file)
            return node.m_file;

        // D16/D17 content read-through: a mirrored (unmodified backing) file
        // over a live underlying directory retains the live backing handle so
        // whole-file content reads resolve to the real bytes. A sealed snapshot
        // (no underlying) or a created / renamed node has no backing to read
        // through and stays metadata-only (read_content reports not_supported).
        // Writes are never forwarded, so the backing is never mutated and the
        // overlay's namespace state remains the only mutated state (isolation).
        std::shared_ptr<ifile> underlying;
        if (node.m_mirrored && m_underlying_directory)
        {
            std::error_code ec;
            m_underlying_directory->open_file(
                open_file_flags::tolerate_not_found, file_path(name.view()), access, underlying, ec);
            if (ec)
                underlying.reset();
        }

        node.m_file     = underlying
                              ? std::make_shared<file>(node.m_metadata, std::move(underlying))
                              : std::make_shared<file>(node.m_metadata);
        node.m_mirrored = false;
        return node.m_file;
    }

    //
    // reads
    //

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
            throw m::invalid_parameter("idirectory::open_directory.flags");

        // Walk a multi-segment path one level at a time through the overlay so
        // every intermediate directory is itself captured. A missing
        // intermediate makes the whole target not found.
        if (path.has_parent_path())
        {
            auto [parent_opt, leaf] = path.split_parent_path_and_leaf_name();
            M_INTERNAL_ERROR_CHECK(parent_opt.has_value());

            std::shared_ptr<idirectory> parent_directory;
            open_directory(open_directory_flags::tolerate_not_found,
                           parent_opt.value(),
                           access,
                           parent_directory,
                           ec);
            if (ec)
                return open_directory_disposition{};

            if (!parent_directory)
            {
                if (!!(flags & open_directory_flags::tolerate_not_found))
                    return open_directory_disposition{open_directory_result_code::not_found};
                ec = std::make_error_code(std::errc::no_such_file_or_directory);
                return open_directory_disposition{};
            }

            return parent_directory->open_directory(flags, leaf, access, returned_directory, ec);
        }

        auto lock = std::unique_lock(m_mutex);

        auto it = m_entries.find(path.native());
        if (it == m_entries.end() || it->second.m_deleted)
        {
            // Exact (case-insensitive) match on the long name missed. A host
            // path may address a child by its alternate (8.3 short) name (D17);
            // resolve that by scanning for a non-deleted entry whose captured
            // short name matches the requested leaf. This also serves sealed
            // snapshots, whose short names were restored from the persisted
            // alias and which have no live underlying to consult.
            auto const& less = m_entries.key_comp();
            auto const& wanted = path.native();
            for (auto cand = m_entries.begin(); cand != m_entries.end(); ++cand)
            {
                if (cand->second.m_deleted || cand->second.m_short_name.empty())
                    continue;
                if (!less(cand->second.m_short_name, wanted)
                    && !less(wanted, cand->second.m_short_name))
                {
                    it = cand;
                    break;
                }
            }
        }

        if (it == m_entries.end() || it->second.m_deleted)
        {
            if (!!(flags & open_directory_flags::tolerate_not_found))
                return open_directory_disposition{open_directory_result_code::not_found};
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return open_directory_disposition{};
        }

        if (it->second.m_kind != node_kind::directory)
        {
            // Unified namespace (D13): opening a file through open_directory is
            // rejected, regardless of the tentative-open flag.
            ec = std::make_error_code(std::errc::not_a_directory);
            return open_directory_disposition{};
        }

        auto materialized = materialize_subdirectory(it->first, it->second, access);
        if (!materialized)
        {
            // D5 lazy consistency repair: a sealed snapshot enumerates this
            // subdirectory by name but its contents were never captured and
            // there is no underlying provider to consult. Drop it from the
            // namespace and advance this directory's version stamp to T_load so
            // the snapshot stays self-consistent.
            m_entries.erase(it);
            m_metadata.m_last_write_time = m_load_stamp;

            if (!!(flags & open_directory_flags::tolerate_not_found))
                return open_directory_disposition{open_directory_result_code::not_found};
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return open_directory_disposition{};
        }

        returned_directory = std::move(materialized);
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
            throw m::invalid_parameter("idirectory::open_file.flags");

        // A multi-segment path resolves its parent directory through the overlay
        // (capturing each level), then opens the leaf file in that parent.
        if (path.has_parent_path())
        {
            auto [parent_opt, leaf] = path.split_parent_path_and_leaf_name();
            M_INTERNAL_ERROR_CHECK(parent_opt.has_value());

            std::shared_ptr<idirectory> parent_directory;
            open_directory(open_directory_flags::tolerate_not_found,
                           parent_opt.value(),
                           access,
                           parent_directory,
                           ec);
            if (ec)
                return open_file_disposition{};

            if (!parent_directory)
            {
                if (!!(flags & open_file_flags::tolerate_not_found))
                    return open_file_disposition{open_file_result_code::not_found};
                ec = std::make_error_code(std::errc::no_such_file_or_directory);
                return open_file_disposition{};
            }

            return parent_directory->open_file(flags, leaf, access, returned_file, ec);
        }

        auto lock = std::unique_lock(m_mutex);

        auto const it = m_entries.find(path.native());
        if (it == m_entries.end() || it->second.m_deleted)
        {
            if (!!(flags & open_file_flags::tolerate_not_found))
                return open_file_disposition{open_file_result_code::not_found};
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return open_file_disposition{};
        }

        if (it->second.m_kind != node_kind::file)
        {
            // Unified namespace (D13): opening a directory through open_file is
            // rejected.
            ec = std::make_error_code(std::errc::is_a_directory);
            return open_file_disposition{};
        }

        returned_file = materialize_file(it->first, it->second, access);
        return open_file_disposition{};
    }

    idirectory::enumerate_entries_disposition
    directory::enumerate_entries(enumerate_entries_flags                          flags,
                                 std::size_t                                      starting_index,
                                 std::span<directory_entry, std::dynamic_extent>& entries)
    {
        if (flags != enumerate_entries_flags{})
            throw m::invalid_parameter("idirectory::enumerate_entries.flags");

        auto lock = std::unique_lock(m_mutex);

        // The visible namespace is every non-deleted entry, in the overlay's
        // sorted (ordinal case-insensitive, D12) order. Tombstones are skipped.
        std::size_t produced{};
        std::size_t position{};

        for (auto const& [name, node]: m_entries)
        {
            if (node.m_deleted)
                continue;

            if (position >= starting_index)
            {
                if (produced >= entries.size())
                    break;

                entries[produced] = directory_entry(name, node.m_metadata);
                ++produced;
            }

            ++position;
        }

        entries = entries.subspan(0, produced);
        return enumerate_entries_disposition{};
    }

    idirectory::query_information_disposition
    directory::query_information(query_information_flags flags, file_metadata& metadata)
    {
        if (flags != query_information_flags{})
            throw m::invalid_parameter("idirectory::query_information.flags");

        auto lock = std::unique_lock(m_mutex);

        metadata = m_metadata;
        return query_information_disposition{};
    }

} // namespace m::pil::impl::buffered
