// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/optional/optional.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/strings/split.h>
#include <m/utility/make_span.h>

namespace m::pil
{
    key::key(key&& other) noexcept
    {
        using std::swap;
        swap(m_key, other.m_key);
    }

    key::key(key const& other): m_key(other.m_key) {}

    key::key(std::shared_ptr<ikey>&& key) noexcept: m_key(std::move(key)) {}

    key&
    key::operator=(key const& other)
    {
        m_key.reset(other.m_key.get());
        return *this;
    }

    key&
    key::operator=(key&& other) noexcept
    {
        using std::swap;
        swap(m_key, other.m_key);
        return *this;
    }

    key
    key::do_create_key(pil::key_path const& key_name)
    {
        std::shared_ptr<ikey> return_value;

        auto const d = m_key->create_key(ikey::create_key_flags{},
                                         key_name,
                                         sam::default_create_key,
                                         std::nullopt,
                                         return_value);
        M_INTERNAL_ERROR_CHECK(!d);

        return key(std::move(return_value));
    }

    std::vector<key_path>
    key::list_subkey_names()
    {
        std::vector<key_path> result;

        std::size_t index{};

        std::array<key_path, 32> key_names;
        auto key_names_span = std::span<key_path, std::dynamic_extent>(key_names);

        for (;;)
        {
            auto const d =
                m_key->enumerate_keys(ikey::enumerate_keys_flags{}, index, key_names_span);
            M_INTERNAL_ERROR_CHECK(!d); // no flags in, no disposition out

            for (auto&& key_name: key_names_span)
                result.emplace_back(key_name);

            // If the batch was short, we're done
            if (key_names_span.size() != key_names.size())
                break;

            index += key_names.size();
        }

        return result;
    }

    void
    key::flush()
    {
        auto const d = m_key->flush(ikey::flush_flags{});
        M_INTERNAL_ERROR_CHECK(!d);
    }

    time_point
    key::last_write_time()
    {
        std::size_t subkey_count;
        std::size_t value_count;
        std::size_t security_descriptor_size;
        time_point  lwt{};

        auto const d = m_key->query_information_key(ikey::query_information_key_flags{},
                                                    subkey_count,
                                                    value_count,
                                                    security_descriptor_size,
                                                    lwt);
        M_INTERNAL_ERROR_CHECK(!d);

        return lwt;
    }

    std::vector<key::value_name_and_type>
    key::list_value_names_and_types()
    {
        std::vector<value_name_and_type> result;

        std::size_t                                                 index{};
        std::u16string                                              value_name_buffer;
        std::array<ikey::enumerate_value_names_and_types_value, 16> values_array;
        std::span<ikey::enumerate_value_names_and_types_value, std::dynamic_extent> values_span(
            values_array);

        for (;;)
        {
            auto const d = m_key->enumerate_value_names_and_types(
                ikey::enumerate_value_names_and_types_flags{}, index, values_span);
            M_INTERNAL_ERROR_CHECK(!d);

            for (auto&& e: values_span)
            {
                value_name_and_type vnt;

                vnt.m_value_name     = to_registry_string(e.m_value_name.view());
                vnt.m_reg_value_type = e.m_reg_value_type;

                result.push_back(std::move(vnt));
            }

            if (values_span.size() < values_array.size())
                break;

            index += values_span.size();
        }

        return result;
    }

    void
    key::do_delete_key(pil::key_path const& key_name)
    {
        auto const d =
            m_key->delete_key(ikey::delete_key_flags{}, key_name, sam::default_delete_key);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    void
    key::do_delete_tree(std::optional<pil::key_path> const& key_name)
    {
        auto const d = m_key->delete_tree(ikey::delete_tree_flags{}, key_name);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    key
    key::do_open_key(std::optional<pil::key_path> const& key_name)
    {
        if (!key_name.has_value())
            return *this;

        key  result{*this};
        auto name = static_cast<typename pil::key_path::string_type>(key_name.value());

        for (;;)
        {
            auto [left, right] = name.split_at(uregistry_delimiter);

            if (!left.empty())
                result = key(result.m_key->open_key(pil::key_path(left)));

            if (right.empty())
                break;

            name = right;
        }

        return result;
    }

    void
    key::do_rename_key(pil::key_path const& old_key_name, pil::key_path const& new_key_name)
    {
        auto const d = m_key->rename_key(ikey::rename_key_flags{}, old_key_name, new_key_name);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    void
    key::do_rename_key(pil::key_path const& new_key_name)
    {
        auto const d = m_key->rename_key(ikey::rename_key_flags{}, std::nullopt, new_key_name);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    void
    key::do_delete_value(std::u16string_view value_name)
    {
        auto const d = m_key->delete_value(ikey::delete_value_flags{}, value_name);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    reg_value_type
    key::do_get_value_type(std::u16string_view value_name)
    {
        return m_key->get_value_type(value_name);
    }

    registry_string_type
    key::do_get_string_value(std::u16string_view value_name)
    {
        auto value = get_value_as_bytes_and_value_type(value_name);
        if (value.m_type != reg_value_type::string)
            throw std::runtime_error("registry value is not reg_value_type::string (REG_SZ)");

        auto const sv = try_interpret_span_as_utf16(
            value.m_type, m::make_span<std::byte const>(&value.m_bytes[0], value.m_bytes.size()));
        return to_registry_string(sv);
    }

    registry_string_type
    key::do_get_expand_string_value(std::u16string_view value_name)
    {
        auto value = get_value_as_bytes_and_value_type(value_name);
        if (value.m_type != reg_value_type::expand_string)
            throw std::runtime_error(
                "registry value is not reg_value_type::expand_string (REG_EXPAND_SZ)");

        auto const sv = try_interpret_span_as_utf16(
            value.m_type, m::make_span<std::byte const>(&value.m_bytes[0], value.m_bytes.size()));
        return to_registry_string(sv);
    }

    std::vector<registry_string_type>
    key::do_get_multi_string_value(std::u16string_view value_name)
    {
        auto value = get_value_as_bytes_and_value_type(value_name);
        if (value.m_type != reg_value_type::multi_string)
            throw std::runtime_error(
                "registry value is not reg_value_type::multi_string (REG_MULTI_SZ)");

        std::vector<registry_string_type> retval;

        // Turn the value into a UTF-16 string and scan through looking for
        // the embedded null characters
        char16_t const* cursor    = reinterpret_cast<char16_t const*>(value.m_bytes.data());
        std::size_t     remaining = value.m_bytes.size() / sizeof(char16_t);

        for (;;)
        {
            std::ignore = cursor;
            std::ignore = remaining;
            // do the scanning in the future
            break;
        }

        return retval;
    }

    uint32_t
    key::do_get_uint32_value(std::u16string_view value_name)
    {
        auto value = get_value_as_bytes_and_value_type(value_name);
        if (value.m_type != reg_value_type::uint32)
            throw std::runtime_error("registry value is not reg_value_type::uint32 (REG_DWORD)");

        uint32_t retval{};

        // the Windows code is very lenient about how it treats REG_DWORD.
        // Values of any length are moved into the four byte destination
        // which is initialized to all zeroes.

        auto outspan1 = std::span(&retval, 1);
        auto outspan2 = std::as_writable_bytes(outspan1);

        auto inspan1 = std::span(value.m_bytes.begin(), value.m_bytes.end());
        auto inspan2 = std::as_bytes(inspan1);

        auto copy_count = (std::min)(outspan2.size(), inspan2.size());
        std::copy_n(inspan2.begin(), copy_count, outspan2.begin());

        return retval;
    }

    key::registry_value
    key::do_get_value(std::u16string_view value_name)
    {
        auto value = get_value_as_bytes_and_value_type(value_name);

        switch (value.m_type)
        {
            using enum reg_value_type;

            case string:
            {
                auto const sv = try_interpret_span_as_utf16(
                    value.m_type,
                    m::make_span<std::byte const>(&value.m_bytes[0], value.m_bytes.size()));
                return registry_value(string_value{to_registry_string(sv)});
            }

            case expand_string:
            {
                auto const sv = try_interpret_span_as_utf16(
                    value.m_type,
                    m::make_span<std::byte const>(&value.m_bytes[0], value.m_bytes.size()));
                return registry_value(expand_string_value{to_registry_string(sv)});
            }

            default: break;
        }

        unmapped_value uv{};

        uv.m_type = value.m_type;

        using std::swap;
        swap(uv.m_value, value.m_bytes);

        return registry_value(std::move(uv));
    }

    void
    key::do_set_value(std::u16string_view value_name, storage_string_value_view const& value)
    {
        M_INTERNAL_ERROR_CHECK(value.m_value.size() != 0 &&
                               value.m_value[value.m_value.size() - 1] == u'\0');

        m_key->set_value(ikey::set_value_flags{},
                         value_name,
                         reg_value_type::string,
                         std::as_bytes(std::span(value.m_value.begin(), value.m_value.end())));
    }

    void
    key::do_set_value(std::u16string_view value_name, storage_expand_string_value_view const& value)
    {
        M_INTERNAL_ERROR_CHECK(value.m_value.size() != 0 &&
                               value.m_value[value.m_value.size() - 1] == u'\0');

        m_key->set_value(ikey::set_value_flags{},
                         value_name,
                         reg_value_type::expand_string,
                         std::as_bytes(std::span(value.m_value.begin(), value.m_value.end())));
    }

    void
    key::do_set_value(std::u16string_view value_name, storage_multi_string_value_view const& value)
    {
        M_INTERNAL_ERROR_CHECK(value.m_value.size() >= 2 &&
                               value.m_value[value.m_value.size() - 1] == u'\0' &&
                               value.m_value[value.m_value.size() - 2] == u'\0');

        m_key->set_value(ikey::set_value_flags{},
                         value_name,
                         reg_value_type::multi_string,
                         std::as_bytes(std::span(value.m_value.begin(), value.m_value.end())));
    }

    void
    key::do_set_value(std::u16string_view value_name, storage_uint32_value const& value)
    {
        auto s  = std::span(&value.m_value, 1);
        auto s2 = std::as_bytes(s);

        m_key->set_value(ikey::set_value_flags{}, value_name, reg_value_type::uint32, s2);
    }

    key::bytes_and_value_type
    key::get_value_as_bytes_and_value_type(std::u16string_view value_name)
    {
        bytes_and_value_type retval{};
        get_value_into_byte_vector(value_name, retval.m_type, retval.m_bytes);
        return retval;
    }

    void
    key::get_value_into_byte_vector(std::u16string_view     value_name,
                                    reg_value_type&         vt,
                                    std::vector<std::byte>& bytes)
    {
        for (;;)
        {
            std::span<std::byte>       s = m::make_span(bytes.data(), bytes.size());
            std::optional<std::size_t> new_bytes_required{std::nullopt};
            reg_value_type             type{};

            auto const d =
                m_key->get_value(ikey::get_value_flags{}, value_name, type, s, new_bytes_required);
            M_INTERNAL_ERROR_CHECK(!d);

            if (!new_bytes_required.has_value())
            {
                // The span we get back could actually be shorter than what we
                // passed in, so trim down if that's the case
                M_INTERNAL_ERROR_CHECK(s.size() <= bytes.size());

                if (s.size() != bytes.size())
                    bytes.resize(s.size());

                break;
            }

            // If it's not asking for more bytes, ???
            M_INTERNAL_ERROR_CHECK(new_bytes_required.value() > bytes.size());

            bytes.resize(new_bytes_required.value());
            vt = type;
        }
    }

    std::u16string_view
    key::try_interpret_span_as_utf16(reg_value_type                                  vt,
                                     std::span<std::byte const, std::dynamic_extent> s)
    {
        M_INTERNAL_ERROR_CHECK((vt == reg_value_type::string) ||
                               (vt == reg_value_type::expand_string));

        //
        // Check to see if it has an even length. There is some logic(?) to
        // trim it down if it does.
        //
        auto byte_count = s.size();
        byte_count      = byte_count & (~1ull);

        char16_t const* p = reinterpret_cast<char16_t const*>(s.data());

        auto char_count = byte_count / sizeof(char16_t);

        // If the string is non-zero length and has trailing null characters, trim
        // them.
        while (char_count > 0)
        {
            if (p[char_count - 1] != u'\0')
                break;

            char_count--;
        }

        return std::u16string_view(p, char_count);
    }

    key_path
    key::do_get_path()
    {
        return m_key->get_path();
    }

} // namespace m::pil
