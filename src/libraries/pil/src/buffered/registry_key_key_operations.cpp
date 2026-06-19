// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/platform_adaptive_strings/convert.h>
#include <m/strings/convert.h>
#include <m/tracing/tracing.h>
#include <m/utility/make_span.h>

#include "buffered.h"

namespace
{
    // Lower-case hexadecimal encoding of a byte span, used to serialize
    // registry value data into an XML attribute. Each byte becomes exactly
    // two characters, high nibble first. The result is a wide string because
    // pugixml is built in wchar mode in this repository.
    std::wstring
    bytes_to_hex(std::span<std::byte const> bytes)
    {
        static constexpr wchar_t  k_hex_digits[]  = L"0123456789abcdef";
        static constexpr unsigned k_nibble_shift   = 4;
        static constexpr unsigned k_nibble_mask    = 0x0Fu;

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

    // Decode a single hex digit. Throws on any non-hex character.
    unsigned
    hex_nibble(wchar_t c)
    {
        static constexpr unsigned k_decimal_radix = 10;
        if (c >= L'0' && c <= L'9')
            return static_cast<unsigned>(c - L'0');
        if (c >= L'a' && c <= L'f')
            return static_cast<unsigned>(c - L'a') + k_decimal_radix;
        if (c >= L'A' && c <= L'F')
            return static_cast<unsigned>(c - L'A') + k_decimal_radix;
        throw m::invalid_parameter("buffered key load: invalid hex digit in value data");
    }

    // Inverse of bytes_to_hex: decode an even-length hex string into bytes.
    std::vector<std::byte>
    hex_to_bytes(std::wstring_view hex)
    {
        static constexpr unsigned k_nibble_shift = 4;
        if ((hex.size() % 2) != 0)
            throw m::invalid_parameter("buffered key load: odd-length hex value data");

        std::vector<std::byte> out;
        out.reserve(hex.size() / 2);
        for (std::size_t i = 0; i < hex.size(); i += 2)
        {
            auto const hi = hex_nibble(hex[i]);
            auto const lo = hex_nibble(hex[i + 1]);
            out.push_back(static_cast<std::byte>((hi << k_nibble_shift) | lo));
        }
        return out;
    }
} // namespace

namespace m::pil::impl::buffered
{
    key::key(key_path const& path, time_point_type last_write_time):
        m_last_write_time(last_write_time), m_key_path(path)
    {}

    key::key(std::shared_ptr<ikey> const& underlying_key): m_underlying_key(underlying_key)
    {
        initialize_overlay();
    }

    key::key(std::shared_ptr<ikey>&& underlying_key) noexcept:
        m_underlying_key(std::move(underlying_key))
    {
        initialize_overlay();
    }

    void
    key::initialize_overlay()
    {
        // M-PS-2: capture the whole key at materialization ("touch"). We record
        // the underlying key's last_write_time, enumerate its subkey names and
        // value names/types, and eagerly load every value's data whole, so the
        // buffered overlay becomes a self-contained snapshot of the key (D2-D4).
        //
        // Best-effort consistency: the registry can change underneath us with no
        // synchronization, so we bracket the capture with last_write_time reads
        // and re-capture if the key changed during enumeration. A bounded retry
        // count keeps this from spinning; this is best-effort, not transactional.
        if (!m_underlying_key)
            return;

        static constexpr unsigned k_max_capture_attempts = 3;

        for (unsigned attempt = 1;; ++attempt)
        {
            m_keys.clear();
            m_values.clear();

            auto const before = query_underlying_last_write_time();

            initialize_keys_overlay();
            initialize_values_overlay();
            load_all_mirrored_values();

            auto const after = query_underlying_last_write_time();

            m_last_write_time = after;

            if (before == after || attempt >= k_max_capture_attempts)
                break;
        }
    }

    time_point_type
    key::query_underlying_last_write_time() const
    {
        std::size_t     subkey_count{};
        std::size_t     value_count{};
        std::size_t     security_descriptor_size{};
        time_point_type last_write_time{(time_point_type::min)()};

        auto const d = m_underlying_key->query_information_key(query_information_key_flags{},
                                                              subkey_count,
                                                              value_count,
                                                              security_descriptor_size,
                                                              last_write_time);
        M_INTERNAL_ERROR_CHECK(!d);

        return last_write_time;
    }

    void
    key::load_all_mirrored_values()
    {
        for (auto it = m_values.begin(); it != m_values.end();)
        {
            try
            {
                load_value_if_not_present(it->first, it->second);
                ++it;
            }
            catch (m::not_found const&)
            {
                // The value vanished from the underlying registry between
                // enumeration and load. Drop it from the captured set rather
                // than treat it as an error (best-effort, D4).
                it = m_values.erase(it);
            }
        }
    }

    void
    key::initialize_keys_overlay()
    {
        if (!m_underlying_key)
            return;

        std::array<pil::key_path, 32>                 key_array;
        std::span<pil::key_path, std::dynamic_extent> key_span{key_array};
        std::size_t                                   index{};

        for (;;)
        {
            auto const d =
                m_underlying_key->enumerate_keys(enumerate_keys_flags{}, index, key_span);
            M_INTERNAL_ERROR_CHECK(!d); // no flags in, no disposition out

            for (auto&& e: key_span)
                m_keys.emplace(e.native(),
                               key_node{.m_key             = {},
                                        .m_last_write_time = {},
                                        .m_deleted         = false,
                                        .m_mirrored        = true});

            if (key_span.size() != key_array.size())
                break;

            index += key_array.size();
        }
    }

    void
    key::save_xml(pugi::xml_node& parent) const
    {
        auto l = std::unique_lock(m_mutex);

        //
        // Serialize this key as a whole-key snapshot into the supplied <Key>
        // element (D2, D3): its own metadata, every captured value, and every
        // child subkey name. A set value becomes a <Value> child; a captured
        // (eagerly loaded) value carries its data whole. A materialized subkey
        // recurses into a nested whole <Key>; a mirrored-but-unopened subkey
        // contributes only its name, since its contents were never captured.
        // Deleted entries are emitted as tombstones (deleted="true"). Saving
        // must never force fresh reads from an underlying registry.
        //

        // M-PS-3: persist this key's own metadata. The last_write_time doubles
        // as the version stamp used by lazy consistency repair on load (D5).
        if (m_last_write_time != (time_point_type::min)())
            parent.append_attribute(M_PUGIXML_T("last_write_time"sv))
                .set_value(
                    static_cast<long long>(m_last_write_time.time_since_epoch().count()));

        for (auto const& [value_name, vnode]: m_values)
        {
            auto value_element = parent.append_child(M_PUGIXML_T("Value"sv));
            value_element.append_attribute(M_PUGIXML_T("name"sv))
                .set_value(m::to_wstring(value_name.view()).c_str());

            if (vnode.m_deleted)
            {
                value_element.append_attribute(M_PUGIXML_T("deleted"sv)).set_value(true);
                continue;
            }

            if (!vnode.m_value.has_value())
            {
                // Mirrored placeholder that was never loaded: no local data
                // to persist. Drop the element we optimistically created.
                parent.remove_child(value_element);
                continue;
            }

            value_element.append_attribute(M_PUGIXML_T("type"sv))
                .set_value(static_cast<unsigned>(std::to_underlying(vnode.m_reg_value_type)));
            value_element.append_attribute(M_PUGIXML_T("data"sv))
                .set_value(bytes_to_hex(vnode.m_value.value()).c_str());
        }

        for (auto const& [subkey_name, knode]: m_keys)
        {
            auto key_element = parent.append_child(M_PUGIXML_T("Key"sv));
            key_element.append_attribute(M_PUGIXML_T("name"sv))
                .set_value(m::to_wstring(subkey_name.view()).c_str());

            if (knode.m_deleted)
            {
                key_element.append_attribute(M_PUGIXML_T("deleted"sv)).set_value(true);
                continue;
            }

            // A materialized subkey serializes its whole self recursively; a
            // mirrored-but-unopened subkey (D3) contributes only its name plus a
            // mirrored marker so the loader restores it as an unmaterialized
            // placeholder (D5) rather than fabricating an empty captured key.
            if (knode.m_key)
                knode.m_key->save_xml(key_element);
            else
                key_element.append_attribute(M_PUGIXML_T("mirrored"sv)).set_value(true);
        }
    }

    void
    key::load_children_xml(pugi::xml_node const& key_element, time_point_type load_stamp)
    {
        //
        // This key is freshly constructed by the snapshot loader and not yet
        // published to other threads, so no lock is taken. Each <Value> child
        // becomes a fully-materialized value. A non-deleted <Key> child becomes
        // either a fully-materialized subkey (its contents were captured) or, if
        // it carries the mirrored marker, an unmaterialized name-only
        // placeholder that enumerates but cannot be opened in the sealed world
        // until lazy consistency repair drops it (D5). Deleted entries are
        // reconstructed as tombstones.
        //

        // D5: remember T_load so a later repair can restamp this key.
        m_load_stamp = load_stamp;

        // M-PS-3/M-PS-4: restore this key's own metadata. An absent attribute
        // (name-only placeholder, older artifact) leaves the stamp at min.
        m_last_write_time = time_point_type(time_point_type::duration(
            key_element.attribute(M_PUGIXML_T("last_write_time"sv))
                .as_llong(static_cast<long long>(
                    (time_point_type::min)().time_since_epoch().count()))));

        for (auto child = key_element.first_child(); child; child = child.next_sibling())
        {
            auto const node_name = std::wstring_view(child.name());

            if (node_name == M_PUGIXML_T("Value"sv))
            {
                auto const value_name = m::u16sstring(m::to_u16string(
                    std::wstring_view(child.attribute(M_PUGIXML_T("name"sv)).as_string())));

                if (child.attribute(M_PUGIXML_T("deleted"sv)).as_bool(false))
                {
                    m_values.emplace(value_name,
                                     value_node{.m_reg_value_type = reg_value_type::none,
                                                .m_value           = std::nullopt,
                                                .m_deleted         = true});
                    continue;
                }

                auto const type = static_cast<reg_value_type>(
                    child.attribute(M_PUGIXML_T("type"sv)).as_uint());
                auto data = hex_to_bytes(
                    std::wstring_view(child.attribute(M_PUGIXML_T("data"sv)).as_string()));

                m_values.emplace(value_name,
                                 value_node{.m_reg_value_type = type,
                                            .m_value           = std::move(data),
                                            .m_deleted         = false});
            }
            else if (node_name == M_PUGIXML_T("Key"sv))
            {
                auto const subkey_name = m::u16sstring(m::to_u16string(
                    std::wstring_view(child.attribute(M_PUGIXML_T("name"sv)).as_string())));

                if (child.attribute(M_PUGIXML_T("deleted"sv)).as_bool(false))
                {
                    m_keys.emplace(subkey_name,
                                   key_node{.m_key             = {},
                                            .m_last_write_time = (time_point_type::min)(),
                                            .m_deleted         = true,
                                            .m_mirrored        = false});
                    continue;
                }

                // A name-only placeholder (D3): the subkey name was observed but
                // its contents were never captured. Restore it as an
                // unmaterialized mirrored entry so it enumerates but is not a
                // fabricated empty key; opening it in the sealed world triggers
                // lazy consistency repair (D5).
                if (child.attribute(M_PUGIXML_T("mirrored"sv)).as_bool(false))
                {
                    m_keys.emplace(subkey_name,
                                   key_node{.m_key             = {},
                                            .m_last_write_time = (time_point_type::min)(),
                                            .m_deleted         = false,
                                            .m_mirrored        = true});
                    continue;
                }

                auto child_path = m_key_path + key_path(subkey_name.view());
                auto child_key =
                    std::make_shared<key>(child_path, (time_point_type::min)());
                child_key->load_children_xml(child, load_stamp);

                m_keys.emplace(subkey_name,
                               key_node{.m_key             = std::move(child_key),
                                        .m_last_write_time = (time_point_type::min)(),
                                        .m_deleted         = false,
                                        .m_mirrored        = false});
            }
        }
    }

    ikey::create_key_disposition
    key::create_key(ikey::create_key_flags             flags,
                    pil::key_path const&               key_name,
                    sam                                sam_desired,
                    std::optional<security_attributes> security,
                    std::shared_ptr<ikey>&             returned_key)
    {
        returned_key.reset();

        if (flags != create_key_flags{})
            throw m::invalid_parameter("ikey::create_key.flags");

        // Live RegCreateKeyExW auto-creates every intermediate key in a
        // multi-component path. Walk the path one level at a time: create (or
        // open) the leading component under this key using the single-component
        // logic below, then recurse for the remainder, returning the leaf. This
        // also makes re-creating an existing multi-level path idempotent because
        // each level inherits the single-component create-or-open semantics.
        if (key_name.has_parent_path())
        {
            std::shared_ptr<ikey> intermediate_key;

            create_key(flags, key_name.root(), sam_desired, security, intermediate_key);

            M_INTERNAL_ERROR_CHECK(static_cast<bool>(intermediate_key));

            return intermediate_key->create_key(
                flags, key_path{key_name.relative_path()}, sam_desired, security, returned_key);
        }

        auto const entry_time = time_point_type::clock::now();

        key_path full_path = ikey::get_path() + key_name;

        auto lock = std::unique_lock(m_mutex);

        //
        // For a creation, first see if we have a local definition, including
        // a tombstone. If we do, we can proceed with the creation without
        // checking in with the underlying registry, if there is one.
        //
        // If we don't already have a key registered, we may have to check
        // with the underlying registry to see if we do not have access to the
        // key and have to fail the creation on that basis.
        //
        auto [insertion_it, inserted] = m_keys.emplace(
            std::make_pair(key_name.string(),
                           key_node{.m_key = std::make_shared<key>(full_path, entry_time),
                                    .m_last_write_time = entry_time,
                                    .m_deleted         = false,
                                    .m_mirrored        = false}));

        if (inserted)
        {
            returned_key = insertion_it->second.m_key;
            return create_key_disposition{};
        }

        M_INTERNAL_ERROR_CHECK(insertion_it != m_keys.end());

        auto& node = insertion_it->second;

        if (node.m_deleted)
        {
            M_INTERNAL_ERROR_CHECK(!node.m_mirrored); // If the node was made a tombstone, it should
                                                      // have been marked as not mirrored any more

            node.m_key     = std::make_shared<key>(full_path, node.m_last_write_time);
            node.m_deleted = false;
        }
        else if (node.m_mirrored)
        {
            // Mirrored keys may not have been materialized yet.
            if (!node.m_key)
            {
                if (!m_underlying_key)
                {
                    // Sealed snapshot: there is no underlying registry to
                    // materialize from. create_key's create-or-open semantics
                    // make a fresh empty key in the placeholder's place.
                    node.m_key = std::make_shared<key>(full_path, entry_time);
                }
                else
                {
                    std::shared_ptr<ikey> child_key{};

                    try
                    {
                        child_key = m_underlying_key->open_key(key_name, sam_desired);
                    }
                    catch (m::not_found const&)
                    {
                        // if the key is not found we are ok with this. It means
                        // that since the enumeration happened when this key
                        // was created and when the child key was opened, the subkey
                        // was deleted. Probably unusual but nonetheless it can
                        // happen.
                        //
                        // It would still be better to have a contract with open_key to
                        // not throw when the key is not found and have a disposition
                        // but for now this is acceptably scoped.
                    }

                    node.m_key = std::make_shared<key>(child_key);
                }
            }

            node.m_mirrored = false;
        }

        node.m_last_write_time = entry_time;
        returned_key           = node.m_key;

        return create_key_disposition{};
    }

    ikey::delete_key_disposition
    key::delete_key(ikey::delete_key_flags flags, pil::key_path const& key_name, sam)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::delete_key", flags);

        if (key_name.has_parent_path())
            throw m::invalid_parameter("ikey::delete_key.key_name");

        auto lock = std::unique_lock(m_mutex);

        auto const find_result = m_keys.find(key_name.native());

        if (find_result == m_keys.end())
            throw m::not_found("ikey::delete_key() registry key not found");

        auto& node = find_result->second;

        if (node.m_deleted)
            throw m::not_found("ikey::delete_key() registry key not found");

        if (!is_subkey_empty(key_name))
            throw m::not_empty("ikey::delete_key() subkey not empty");

        node.m_deleted  = true;
        node.m_mirrored = false;

        return delete_key_disposition{};
    }

    ikey::delete_tree_disposition
    key::delete_tree(ikey::delete_tree_flags flags, std::optional<pil::key_path> const& name)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::delete_tree", flags);

        auto lock = std::unique_lock(m_mutex);

        // No name (or empty name): delete the contents of this key — every
        // subkey and value — but leave this key itself in place. Tombstone each
        // live overlay node so the emptied state shadows any underlying
        // registry, mirroring how delete_key / delete_value tombstone.
        if (!name.has_value() || name.value().native().empty())
        {
            for (auto& [subkey_name, node]: m_keys)
            {
                node.m_key.reset();
                node.m_deleted  = true;
                node.m_mirrored = false;
            }

            for (auto& [value_name, value]: m_values)
            {
                value.m_value.reset();
                value.m_deleted = true;
            }

            return delete_tree_disposition{};
        }

        if (name.value().has_parent_path())
            throw m::invalid_parameter("ikey::delete_tree.key_name");

        auto const find_result = m_keys.find(name.value().native());

        if (find_result == m_keys.end())
            throw m::not_found("ikey::delete_tree() registry key not found");

        auto& node = find_result->second;

        if (node.m_deleted)
            throw m::not_found("ikey::delete_tree() registry key not found");

        // Unlike delete_key, delete_tree removes the named subkey together with
        // all of its descendants, so there is no "subkey must be empty" check.
        // Tombstoning the subkey node hides the whole subtree at once: any
        // materialized child key object becomes unreachable, and any
        // mirrored-but-unmaterialized contents in an underlying registry are
        // shadowed by the tombstone. The descendants need not be visited
        // individually because nothing can reach past the tombstoned parent.
        node.m_key.reset();
        node.m_deleted  = true;
        node.m_mirrored = false;

        return delete_tree_disposition{};
    }

    ikey::enumerate_keys_disposition
    key::enumerate_keys(ikey::enumerate_keys_flags                     flags,
                        std::size_t                                    index,
                        std::span<pil::key_path, std::dynamic_extent>& key_names)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::enumerate_keys", flags);

        auto const  lock = std::unique_lock(m_mutex);
        auto        it   = m_keys.begin();
        std::size_t span_index{};

        while (it != m_keys.end() && span_index < key_names.size())
        {
            // We have to burn through the 'index' input argument
            // iterations first
            if (index != 0)
            {
                it++;
                index--;
                continue;
            }

            key_names[span_index] = pil::key_path(it->first);
            span_index++;
            it++;
        }

        M_INTERNAL_ERROR_CHECK(span_index <= key_names.size());

        key_names = key_names.subspan(0, span_index);

        return enumerate_keys_disposition{};
    }

    ikey::flush_disposition
    key::flush(ikey::flush_flags flags)
    {
        M_API_PARAMETER_MUST_BE_ZERO("ikey::flush", flags);

        auto lock = std::unique_lock(m_mutex);

        if (m_underlying_key)
            m_underlying_key->flush();

        return flush_disposition{};
    }

    ikey::open_key_disposition
    key::open_key(ikey::open_key_flags                flags,
                  std::optional<pil::key_path> const& key_name,
                  sam                                 sam_desired,
                  std::shared_ptr<ikey>&              returned_key,
                  std::error_code&                    ec)
    {
        ec.clear();
        returned_key.reset();

        M_VALIDATE_FLAGS_PARAMETER(flags, open_key_flags::tolerate_not_found);

        auto const tolerate_not_found = !!(flags & open_key_flags::tolerate_not_found);

        if (key_name.has_value() && key_name.value().has_parent_path())
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return open_key_disposition{};
        }

        auto lock = std::unique_lock(m_mutex);

        // key name not present is "reopen key with different security attributes"
        // we don't do anything with security today so just give a new reference
        // to the same object. To really implement security correctly we will need
        // to introduce a "handle" layer in front of the objects which is a whole
        // new level of rearchitecture.
        if (!key_name)
        {
            returned_key = shared_from_this();
            return open_key_disposition{};
        }

        auto find_iter = m_keys.find(key_name.value().native());

        if (find_iter == m_keys.end())
        {
            if (tolerate_not_found)
                return open_key_disposition{open_key_result_code::key_not_found};

            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return open_key_disposition{};
        }

        auto& node = find_iter->second;

        if (node.m_deleted)
        {
            if (tolerate_not_found)
                return open_key_disposition{open_key_result_code::key_not_found};

            ec = std::make_error_code(std::errc::no_such_file_or_directory);
            return open_key_disposition{};
        }

        if (node.m_mirrored)
        {
            // Mirrored keys may not have been materialized yet.
            if (!node.m_key)
            {
                if (!m_underlying_key)
                {
                    // D5 lazy consistency repair: a sealed snapshot enumerates
                    // this subkey by name but its contents were never captured
                    // and there is no underlying registry to consult. Drop it
                    // from the enumeration and advance this key's version stamp
                    // to T_load so the snapshot stays self-consistent.
                    m_keys.erase(find_iter);
                    m_last_write_time = m_load_stamp;

                    if (tolerate_not_found)
                        return open_key_disposition{open_key_result_code::key_not_found};

                    ec = std::make_error_code(std::errc::no_such_file_or_directory);
                    return open_key_disposition{};
                }

                std::shared_ptr<ikey> child_key{};
                std::error_code       child_ec;

                m_underlying_key->open_key(
                    open_key_flags{}, key_name, sam_desired, child_key, child_ec);

                // if the key is not found we are ok with this. It means
                // that since the enumeration happened when this key
                // was created and when the child key was opened, the subkey
                // was deleted. Probably unusual but nonetheless it can
                // happen. Any other error is propagated to the caller.
                if (child_ec && child_ec != std::errc::no_such_file_or_directory)
                {
                    ec = child_ec;
                    return open_key_disposition{};
                }

                node.m_key = std::make_shared<key>(child_key);
            }

            node.m_mirrored = false;
        }

        returned_key = node.m_key;

        return open_key_disposition{};
    }

    ikey::query_information_key_disposition
    key::query_information_key(ikey::query_information_key_flags flags,
                               std::size_t&                      subkey_count,
                               std::size_t&                      value_count,
                               std::size_t&                      security_descriptor_size,
                               m::pil::time_point_type&          last_write_time)
    {
        subkey_count             = 0;
        value_count              = 0;
        security_descriptor_size = 0;
        last_write_time          = (time_point_type::min)();
        M_API_PARAMETER_MUST_BE_ZERO("ikey::query_information_key", flags);

        auto lock = std::unique_lock(m_mutex);

        subkey_count             = m_keys.size();
        value_count              = m_values.size();
        security_descriptor_size = m_security_descriptor.size();
        last_write_time          = m_last_write_time;

        return query_information_key_disposition{};
    }

    ikey::rename_key_disposition
    key::rename_key(ikey::rename_key_flags              flags,
                    std::optional<pil::key_path> const& old_key_name,
                    pil::key_path const&                new_key_name)
    {
        auto const entry_time = time_point_type::clock::now();

        M_API_PARAMETER_MUST_BE_ZERO("ikey::rename_key", flags);

        // we have no way to navigate to our parent in the hierarchy in the
        // buffered registry, at least not now, so with the buffered
        // registry throw the not-supported exception if the old_name is
        // omitted.
        if (!old_key_name)
            throw m::not_supported("buffered registry does not support rename_key(self, new-name)");

        auto const& old_key_name_value = old_key_name.value();

        if (old_key_name_value.has_parent_path())
            throw m::invalid_parameter("ikey::rename_key.old_key_name");

        if (new_key_name.has_parent_path())
            throw m::invalid_parameter("ikey::rename_key.new_key_name");

        auto lock = std::unique_lock(m_mutex);

        auto const find_iter = m_keys.find(old_key_name_value.native());

        if (find_iter == m_keys.end())
            throw m::not_found("ikey::rename_key(): Key not found for rename");

        auto& node = find_iter->second;
        if (node.m_deleted)
            throw m::not_found("ikey::rename_key(): Key not found for rename");

        key_node new_node{.m_key             = node.m_key,
                          .m_last_write_time = entry_time,
                          .m_deleted         = false,
                          .m_mirrored        = false};

        auto const [new_it, succeeded] = m_keys.emplace(new_key_name.string(), new_node);

        if (!succeeded)
        {
            M_INTERNAL_ERROR_CHECK(new_it != m_keys.end());

            auto& colliding_node = new_it->second;

            if (colliding_node.m_deleted)
            {
                colliding_node.m_deleted         = false;
                colliding_node.m_mirrored        = false;
                colliding_node.m_key             = node.m_key;
                colliding_node.m_last_write_time = entry_time;

                // We have taken over the new node!
            }
            else
            {
                colliding_node.m_deleted  = true;
                colliding_node.m_mirrored = false;
                colliding_node.m_key.reset();
            }
        }

        return rename_key_disposition{};
    }

    bool
    key::is_subkey_empty(pil::key_path const& key_name)
    {
        auto const it = m_keys.find(key_name.native());

        if (it == m_keys.end())
            return true;

        auto& node = it->second;

        if (node.m_deleted)
            return true;

        if (!node.m_key)
        {
            M_INTERNAL_ERROR_CHECK(node.m_mirrored);

            unmirror_node(key_name, node);

            if (node.m_mirrored)
            {
                auto const& key_name_string = key_name.native();
                auto const  c_str           = key_name_string.c_str();
                auto const  ws              = m::to_wstring(c_str);

                using namespace std::string_literals;
                m::wtrace(L"Attempt to unmirror subkey {} left the subkey mirrored", ws);
                return false;
            }
        }

        pil::key_path                                 subkey_name;
        std::span<pil::key_path, std::dynamic_extent> subkey_name_span(&subkey_name, 1);

        auto const d = node.m_key->enumerate_keys(enumerate_keys_flags{}, 0, subkey_name_span);
        M_INTERNAL_ERROR_CHECK(!d);

        return subkey_name_span.size() == 0;
    }

    void
    key::unmirror_node(pil::key_path const& key_name, key_node& node)
    {
        std::ignore = key_name;
        std::ignore = node;
        // to do
    }

    ikey::get_path_disposition
    key::get_path(ikey::get_path_flags flags, m::pil::key_path& path_out)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, get_path_flags{});

        auto l = std::unique_lock(m_mutex);

        auto underlying_key = m_underlying_key;
        if (underlying_key)
        {
            l.unlock();
            return underlying_key->get_path(flags, path_out);
        }

        path_out = m_key_path;

        return get_path_disposition{};
    }
} // namespace m::pil::impl::buffered
