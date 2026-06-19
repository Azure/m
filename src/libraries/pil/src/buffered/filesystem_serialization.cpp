// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/pil/common.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/strings/convert.h>

#include "buffered.h"

using namespace std::string_view_literals;

namespace m::pil::impl::buffered
{
    namespace
    {
        // Emit a node's whole captured metadata as attributes of the given
        // element. last_write_time doubles as the version stamp consulted by
        // lazy consistency repair on load (D5).
        void
        write_metadata(pugi::xml_node& element, file_metadata const& md)
        {
            element.append_attribute(M_PUGIXML_T("size"sv))
                .set_value(static_cast<long long>(md.m_size));
            element.append_attribute(M_PUGIXML_T("creation_time"sv))
                .set_value(static_cast<long long>(md.m_creation_time.time_since_epoch().count()));
            element.append_attribute(M_PUGIXML_T("last_write_time"sv))
                .set_value(static_cast<long long>(md.m_last_write_time.time_since_epoch().count()));
            element.append_attribute(M_PUGIXML_T("last_access_time"sv))
                .set_value(static_cast<long long>(md.m_last_access_time.time_since_epoch().count()));
            element.append_attribute(M_PUGIXML_T("attributes"sv))
                .set_value(static_cast<unsigned>(std::to_underlying(md.m_attributes)));
        }

        // Reconstruct a node's metadata from a persisted element. The kind is
        // not serialized as an attribute — it is implied by the element name
        // (<Directory> / <File>) — so it is supplied by the caller.
        file_metadata
        read_metadata(pugi::xml_node const& element, node_kind kind)
        {
            file_metadata md;
            md.m_kind = kind;
            md.m_size =
                static_cast<std::uint64_t>(element.attribute(M_PUGIXML_T("size"sv)).as_llong(0));
            md.m_creation_time = time_point_type(time_point_type::duration(
                element.attribute(M_PUGIXML_T("creation_time"sv)).as_llong(0)));
            md.m_last_write_time = time_point_type(time_point_type::duration(
                element.attribute(M_PUGIXML_T("last_write_time"sv)).as_llong(0)));
            md.m_last_access_time = time_point_type(time_point_type::duration(
                element.attribute(M_PUGIXML_T("last_access_time"sv)).as_llong(0)));
            md.m_attributes = static_cast<file_attributes>(
                element.attribute(M_PUGIXML_T("attributes"sv)).as_uint(0));
            return md;
        }
    } // namespace

    void
    directory::save_xml(pugi::xml_node& parent) const
    {
        auto l = std::unique_lock(m_mutex);

        // This directory's own captured metadata.
        write_metadata(parent, m_metadata);

        for (auto const& [name, node]: m_entries)
        {
            if (node.m_kind == node_kind::directory)
            {
                auto element = parent.append_child(M_PUGIXML_T("Directory"sv));
                element.append_attribute(M_PUGIXML_T("name"sv))
                    .set_value(m::to_wstring(name.view()).c_str());

                if (!node.m_short_name.empty())
                    element.append_attribute(M_PUGIXML_T("short_name"sv))
                        .set_value(m::to_wstring(node.m_short_name.view()).c_str());

                if (node.m_deleted)
                {
                    element.append_attribute(M_PUGIXML_T("deleted"sv)).set_value(true);
                    continue;
                }

                if (node.m_directory)
                {
                    // Materialized: serialize the whole subtree (its own
                    // metadata and children are written into this element).
                    node.m_directory->save_xml(element);
                }
                else
                {
                    // A mirrored-but-unopened placeholder (D3): only its name and
                    // captured metadata are known. Mark it mirrored so the loader
                    // restores an unmaterialized placeholder that triggers lazy
                    // consistency repair on first touch (D5) rather than a
                    // fabricated empty directory.
                    element.append_attribute(M_PUGIXML_T("mirrored"sv)).set_value(true);
                    write_metadata(element, node.m_metadata);
                }
            }
            else
            {
                auto element = parent.append_child(M_PUGIXML_T("File"sv));
                element.append_attribute(M_PUGIXML_T("name"sv))
                    .set_value(m::to_wstring(name.view()).c_str());

                if (!node.m_short_name.empty())
                    element.append_attribute(M_PUGIXML_T("short_name"sv))
                        .set_value(m::to_wstring(node.m_short_name.view()).c_str());

                if (node.m_deleted)
                {
                    element.append_attribute(M_PUGIXML_T("deleted"sv)).set_value(true);
                    continue;
                }

                // A file's metadata arrives whole with its parent directory's
                // enumeration (D14), so it always serializes fully; there is no
                // mirrored placeholder for a file.
                write_metadata(element, node.m_metadata);
            }
        }
    }

    void
    directory::load_children_xml(pugi::xml_node const& directory_element, time_point_type load_stamp)
    {
        // Freshly constructed by the loader and not yet published, so no lock.
        m_load_stamp = load_stamp;
        m_metadata   = read_metadata(directory_element, node_kind::directory);

        for (auto child = directory_element.first_child(); child; child = child.next_sibling())
        {
            auto const element_name = std::wstring_view(child.name());

            if (element_name == std::wstring_view(M_PUGIXML_T("Directory"sv)))
            {
                auto const name = m::u16sstring(m::to_u16string(
                    std::wstring_view(child.attribute(M_PUGIXML_T("name"sv)).as_string())));

                auto short_name = m::u16sstring(m::to_u16string(
                    std::wstring_view(child.attribute(M_PUGIXML_T("short_name"sv)).as_string())));

                if (child.attribute(M_PUGIXML_T("deleted"sv)).as_bool(false))
                {
                    m_entries.emplace(name,
                                      entry_node{.m_kind      = node_kind::directory,
                                                 .m_directory = {},
                                                 .m_file      = {},
                                                 .m_metadata  = {},
                                                 .m_deleted   = true,
                                                 .m_mirrored  = false,
                                                 .m_short_name = {}});
                    continue;
                }

                auto const md = read_metadata(child, node_kind::directory);

                if (child.attribute(M_PUGIXML_T("mirrored"sv)).as_bool(false))
                {
                    // Name-only placeholder (D3): enumerated but never captured;
                    // opening it triggers lazy consistency repair (D5).
                    m_entries.emplace(name,
                                      entry_node{.m_kind      = node_kind::directory,
                                                 .m_directory = {},
                                                 .m_file      = {},
                                                 .m_metadata  = md,
                                                 .m_deleted   = false,
                                                 .m_mirrored  = true,
                                                 .m_short_name = std::move(short_name)});
                    continue;
                }

                auto child_dir = std::make_shared<directory>(md, nullptr);
                child_dir->load_children_xml(child, load_stamp);

                m_entries.emplace(name,
                                  entry_node{.m_kind      = node_kind::directory,
                                             .m_directory = std::move(child_dir),
                                             .m_file      = {},
                                             .m_metadata  = md,
                                             .m_deleted   = false,
                                             .m_mirrored  = false,
                                             .m_short_name = std::move(short_name)});
            }
            else if (element_name == std::wstring_view(M_PUGIXML_T("File"sv)))
            {
                auto const name = m::u16sstring(m::to_u16string(
                    std::wstring_view(child.attribute(M_PUGIXML_T("name"sv)).as_string())));

                auto short_name = m::u16sstring(m::to_u16string(
                    std::wstring_view(child.attribute(M_PUGIXML_T("short_name"sv)).as_string())));

                if (child.attribute(M_PUGIXML_T("deleted"sv)).as_bool(false))
                {
                    m_entries.emplace(name,
                                      entry_node{.m_kind      = node_kind::file,
                                                 .m_directory = {},
                                                 .m_file      = {},
                                                 .m_metadata  = {},
                                                 .m_deleted   = true,
                                                 .m_mirrored  = false,
                                                 .m_short_name = {}});
                    continue;
                }

                auto const md = read_metadata(child, node_kind::file);

                m_entries.emplace(name,
                                  entry_node{.m_kind      = node_kind::file,
                                             .m_directory = {},
                                             .m_file      = std::make_shared<file>(md),
                                             .m_metadata  = md,
                                             .m_deleted   = false,
                                             .m_mirrored  = false,
                                             .m_short_name = std::move(short_name)});
            }
        }
    }

    void
    filesystem::save_xml(pugi::xml_node& doc_node) const
    {
        auto l = std::unique_lock(m_mutex);

        auto fs_node = doc_node.append_child(M_PUGIXML_T("Filesystem"sv));

        for (auto const& [root, dir]: m_roots)
        {
            auto root_node = fs_node.append_child(M_PUGIXML_T("Root"sv));
            root_node.append_attribute(M_PUGIXML_T("kind"sv))
                .set_value(static_cast<unsigned>(std::to_underlying(root.kind())));
            root_node.append_attribute(M_PUGIXML_T("text"sv))
                .set_value(m::to_wstring(root.text()).c_str());

            dir->save_xml(root_node);
        }
    }

    std::shared_ptr<filesystem>
    filesystem::load_xml(pugi::xml_node const& platform_node)
    {
        // A snapshot filesystem has no underlying (live) filesystem; every root
        // it serves is materialized from the persisted file.
        auto fs = std::make_shared<filesystem>(std::shared_ptr<ifilesystem>{});

        // D5: capture a single T_load for the whole snapshot. Lazy consistency
        // repair restamps any directory from which it drops an unmaterializable
        // name-only child to this value.
        auto const t_load = clock_type::now();

        auto const filesystem_node = platform_node.child(M_PUGIXML_T("Filesystem"sv));
        if (!filesystem_node)
            return fs;

        for (auto root_node = filesystem_node.child(M_PUGIXML_T("Root"sv)); root_node;
             root_node      = root_node.next_sibling(M_PUGIXML_T("Root"sv)))
        {
            auto const kind = static_cast<file_root_kind>(
                root_node.attribute(M_PUGIXML_T("kind"sv)).as_uint());
            auto text = file_root::string_type(m::to_u16string(
                std::wstring_view(root_node.attribute(M_PUGIXML_T("text"sv)).as_string())));
            file_root const root(kind, std::move(text));

            auto dir = std::make_shared<directory>(read_metadata(root_node, node_kind::directory),
                                                   nullptr);
            dir->load_children_xml(root_node, t_load);

            fs->m_roots.emplace(root, std::move(dir));
        }

        return fs;
    }

} // namespace m::pil::impl::buffered
