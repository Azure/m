// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <string>
#include <string_view>

#include <m/pil/file_path.h>
#include <m/strings/convert.h>

#include "journaling.h"

using namespace std::string_view_literals;

namespace m::pil::impl::journaling
{
    namespace
    {
        void
        write_fs_path_attribute(pugi::xml_node& n, pugi::string_view_t name, file_path const& path)
        {
            n.append_attribute(name).set_value(m::to_wstring(path.native().view()).c_str());
        }
    } // namespace

    fs_create_directory_entry::fs_create_directory_entry(file_path const& base_directory_path,
                                                         file_path const& path):
        m_base_directory_path(base_directory_path), m_path(path)
    {}

    void
    fs_create_directory_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("Filesystem.CreateDirectory"sv));
        write_fs_path_attribute(n, M_PUGIXML_T("dir"sv), m_base_directory_path);
        write_fs_path_attribute(n, M_PUGIXML_T("path"sv), m_path);
    }

    fs_create_file_entry::fs_create_file_entry(file_path const& base_directory_path,
                                               file_path const& path):
        m_base_directory_path(base_directory_path), m_path(path)
    {}

    void
    fs_create_file_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("Filesystem.CreateFile"sv));
        write_fs_path_attribute(n, M_PUGIXML_T("dir"sv), m_base_directory_path);
        write_fs_path_attribute(n, M_PUGIXML_T("path"sv), m_path);
    }

    fs_remove_entry::fs_remove_entry(file_path const& base_directory_path, file_path const& name):
        m_base_directory_path(base_directory_path), m_name(name)
    {}

    void
    fs_remove_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("Filesystem.Remove"sv));
        write_fs_path_attribute(n, M_PUGIXML_T("dir"sv), m_base_directory_path);
        write_fs_path_attribute(n, M_PUGIXML_T("name"sv), m_name);
    }

    fs_delete_tree_entry::fs_delete_tree_entry(file_path const&                base_directory_path,
                                               std::optional<file_path> const& name):
        m_base_directory_path(base_directory_path), m_name(name)
    {}

    void
    fs_delete_tree_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("Filesystem.DeleteTree"sv));
        write_fs_path_attribute(n, M_PUGIXML_T("dir"sv), m_base_directory_path);
        if (m_name.has_value())
            write_fs_path_attribute(n, M_PUGIXML_T("name"sv), m_name.value());
    }

    fs_rename_entry::fs_rename_entry(file_path const& base_directory_path,
                                     file_path const& old_path,
                                     file_path const& new_path):
        m_base_directory_path(base_directory_path), m_old_path(old_path), m_new_path(new_path)
    {}

    void
    fs_rename_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("Filesystem.Rename"sv));
        write_fs_path_attribute(n, M_PUGIXML_T("dir"sv), m_base_directory_path);
        write_fs_path_attribute(n, M_PUGIXML_T("oldPath"sv), m_old_path);
        write_fs_path_attribute(n, M_PUGIXML_T("newPath"sv), m_new_path);
    }
} // namespace m::pil::impl::journaling
