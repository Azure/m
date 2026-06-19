// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <m/exception/exception.h>
#include <m/pil/common.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>

#include "buffered.h"

namespace m::pil::impl::buffered
{
    namespace
    {
        file_metadata
        make_directory_metadata(time_point_type now)
        {
            file_metadata md;
            md.m_kind             = node_kind::directory;
            md.m_size             = 0;
            md.m_creation_time    = now;
            md.m_last_write_time  = now;
            md.m_last_access_time = now;
            md.m_attributes       = file_attributes::directory;
            return md;
        }

        file_metadata
        make_file_metadata(time_point_type now)
        {
            file_metadata md;
            md.m_kind             = node_kind::file;
            md.m_size             = 0;
            md.m_creation_time    = now;
            md.m_last_write_time  = now;
            md.m_last_access_time = now;
            md.m_attributes       = file_attributes::normal;
            return md;
        }
    } // namespace

    //
    // small overlay helpers shared by the mutation verbs
    //

    bool
    directory::is_empty() const
    {
        auto lock = std::unique_lock(m_mutex);

        for (auto const& [name, node]: m_entries)
        {
            static_cast<void>(name);
            if (!node.m_deleted)
                return false;
        }

        return true;
    }

    directory::entry_node
    directory::extract_entry(m::u16sstring const& leaf)
    {
        auto lock = std::unique_lock(m_mutex);

        auto const it = m_entries.find(leaf);
        if (it == m_entries.end() || it->second.m_deleted)
            throw m::not_found("idirectory::rename_entry() source not found");

        auto& node = it->second;

        // Materialize so the moved node carries its own state, detached from the
        // underlying provider at its current path. A file is metadata-only (D14);
        // a directory captures its direct children (non-recursive).
        if (node.m_kind == node_kind::directory)
            materialize_subdirectory(it->first, node, file_access::default_create);
        else
            materialize_file(it->first, node, file_access::default_open);

        entry_node moved = node;
        moved.m_deleted  = false;
        moved.m_mirrored = false;

        // Tombstone the old slot so the move shadows any underlying provider.
        node.m_directory.reset();
        node.m_file.reset();
        node.m_deleted  = true;
        node.m_mirrored = false;

        return moved;
    }

    void
    directory::insert_entry(m::u16sstring const& leaf, entry_node node)
    {
        auto lock = std::unique_lock(m_mutex);

        auto const it = m_entries.find(leaf);
        if (it == m_entries.end())
        {
            m_entries.emplace(leaf, std::move(node));
            return;
        }

        if (!it->second.m_deleted)
            throw m::already_exists("idirectory::rename_entry() destination already exists");

        // Overwrite a tombstone in place.
        it->second = std::move(node);
    }

    //
    // mutations
    //

    idirectory::create_directory_disposition
    directory::create_directory(create_directory_flags       flags,
                                file_path const&             path,
                                file_access                  access,
                                std::shared_ptr<idirectory>& returned_directory)
    {
        returned_directory.reset();

        if (flags != create_directory_flags{})
            throw m::invalid_parameter("idirectory::create_directory.flags");

        // Auto-create every intermediate component of a multi-segment path,
        // create-or-open at each level, then create the leaf in the parent.
        if (path.has_parent_path())
        {
            auto [parent_opt, leaf] = path.split_parent_path_and_leaf_name();
            M_INTERNAL_ERROR_CHECK(parent_opt.has_value());

            auto parent = idirectory::create_directory(parent_opt.value(), access);
            return parent->create_directory(flags, leaf, access, returned_directory);
        }

        auto const    now    = time_point_type::clock::now();
        file_metadata dir_md = make_directory_metadata(now);

        auto lock = std::unique_lock(m_mutex);

        auto [it, inserted] =
            m_entries.emplace(path.native(),
                              entry_node{.m_kind      = node_kind::directory,
                                         .m_directory = std::make_shared<directory>(dir_md, nullptr),
                                         .m_file      = {},
                                         .m_metadata  = dir_md,
                                         .m_deleted   = false,
                                         .m_mirrored  = false,
                                         .m_short_name = {}});

        if (inserted)
        {
            returned_directory = it->second.m_directory;
            return create_directory_disposition{};
        }

        auto& node = it->second;

        if (node.m_deleted)
        {
            // Revive a tombstone as a fresh empty directory.
            node.m_kind      = node_kind::directory;
            node.m_file.reset();
            node.m_directory = std::make_shared<directory>(dir_md, nullptr);
            node.m_metadata  = dir_md;
            node.m_deleted   = false;
            node.m_mirrored  = false;
            returned_directory = node.m_directory;
            return create_directory_disposition{};
        }

        // Unified namespace (D13): a name already taken by a file conflicts.
        if (node.m_kind != node_kind::directory)
            throw m::already_exists("idirectory::create_directory() name exists as a file");

        auto materialized = materialize_subdirectory(it->first, node, access);
        if (!materialized)
        {
            // Sealed-snapshot placeholder: create-or-open makes a fresh empty
            // directory in its place (D5 lazy repair, fuller form in M-FS-BUF-3).
            node.m_directory = std::make_shared<directory>(dir_md, nullptr);
            node.m_metadata  = dir_md;
            node.m_mirrored  = false;
            materialized     = node.m_directory;
        }

        returned_directory = std::move(materialized);
        return create_directory_disposition{};
    }

    idirectory::create_file_disposition
    directory::create_file(create_file_flags       flags,
                           file_path const&        path,
                           file_access             access,
                           std::shared_ptr<ifile>& returned_file)
    {
        returned_file.reset();

        if (flags != create_file_flags{})
            throw m::invalid_parameter("idirectory::create_file.flags");

        // A multi-segment path creates its intermediate directories, then the
        // leaf file in the resulting parent.
        if (path.has_parent_path())
        {
            auto [parent_opt, leaf] = path.split_parent_path_and_leaf_name();
            M_INTERNAL_ERROR_CHECK(parent_opt.has_value());

            auto parent = idirectory::create_directory(parent_opt.value(), access);
            return parent->create_file(flags, leaf, access, returned_file);
        }

        auto const    now     = time_point_type::clock::now();
        file_metadata file_md = make_file_metadata(now);

        auto lock = std::unique_lock(m_mutex);

        auto [it, inserted] =
            m_entries.emplace(path.native(),
                              entry_node{.m_kind      = node_kind::file,
                                         .m_directory = {},
                                         .m_file      = std::make_shared<file>(file_md),
                                         .m_metadata  = file_md,
                                         .m_deleted   = false,
                                         .m_mirrored  = false,
                                         .m_short_name = {}});

        if (inserted)
        {
            returned_file = it->second.m_file;
            return create_file_disposition{};
        }

        auto& node = it->second;

        if (node.m_deleted)
        {
            // Revive a tombstone as a fresh empty file.
            node.m_kind      = node_kind::file;
            node.m_directory.reset();
            node.m_file      = std::make_shared<file>(file_md);
            node.m_metadata  = file_md;
            node.m_deleted   = false;
            node.m_mirrored  = false;
            returned_file    = node.m_file;
            return create_file_disposition{};
        }

        // Unified namespace (D13): a name already taken by a directory conflicts.
        if (node.m_kind != node_kind::file)
            throw m::already_exists("idirectory::create_file() name exists as a directory");

        // Create-or-open: an existing file is opened.
        returned_file = materialize_file(it->first, node, access);
        return create_file_disposition{};
    }

    idirectory::remove_entry_disposition
    directory::remove_entry(remove_entry_flags flags, file_path const& name)
    {
        if (flags != remove_entry_flags{})
            throw m::invalid_parameter("idirectory::remove_entry.flags");

        // Resolve a multi-segment name through the overlay to its parent, then
        // remove the single leaf there.
        if (name.has_parent_path())
        {
            auto [parent_opt, leaf] = name.split_parent_path_and_leaf_name();
            M_INTERNAL_ERROR_CHECK(parent_opt.has_value());

            auto parent = idirectory::try_open_directory(parent_opt.value(), file_access::default_open);
            if (!parent)
                throw m::not_found("idirectory::remove_entry() parent not found");

            parent->remove_entry(leaf);
            return remove_entry_disposition{};
        }

        auto lock = std::unique_lock(m_mutex);

        auto const it = m_entries.find(name.native());
        if (it == m_entries.end() || it->second.m_deleted)
            throw m::not_found("idirectory::remove_entry() entry not found");

        auto& node = it->second;

        // A non-empty directory is rejected; delete_tree removes recursively.
        if (node.m_kind == node_kind::directory)
        {
            auto child = materialize_subdirectory(it->first, node, file_access::default_open);
            if (child && !child->is_empty())
                throw m::not_empty("idirectory::remove_entry() directory not empty");
        }

        node.m_directory.reset();
        node.m_file.reset();
        node.m_deleted  = true;
        node.m_mirrored = false;

        return remove_entry_disposition{};
    }

    idirectory::delete_tree_disposition
    directory::delete_tree(delete_tree_flags flags, std::optional<file_path> const& name)
    {
        if (flags != delete_tree_flags{})
            throw m::invalid_parameter("idirectory::delete_tree.flags");

        // No name (or empty name): empty this directory's contents — tombstone
        // every live child — but leave this directory in place. A single
        // tombstone per child shadows any underlying provider (M-BUFTREE).
        if (!name.has_value() || name.value().native().empty())
        {
            auto lock = std::unique_lock(m_mutex);

            for (auto& [child_name, node]: m_entries)
            {
                static_cast<void>(child_name);
                node.m_directory.reset();
                node.m_file.reset();
                node.m_deleted  = true;
                node.m_mirrored = false;
            }

            return delete_tree_disposition{};
        }

        auto const& path = name.value();

        // A multi-segment name resolves to its parent, then deletes the leaf
        // subtree there.
        if (path.has_parent_path())
        {
            auto [parent_opt, leaf] = path.split_parent_path_and_leaf_name();
            M_INTERNAL_ERROR_CHECK(parent_opt.has_value());

            auto parent = idirectory::try_open_directory(parent_opt.value(), file_access::default_open);
            if (!parent)
                throw m::not_found("idirectory::delete_tree() parent not found");

            parent->delete_tree(std::optional<file_path>(leaf));
            return delete_tree_disposition{};
        }

        auto lock = std::unique_lock(m_mutex);

        auto const it = m_entries.find(path.native());
        if (it == m_entries.end() || it->second.m_deleted)
            throw m::not_found("idirectory::delete_tree() entry not found");

        auto& node = it->second;

        // Tombstoning the entry hides the whole subtree at once: a single
        // tombstone shadows every descendant (the M-BUFTREE technique).
        node.m_directory.reset();
        node.m_file.reset();
        node.m_deleted  = true;
        node.m_mirrored = false;

        return delete_tree_disposition{};
    }

    idirectory::rename_entry_disposition
    directory::rename_entry(rename_entry_flags flags,
                            file_path const&   old_path,
                            file_path const&   new_path)
    {
        if (flags != rename_entry_flags{})
            throw m::invalid_parameter("idirectory::rename_entry.flags");

        // Resolve the source parent (must exist) and the source leaf name.
        std::shared_ptr<directory> old_parent_holder;
        file_path                  old_leaf = old_path;
        if (old_path.has_parent_path())
        {
            auto [parent_opt, leaf] = old_path.split_parent_path_and_leaf_name();
            M_INTERNAL_ERROR_CHECK(parent_opt.has_value());

            auto parent = idirectory::try_open_directory(parent_opt.value(), file_access::default_open);
            if (!parent)
                throw m::not_found("idirectory::rename_entry() source parent not found");

            old_parent_holder = std::static_pointer_cast<directory>(parent);
            old_leaf          = leaf;
        }

        // Resolve the destination parent, creating intermediates (move across
        // the subtree this directory roots), and the destination leaf name.
        std::shared_ptr<directory> new_parent_holder;
        file_path                  new_leaf = new_path;
        if (new_path.has_parent_path())
        {
            auto [parent_opt, leaf] = new_path.split_parent_path_and_leaf_name();
            M_INTERNAL_ERROR_CHECK(parent_opt.has_value());

            auto parent = idirectory::create_directory(parent_opt.value(), file_access::default_create);
            new_parent_holder = std::static_pointer_cast<directory>(parent);
            new_leaf          = leaf;
        }

        directory& old_parent = old_parent_holder ? *old_parent_holder : *this;
        directory& new_parent = new_parent_holder ? *new_parent_holder : *this;

        // Detach the source node (self-contained after materialization,
        // tombstoned at its old slot), then re-key it at the destination.
        auto moved = old_parent.extract_entry(old_leaf.native());
        new_parent.insert_entry(new_leaf.native(), std::move(moved));

        return rename_entry_disposition{};
    }

} // namespace m::pil::impl::buffered
