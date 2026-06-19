// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <m/pil/file_path.h>
#include <m/pil/filesystem.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>

namespace m::pil
{
    //
    // file
    //

    file::file(file const& other): m_file(other.m_file) {}

    file::file(file&& other) noexcept
    {
        using std::swap;
        swap(m_file, other.m_file);
    }

    file::file(std::shared_ptr<ifile>&& sp) noexcept: m_file(std::move(sp)) {}

    file&
    file::operator=(file const& other)
    {
        m_file = other.m_file;
        return *this;
    }

    file&
    file::operator=(file&& other) noexcept
    {
        using std::swap;
        swap(m_file, other.m_file);
        return *this;
    }

    file_metadata
    file::query_information()
    {
        return m_file->query_information();
    }

    std::size_t
    file::read_content(std::uint64_t offset, std::span<std::byte> buffer)
    {
        return m_file->read_content(offset, buffer);
    }

    std::size_t
    file::write_content(std::uint64_t offset, std::span<std::byte const> buffer)
    {
        return m_file->write_content(offset, buffer);
    }

    //
    // directory
    //

    directory::directory(directory const& other): m_directory(other.m_directory) {}

    directory::directory(directory&& other) noexcept
    {
        using std::swap;
        swap(m_directory, other.m_directory);
    }

    directory::directory(std::shared_ptr<idirectory>&& sp) noexcept: m_directory(std::move(sp)) {}

    directory&
    directory::operator=(directory const& other)
    {
        m_directory = other.m_directory;
        return *this;
    }

    directory&
    directory::operator=(directory&& other) noexcept
    {
        using std::swap;
        swap(m_directory, other.m_directory);
        return *this;
    }

    directory
    directory::do_create_directory(file_path const& name)
    {
        return directory(m_directory->create_directory(name));
    }

    directory
    directory::do_open_directory(file_path const& name)
    {
        return directory(m_directory->open_directory(name));
    }

    std::optional<directory>
    directory::do_try_open_directory(file_path const& name)
    {
        auto sp = m_directory->try_open_directory(name);
        if (!sp)
            return std::nullopt;

        return directory(std::move(sp));
    }

    file
    directory::do_create_file(file_path const& name)
    {
        return file(m_directory->create_file(name));
    }

    file
    directory::do_open_file(file_path const& name)
    {
        return file(m_directory->open_file(name));
    }

    std::optional<file>
    directory::do_try_open_file(file_path const& name)
    {
        auto sp = m_directory->try_open_file(name);
        if (!sp)
            return std::nullopt;

        return file(std::move(sp));
    }

    void
    directory::do_remove_entry(file_path const& name)
    {
        m_directory->remove_entry(name);
    }

    void
    directory::do_delete_tree(std::optional<file_path> const& name)
    {
        m_directory->delete_tree(name);
    }

    void
    directory::do_rename_entry(file_path const& old_name, file_path const& new_name)
    {
        m_directory->rename_entry(old_name, new_name);
    }

    std::vector<directory_entry>
    directory::list_entries()
    {
        std::vector<directory_entry> result;

        std::size_t index{};

        std::array<directory_entry, 32> entries;
        auto entries_span = std::span<directory_entry, std::dynamic_extent>(entries);

        for (;;)
        {
            auto const d = m_directory->enumerate_entries(
                idirectory::enumerate_entries_flags{}, index, entries_span);
            M_INTERNAL_ERROR_CHECK(!d); // no flags in, no disposition out

            for (auto&& entry: entries_span)
                result.emplace_back(std::move(entry));

            // If the batch was short, we're done.
            if (entries_span.size() != entries.size())
                break;

            index += entries.size();
        }

        return result;
    }

    file_metadata
    directory::query_information()
    {
        return m_directory->query_information();
    }

    //
    // filesystem_class
    //

    filesystem_class::filesystem_class(std::shared_ptr<ifilesystem>&& sp) noexcept:
        m_filesystem(std::move(sp))
    {}

    filesystem_class::filesystem_class(filesystem_class&& other) noexcept
    {
        using std::swap;
        swap(m_filesystem, other.m_filesystem);
    }

    filesystem_class::filesystem_class(filesystem_class const& other):
        m_filesystem(other.get_filesystem())
    {}

    filesystem_class&
    filesystem_class::operator=(filesystem_class const& other)
    {
        auto filesystem = other.get_filesystem();
        auto l          = std::unique_lock(m_mutex);
        m_filesystem    = filesystem;
        return *this;
    }

    filesystem_class&
    filesystem_class::operator=(filesystem_class&& other) noexcept
    {
        using std::swap;
        swap(m_filesystem, other.m_filesystem);
        return *this;
    }

    std::shared_ptr<ifilesystem>
    filesystem_class::get_filesystem() const
    {
        auto l = std::unique_lock(m_mutex);
        return m_filesystem;
    }

    void
    filesystem_class::swap(filesystem_class& other) noexcept
    {
        using std::swap;
        swap(m_filesystem, other.m_filesystem);
    }

    directory
    filesystem_class::open_root(file_root const& root) const
    {
        auto l = std::unique_lock(m_mutex);
        return directory(m_filesystem->open_root(root));
    }

    filesystem_monitor
    filesystem_class::monitor() const
    {
        auto l = std::unique_lock(m_mutex);
        return filesystem_monitor(m_filesystem->monitor());
    }

} // namespace m::pil
