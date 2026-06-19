// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>

//
// Convenience value-wrapper layer over the filesystem interfaces. These mirror
// the registry wrappers (registry.h: registry_class / key): value types that
// own a shared_ptr to the underlying interface and expose ergonomic, throwing
// methods. `filesystem_class` is the analogue of `registry_class`, `directory`
// is the analogue of `key` (the unified-namespace container, D13), and `file`
// is the leaf node (metadata only for now, D14).
//

namespace m::pil
{
    class file
    {
    public:
        file() = default;
        file(file const& other);
        file(file&& other) noexcept;
        file(std::shared_ptr<ifile>&& sp) noexcept;
        ~file() = default;

        file&
        operator=(file const& other);
        file&
        operator=(file&& other) noexcept;

        friend void
        swap(file& l, file& r) noexcept
        {
            using std::swap;
            swap(l.m_file, r.m_file);
        }

        // True when this wrapper refers to a live node.
        explicit
        operator bool() const noexcept
        {
            return static_cast<bool>(m_file);
        }

        file_metadata
        query_information();

        // Reads up to buffer.size() bytes of this file's content beginning at
        // byte offset `offset`, returning the count actually read (a short count
        // signals end-of-file). Throws if the underlying provider does not model
        // content (the deferred-content outcome, D14/D16/D17).
        std::size_t
        read_content(std::uint64_t offset, std::span<std::byte> buffer);

        // Whole-file replacement: writes buffer.size() bytes as this file's
        // content beginning at `offset` 0 and sets the file's extent to that
        // length, returning the count written. A non-zero offset is rejected
        // (whole-file only, D16). Throws if the underlying provider does not
        // model content (the deferred-content outcome, D14/D16/D17).
        std::size_t
        write_content(std::uint64_t offset, std::span<std::byte const> buffer);

    private:
        std::shared_ptr<ifile> m_file;
    };

    class directory
    {
    public:
        directory() = default;
        directory(directory const& other);
        directory(directory&& other) noexcept;
        directory(std::shared_ptr<idirectory>&& sp) noexcept;
        ~directory() = default;

        directory&
        operator=(directory const& other);
        directory&
        operator=(directory&& other) noexcept;

        friend void
        swap(directory& l, directory& r) noexcept
        {
            using std::swap;
            swap(l.m_directory, r.m_directory);
        }

        explicit
        operator bool() const noexcept
        {
            return static_cast<bool>(m_directory);
        }

        template <typename CharT>
        directory
        create_directory(std::basic_string_view<CharT> name)
        {
            return create_directory(file_path(name));
        }

        directory
        create_directory(file_path const& name)
        {
            return do_create_directory(name);
        }

        template <typename CharT>
        directory
        open_directory(std::basic_string_view<CharT> name)
        {
            return do_open_directory(file_path(name));
        }

        directory
        open_directory(file_path const& name)
        {
            return do_open_directory(name);
        }

        // Tentative open: returns the directory, or std::nullopt if it does not
        // exist. Other failures (e.g. access denied) still throw.
        std::optional<directory>
        try_open_directory(file_path const& name)
        {
            return do_try_open_directory(name);
        }

        template <typename CharT>
        file
        create_file(std::basic_string_view<CharT> name)
        {
            return create_file(file_path(name));
        }

        file
        create_file(file_path const& name)
        {
            return do_create_file(name);
        }

        template <typename CharT>
        file
        open_file(std::basic_string_view<CharT> name)
        {
            return do_open_file(file_path(name));
        }

        file
        open_file(file_path const& name)
        {
            return do_open_file(name);
        }

        std::optional<file>
        try_open_file(file_path const& name)
        {
            return do_try_open_file(name);
        }

        template <typename CharT>
        void
        remove_entry(std::basic_string_view<CharT> name)
        {
            remove_entry(file_path(name));
        }

        void
        remove_entry(file_path const& name)
        {
            do_remove_entry(name);
        }

        void
        delete_tree(std::optional<file_path> const& name)
        {
            do_delete_tree(name);
        }

        void
        rename_entry(file_path const& old_name, file_path const& new_name)
        {
            do_rename_entry(old_name, new_name);
        }

        std::vector<directory_entry>
        list_entries();

        file_metadata
        query_information();

    private:
        directory
        do_create_directory(file_path const& name);

        directory
        do_open_directory(file_path const& name);

        std::optional<directory>
        do_try_open_directory(file_path const& name);

        file
        do_create_file(file_path const& name);

        file
        do_open_file(file_path const& name);

        std::optional<file>
        do_try_open_file(file_path const& name);

        void
        do_remove_entry(file_path const& name);

        void
        do_delete_tree(std::optional<file_path> const& name);

        void
        do_rename_entry(file_path const& old_name, file_path const& new_name);

        std::shared_ptr<idirectory> m_directory;
    };

    //
    // Value-wrapper over a change-notification monitor (M-FS-MONITOR-1). The
    // analogue of registry_monitor: minted by filesystem_class::monitor(), it
    // registers ReadDirectoryChangesW-backed watches that deliver detailed
    // create / rename / delete notifications.
    //
    class filesystem_monitor
    {
    public:
        filesystem_monitor() = default;

        filesystem_monitor(filesystem_monitor const& other) = delete;
        filesystem_monitor(filesystem_monitor&& other)      = delete;

        filesystem_monitor&
        operator=(filesystem_monitor const& other) = delete;

        filesystem_monitor&
        operator=(filesystem_monitor&& other) = delete;

        void
        swap(filesystem_monitor& other) = delete;

        //
        // Selects which categories of change the watch reports. When
        // `watch_subtree` is selected the entire subtree rooted at the watched
        // directory is observed; otherwise only its immediate children are.
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

        std::unique_ptr<ifilesystem_monitor_token>
        register_watch(
            register_watch_flags                                  flags,
            file_path const&                                      directory,
            m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr)
        {
            return do_register_watch(flags, directory, change_notification_ptr);
        }

    protected:
        filesystem_monitor(std::shared_ptr<pil::ifilesystem_monitor> sp):
            m_ifilesystem_monitor(std::move(sp))
        {}

        std::unique_ptr<ifilesystem_monitor_token>
        do_register_watch(
            register_watch_flags                                  flags,
            file_path const&                                      directory,
            m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr);

        std::mutex                                m_mutex;
        std::shared_ptr<pil::ifilesystem_monitor> m_ifilesystem_monitor;

        friend class filesystem_class;
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(filesystem_monitor::register_watch_flags);

    class filesystem_class
    {
    public:
        filesystem_class() = default;
        filesystem_class(filesystem_class const& other);
        filesystem_class(filesystem_class&& other) noexcept;
        filesystem_class(std::shared_ptr<ifilesystem>&& sp) noexcept;
        ~filesystem_class() = default;

        filesystem_class&
        operator=(filesystem_class const& other);
        filesystem_class&
        operator=(filesystem_class&& other) noexcept;

        void
        swap(filesystem_class& other) noexcept;

        directory
        open_root(file_root const& root) const;

        filesystem_monitor
        monitor() const;

    private:
        std::shared_ptr<ifilesystem>
                                     get_filesystem() const;
        mutable std::mutex           m_mutex;
        std::shared_ptr<ifilesystem> m_filesystem;
    };

} // namespace m::pil
