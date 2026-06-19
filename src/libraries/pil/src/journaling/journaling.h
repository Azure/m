// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/pil/registry_interfaces.h>
#include <m/utility/locked.h>

#include <pugixml.hpp>

#include "../pugihelp.h"

namespace m::pil::impl::journaling
{
    using key_path = pil::key_path;

    // A journal is an ordered stream of mutation verbs (D7). Unlike the logging
    // layer (a human-oriented side diagnostic) and unlike the buffered snapshot
    // (sealed end state), the journal records exactly the mutating operations,
    // in the order they were issued, with enough information to replay them onto
    // a base world and reach the same observable state. It is a separate
    // artifact, distinct from the persisted <Platform>.

    // Base class for the verb entries held in the journal. Each entry knows how
    // to serialize itself as one child element of the <Journal> node.
    class journal_entry
    {
    public:
        virtual ~journal_entry() = default;

        virtual void
        save(pugi::xml_node& journal_node) const = 0;

    protected:
        journal_entry() = default;
    };

    class journal : public std::enable_shared_from_this<journal>
    {
    public:
        journal() = default;

        template <typename T>
            requires(std::derived_from<T, journal_entry>)
        void
        add(std::unique_ptr<T>& entry)
        {
            auto l = std::unique_lock(m_mutex);
            m_deque.emplace_back(std::move(entry));
        }

        // Serialize every recorded verb, in order, as children of journal_node.
        void
        save(pugi::xml_node& journal_node) const;

    private:
        mutable std::mutex                         m_mutex;
        std::deque<std::unique_ptr<journal_entry>> m_deque;
    };

    // Verb entries. Each records the absolute path of the key the operation was
    // invoked on (the "base" key) plus the operation-specific arguments needed
    // to replay it.

    class create_key_entry : public journal_entry
    {
    public:
        create_key_entry(key_path const& base_key_path, key_path const& subkey_path);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        key_path m_base_key_path;
        key_path m_subkey_path;
    };

    class delete_key_entry : public journal_entry
    {
    public:
        delete_key_entry(key_path const& base_key_path, key_path const& subkey_path);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        key_path m_base_key_path;
        key_path m_subkey_path;
    };

    class delete_tree_entry : public journal_entry
    {
    public:
        delete_tree_entry(key_path const& base_key_path, std::optional<key_path> const& subkey_path);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        key_path                m_base_key_path;
        std::optional<key_path> m_subkey_path;
    };

    class rename_key_entry : public journal_entry
    {
    public:
        rename_key_entry(key_path const&                base_key_path,
                         std::optional<key_path> const& old_subkey_name,
                         key_path const&                new_key_name);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        key_path                m_base_key_path;
        std::optional<key_path> m_old_subkey_name;
        key_path                m_new_key_name;
    };

    class delete_value_entry : public journal_entry
    {
    public:
        delete_value_entry(key_path const& base_key_path, value_name_string_type const& value_name);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        key_path               m_base_key_path;
        value_name_string_type m_value_name;
    };

    class set_value_entry : public journal_entry
    {
    public:
        set_value_entry(key_path const&               base_key_path,
                        value_name_string_type const& value_name,
                        reg_value_type                type,
                        std::span<std::byte const>    value);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        key_path               m_base_key_path;
        value_name_string_type m_value_name;
        reg_value_type         m_type;
        std::vector<std::byte> m_value;
    };

    //
    // Filesystem facet (D7 / D14). The journal records the unified-namespace
    // mutation verbs, in issue order, with enough information to replay them
    // onto a base world: each verb records the absolute path of the directory
    // it was invoked on plus the operation-specific path arguments. Per D14 the
    // journal records namespace mutations only; file *content* is out of scope.
    //
    // Unlike a registry key (which carries its own absolute path via
    // ikey::get_path), idirectory has no path accessor, so the journaling
    // directory wrapper tracks the absolute path of the node it represents and
    // supplies it to each verb entry.
    //

    class fs_create_directory_entry : public journal_entry
    {
    public:
        fs_create_directory_entry(file_path const& base_directory_path, file_path const& path);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        file_path m_base_directory_path;
        file_path m_path;
    };

    class fs_create_file_entry : public journal_entry
    {
    public:
        fs_create_file_entry(file_path const& base_directory_path, file_path const& path);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        file_path m_base_directory_path;
        file_path m_path;
    };

    class fs_remove_entry : public journal_entry
    {
    public:
        fs_remove_entry(file_path const& base_directory_path, file_path const& name);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        file_path m_base_directory_path;
        file_path m_name;
    };

    class fs_delete_tree_entry : public journal_entry
    {
    public:
        fs_delete_tree_entry(file_path const&                base_directory_path,
                             std::optional<file_path> const& name);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        file_path                m_base_directory_path;
        std::optional<file_path> m_name;
    };

    class fs_rename_entry : public journal_entry
    {
    public:
        fs_rename_entry(file_path const& base_directory_path,
                        file_path const& old_path,
                        file_path const& new_path);

        void
        save(pugi::xml_node& journal_node) const override;

    private:
        file_path m_base_directory_path;
        file_path m_old_path;
        file_path m_new_path;
    };

    class registry : public iregistry, public std::enable_shared_from_this<registry>
    {
    public:
        registry() = delete;
        registry(std::shared_ptr<iregistry> const& underlying_registry,
                 std::shared_ptr<journal> const&   journal_ptr);
        registry(registry&&) noexcept = delete;
        registry(registry const&)     = delete;
        ~registry()                   = default;

        registry&
        operator=(registry&&) noexcept = delete;
        registry&
        operator=(registry const&) = delete;

        iregistry::open_predefined_key_disposition
        open_predefined_key(open_predefined_key_flags      flags,
                            predefined_key                 pk,
                            sam                            sam_desired,
                            std::shared_ptr<m::pil::ikey>& returned_key) override;

        monitor_disposition
        monitor(monitor_flags                               flags,
                std::shared_ptr<m::pil::iregistry_monitor>& returned_registry_monitor) override;

    private:
        std::mutex                                      m_mutex;
        std::shared_ptr<iregistry>                      m_underlying_registry;
        std::shared_ptr<journal>                        m_journal;
        std::map<predefined_key, std::shared_ptr<ikey>> m_predefined_keys;
    };

    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key() = delete;
        key(std::shared_ptr<ikey> const& underlying_key, std::shared_ptr<journal> const& journal_ptr);
        key(key const&)     = delete;
        key(key&&) noexcept = delete;
        ~key()              = default;

        key&
        operator=(key const&) = delete;
        key&
        operator=(key&&) noexcept = delete;

        ikey::create_key_disposition
        create_key(ikey::create_key_flags             flags,
                   key_path const&                    name,
                   sam                                sam_desired,
                   std::optional<security_attributes> sa,
                   std::shared_ptr<ikey>&             returned_key) override;

        ikey::delete_key_disposition
        delete_key(ikey::delete_key_flags flags, key_path const& name, sam sam_desired) override;

        ikey::delete_tree_disposition
        delete_tree(ikey::delete_tree_flags flags, std::optional<key_path> const& name) override;

        ikey::enumerate_keys_disposition
        enumerate_keys(ikey::enumerate_keys_flags                flags,
                       std::size_t                               index,
                       std::span<key_path, std::dynamic_extent>& key_names) override;

        ikey::flush_disposition
        flush(ikey::flush_flags flags) override;

        ikey::open_key_disposition
        open_key(ikey::open_key_flags           flags,
                 std::optional<key_path> const& key_name,
                 sam                            sam_desired,
                 std::shared_ptr<ikey>&         returned_key,
                 std::error_code&               ec) override;

        ikey::query_information_key_disposition
        query_information_key(ikey::query_information_key_flags flags,
                              std::size_t&                      subkey_count,
                              std::size_t&                      value_count,
                              std::size_t&                      security_descriptor_size,
                              time_point_type&                  last_write_time) override;

        ikey::rename_key_disposition
        rename_key(ikey::rename_key_flags         flags,
                   std::optional<key_path> const& old_key_name,
                   key_path const&                new_key_name) override;

        ikey::delete_value_disposition
        delete_value(ikey::delete_value_flags      flags,
                     value_name_string_type const& value_name) override;

        ikey::enumerate_value_names_and_types_disposition
        enumerate_value_names_and_types(ikey::enumerate_value_names_and_types_flags flags,
                                        std::size_t                                 index,
                                        std::span<enumerate_value_names_and_types_value,
                                                  std::dynamic_extent>& values_span) override;

        ikey::get_value_size_disposition
        get_value_size(ikey::get_value_size_flags    flags,
                       value_name_string_type const& value_name,
                       std::size_t&                  size) override;

        ikey::get_value_type_disposition
        get_value_type(ikey::get_value_type_flags    flags,
                       value_name_string_type const& value_name,
                       reg_value_type&               type) override;

        ikey::get_value_disposition
        get_value(ikey::get_value_flags         flags,
                  value_name_string_type const& value_name,
                  reg_value_type&               type,
                  std::span<std::byte>&         value,
                  std::optional<std::size_t>&   new_bytes_required) override;

        ikey::set_value_disposition
        set_value(ikey::set_value_flags         flags,
                  value_name_string_type const& value_name,
                  reg_value_type                type,
                  std::span<std::byte const>    value) override;

        ikey::get_path_disposition
        get_path(ikey::get_path_flags flags, m::pil::key_path& path_out) override;

    private:
        std::shared_ptr<ikey>    m_key;
        std::shared_ptr<journal> m_journal;
    };

    // A journaling directory wrapper. Reads forward unchanged; mutation verbs
    // are recorded into the journal (carrying this directory's absolute path so
    // replay can navigate back to it) and then forwarded. Returned directory
    // nodes are re-wrapped so the whole subtree stays inside the journaling
    // layer and keeps an accurate absolute path. Files carry no mutating verbs,
    // so opened/created files are forwarded unwrapped.
    class directory : public idirectory, public std::enable_shared_from_this<directory>
    {
    public:
        directory() = delete;
        directory(std::shared_ptr<idirectory> const& underlying_directory,
                  std::shared_ptr<journal> const&    journal_ptr,
                  file_path                          absolute_path);
        directory(directory const&)           = delete;
        directory(directory&& other) noexcept = delete;
        ~directory()                          = default;

        directory&
        operator=(directory const&) = delete;
        directory&
        operator=(directory&& other) noexcept = delete;

        idirectory::create_directory_disposition
        create_directory(create_directory_flags       flags,
                         file_path const&             path,
                         file_access                  access,
                         std::shared_ptr<idirectory>& returned_directory) override;

        idirectory::create_file_disposition
        create_file(create_file_flags       flags,
                    file_path const&        path,
                    file_access             access,
                    std::shared_ptr<ifile>& returned_file) override;

        idirectory::open_directory_disposition
        open_directory(open_directory_flags         flags,
                       file_path const&             path,
                       file_access                  access,
                       std::shared_ptr<idirectory>& returned_directory,
                       std::error_code&             ec) override;

        idirectory::open_file_disposition
        open_file(open_file_flags         flags,
                  file_path const&        path,
                  file_access             access,
                  std::shared_ptr<ifile>& returned_file,
                  std::error_code&        ec) override;

        idirectory::remove_entry_disposition
        remove_entry(remove_entry_flags flags, file_path const& name) override;

        idirectory::delete_tree_disposition
        delete_tree(delete_tree_flags flags, std::optional<file_path> const& name) override;

        idirectory::rename_entry_disposition
        rename_entry(rename_entry_flags flags,
                     file_path const&   old_path,
                     file_path const&   new_path) override;

        idirectory::enumerate_entries_disposition
        enumerate_entries(enumerate_entries_flags                          flags,
                          std::size_t                                      starting_index,
                          std::span<directory_entry, std::dynamic_extent>& entries) override;

        idirectory::query_information_disposition
        query_information(query_information_flags flags, file_metadata& metadata) override;

    private:
        std::shared_ptr<idirectory> m_directory;
        std::shared_ptr<journal>    m_journal;
        file_path                   m_absolute_path;
    };

    class filesystem : public ifilesystem, public std::enable_shared_from_this<filesystem>
    {
    public:
        filesystem() = delete;
        filesystem(std::shared_ptr<ifilesystem> const& underlying_filesystem,
                   std::shared_ptr<journal> const&     journal_ptr);
        filesystem(filesystem const&)           = delete;
        filesystem(filesystem&& other) noexcept = delete;
        ~filesystem()                           = default;

        filesystem&
        operator=(filesystem const&) = delete;
        filesystem&
        operator=(filesystem&& other) noexcept = delete;

        ifilesystem::open_root_disposition
        open_root(open_root_flags              flags,
                  file_root const&             root,
                  file_access                  access,
                  std::shared_ptr<idirectory>& returned_directory) override;

        ifilesystem::monitor_disposition
        monitor(monitor_flags                                 flags,
                std::shared_ptr<m::pil::ifilesystem_monitor>& returned_filesystem_monitor) override;

    private:
        std::shared_ptr<ifilesystem> m_filesystem;
        std::shared_ptr<journal>     m_journal;
    };

    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = delete;
        platform(std::shared_ptr<iplatform> const& underlying_platform);
        platform(platform&&) noexcept = delete;
        platform(platform const&)     = delete;
        ~platform()                   = default;

        platform&
        operator=(platform&&) noexcept = delete;
        platform&
        operator=(platform const&) = delete;

        get_registry_disposition
        get_registry(get_registry_flags          flags,
                     std::shared_ptr<iregistry>& returned_registry) override;

        get_filesystem_disposition
        get_filesystem(get_filesystem_flags          flags,
                       std::shared_ptr<ifilesystem>& returned_filesystem) override;

        get_webcore_disposition
        get_webcore(get_webcore_flags          flags,
                    std::shared_ptr<iwebcore>& returned_webcore) override;

        get_http_contract_disposition
        get_http_contract(get_http_contract_flags          flags,
                          std::shared_ptr<ihttp_contract>& returned_http_contract) override;

        save_disposition
        save(save_flags flags, save_contents contents, pugi::xml_node& platform_element) override;

        save_disposition
        save_diagnostic_log(save_flags flags, pugi::xml_node& diagnostic_element) override;

        // Serialize the recorded verb stream (in order) as children of
        // journal_node. The journal is a separate artifact, never part of the
        // persisted <Platform> (D7).
        void
        save_journal(pugi::xml_node& journal_node) const;

    private:
        std::shared_ptr<iplatform>  m_underlying_platform;
        std::shared_ptr<journal>    m_journal;
        std::shared_ptr<registry>   m_registry;
        std::shared_ptr<filesystem> m_filesystem;
    };

    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const& underlying_platform);

    // Ordered replay (D7). Reapplies every verb recorded under journal_node, in
    // document order, onto target_registry. After replay the target reaches the
    // same observable state the journal's source reached for the journaled
    // operations. journal_node is the <Journal> element produced by
    // platform::save_journal.
    void
    replay(pugi::xml_node const& journal_node, iregistry& target_registry);

    // Ordered replay of the filesystem namespace verbs (D7). Reapplies every
    // Filesystem.* verb recorded under journal_node, in document order, onto
    // target_filesystem. Each verb's recorded directory path is resolved (its
    // parents created as needed) before the verb is reissued, so after replay
    // the target reaches the same observable namespace the journal's source
    // reached. Non-filesystem entries are ignored.
    void
    replay(pugi::xml_node const& journal_node, ifilesystem& target_filesystem);

} // namespace m::pil::impl::journaling
