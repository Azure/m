// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

#include <m/error_handling/macros.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>

#include "journaling.h"

namespace m::pil::impl::journaling
{
    //
    // directory: mutations are recorded into the journal (carrying this
    // directory's absolute path so replay can navigate back) and then
    // forwarded. Returned directory nodes are re-wrapped with their own
    // absolute path. Reads forward unchanged; files carry no journaled verbs so
    // they are forwarded unwrapped.
    //

    directory::directory(std::shared_ptr<idirectory> const& underlying_directory,
                         std::shared_ptr<journal> const&    journal_ptr,
                         file_path                          absolute_path):
        m_directory(underlying_directory),
        m_journal(journal_ptr),
        m_absolute_path(std::move(absolute_path))
    {
        M_INTERNAL_ERROR_CHECK(m_directory.get() != nullptr);
        M_INTERNAL_ERROR_CHECK(m_journal.get() != nullptr);
    }

    idirectory::create_directory_disposition
    directory::create_directory(create_directory_flags       flags,
                                file_path const&             path,
                                file_access                  access,
                                std::shared_ptr<idirectory>& returned_directory)
    {
        std::shared_ptr<idirectory> unwrapped;
        auto const d = m_directory->create_directory(flags, path, access, unwrapped);
        if (unwrapped)
            returned_directory =
                std::make_shared<directory>(unwrapped, m_journal, m_absolute_path / path);

        auto entry = std::make_unique<fs_create_directory_entry>(m_absolute_path, path);
        m_journal->add(entry);
        return d;
    }

    idirectory::create_file_disposition
    directory::create_file(create_file_flags       flags,
                           file_path const&        path,
                           file_access             access,
                           std::shared_ptr<ifile>& returned_file)
    {
        auto const d = m_directory->create_file(flags, path, access, returned_file);

        auto entry = std::make_unique<fs_create_file_entry>(m_absolute_path, path);
        m_journal->add(entry);
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
            returned_directory =
                std::make_shared<directory>(unwrapped, m_journal, m_absolute_path / path);
        return d;
    }

    idirectory::open_file_disposition
    directory::open_file(open_file_flags         flags,
                         file_path const&        path,
                         file_access             access,
                         std::shared_ptr<ifile>& returned_file,
                         std::error_code&        ec)
    {
        return m_directory->open_file(flags, path, access, returned_file, ec);
    }

    idirectory::remove_entry_disposition
    directory::remove_entry(remove_entry_flags flags, file_path const& name)
    {
        auto const d = m_directory->remove_entry(flags, name);

        auto entry = std::make_unique<fs_remove_entry>(m_absolute_path, name);
        m_journal->add(entry);
        return d;
    }

    idirectory::delete_tree_disposition
    directory::delete_tree(delete_tree_flags flags, std::optional<file_path> const& name)
    {
        auto const d = m_directory->delete_tree(flags, name);

        auto entry = std::make_unique<fs_delete_tree_entry>(m_absolute_path, name);
        m_journal->add(entry);
        return d;
    }

    idirectory::rename_entry_disposition
    directory::rename_entry(rename_entry_flags flags,
                            file_path const&   old_path,
                            file_path const&   new_path)
    {
        auto const d = m_directory->rename_entry(flags, old_path, new_path);

        auto entry = std::make_unique<fs_rename_entry>(m_absolute_path, old_path, new_path);
        m_journal->add(entry);
        return d;
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
    // filesystem: open_root is a read; forward and wrap the returned root with
    // its absolute path (the root text) so descendants track an accurate path.
    //

    filesystem::filesystem(std::shared_ptr<ifilesystem> const& underlying_filesystem,
                           std::shared_ptr<journal> const&     journal_ptr):
        m_filesystem(underlying_filesystem), m_journal(journal_ptr)
    {
        M_INTERNAL_ERROR_CHECK(m_filesystem.get() != nullptr);
        M_INTERNAL_ERROR_CHECK(m_journal.get() != nullptr);
    }

    ifilesystem::open_root_disposition
    filesystem::open_root(open_root_flags              flags,
                          file_root const&             root,
                          file_access                  access,
                          std::shared_ptr<idirectory>& returned_directory)
    {
        std::shared_ptr<idirectory> unwrapped;
        auto const d = m_filesystem->open_root(flags, root, access, unwrapped);
        if (unwrapped)
            returned_directory =
                std::make_shared<directory>(unwrapped, m_journal, file_path(root.text()));
        return d;
    }

    ifilesystem::monitor_disposition
    filesystem::monitor(monitor_flags                                 flags,
                        std::shared_ptr<m::pil::ifilesystem_monitor>& returned_filesystem_monitor)
    {
        // Monitoring is a read-side capability and is not journaled; forward the
        // underlying monitor directly.
        return m_filesystem->monitor(flags, returned_filesystem_monitor);
    }

} // namespace m::pil::impl::journaling
