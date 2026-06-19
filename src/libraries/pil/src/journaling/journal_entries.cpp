// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include <m/strings/convert.h>
#include <m/utility/to_underlying.h>

#include "journaling.h"

using namespace std::string_view_literals;

namespace m::pil::impl::journaling
{
    namespace
    {
        // Lower-case hexadecimal encoding of a byte span. Each byte becomes
        // exactly two characters, high nibble first. The result is a wide
        // string because pugixml is built in wchar mode in this repository.
        // Changing this encoding is a breaking change to the journal artifact.
        std::wstring
        bytes_to_hex(std::span<std::byte const> bytes)
        {
            static constexpr wchar_t  k_hex_digits[] = L"0123456789abcdef";
            static constexpr unsigned k_nibble_shift = 4;
            static constexpr unsigned k_nibble_mask  = 0x0Fu;

            std::wstring out;
            out.reserve(bytes.size() * 2);
            for (auto const b: bytes)
            {
                auto const v = std::to_integer<unsigned>(b);
                out.push_back(k_hex_digits[(v >> k_nibble_shift) & k_nibble_mask]);
                out.push_back(k_hex_digits[v & k_nibble_mask]);
            }
            return out;
        }

        void
        write_path_attribute(pugi::xml_node& n, pugi::string_view_t name, key_path const& path)
        {
            n.append_attribute(name).set_value(
                m::to_wstring(path.native().view()).c_str());
        }

        void
        write_value_name_attribute(pugi::xml_node&               n,
                                   pugi::string_view_t           name,
                                   value_name_string_type const& value_name)
        {
            n.append_attribute(name).set_value(m::to_wstring(value_name.view()).c_str());
        }
    } // namespace

    create_key_entry::create_key_entry(key_path const& base_key_path, key_path const& subkey_path):
        m_base_key_path(base_key_path), m_subkey_path(subkey_path)
    {}

    void
    create_key_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("CreateKey"sv));
        write_path_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_path_attribute(n, M_PUGIXML_T("subKey"sv), m_subkey_path);
    }

    delete_key_entry::delete_key_entry(key_path const& base_key_path, key_path const& subkey_path):
        m_base_key_path(base_key_path), m_subkey_path(subkey_path)
    {}

    void
    delete_key_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("DeleteKey"sv));
        write_path_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_path_attribute(n, M_PUGIXML_T("subKey"sv), m_subkey_path);
    }

    delete_tree_entry::delete_tree_entry(key_path const&                base_key_path,
                                         std::optional<key_path> const& subkey_path):
        m_base_key_path(base_key_path), m_subkey_path(subkey_path)
    {}

    void
    delete_tree_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("DeleteTree"sv));
        write_path_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        if (m_subkey_path.has_value())
            write_path_attribute(n, M_PUGIXML_T("subKey"sv), m_subkey_path.value());
    }

    rename_key_entry::rename_key_entry(key_path const&                base_key_path,
                                       std::optional<key_path> const& old_subkey_name,
                                       key_path const&                new_key_name):
        m_base_key_path(base_key_path),
        m_old_subkey_name(old_subkey_name),
        m_new_key_name(new_key_name)
    {}

    void
    rename_key_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("RenameKey"sv));
        write_path_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        if (m_old_subkey_name.has_value())
            write_path_attribute(n, M_PUGIXML_T("subKeyName"sv), m_old_subkey_name.value());
        write_path_attribute(n, M_PUGIXML_T("newKeyName"sv), m_new_key_name);
    }

    delete_value_entry::delete_value_entry(key_path const&               base_key_path,
                                           value_name_string_type const& value_name):
        m_base_key_path(base_key_path), m_value_name(value_name)
    {}

    void
    delete_value_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("DeleteValue"sv));
        write_path_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_value_name_attribute(n, M_PUGIXML_T("valueName"sv), m_value_name);
    }

    set_value_entry::set_value_entry(key_path const&               base_key_path,
                                     value_name_string_type const& value_name,
                                     reg_value_type                type,
                                     std::span<std::byte const>    value):
        m_base_key_path(base_key_path),
        m_value_name(value_name),
        m_type(type),
        m_value(value.begin(), value.end())
    {}

    void
    set_value_entry::save(pugi::xml_node& journal_node) const
    {
        auto n = journal_node.append_child(M_PUGIXML_T("SetValue"sv));
        write_path_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_value_name_attribute(n, M_PUGIXML_T("valueName"sv), m_value_name);

        // The journal stores the value's raw type and bytes so replay is exact
        // and lossless (the human-readable rendering is the logging layer's job).
        n.append_attribute(M_PUGIXML_T("type"sv))
            .set_value(static_cast<unsigned>(m::to_underlying(m_type)));
        n.append_attribute(M_PUGIXML_T("data"sv)).set_value(bytes_to_hex(m_value).c_str());
    }
} // namespace m::pil::impl::journaling
