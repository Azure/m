// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <string_view>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>

#include "logging.h"

using namespace std::string_view_literals;

namespace m::pil::impl::logging
{
    //
    // Filesystem.CreateDirectory
    //

    fs_create_directory_log_entry::fs_create_directory_log_entry(
        idirectory::create_directory_flags flags, file_path const& path, file_access access):
        m_flags(flags), m_path(path), m_access(access), m_disposition{}
    {}

    void
    fs_create_directory_log_entry::set_disposition(idirectory::create_directory_disposition d)
    {
        m_disposition = d;
    }

    void
    fs_create_directory_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Filesystem.CreateDirectory"sv));

        write_attribute(n, M_PUGIXML_T("path"sv), m_path);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("access"sv), m_access);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    //
    // Filesystem.CreateFile
    //

    fs_create_file_log_entry::fs_create_file_log_entry(idirectory::create_file_flags flags,
                                                       file_path const&              path,
                                                       file_access                   access):
        m_flags(flags), m_path(path), m_access(access), m_disposition{}
    {}

    void
    fs_create_file_log_entry::set_disposition(idirectory::create_file_disposition d)
    {
        m_disposition = d;
    }

    void
    fs_create_file_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Filesystem.CreateFile"sv));

        write_attribute(n, M_PUGIXML_T("path"sv), m_path);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("access"sv), m_access);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    //
    // Filesystem.Remove
    //

    fs_remove_entry_log_entry::fs_remove_entry_log_entry(idirectory::remove_entry_flags flags,
                                                         file_path const&               name):
        m_flags(flags), m_name(name), m_disposition{}
    {}

    void
    fs_remove_entry_log_entry::set_disposition(idirectory::remove_entry_disposition d)
    {
        m_disposition = d;
    }

    void
    fs_remove_entry_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Filesystem.Remove"sv));

        write_attribute(n, M_PUGIXML_T("name"sv), m_name);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    //
    // Filesystem.DeleteTree
    //

    fs_delete_tree_log_entry::fs_delete_tree_log_entry(idirectory::delete_tree_flags   flags,
                                                       std::optional<file_path> const& name):
        m_flags(flags), m_name(name), m_disposition{}
    {}

    void
    fs_delete_tree_log_entry::set_disposition(idirectory::delete_tree_disposition d)
    {
        m_disposition = d;
    }

    void
    fs_delete_tree_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Filesystem.DeleteTree"sv));

        write_attribute(n, M_PUGIXML_T("name"sv), m_name);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    //
    // Filesystem.Rename
    //

    fs_rename_entry_log_entry::fs_rename_entry_log_entry(idirectory::rename_entry_flags flags,
                                                         file_path const&               old_path,
                                                         file_path const&               new_path):
        m_flags(flags), m_old_path(old_path), m_new_path(new_path), m_disposition{}
    {}

    void
    fs_rename_entry_log_entry::set_disposition(idirectory::rename_entry_disposition d)
    {
        m_disposition = d;
    }

    void
    fs_rename_entry_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Filesystem.Rename"sv));

        write_attribute(n, M_PUGIXML_T("oldPath"sv), m_old_path);
        write_attribute(n, M_PUGIXML_T("newPath"sv), m_new_path);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

} // namespace m::pil::impl::logging
