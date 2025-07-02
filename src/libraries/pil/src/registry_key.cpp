// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

namespace m::pil::registry
{
    key::key(key&& other) noexcept
    {
        using std::swap;
        swap(m_key, other.m_key);
    }

    key::key(key const& other): m_key(other.m_key) {}

    key&
    key::operator=(key const& other)
    {
        m_key.reset(other.m_key.get());
        return *this;
    }

    key&
    key::operator=(key&& other)
    {
        using std::swap;
        swap(m_key, other.m_key);
        return *this;
    }

    key
    key::do_create_key(std::u16string_view key_name)
    {
        std::shared_ptr<interfaces::key> return_value;

        auto const d = m_key->create_key(interfaces::key::create_key_flags{},
                                         key_name,
                                         std::nullopt,
                                         sam::default_create_key,
                                         std::nullopt,
                                         return_value);
        M_INTERNAL_ERROR_CHECK(!d);

        return key(std::move(return_value));
    }

    std::vector<registry_string>
    key::list_subkey_names()
    {
        std::vector<registry_string> result;

        std::size_t index{};

        for (;;)
        {
            std::optional<std::u16string> subkey_name = m_key->enumerate_keys(index++);

            if (!subkey_name.has_value())
                break;

            result.emplace_back(to_registry_string(subkey_name.value()));
        }

        return result;
    }

    void
    key::flush()
    {
        auto const d = m_key->flush(interfaces::key::flush_flags{});
        M_INTERNAL_ERROR_CHECK(!d);
    }

    time_point
    key::last_write_time()
    {
        std::size_t subkey_count;
        std::size_t value_count;
        std::size_t security_descriptor_size;
        time_point  lwt{};

        auto const d = m_key->query_information_key(interfaces::key::query_information_key_flags{},
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

        std::size_t index{};
        std::u16string value_name_buffer;

        for (;;)
        {
            std::optional<interfaces::key::enumerate_values_value> value_value =
                m_key->enumerate_values(index++, value_name_buffer);

            if (!value_value.has_value())
                break;

            value_name_and_type vnat;

            vnat.m_value_name =
                to_registry_string(value_value.value().m_value_name); // now *that's* a lot of value
            vnat.m_type = value_value.value().m_type;

            result.emplace_back(std::move(vnat));
        }

        return result;
    }

    void
    key::do_delete_key(std::u16string_view key_name)
    {
        auto const d = m_key->delete_key(
            interfaces::key::delete_key_flags{}, key_name, sam::default_delete_key);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    void
    key::do_delete_tree(std::u16string_view key_name)
    {
        auto const d = m_key->delete_tree(interfaces::key::delete_tree_flags{}, key_name);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    key
    key::do_open_key(std::u16string_view key_name)
    {
        std::shared_ptr<interfaces::key> key_interface;
        auto const                       d = m_key->open_key(
            interfaces::key::open_key_flags{}, key_name, sam::default_open_key, key_interface);
        M_INTERNAL_ERROR_CHECK(!d);
        return key(std::move(key_interface));
    }

    void
    key::do_rename_key(std::u16string_view old_key_name, std::u16string_view new_key_name)
    {
        auto const d =
            m_key->rename_key(interfaces::key::rename_key_flags{}, old_key_name, new_key_name);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    void
    key::do_rename_key(std::u16string_view new_key_name)
    {
        auto const d =
            m_key->rename_key(interfaces::key::rename_key_flags{}, std::nullopt, new_key_name);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    void
    key::do_delete_value(std::u16string_view value_name)
    {
        auto const d = m_key->delete_value(interfaces::key::delete_value_flags{}, value_name);
        M_INTERNAL_ERROR_CHECK(!d);
    }

    value_type
    key::do_get_value_type(std::u16string_view value_name)
    {
        return m_key->get_value_type(value_name);
    }

    registry_string
    key::do_get_string_value(std::u16string_view value_name)
    {
        auto value = get_value_as_bytes_and_value_type(value_name);
        if (value.m_type != value_type::string)
            throw std::runtime_error("registry value is not value_type::string (REG_SZ)");

        auto const sv = try_interpret_span_as_utf16(
            value.m_type, m::make_span<std::byte const>(&value.m_bytes[0], value.m_bytes.size()));
        return to_registry_string(sv);
    }

    registry_string
    key::do_get_expand_string_value(std::u16string_view value_name)
    {
        auto value = get_value_as_bytes_and_value_type(value_name);
        if (value.m_type != value_type::expand_string)
            throw std::runtime_error(
                "registry value is not value_type::expand_string (REG_EXPAND_SZ)");

        auto const sv = try_interpret_span_as_utf16(
            value.m_type, m::make_span<std::byte const>(&value.m_bytes[0], value.m_bytes.size()));
        return to_registry_string(sv);
    }

    std::vector<registry_string>
    key::do_get_multi_string_value(std::u16string_view value_name)
    {
        auto value = get_value_as_bytes_and_value_type(value_name);
        if (value.m_type != value_type::multi_string)
            throw std::runtime_error(
                "registry value is not value_type::multi_string (REG_MULTI_SZ)");

        std::vector<registry_string> retval;

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
        if (value.m_type != value_type::uint32)
            throw std::runtime_error("registry value is not value_type::uint32 (REG_DWORD)");

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
            using enum value_type;

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

        m_key->set_value(interfaces::key::set_value_flags{},
                         value_name,
                         value_type::string,
                         std::as_bytes(std::span(value.m_value.begin(), value.m_value.end())));
    }

    void
    key::do_set_value(std::u16string_view value_name, storage_expand_string_value_view const& value)
    {
        M_INTERNAL_ERROR_CHECK(value.m_value.size() != 0 &&
                               value.m_value[value.m_value.size() - 1] == u'\0');

        m_key->set_value(interfaces::key::set_value_flags{},
                         value_name,
                         value_type::expand_string,
                         std::as_bytes(std::span(value.m_value.begin(), value.m_value.end())));
    }

    void
    key::do_set_value(std::u16string_view value_name, storage_multi_string_value_view const& value)
    {
        M_INTERNAL_ERROR_CHECK(value.m_value.size() >= 2 &&
                               value.m_value[value.m_value.size() - 1] == u'\0' &&
                               value.m_value[value.m_value.size() - 2] == u'\0');

        m_key->set_value(interfaces::key::set_value_flags{},
                         value_name,
                         value_type::multi_string,
                         std::as_bytes(std::span(value.m_value.begin(), value.m_value.end())));
    }

    void
    key::do_set_value(std::u16string_view value_name, storage_uint32_value const& value)
    {
        auto s  = std::span(&value.m_value, 1);
        auto s2 = std::as_bytes(s);

        m_key->set_value(interfaces::key::set_value_flags{}, value_name, value_type::uint32, s2);
    }

#if 0
    std::vector < std::byte>
    key::to_null_terminated_utf16_as_byte_vector(std::u16string_view view)
    {
        std::vector<std::byte> result((view.size() + 1) * sizeof(char16_t));

        std::size_t size = view.size();

        result.resize((size + 1) * sizeof(char16_t));

        auto inspan1 = std::span(view.begin(), view.end());
        auto inspan2 = std::as_bytes(inspan1);

        auto outspan1 = std::span(result.begin(), result.end());
        auto outspan2 =
            std::as_writable_bytes(outspan1); // outspan1 is probably already span<byte> but this
                                              // makes it explicit and symmetric

        // outspan is supposed to be one (UTF-16) character larger than inspan.
        M_INTERNAL_ERROR_CHECK(outspan2.size() == (inspan2.size() + sizeof(char16_t)));

        auto outit = std::copy_n(inspan2.begin(), inspan2.size(), outspan2.begin());

        M_INTERNAL_ERROR_CHECK(outit != outspan2.end());

        *outit++ = std::byte{0};

        M_INTERNAL_ERROR_CHECK(outit != outspan2.end());

        *outit++ = std::byte{0};

        M_INTERNAL_ERROR_CHECK(outit == outspan2.end());

        return result;
    }
#endif

    key::bytes_and_value_type
    key::get_value_as_bytes_and_value_type(std::u16string_view value_name)
    {
        bytes_and_value_type retval{};
        get_value_into_byte_vector(value_name, retval.m_type, retval.m_bytes);
        return retval;
    }

    void
    key::get_value_into_byte_vector(std::u16string_view     value_name,
                                    value_type&             vt,
                                    std::vector<std::byte>& bytes)
    {
        for (;;)
        {
            std::span<std::byte>       s = m::make_span(&bytes[0], bytes.size());
            std::optional<std::size_t> new_bytes_required{std::nullopt};
            value_type                 type{};

            auto const d = m_key->get_value(
                interfaces::key::get_value_flags{}, value_name, type, s, new_bytes_required);
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
    key::try_interpret_span_as_utf16(value_type                                      vt,
                                     std::span<std::byte const, std::dynamic_extent> s)
    {
        M_INTERNAL_ERROR_CHECK((vt == value_type::string) || (vt == value_type::expand_string));

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

} // namespace m::pil::registry
