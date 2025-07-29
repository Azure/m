// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include <m/pil/pil.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry_interfaces.h>
#include <m/strings/compare.h>

//
// A buffered platform layer may be an overlay or may be standalone.
//
// Overlay buffered platforms are "deltas" in that they store changes over an
// underlying platform's state.
//
// That underlying platform's state of course may change over the lifetime of
// the overlay but there is no single overarching answer for how to approach
// this issue.
//
// In general, containers for a platform must keep record of changes (new
// items, changes to existing items, deletion of items) and report state
// which at the very least represents the updated state.
//
// The buffered platform implementation may do this by maintaining a copy of
// the container's state in shallow form and report the state by only
// returning the local copy, or by keeping track only of the deltas and
// then merging the state with the underlying platform's state.
//
// What a buffered platform container must be careful to NOT do is to not
// make a full copy of all its contained state, including streams of data and
// copies of all its subcontainers.
//
// Depending on the model here, it may, or may not, maintain copies of
// streams. It's impossible to make a one-size-fits-all recommendation here,
// this is not a forum for designing a filesystem platform, a buffered PIL is
// a tool to aid testing and certain limited production scenarios.
//
// For the Windows Registry, for example, a buffered registry key may
// well keep copies of all the values. Or it may keep copies of all values
// previously fetched.
//
// But for a filesystem directory, it would be unreasonable to expect that a
// buffered PIL directory would keep in-memory copy of files.
//
// Breaking, 7/9/2025: Because of the "stateless" enumeration model, it's
// not feasible to have a delta-only based in memory key state.
//
// I had been thinking that there were three models. The key has a list of
// all its children ("mirrored") but there was an underlying registry,
// there was no underlying registry, and then there was a model where
// the in-memory list was a delta over the underlying registry state,
// a list of additions and subtractions.
//
// The problem is enumeration. If you had a formal enumerator object it
// could work - I will explain. The problem is that when enumerating
// you don't know until you are done which are which. You could re-form
// a list of the added and deleted keys/values on every call into
// enumeration, but this would seem extremely expensive. (The expense would
// be mitigated with an enumeration context but the registry API today
// has none because today it is modeled directly on the Windows registry
// API with some simplifications.)
//

namespace m::pil::impl::buffered
{
    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = default;
        platform(std::shared_ptr<iplatform> const& underlying_platform);
        platform(std::shared_ptr<iplatform>&& underlying_platform) noexcept;
        platform(platform&& other) noexcept;
        platform(platform const&) = delete;
        ~platform()               = default;

        platform&
        operator=(platform&& other) noexcept;

        platform&
        operator=(platform const&) = delete;

        friend void
        swap(platform& l, platform& r) noexcept
        {
            using std::swap;
            swap(l.m_underlying_platform, r.m_underlying_platform);
        }

        void
        write_to_xml(std::filesystem::path const& path);

        static std::shared_ptr<iplatform>
        load_from_xml(std::shared_ptr<iplatform> const& underlying_platform,
                 std::filesystem::path const&      path);

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;

    protected:
        bool
        overlaid() const
        {
            return static_cast<bool>(m_underlying_platform);
        }

        std::shared_ptr<iplatform> m_underlying_platform;
    };

    class registry : public iregistry, public std::enable_shared_from_this<registry>
    {
    public:
        registry() = default;
        registry(std::shared_ptr<iregistry> const& underlying_registry);
        registry(std::shared_ptr<iregistry>&& underlying_registry) noexcept;
        registry(registry&& other) noexcept;
        registry(registry const&) = delete;
        ~registry()               = default;

        registry&
        operator=(registry&& other) noexcept;

        registry&
        operator=(registry const&) = delete;

        friend void
        swap(registry& l, registry& r) noexcept
        {
            using std::swap;
            swap(l.m_underlying_registry, r.m_underlying_registry);
            swap(l.m_predefined_keys, r.m_predefined_keys);
        }

        iregistry::open_predefined_key_disposition
        open_predefined_key(open_predefined_key_flags      flags,
                            predefined_key                 pk,
                            sam                            sam_desired,
                            std::shared_ptr<m::pil::ikey>& returned_key) override;

        static bool
        simple_path(std::u16string_view key_path);

    protected:
        bool
        overlaid() const
        {
            return static_cast<bool>(m_underlying_registry);
        }

        std::mutex                                      m_mutex;
        std::shared_ptr<iregistry>                      m_underlying_registry;
        std::map<predefined_key, std::shared_ptr<ikey>> m_predefined_keys;
    };

    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key() = default;
        key(time_point last_write_time);
        key(std::shared_ptr<ikey> const& underlying_key);
        key(std::shared_ptr<ikey>&& underlying_key) noexcept;
        key(key&& other) noexcept;
        key(key const&) = delete;
        ~key()          = default;

        key&
        operator=(key&& other) noexcept;

        key&
        operator=(key const&) = delete;

        friend void
        swap(key& l, key& r) noexcept
        {
            using std::swap;

            swap(l.m_underlying_key, r.m_underlying_key);
            swap(l.m_keys, r.m_keys);
            swap(l.m_values, r.m_values);
            swap(l.m_last_write_time, r.m_last_write_time);
            swap(l.m_security_descriptor, r.m_security_descriptor);
        }

        ikey::create_key_disposition
        create_key(ikey::create_key_flags             flags,
                   std::u16string_view                name,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<ikey>&             returned_key) override;

        ikey::delete_key_disposition
        delete_key(ikey::delete_key_flags flags,
                   std::u16string_view    name,
                   sam                    sam_desired) override;

        ikey::delete_tree_disposition
        delete_tree(ikey::delete_tree_flags            flags,
                    std::optional<std::u16string_view> name) override;

        ikey::enumerate_keys_disposition
        enumerate_keys(ikey::enumerate_keys_flags                      flags,
                       std::size_t                                     index,
                       std::span<std::u16string, std::dynamic_extent>& key_names) override;

        ikey::flush_disposition
        flush(ikey::flush_flags flags) override;

        ikey::open_key_disposition
        open_key(ikey::open_key_flags               flags,
                 std::optional<std::u16string_view> key_name,
                 sam                                sam_desired,
                 std::shared_ptr<ikey>&             returned_key) override;

        ikey::query_information_key_disposition
        query_information_key(ikey::query_information_key_flags flags,
                              std::size_t&                      subkey_count,
                              std::size_t&                      value_count,
                              std::size_t&                      security_descriptor_size,
                              m::pil::time_point&               last_write_time) override;

        ikey::rename_key_disposition
        rename_key(ikey::rename_key_flags             flags,
                   std::optional<std::u16string_view> old_key_name,
                   std::u16string_view                new_key_name) override;

        ikey::delete_value_disposition
        delete_value(ikey::delete_value_flags flags, std::u16string_view value_name) override;

        ikey::enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(ikey::enumerate_value_names_and_types_flags flags,
                                        std::size_t                                 index,
                                        std::span<enumerate_value_names_and_types_value,
                                                  std::dynamic_extent>& values_span) override;

        ikey::get_value_size_disposition
        get_value_size(ikey::get_value_size_flags flags,
                       std::u16string_view        value_name,
                       std::size_t&               size) override;

        ikey::get_value_type_disposition
        get_value_type(ikey::get_value_type_flags flags,
                       std::u16string_view        value_name,
                       reg_value_type&            type) override;

        ikey::get_value_disposition
        get_value(ikey::get_value_flags       flags,
                  std::u16string_view         value_name,
                  reg_value_type&             type,
                  std::span<std::byte>&       value,
                  std::optional<std::size_t>& new_bytes_required) override;

        ikey::set_value_disposition
        set_value(ikey::set_value_flags      flags,
                  std::u16string_view        value_name,
                  reg_value_type             type,
                  std::span<std::byte const> value) override;

    protected:
        void
        initialize_overlay();

        void
        initialize_keys_overlay();

        void
        initialize_values_overlay();

        bool
        is_subkey_empty(std::u16string_view key_name);

        struct key_node
        {
            std::shared_ptr<ikey> m_key;
            time_point            m_last_write_time{(time_point::min)()};
            bool                  m_deleted : 1;
            bool                  m_mirrored : 1;
        };

        struct value_node
        {
            reg_value_type                        m_reg_value_type;
            std::optional<std::vector<std::byte>> m_value;
            bool                                  m_deleted;
        };

        void
        unmirror_node(key_name_view_type key_name, key_node& node);

        void
        unmirror_node(value_name_view_type value_name, value_node& node);

        using key_map_type =
            std::map<std::u16string, key_node, m::case_insensitive_less<std::u16string>>;

        using value_map_type = std::map<std::u16string, value_node, m::case_insensitive_less<std::u16string>>;

        void
        load_value_if_not_present(std::u16string_view const& value_name, value_node& vnv);

        //
        // data
        //

        std::mutex             m_mutex;
        std::shared_ptr<ikey>  m_underlying_key;
        time_point             m_last_write_time;
        key_map_type           m_keys;
        value_map_type         m_values;
        std::vector<std::byte> m_security_descriptor;
    };
} // namespace m::pil::impl::buffered
