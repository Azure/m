// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <m/strings/convert.h>

#include "journaling.h"

using namespace std::string_view_literals;

namespace m::pil::impl::journaling
{
    namespace
    {
        // Inverse of bytes_to_hex (journal_entries.cpp). Two lower-case hex
        // characters per byte, high nibble first. Throws if the string is
        // malformed (odd length or non-hex digit) so a corrupt journal fails
        // loudly rather than replaying garbage.
        unsigned
        hex_nibble(wchar_t c)
        {
            if (c >= L'0' && c <= L'9')
                return static_cast<unsigned>(c - L'0');
            if (c >= L'a' && c <= L'f')
                return static_cast<unsigned>(c - L'a') + 10u;
            if (c >= L'A' && c <= L'F')
                return static_cast<unsigned>(c - L'A') + 10u;
            throw std::runtime_error("Invalid hex digit in journal SetValue data");
        }

        std::vector<std::byte>
        hex_to_bytes(std::wstring_view hex)
        {
            static constexpr unsigned k_nibble_shift = 4;

            if ((hex.size() % 2) != 0)
                throw std::runtime_error("Odd-length hex string in journal SetValue data");

            std::vector<std::byte> out;
            out.reserve(hex.size() / 2);
            for (std::size_t i = 0; i < hex.size(); i += 2)
            {
                auto const hi = hex_nibble(hex[i]);
                auto const lo = hex_nibble(hex[i + 1]);
                out.push_back(std::byte{static_cast<std::uint8_t>((hi << k_nibble_shift) | lo)});
            }
            return out;
        }

        // Parse an absolute key path (including its predefined-root prefix) from
        // a journal attribute. Returns a key_path whose root_key() is populated.
        key_path
        parse_path_attribute(pugi::xml_attribute const& attr)
        {
            auto const u16 = m::to_u16string(attr.as_string());
            return key_path{key_path::view_type{u16}};
        }

        // Parse a relative subkey path (no predefined-root prefix) from a journal
        // attribute.
        key_path
        parse_relative_attribute(pugi::xml_attribute const& attr)
        {
            auto const u16 = m::to_u16string(attr.as_string());
            return key_path{key_path::view_type{u16}};
        }

        value_name_string_type
        parse_value_name_attribute(pugi::xml_attribute const& attr)
        {
            return value_name_string_type{m::to_u16string(attr.as_string())};
        }

        // Resolve (creating if necessary) the base key an entry was invoked on.
        // The entry stores the base key's absolute path; replay opens its
        // predefined root and then descends the relative path one segment at a
        // time, creating each segment (create_key is idempotent and, in the
        // base world here, accepts only single-segment names). This is safe
        // whether or not the keys already exist in the target world.
        std::shared_ptr<ikey>
        resolve_base_key(pugi::xml_node const& entry, iregistry& target_registry)
        {
            auto const base_path = parse_path_attribute(entry.attribute(M_PUGIXML_T("key"sv)));

            auto const root = base_path.root_key();
            if (!root.has_value())
                throw std::runtime_error("Journal entry key path has no predefined root");

            auto current = target_registry.open_predefined_key(root.value());

            auto const               relative = base_path.relative_path();
            std::u16string_view const view{relative.view()};

            std::size_t start = 0;
            while (start < view.size())
            {
                auto const pos = view.find(u'\\', start);
                auto const seg =
                    view.substr(start, pos == std::u16string_view::npos ? pos : pos - start);
                if (!seg.empty())
                    current = current->create_key(key_path{key_path::view_type{seg}});
                if (pos == std::u16string_view::npos)
                    break;
                start = pos + 1;
            }

            return current;
        }
    } // namespace

    void
    replay(pugi::xml_node const& journal_node, iregistry& target_registry)
    {
        for (auto entry = journal_node.first_child(); entry; entry = entry.next_sibling())
        {
            std::wstring_view const name{entry.name()};
            auto                    base_key = resolve_base_key(entry, target_registry);

            if (name == L"CreateKey"sv)
            {
                base_key->create_key(parse_relative_attribute(entry.attribute(M_PUGIXML_T("subKey"sv))));
            }
            else if (name == L"DeleteKey"sv)
            {
                base_key->delete_key(parse_relative_attribute(entry.attribute(M_PUGIXML_T("subKey"sv))));
            }
            else if (name == L"DeleteTree"sv)
            {
                std::optional<key_path> subkey;
                if (auto const a = entry.attribute(M_PUGIXML_T("subKey"sv)))
                    subkey = parse_relative_attribute(a);
                base_key->delete_tree(subkey);
            }
            else if (name == L"RenameKey"sv)
            {
                std::optional<key_path> old_name;
                if (auto const a = entry.attribute(M_PUGIXML_T("subKeyName"sv)))
                    old_name = parse_relative_attribute(a);
                base_key->rename_key(
                    old_name, parse_relative_attribute(entry.attribute(M_PUGIXML_T("newKeyName"sv))));
            }
            else if (name == L"DeleteValue"sv)
            {
                base_key->delete_value(
                    parse_value_name_attribute(entry.attribute(M_PUGIXML_T("valueName"sv))));
            }
            else if (name == L"SetValue"sv)
            {
                auto const value_name =
                    parse_value_name_attribute(entry.attribute(M_PUGIXML_T("valueName"sv)));
                auto const type = static_cast<reg_value_type>(
                    entry.attribute(M_PUGIXML_T("type"sv)).as_uint());
                auto const bytes =
                    hex_to_bytes(std::wstring_view{entry.attribute(M_PUGIXML_T("data"sv)).as_string()});

                base_key->set_value(
                    ikey::set_value_flags{}, value_name, type, std::span<std::byte const>{bytes});
            }
            else
            {
                throw std::runtime_error("Unknown journal entry element during replay");
            }
        }
    }

    namespace
    {
        file_path
        parse_fs_path_attribute(pugi::xml_attribute const& attr)
        {
            auto const u16 = m::to_u16string(attr.as_string());
            return file_path{file_path::view_type{u16}};
        }

        // Resolve (creating if necessary) the directory a filesystem verb was
        // invoked on. The entry stores that directory's absolute path; replay
        // opens its root and descends the relative path one segment at a time,
        // opening each existing segment or creating it when absent. This is safe
        // whether or not the directories already exist in the target world.
        std::shared_ptr<idirectory>
        resolve_base_directory(pugi::xml_node const& entry, ifilesystem& target_filesystem)
        {
            auto const dir_path = parse_fs_path_attribute(entry.attribute(M_PUGIXML_T("dir"sv)));

            // Traverse with read access so opening existing (possibly protected)
            // ancestor directories never requests write rights it does not need;
            // a genuinely missing segment is created with create access. The
            // win32 provider reopens each child by full path, so the access used
            // to open an ancestor does not constrain creating entries under it.
            auto current = target_filesystem.open_root(dir_path.root(), file_access::default_open);

            auto const               relative = dir_path.relative_path();
            std::u16string_view const view{relative.view()};

            std::size_t start = 0;
            while (start < view.size())
            {
                auto const pos = view.find_first_of(u"\\/", start);
                auto const seg =
                    view.substr(start, pos == std::u16string_view::npos ? pos : pos - start);
                if (!seg.empty())
                {
                    file_path const seg_path{file_path::view_type{seg}};
                    auto child = current->try_open_directory(seg_path, file_access::default_open);
                    if (!child)
                        child = current->create_directory(seg_path, file_access::default_create);
                    current = child;
                }
                if (pos == std::u16string_view::npos)
                    break;
                start = pos + 1;
            }

            return current;
        }
    } // namespace

    void
    replay(pugi::xml_node const& journal_node, ifilesystem& target_filesystem)
    {
        for (auto entry = journal_node.first_child(); entry; entry = entry.next_sibling())
        {
            std::wstring_view const name{entry.name()};

            if (name == L"Filesystem.CreateDirectory"sv)
            {
                auto base = resolve_base_directory(entry, target_filesystem);
                base->create_directory(parse_fs_path_attribute(entry.attribute(M_PUGIXML_T("path"sv))),
                                       file_access::default_create);
            }
            else if (name == L"Filesystem.CreateFile"sv)
            {
                auto base = resolve_base_directory(entry, target_filesystem);
                base->create_file(parse_fs_path_attribute(entry.attribute(M_PUGIXML_T("path"sv))),
                                  file_access::default_create);
            }
            else if (name == L"Filesystem.Remove"sv)
            {
                auto base = resolve_base_directory(entry, target_filesystem);
                base->remove_entry(parse_fs_path_attribute(entry.attribute(M_PUGIXML_T("name"sv))));
            }
            else if (name == L"Filesystem.DeleteTree"sv)
            {
                auto                     base = resolve_base_directory(entry, target_filesystem);
                std::optional<file_path> nm;
                if (auto const a = entry.attribute(M_PUGIXML_T("name"sv)))
                    nm = parse_fs_path_attribute(a);
                base->delete_tree(nm);
            }
            else if (name == L"Filesystem.Rename"sv)
            {
                auto base = resolve_base_directory(entry, target_filesystem);
                base->rename_entry(
                    parse_fs_path_attribute(entry.attribute(M_PUGIXML_T("oldPath"sv))),
                    parse_fs_path_attribute(entry.attribute(M_PUGIXML_T("newPath"sv))));
            }
            // Non-filesystem entries are ignored by the filesystem replay.
        }
    }
} // namespace m::pil::impl::journaling
