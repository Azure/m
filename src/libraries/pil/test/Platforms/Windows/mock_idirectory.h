// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/utility/exception.h>

namespace m::pil::test
{
    // A minimal, fully controllable mock of m::pil::idirectory for
    // deterministically exercising the buffered filesystem layer's best-effort
    // whole-node capture (M-FS-BUF-1, D2-D4). It is the filesystem analogue of
    // mock_ikey: the buffered capture touches only two read paths on its
    // underlying directory — query_information (the last_write_time bracket) and
    // enumerate_entries — so those are the only methods that do real work; every
    // other idirectory method throws, since capture never calls it.
    //
    // A test scripts:
    //
    //   * a sequence of last_write_time values returned by successive
    //     query_information calls, so the capture's before/after bracket can be
    //     made to observe a "torn read" (a stamp that changed across the
    //     bracket) and then stabilize; and
    //   * a per-pass snapshot of the child entries returned by enumerate_entries,
    //     so an entry present in one capture pass can be made to vanish in the
    //     stabilized re-read (modelling an entry that disappeared from the
    //     underlying directory between enumeration and the consistent re-read).
    //     Because a child's metadata arrives whole with the enumeration (D14,
    //     no separate per-entry load), a vanished entry is simply absent from
    //     the final captured set.
    //
    // It records how many capture passes occurred (one enumeration sweep, i.e.
    // one enumerate_entries call at starting_index 0, per attempt) so a test can
    // assert the bounded retry fired the expected number of times.
    class mock_idirectory : public idirectory
    {
    public:
        // last_write_times is the scripted sequence returned by successive
        // query_information calls. When exhausted, the last element is returned
        // for all further calls. An empty sequence yields a fixed min stamp.
        //
        // enumeration_passes is the per-pass child snapshot: the entries
        // enumerate_entries reports during pass N. When exhausted, the last
        // snapshot is reused for all further passes. An empty outer vector
        // yields an empty directory.
        mock_idirectory(std::vector<time_point_type>             last_write_times,
                        std::vector<std::vector<directory_entry>> enumeration_passes):
            m_last_write_times(std::move(last_write_times)),
            m_enumeration_passes(std::move(enumeration_passes))
        {}

        // Number of whole-node capture passes the buffered layer ran against
        // this mock (one per enumeration sweep). Equals the number of capture
        // attempts, so a value > 1 proves a torn-read retry fired.
        unsigned
        capture_pass_count() const noexcept
        {
            return m_capture_passes;
        }

        // --- read paths exercised by capture ---

        idirectory::query_information_disposition
        query_information(query_information_flags, file_metadata& metadata) override
        {
            metadata            = file_metadata{};
            metadata.m_kind     = node_kind::directory;
            metadata.m_attributes = file_attributes::directory;

            if (m_last_write_times.empty())
            {
                metadata.m_last_write_time = (time_point_type::min)();
            }
            else
            {
                auto const i = (m_lwt_index < m_last_write_times.size())
                                   ? m_lwt_index
                                   : (m_last_write_times.size() - 1);
                metadata.m_last_write_time = m_last_write_times[i];
                ++m_lwt_index;
            }

            return query_information_disposition{};
        }

        idirectory::enumerate_entries_disposition
        enumerate_entries(enumerate_entries_flags,
                          std::size_t                                      starting_index,
                          std::span<directory_entry, std::dynamic_extent>& entries) override
        {
            // A new enumeration sweep (one capture pass) starts at index 0.
            if (starting_index == 0)
                ++m_capture_passes;

            auto const& snapshot = current_pass_snapshot();

            std::size_t written{};
            while (written < entries.size() && (starting_index + written) < snapshot.size())
            {
                entries[written] = snapshot[starting_index + written];
                ++written;
            }
            entries = entries.subspan(0, written);
            return enumerate_entries_disposition{};
        }

        // --- paths capture never touches ---

        idirectory::create_directory_disposition
        create_directory(create_directory_flags,
                         file_path const&,
                         file_access,
                         std::shared_ptr<idirectory>&) override
        {
            throw m::not_supported("mock_idirectory::create_directory");
        }

        idirectory::create_file_disposition
        create_file(create_file_flags,
                    file_path const&,
                    file_access,
                    std::shared_ptr<ifile>&) override
        {
            throw m::not_supported("mock_idirectory::create_file");
        }

        idirectory::open_directory_disposition
        open_directory(open_directory_flags,
                       file_path const&,
                       file_access,
                       std::shared_ptr<idirectory>&,
                       std::error_code&) override
        {
            throw m::not_supported("mock_idirectory::open_directory");
        }

        idirectory::open_file_disposition
        open_file(open_file_flags,
                  file_path const&,
                  file_access,
                  std::shared_ptr<ifile>&,
                  std::error_code&) override
        {
            throw m::not_supported("mock_idirectory::open_file");
        }

        idirectory::remove_entry_disposition
        remove_entry(remove_entry_flags, file_path const&) override
        {
            throw m::not_supported("mock_idirectory::remove_entry");
        }

        idirectory::delete_tree_disposition
        delete_tree(delete_tree_flags, std::optional<file_path> const&) override
        {
            throw m::not_supported("mock_idirectory::delete_tree");
        }

        idirectory::rename_entry_disposition
        rename_entry(rename_entry_flags, file_path const&, file_path const&) override
        {
            throw m::not_supported("mock_idirectory::rename_entry");
        }

    private:
        std::vector<directory_entry> const&
        current_pass_snapshot() const
        {
            static std::vector<directory_entry> const s_empty;

            if (m_enumeration_passes.empty())
                return s_empty;

            // The current pass index is one less than the number of sweeps begun
            // (capture passes are 1-based once a sweep has started). Clamp to the
            // last scripted snapshot when the sequence is exhausted.
            auto const pass = (m_capture_passes == 0) ? 0u : (m_capture_passes - 1);
            auto const idx  = std::min<std::size_t>(pass, m_enumeration_passes.size() - 1);
            return m_enumeration_passes[idx];
        }

        std::vector<time_point_type>              m_last_write_times;
        std::vector<std::vector<directory_entry>> m_enumeration_passes;
        std::size_t                               m_lwt_index{0};
        unsigned                                  m_capture_passes{0};
    };

} // namespace m::pil::test
