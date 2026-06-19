// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

#include <m/chrono/chrono.h>
#include <m/error_handling/macros.h>
#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/utility/enum_operations.h>
#include <m/utility/error_macros.h>
#include <m/utility/pointers.h>

//
// Interface (provider) layer for the filesystem isolation surface. This mirrors
// the registry interface layer (registry_interfaces.h) exactly: each verb has a
// flags enum, a result-code enum, a result-flags enum, a `disposition` alias,
// a pure-virtual primitive that providers implement, and inline throwing
// wrappers for the common callers. Operations whose absence-of-target is an
// expected (non-error) outcome additionally expose ec-form primitives plus a
// `tolerate_not_found` tentative form.
//
// Three interfaces compose the surface:
//   - ifilesystem  : the entry point; opens a root (D10) as a directory.
//                    Analogue of iregistry::open_predefined_key.
//   - idirectory   : a directory node; the unified-namespace (D13) container.
//   - ifile        : a file node; metadata only for now (content deferred, D14).
//

namespace m::pil
{
    //
    // A file node. In the unified namespace (D13) a file is a leaf: it carries
    // metadata and, since M-FS-STREAMS tier 1, redirection-backed byte content
    // (D16, D17) reachable through read_content / write_content.
    //
    struct ifile
    {
        virtual ~ifile() = default;

        //
        //  query_information
        //

        enum class query_information_flags : uint64_t
        {
        };

        enum class query_information_result_code : uint32_t
        {
        };

        enum class query_information_result_flags : uint32_t
        {
        };

        using query_information_disposition =
            disposition<query_information_result_code, query_information_result_flags>;

        virtual query_information_disposition
        query_information(query_information_flags flags, file_metadata& metadata) = 0;

        file_metadata
        query_information()
        {
            file_metadata metadata;
            auto const    d = query_information(query_information_flags{}, metadata);
            M_INTERNAL_ERROR_CHECK(!d);
            return metadata;
        }

        //
        //  read_content (D17)
        //
        //  Reads up to buffer.size() bytes of this file's content beginning at
        //  byte offset `offset`, returning the count actually read in
        //  `bytes_read`. A short read (including zero) signals end-of-file. Hard
        //  errors are reported through `ec`.
        //
        //  Content is the redirection-backed (D16) byte stream of the node:
        //  providers backed by a real file (direct, and the decorators over it)
        //  serve it whole-file and natural. Providers that model only the
        //  namespace + metadata (a sealed buffered snapshot, the null leaf)
        //  cannot serve content; the defaulted implementation below reports
        //  std::errc::not_supported through `ec` — the documented
        //  "deferred-content" outcome (D14/D16).
        //

        enum class read_content_flags : uint64_t
        {
        };

        enum class read_content_result_code : uint32_t
        {
        };

        enum class read_content_result_flags : uint32_t
        {
        };

        using read_content_disposition =
            disposition<read_content_result_code, read_content_result_flags>;

        //
        // Primitive: providers that model byte content override this. The
        // default reports "content not modeled" through `ec` so that nodes which
        // carry only namespace + metadata (and unrelated mocks / test doubles)
        // need no edit (mirrors the get_filesystem / get_webcore defaulting,
        // D9 / D-HWC-2).
        //
        virtual read_content_disposition
        read_content(read_content_flags   flags,
                     std::uint64_t        offset,
                     std::span<std::byte> buffer,
                     std::size_t&         bytes_read,
                     std::error_code&     ec)
        {
            (void)flags;
            (void)offset;
            (void)buffer;
            bytes_read = 0;
            ec         = std::make_error_code(std::errc::not_supported);
            return read_content_disposition{};
        }

        //
        // Throwing wrapper over the ec-form primitive.
        //
        read_content_disposition
        read_content(read_content_flags   flags,
                     std::uint64_t        offset,
                     std::span<std::byte> buffer,
                     std::size_t&         bytes_read)
        {
            std::error_code ec;
            auto const      d = read_content(flags, offset, buffer, bytes_read, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        // Convenience: read up to buffer.size() bytes at `offset`, returning the
        // count actually read (a short count signals end-of-file).
        //
        std::size_t
        read_content(std::uint64_t offset, std::span<std::byte> buffer)
        {
            std::size_t bytes_read = 0;
            read_content(read_content_flags{}, offset, buffer, bytes_read);
            return bytes_read;
        }

        //
        //  write_content (D17)
        //
        //  Whole-file replacement of the node's redirection-backed (D16) byte
        //  content. A write at offset 0 sets the file's extent to the bytes
        //  supplied (truncating any trailing remainder). A non-zero offset — a
        //  partial / mid-file overwrite — is *not* modeled and is rejected with
        //  std::errc::not_supported (D16). As with read_content the default
        //  reports std::errc::not_supported so that namespace-only nodes (a
        //  sealed buffered snapshot, the null leaf) need no edit.
        //

        enum class write_content_flags : uint64_t
        {
        };

        enum class write_content_result_code : uint32_t
        {
        };

        enum class write_content_result_flags : uint32_t
        {
        };

        using write_content_disposition =
            disposition<write_content_result_code, write_content_result_flags>;

        //
        // Primitive: providers that model byte content override this. The
        // default reports "content not modeled" through `ec`.
        //
        virtual write_content_disposition
        write_content(write_content_flags        flags,
                      std::uint64_t              offset,
                      std::span<std::byte const> buffer,
                      std::size_t&               bytes_written,
                      std::error_code&           ec)
        {
            (void)flags;
            (void)offset;
            (void)buffer;
            bytes_written = 0;
            ec            = std::make_error_code(std::errc::not_supported);
            return write_content_disposition{};
        }

        //
        // Throwing wrapper over the ec-form primitive.
        //
        write_content_disposition
        write_content(write_content_flags        flags,
                      std::uint64_t              offset,
                      std::span<std::byte const> buffer,
                      std::size_t&               bytes_written)
        {
            std::error_code ec;
            auto const      d = write_content(flags, offset, buffer, bytes_written, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        // Convenience: whole-file replacement at `offset` 0, returning the count
        // of bytes written.
        //
        std::size_t
        write_content(std::uint64_t offset, std::span<std::byte const> buffer)
        {
            std::size_t bytes_written = 0;
            write_content(write_content_flags{}, offset, buffer, bytes_written);
            return bytes_written;
        }

        //
        //  enumerate_streams (M-FS-STREAMS-2)
        //
        //  Enumerates, one by one, the alternate data streams (ADS) of this file.
        //  The primary (unnamed) stream is always present; named streams are
        //  additional. On entry, the span identifies the buffer of entries to fill
        //  starting at `starting_index`. On return, the span is shrunk to the
        //  number of entries actually produced; an empty span signals the end of
        //  the list.
        //
        //  The default implementation reports std::errc::not_supported so that
        //  namespace-only nodes (a sealed buffered snapshot, the null leaf) need
        //  no edit.
        //

        enum class enumerate_streams_flags : uint64_t
        {
        };

        enum class enumerate_streams_result_code : uint32_t
        {
        };

        enum class enumerate_streams_result_flags : uint32_t
        {
        };

        using enumerate_streams_disposition =
            disposition<enumerate_streams_result_code, enumerate_streams_result_flags>;

        virtual enumerate_streams_disposition
        enumerate_streams(enumerate_streams_flags                     flags,
                          std::size_t                                 starting_index,
                          std::span<stream_entry, std::dynamic_extent>& entries,
                          std::error_code&                            ec)
        {
            (void)flags;
            (void)starting_index;
            entries = {};
            ec      = std::make_error_code(std::errc::not_supported);
            return enumerate_streams_disposition{};
        }

        enumerate_streams_disposition
        enumerate_streams(enumerate_streams_flags                       flags,
                          std::size_t                                   starting_index,
                          std::span<stream_entry, std::dynamic_extent>& entries)
        {
            std::error_code ec;
            auto const      d = enumerate_streams(flags, starting_index, entries, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        std::optional<stream_entry>
        enumerate_streams(std::size_t index)
        {
            stream_entry entry;
            auto         s = std::span<stream_entry, std::dynamic_extent>(&entry, 1);

            auto const d = enumerate_streams(enumerate_streams_flags{}, index, s);
            M_INTERNAL_ERROR_CHECK(!d);

            if (s.size() == 0)
                return std::nullopt;

            return entry;
        }
    };

    //
    // A directory node. This is the container of the unified namespace (D13):
    // each child is exactly one node, reached by name, that is itself either a
    // directory or a file.
    //
    struct idirectory
    {
        virtual ~idirectory() = default;

        //
        //  create_directory
        //

        enum class create_directory_flags : uint64_t
        {
        };

        enum class create_directory_result_code : uint32_t
        {
        };

        enum class create_directory_result_flags : uint32_t
        {
        };

        using create_directory_disposition =
            disposition<create_directory_result_code, create_directory_result_flags>;

        virtual create_directory_disposition
        create_directory(create_directory_flags        flags,
                         file_path const&              path,
                         file_access                   access,
                         std::shared_ptr<idirectory>&  returned_directory) = 0;

        std::shared_ptr<idirectory>
        create_directory(file_path const& path, file_access access)
        {
            std::shared_ptr<idirectory> returned_directory;
            auto const d = create_directory(create_directory_flags{}, path, access, returned_directory);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_directory;
        }

        std::shared_ptr<idirectory>
        create_directory(file_path const& path)
        {
            return create_directory(path, file_access::default_create);
        }

        //
        //  create_file
        //

        enum class create_file_flags : uint64_t
        {
        };

        enum class create_file_result_code : uint32_t
        {
        };

        enum class create_file_result_flags : uint32_t
        {
        };

        using create_file_disposition =
            disposition<create_file_result_code, create_file_result_flags>;

        virtual create_file_disposition
        create_file(create_file_flags        flags,
                    file_path const&         path,
                    file_access              access,
                    std::shared_ptr<ifile>&  returned_file) = 0;

        std::shared_ptr<ifile>
        create_file(file_path const& path, file_access access)
        {
            std::shared_ptr<ifile> returned_file;
            auto const             d = create_file(create_file_flags{}, path, access, returned_file);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_file;
        }

        std::shared_ptr<ifile>
        create_file(file_path const& path)
        {
            return create_file(path, file_access::default_create);
        }

        //
        //  open_directory
        //

        enum class open_directory_flags : uint64_t
        {
            //
            // Opt-in to "tentative open" semantics: when set, asking to open a
            // directory that does not exist is not an error. Instead `ec` is
            // left clear, `returned_directory` is left null, and the returned
            // disposition's code is open_directory_result_code::not_found.
            //
            // Per the disposition opt-in gate, that code is only ever produced
            // when this flag was passed; callers that pass open_directory_flags{}
            // continue to receive a missing node through `ec`.
            //
            tolerate_not_found = 1ull << 0,
        };

        enum class open_directory_result_code : uint32_t
        {
            //
            // The requested directory did not exist. Only produced when the
            // caller passed open_directory_flags::tolerate_not_found.
            //
            not_found = 1,
        };

        enum class open_directory_result_flags : uint32_t
        {
        };

        using open_directory_disposition =
            disposition<open_directory_result_code, open_directory_result_flags>;

        //
        // Primitive: providers implement this non-throwing form. Errors are
        // reported through ec; the disposition carries only contractual
        // (non-error) outcomes. The two channels are independent.
        //
        virtual open_directory_disposition
        open_directory(open_directory_flags         flags,
                       file_path const&             path,
                       file_access                  access,
                       std::shared_ptr<idirectory>& returned_directory,
                       std::error_code&             ec) = 0;

        //
        // Throwing wrapper over the ec-form primitive.
        //
        open_directory_disposition
        open_directory(open_directory_flags         flags,
                       file_path const&             path,
                       file_access                  access,
                       std::shared_ptr<idirectory>& returned_directory)
        {
            std::error_code ec;
            auto const      d = open_directory(flags, path, access, returned_directory, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        std::shared_ptr<idirectory>
        open_directory(file_path const& path, file_access access)
        {
            std::shared_ptr<idirectory> returned_directory;
            auto const d = open_directory(open_directory_flags{}, path, access, returned_directory);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_directory;
        }

        std::shared_ptr<idirectory>
        open_directory(file_path const& path)
        {
            return open_directory(path, file_access::default_open);
        }

        //
        // Tentative open: returns the opened directory, or a null shared_ptr if
        // it does not exist. Other failures (e.g. access denied) still throw,
        // because only "not found" is opted into via tolerate_not_found.
        //
        std::shared_ptr<idirectory>
        try_open_directory(file_path const& path, file_access access)
        {
            std::shared_ptr<idirectory> returned_directory;
            open_directory(open_directory_flags::tolerate_not_found, path, access, returned_directory);
            return returned_directory;
        }

        std::shared_ptr<idirectory>
        try_open_directory(file_path const& path)
        {
            return try_open_directory(path, file_access::default_open);
        }

        //
        //  open_file
        //

        enum class open_file_flags : uint64_t
        {
            //
            // Opt-in to "tentative open" semantics for files. See the analogous
            // open_directory_flags::tolerate_not_found documentation.
            //
            tolerate_not_found = 1ull << 0,
        };

        enum class open_file_result_code : uint32_t
        {
            //
            // The requested file did not exist. Only produced when the caller
            // passed open_file_flags::tolerate_not_found.
            //
            not_found = 1,
        };

        enum class open_file_result_flags : uint32_t
        {
        };

        using open_file_disposition = disposition<open_file_result_code, open_file_result_flags>;

        virtual open_file_disposition
        open_file(open_file_flags         flags,
                  file_path const&        path,
                  file_access             access,
                  std::shared_ptr<ifile>& returned_file,
                  std::error_code&        ec) = 0;

        open_file_disposition
        open_file(open_file_flags         flags,
                  file_path const&        path,
                  file_access             access,
                  std::shared_ptr<ifile>& returned_file)
        {
            std::error_code ec;
            auto const      d = open_file(flags, path, access, returned_file, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        std::shared_ptr<ifile>
        open_file(file_path const& path, file_access access)
        {
            std::shared_ptr<ifile> returned_file;
            auto const             d = open_file(open_file_flags{}, path, access, returned_file);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_file;
        }

        std::shared_ptr<ifile>
        open_file(file_path const& path)
        {
            return open_file(path, file_access::default_open);
        }

        std::shared_ptr<ifile>
        try_open_file(file_path const& path, file_access access)
        {
            std::shared_ptr<ifile> returned_file;
            open_file(open_file_flags::tolerate_not_found, path, access, returned_file);
            return returned_file;
        }

        std::shared_ptr<ifile>
        try_open_file(file_path const& path)
        {
            return try_open_file(path, file_access::default_open);
        }

        //
        //  remove_entry
        //
        //  Removes a single child by name. Because the namespace is unified
        //  (D13), one verb removes whichever kind of node the name refers to;
        //  a non-empty directory is rejected (use delete_tree for recursion).
        //

        enum class remove_entry_flags : uint64_t
        {
        };

        enum class remove_entry_result_code : uint32_t
        {
        };

        enum class remove_entry_result_flags : uint32_t
        {
        };

        using remove_entry_disposition =
            disposition<remove_entry_result_code, remove_entry_result_flags>;

        virtual remove_entry_disposition
        remove_entry(remove_entry_flags flags, file_path const& name) = 0;

        void
        remove_entry(file_path const& name)
        {
            auto const d = remove_entry(remove_entry_flags{}, name);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  delete_tree
        //
        //  Recursively removes a child subtree (or the contents of this
        //  directory when no name is supplied), mirroring ikey::delete_tree.
        //

        enum class delete_tree_flags : uint64_t
        {
        };

        enum class delete_tree_result_code : uint32_t
        {
        };

        enum class delete_tree_result_flags : uint32_t
        {
        };

        using delete_tree_disposition =
            disposition<delete_tree_result_code, delete_tree_result_flags>;

        virtual delete_tree_disposition
        delete_tree(delete_tree_flags flags, std::optional<file_path> const& name) = 0;

        void
        delete_tree(std::optional<file_path> const& name)
        {
            auto const d = delete_tree(delete_tree_flags{}, name);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  rename_entry
        //
        //  Renames or moves a child. `old_path` and `new_path` are interpreted
        //  relative to this directory, so this both renames within the
        //  directory and moves across the subtree it roots.
        //

        enum class rename_entry_flags : uint64_t
        {
        };

        enum class rename_entry_result_code : uint32_t
        {
        };

        enum class rename_entry_result_flags : uint32_t
        {
        };

        using rename_entry_disposition =
            disposition<rename_entry_result_code, rename_entry_result_flags>;

        virtual rename_entry_disposition
        rename_entry(rename_entry_flags flags,
                     file_path const&   old_path,
                     file_path const&   new_path) = 0;

        void
        rename_entry(file_path const& old_path, file_path const& new_path)
        {
            auto const d = rename_entry(rename_entry_flags{}, old_path, new_path);
            M_INTERNAL_ERROR_CHECK(!d);
        }

        //
        //  enumerate_entries
        //

        enum class enumerate_entries_flags : uint64_t
        {
        };

        enum class enumerate_entries_result_code : uint32_t
        {
        };

        enum class enumerate_entries_result_flags : uint32_t
        {
        };

        using enumerate_entries_disposition =
            disposition<enumerate_entries_result_code, enumerate_entries_result_flags>;

        /// <summary>
        /// Enumerates, one by one, the entries (children) of this directory.
        ///
        /// On entry, the span identifies the buffer of entries to fill starting
        /// at `starting_index`. On return, the span is shrunk to the number of
        /// entries actually produced; an empty span signals the end of the list.
        /// </summary>
        virtual enumerate_entries_disposition
        enumerate_entries(enumerate_entries_flags                          flags,
                          std::size_t                                      starting_index,
                          std::span<directory_entry, std::dynamic_extent>& entries) = 0;

        std::optional<directory_entry>
        enumerate_entries(std::size_t index)
        {
            directory_entry entry;
            auto            s = std::span<directory_entry, std::dynamic_extent>(&entry, 1);

            auto const d = enumerate_entries(enumerate_entries_flags{}, index, s);
            M_INTERNAL_ERROR_CHECK(!d);

            if (s.size() == 0)
                return std::nullopt;

            return entry;
        }

        //
        //  query_information
        //

        enum class query_information_flags : uint64_t
        {
        };

        enum class query_information_result_code : uint32_t
        {
        };

        enum class query_information_result_flags : uint32_t
        {
        };

        using query_information_disposition =
            disposition<query_information_result_code, query_information_result_flags>;

        virtual query_information_disposition
        query_information(query_information_flags flags, file_metadata& metadata) = 0;

        file_metadata
        query_information()
        {
            file_metadata metadata;
            auto const    d = query_information(query_information_flags{}, metadata);
            M_INTERNAL_ERROR_CHECK(!d);
            return metadata;
        }
    };

    //
    // Filesystem change-notification surface (D9). Mirrors the registry monitor
    // (registry_interfaces.h: iregistry_monitor and friends) but carries a
    // richer payload: each change reports both the kind of change and the name
    // of the affected entry, because the unified namespace (D13) must let
    // callers distinguish create / rename / delete at the granularity that
    // ReadDirectoryChangesW provides on Windows. The coarse registry monitor,
    // by contrast, only reports that "something under the watched key changed".
    //

    //
    // The kind of change reported for a single namespace entry. The values
    // mirror the FILE_ACTION_* codes carried by a Win32 FILE_NOTIFY_INFORMATION
    // record; a rename surfaces as a renamed_old_name / renamed_new_name pair.
    // Changing any value is a breaking change.
    //
    enum class filesystem_change_kind : uint32_t
    {
        added            = 1,
        removed          = 2,
        modified         = 3,
        renamed_old_name = 4,
        renamed_new_name = 5,
    };

    struct ifilesystem_monitor_change_notification
    {
        virtual void
        on_begin(utc_time_point_type const& when) = 0;

        struct requeue_directory_access_attempt
        {
            std::chrono::milliseconds m_milliseconds;
        };

        virtual std::optional<requeue_directory_access_attempt>
        on_directory_access_failure(utc_time_point_type const& when,
                                    file_path const&           directory,
                                    std::system_error const&   ec) = 0;

        struct requeue_change_notification_attempt
        {
            std::chrono::milliseconds m_milliseconds;
        };

        virtual std::optional<requeue_change_notification_attempt>
        on_change_notification_attempt_failure(utc_time_point_type const& when,
                                               file_path const&           directory,
                                               std::system_error const&   ec) = 0;

        virtual void
        on_change(utc_time_point_type const& when,
                  file_path const&           directory,
                  filesystem_change_kind     kind,
                  file_path const&           entry_name) = 0;

        virtual void
        on_cancelled(utc_time_point_type const& when) = 0;

    protected:
        virtual ~ifilesystem_monitor_change_notification() {}
    };

    struct ifilesystem_monitor_token
    {
        virtual ~ifilesystem_monitor_token() {}
    };

    struct ifilesystem_monitor
    {
        virtual ~ifilesystem_monitor() {}

        //
        // Selects which categories of change the watch reports. The flags mirror
        // the FILE_NOTIFY_CHANGE_* filter bits of ReadDirectoryChangesW; when
        // watch_subtree is selected the entire subtree rooted at the watched
        // directory is observed rather than just its immediate children.
        //
        enum class register_watch_flags : uint64_t
        {
            watch_subtree          = 1ull << 0,
            file_name_changes      = 1ull << 1,
            directory_name_changes = 1ull << 2,
            attribute_changes      = 1ull << 3,
            size_changes           = 1ull << 4,
            last_write_changes     = 1ull << 5,
            last_access_changes    = 1ull << 6,
            creation_changes       = 1ull << 7,
            security_changes       = 1ull << 8,
        };

        enum class register_watch_result_code : uint32_t
        {
        };

        enum class register_watch_result_flags : uint32_t
        {
        };

        using register_watch_disposition =
            disposition<register_watch_result_code, register_watch_result_flags>;

        virtual register_watch_disposition
        register_watch(
            register_watch_flags                                  flags,
            file_path const&                                      directory,
            m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr,
            std::unique_ptr<ifilesystem_monitor_token>&           returned_ptr) = 0;

        std::unique_ptr<ifilesystem_monitor_token>
        register_watch(
            file_path const&                                      directory,
            m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr)
        {
            std::unique_ptr<ifilesystem_monitor_token> returned_ptr;
            auto const                                 d = register_watch(
                register_watch_flags{}, directory, change_notification_ptr, returned_ptr);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_ptr;
        }

        std::unique_ptr<ifilesystem_monitor_token>
        register_watch(
            register_watch_flags                                  flags,
            file_path const&                                      directory,
            m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr)
        {
            std::unique_ptr<ifilesystem_monitor_token> returned_ptr;
            auto const d = register_watch(flags, directory, change_notification_ptr, returned_ptr);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_ptr;
        }
    };

    //
    // The filesystem entry point. Opening a root (D10 — roots are open-ended,
    // not a closed enum) yields the directory that anchors a namespace. This is
    // the analogue of iregistry::open_predefined_key.
    //
    struct ifilesystem
    {
        virtual ~ifilesystem() = default;

        //
        //  open_root
        //

        enum class open_root_flags : uint64_t
        {
        };

        enum class open_root_result_code : uint32_t
        {
        };

        enum class open_root_result_flags : uint32_t
        {
        };

        using open_root_disposition = disposition<open_root_result_code, open_root_result_flags>;

        virtual open_root_disposition
        open_root(open_root_flags              flags,
                  file_root const&             root,
                  file_access                  access,
                  std::shared_ptr<idirectory>& returned_directory) = 0;

        std::shared_ptr<idirectory>
        open_root(file_root const& root, file_access access)
        {
            std::shared_ptr<idirectory> returned_directory;
            auto const d = open_root(open_root_flags{}, root, access, returned_directory);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_directory;
        }

        std::shared_ptr<idirectory>
        open_root(file_root const& root)
        {
            return open_root(root, file_access::default_open);
        }

        //
        //  monitor
        //
        //  Returns the filesystem change-notification surface (D9), the analogue
        //  of iregistry::monitor.
        //

        enum class monitor_flags : uint64_t
        {
        };

        enum class monitor_result_code : uint32_t
        {
        };

        enum class monitor_result_flags : uint32_t
        {
        };

        using monitor_disposition = disposition<monitor_result_code, monitor_result_flags>;

        virtual monitor_disposition
        monitor(monitor_flags                                  flags,
                std::shared_ptr<m::pil::ifilesystem_monitor>&  returned_filesystem_monitor) = 0;

        std::shared_ptr<m::pil::ifilesystem_monitor>
        monitor()
        {
            std::shared_ptr<m::pil::ifilesystem_monitor> returned_monitor;
            auto const d = monitor(monitor_flags{}, returned_monitor);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_monitor;
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifile::query_information_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifile::query_information_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifile::read_content_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifile::read_content_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifile::write_content_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifile::write_content_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifile::enumerate_streams_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifile::enumerate_streams_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::create_directory_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::create_directory_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::create_file_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::create_file_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::open_directory_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::open_directory_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::open_file_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::open_file_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::remove_entry_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::remove_entry_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::delete_tree_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::delete_tree_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::rename_entry_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::rename_entry_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::enumerate_entries_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::enumerate_entries_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::query_information_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(idirectory::query_information_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifilesystem::open_root_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifilesystem::open_root_result_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifilesystem::monitor_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifilesystem::monitor_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifilesystem_monitor::register_watch_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ifilesystem_monitor::register_watch_result_flags);
    //
    // A placeholder filesystem that resolves through the platform stack but has
    // no live backing store yet. Every operation throws "not implemented". The
    // base iplatform wiring hands one of these out (see
    // iplatform::get_filesystem) until a real provider lands in M-FS-DIRECT, so
    // that the surface compiles and resolves cross-platform before any provider
    // exists.
    //
    struct null_filesystem final : ifilesystem
    {
        open_root_disposition
        open_root(open_root_flags,
                  file_root const&,
                  file_access,
                  std::shared_ptr<idirectory>&) override
        {
            M_NOT_IMPLEMENTED("null_filesystem::open_root");
        }

        monitor_disposition
        monitor(monitor_flags, std::shared_ptr<m::pil::ifilesystem_monitor>&) override
        {
            M_NOT_IMPLEMENTED("null_filesystem::monitor");
        }
    };

} // namespace m::pil
