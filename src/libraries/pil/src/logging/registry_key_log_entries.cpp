// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/to_underlying.h>

#include "logging.h"

namespace m::pil::impl::logging
{
    create_key_log_entry::create_key_log_entry(key_path const&                    base_key_path,
                                               ikey::create_key_flags             flags,
                                               key_path const&                    subkey_path,
                                               sam                                sam_desired,
                                               std::optional<security_attributes> sa):
        m_base_key_path(base_key_path),
        m_flags(flags),
        m_subkey_path(subkey_path),
        m_sam_desired(sam_desired),
        m_sa(sa)
    {}

    void
    create_key_log_entry::set_disposition(ikey::create_key_disposition disposition)
    {
        m_disposition = disposition;
    }

    void
    create_key_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Registry.CreateKey"sv));

        write_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_hex_attribute_omitting_default(n, "flags"sv, m_flags);
        write_attribute(n, "subKey"sv, m_subkey_path);
        write_hex_attribute_omitting_default(n, "samDesired"sv, m_sam_desired, sam{0x2000000});
        write_attribute(n, "disposition"sv, m_disposition);
    }

    delete_key_log_entry::delete_key_log_entry(key_path const&        base_key_path,
                                               ikey::delete_key_flags flags,
                                               key_path const&        subkey_path,
                                               sam                    sam_desired):
        m_base_key_path(base_key_path),
        m_flags(flags),
        m_subkey_path(subkey_path),
        m_sam_desired(sam_desired)
    {}

    void
    delete_key_log_entry::set_disposition(ikey::delete_key_disposition disposition)
    {
        m_disposition = disposition;
    }

    void
    delete_key_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Registry.DeleteKey"sv));

        write_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("subKey"sv), m_subkey_path);
        write_hex_attribute_omitting_default(
            n, M_PUGIXML_T("samDesired"sv), m_sam_desired, sam{0x2000000});
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    delete_tree_log_entry::delete_tree_log_entry(key_path const&                base_key_path,
                                                 ikey::delete_tree_flags        flags,
                                                 std::optional<key_path> const& subkey_path):
        m_base_key_path(base_key_path), m_flags(flags), m_subkey_path(subkey_path)
    {}

    void
    delete_tree_log_entry::set_disposition(ikey::delete_tree_disposition disposition)
    {
        m_disposition = disposition;
    }

    void
    delete_tree_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Registry.DeleteTree"sv));

        write_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("subKey"sv), m_subkey_path);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    rename_key_log_entry::rename_key_log_entry(key_path const&                base_key_path,
                                               ikey::rename_key_flags         flags,
                                               std::optional<key_path> const& sub_key_name,
                                               pil::key_path const&           new_key_name):
        m_base_key_path(base_key_path),
        m_flags(flags),
        m_sub_key_name(sub_key_name),
        m_new_key_name(new_key_name)
    {}

    void
    rename_key_log_entry::set_disposition(ikey::rename_key_disposition disposition)
    {
        m_disposition = disposition;
    }

    void
    rename_key_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Registry.RenameKey"sv));

        write_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("subKeyName"sv), m_sub_key_name);
        write_attribute(n, M_PUGIXML_T("newKeyName"sv), m_new_key_name);
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    delete_value_log_entry::delete_value_log_entry(key_path const&               base_key_path,
                                                   ikey::delete_value_flags      flags,
                                                   value_name_string_type const& value_name):
        m_base_key_path(base_key_path), m_flags(flags), m_value_name(value_name), m_disposition{}
    {}

    void
    delete_value_log_entry::set_disposition(ikey::delete_value_disposition disposition)
    {
        m_disposition = disposition;
    }

    void
    delete_value_log_entry::save(pugi::xml_node& log_node) const
    {
        auto n = log_node.append_child(M_PUGIXML_T("Registry.DeleteValue"sv));

        write_attribute(n, M_PUGIXML_T("key"sv), m_base_key_path);
        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);
        write_attribute(n, M_PUGIXML_T("valueName"sv), m_value_name.view());
        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    set_value_log_entry::set_value_log_entry(key_path const&               base_key_path,
                                             ikey::set_value_flags         flags,
                                             value_name_string_type const& value_name,
                                             reg_value_type                type,
                                             std::span<std::byte const>    value):
        m_base_key_path(base_key_path),
        m_flags(flags),
        m_value_name(value_name),
        m_type(type),
        m_value(value),
        m_disposition{}
    {
        //
    }

    void
    set_value_log_entry::set_disposition(ikey::set_value_disposition disposition)
    {
        m_disposition = disposition;
    }

    void
    set_value_log_entry::save(pugi::xml_node& log_node) const
    {
        //
        // The registry is at its heart a binary store, where clients usually store well formed
        // data. If the data is well formed, we can put it into XML and have some hope that
        // it can be nicely shown.
        //
        // If it cannot, we will need to handle it because the platform does, but it cannot be
        // written into the XML in the form that most people want to see, so we will drop back
        // to a binary format with some accompanying "ASCII" comments.
        //
        // For each "normal" registry data type, we will see if the data looks like it is
        // normally formed, and if it is, write it concisely. Otherwise, we will rely on the
        // binary in child elements.
        //

        bool handled = false;

        auto n = log_node.append_child(M_PUGIXML_T("Registry.SetValue"sv));

        auto key = n.append_attribute(M_PUGIXML_T("key"sv));
        key.set_value(m::to_string(m_base_key_path.native().view()).c_str());

        write_hex_attribute_omitting_default(n, M_PUGIXML_T("flags"sv), m_flags);

        write_attribute(n, M_PUGIXML_T("valueName"sv), m_value_name.view());

        auto type_a = n.append_attribute(M_PUGIXML_T("type"sv));

        switch (m_type)
        {
            case reg_value_type::string:
            {
                type_a.set_value(M_PUGIXML_T("REG_SZ"sv));

                //
                // Strings are always encoded as Utf-16 strings with a trailing pair of
                // null bytes.
                //

                if (data_is_utf16(m_value))
                {
                    auto data = n.append_attribute(M_PUGIXML_T("reg_sz_data"sv));
                    set_value_as_string(data, m_value);
                    handled = true;
                }

                break;
            }

            case reg_value_type::expand_string:
            {
                type_a.set_value(M_PUGIXML_T("REG_EXPAND_SZ"sv));

                //
                // Strings are always encoded as Utf-16 strings with a trailing pair of
                // null bytes.
                //

                if (data_is_utf16(m_value))
                {
                    auto data = n.append_attribute(M_PUGIXML_T("reg_expand_sz_data"sv));
                    set_value_as_string(data, m_value);

                    handled = true;
                }

                break;
            }

            case reg_value_type::uint32:
            {
                type_a.set_value(M_PUGIXML_T("REG_DWORD"sv));

                if (m_value.size() == sizeof(uint32_t))
                {
                    std::array<uint32_t, 1> v;

                    std::copy_n(m_value.begin(),
                                sizeof(uint32_t),
                                std::as_writable_bytes(std::span(v)).begin());

                    auto data = n.append_attribute(M_PUGIXML_T("reg_dword_data"sv));
                    data.set_value(std::format(M_PUGIXML_T("{:#x}"), v[0]).c_str());

                    handled = true;
                }

                break;
            }

            case reg_value_type::uint64:
            {
                type_a.set_value("REG_QWORD");

                if (m_value.size() == sizeof(uint64_t))
                {
                    std::array<uint64_t, 1> v;

                    std::copy_n(m_value.begin(),
                                sizeof(uint64_t),
                                std::as_writable_bytes(std::span(v)).begin());

                    auto data = n.append_attribute(M_PUGIXML_T("reg_qword_data"sv));
                    data.set_value(std::format(M_PUGIXML_T("{:#x}"), v[0]).c_str());

                    handled = true;
                }

                break;
            }

            case reg_value_type::binary:
            {
                type_a.set_value(M_PUGIXML_T("REG_BINARY"sv));

                save_binary(n);

                handled = true;

                break;
            }

            case reg_value_type::link:
            {
                type_a.set_value(M_PUGIXML_T("REG_LINK"sv));
                save_binary(n);
                handled = true;
                break;
            }

            case reg_value_type::multi_string:
            {
                type_a.set_value(M_PUGIXML_T("REG_MULTI_SZ"sv));
                save_binary(n);
                handled = true;
                break;
            }

            case reg_value_type::none:
            {
                type_a.set_value(M_PUGIXML_T("REG_NONE"sv));
                save_binary(n);
                handled = true;
                break;
            }

            case reg_value_type::uint32_big_endian:
            {
                type_a.set_value(M_PUGIXML_T("REG_DWORD_BIG_ENDIAN"sv));
                save_binary(n);
                handled = true;
                break;
            }

            default:
            {
                type_a.set_value(pugi::string_view_t(
                    std::format(M_PUGIXML_T("{:#x}"), m::to_underlying(m_type))));
                save_binary(n);
                handled = true;
                break;
            }
        }

        if (!handled)
            save_binary(n);

        write_attribute(n, M_PUGIXML_T("disposition"sv), m_disposition);
    }

    void
    set_value_log_entry::set_value_as_string(pugi::xml_attribute&              attr,
                                             std::span<std::byte const> const& s)
    {
        auto view = std::u16string_view(reinterpret_cast<char16_t const*>(s.data()),
                                        (s.size() - sizeof(char16_t)) - 1);

#ifdef WIN322
        auto tempstring = m::to_string_t<pugi::char_t>(view);
        attr.set_value(tempstring.data(), tempstring.size());
#else
        attr.set_value(M_PUGIXML_T("TO-DO"sv));
#endif
    }

    void
    set_value_log_entry::save_binary(pugi::xml_node& set_value_node) const
    {
        std::ignore = set_value_node;
    }

    bool
    set_value_log_entry::data_is_utf16(std::span<std::byte const> const& s)
    {
        auto const size = s.size();

        // If the size isn't even, it's not valid utf-16 data
        if ((size % sizeof(char16_t)) != 0)
            return false;

        // It has to have a trailing null character so if it's not at least one character
        // it's not utf-16 data.
        if (size < sizeof(char16_t))
            return false;

        auto const data  = reinterpret_cast<char16_t const*>(s.data());
        auto const chars = size / sizeof(char16_t);

        if (data[chars - 1] != u'\0')
            return false;

        // TODO: scan for invalid surrogate pairs

        return true;
    }
} // namespace m::pil::impl::logging
