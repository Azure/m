// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
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
#include <m/pil/webcore_interfaces.h>

#include <pugixml.hpp>

#include "../pugihelp.h"

namespace m::pil::impl::fault
{
    using key_path = pil::key_path;

    // The fault-injecting layer (D8) is a transparent decorator stack driven by
    // a declarative, stateful fault script. The script is a *separate* input
    // artifact, never folded into the persisted <Platform>. Each rule maps a
    // predicate — (operation type, target path, optional value name, Nth-
    // occurrence counter) — to an action (an error to raise). Matching is
    // counted per rule, so "the third open of X fails with out-of-resources" is
    // expressible.

    // The registry operations a fault rule can target. The string spellings
    // accepted in the artifact are defined by parse_fault_script; changing a
    // spelling is a breaking change to the artifact grammar.
    enum class fault_operation : std::uint32_t
    {
        create_key,
        open_key,
        delete_key,
        delete_tree,
        rename_key,
        set_value,
        delete_value,
        get_value,

        // Filesystem operations (D8 / M-FS-FAULT). These target a file_path
        // rather than a key_path; the grammar spellings are distinct from the
        // registry verbs (notably delete_tree_entry vs. the registry's
        // delete_tree) so a single <FaultScript> may name either domain
        // unambiguously. Changing a spelling is a breaking change.
        create_directory,
        create_file,
        open_directory,
        open_file,
        remove_entry,
        delete_tree_entry,
        rename_entry,

        // Webcore operations (D8 / M-HWC-FACETS-3). The webcore_activate verb
        // targets an instance name string (the activation_request.instance_name);
        // the grammar spelling is distinct from the registry and filesystem verbs.
        webcore_activate,
    };

    // The error a fired rule raises. Each maps to a thrown m exception so the
    // consumer observes the same failure category the real platform would
    // raise. Changing a spelling is a breaking change to the artifact grammar.
    enum class fault_action : std::uint32_t
    {
        not_found,
        access_denied,
        out_of_resources,
        sharing_violation,
        already_exists,
        not_supported,
    };

    // A single counted fault rule. The rule owns its hit counter; counting is
    // serialized by the fault_script that holds it (the rule itself takes no
    // lock). The target path is stored as the already-normalized native text of
    // the originating path type (key_path for registry verbs, file_path for
    // filesystem verbs); matching is a case-insensitive comparison of that text
    // against the runtime operation's path, so the two domains share one
    // counting mechanism without one path type's normalization leaking into the
    // other.
    class fault_rule
    {
    public:
        // Registry-targeting rule: the target is a key_path.
        fault_rule(fault_operation                            op,
                   key_path                                   target,
                   std::optional<pil::value_name_string_type> value_name,
                   std::uint64_t                              occurrence,
                   fault_action                               action);

        // Filesystem-targeting rule: the target is a file_path; filesystem
        // verbs carry no secondary (value-name) constraint.
        fault_rule(fault_operation op,
                   file_path       target,
                   std::uint64_t   occurrence,
                   fault_action    action);

        // Webcore-targeting rule: the target is an instance name string;
        // webcore verbs carry no secondary constraint.
        fault_rule(fault_operation        op,
                   std::u16string_view    instance_name,
                   std::uint64_t          occurrence,
                   fault_action           action);

        // If this rule's predicate matches the registry operation, increment
        // its hit count and, when the count reaches the configured occurrence,
        // return the action to raise; otherwise std::nullopt. A non-matching
        // operation does not advance the counter.
        std::optional<fault_action>
        match_and_count(fault_operation                          op,
                        key_path const&                          target,
                        std::optional<pil::value_name_view_type> value_name);

        // The filesystem analogue: match a filesystem operation against this
        // rule's target path.
        std::optional<fault_action>
        match_and_count(fault_operation op, file_path const& target);

        // The webcore analogue: match a webcore operation against this rule's
        // target instance name.
        std::optional<fault_action>
        match_and_count(fault_operation op, std::u16string_view instance_name);

    private:
        std::optional<fault_action>
        match_text_and_count(fault_operation                          op,
                             std::u16string_view                      target_text,
                             std::optional<pil::value_name_view_type> value_name);

        fault_operation                            m_operation;
        std::u16string                             m_target;
        std::optional<pil::value_name_string_type> m_value_name;
        std::uint64_t                              m_occurrence;
        fault_action                               m_action;
        std::uint64_t                              m_hits = 0;
    };

    // An ordered set of counted rules plus the shared, mutex-guarded counting
    // state threaded through the fault decorators. Consulted before each
    // faultable operation forwards to the underlying layer.
    class fault_script : public std::enable_shared_from_this<fault_script>
    {
    public:
        fault_script() = default;

        void
        add_rule(fault_rule rule);

        // Consult every rule for this operation, advancing the counters of all
        // rules whose predicate matches. If any rule reaches its configured
        // occurrence, throw the mapped exception (the operation never reaches
        // the underlying layer). All matching rules are counted even when one
        // fires, so independent rules stay consistent.
        void
        check(fault_operation                          op,
              key_path const&                          target,
              std::optional<pil::value_name_view_type> value_name = std::nullopt);

        // The filesystem analogue of check: consult every rule for a filesystem
        // operation on target, advancing all matching counters, and throw the
        // mapped exception if any rule reaches its configured occurrence.
        void
        check_filesystem(fault_operation op, file_path const& target);

        // The webcore analogue of check: consult every rule for a webcore
        // operation on the instance name, advancing all matching counters, and
        // throw the mapped exception if any rule reaches its configured
        // occurrence.
        void
        check_webcore(fault_operation op, std::u16string_view instance_name);

    private:
        std::mutex              m_mutex;
        std::vector<fault_rule> m_rules;
    };

    // Parse a <FaultScript> element into a fault_script. The grammar:
    //
    //   <FaultScript>
    //     <Rule operation="open_key" path="HKEY_CURRENT_USER\Foo"
    //           occurrence="3" action="out_of_resources"/>
    //     <Rule operation="set_value" path="HKEY_CURRENT_USER\Bar"
    //           valueName="x" occurrence="1" action="access_denied"/>
    //   </FaultScript>
    //
    // Each <Rule> requires operation, path, occurrence (>= 1), and action; the
    // valueName attribute is optional and, when present, additionally
    // constrains value operations to that value name. Unknown operation/action
    // spellings, a missing required attribute, or occurrence < 1 throw.
    //
    // Filesystem rules use the same grammar; their operation spellings are the
    // filesystem verbs (create_directory, create_file, open_directory,
    // open_file, remove_entry, delete_tree_entry, rename_entry) and the path is
    // a file_path. valueName is ignored for filesystem operations.
    std::shared_ptr<fault_script>
    parse_fault_script(pugi::xml_node const& fault_script_node);

    // The fault-injecting decorators. Each wraps the corresponding underlying
    // entity and shares a single fault_script. A faultable operation consults
    // the script (which may throw) before forwarding to the underlying layer;
    // read-only and structural operations forward transparently.

    class registry : public iregistry, public std::enable_shared_from_this<registry>
    {
    public:
        registry() = delete;
        registry(std::shared_ptr<iregistry> const&    underlying_registry,
                 std::shared_ptr<fault_script> const& script);
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
        std::shared_ptr<fault_script>                   m_script;
        std::map<predefined_key, std::shared_ptr<ikey>> m_predefined_keys;
    };

    class key : public ikey, public std::enable_shared_from_this<key>
    {
    public:
        key() = delete;
        key(std::shared_ptr<ikey> const& underlying_key, std::shared_ptr<fault_script> const& script);
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
        std::shared_ptr<ikey>         m_key;
        std::shared_ptr<fault_script> m_script;
    };

    // A fault-injecting directory wrapper. Each faultable namespace verb
    // consults the script (which may throw, in which case the underlying layer
    // is never touched) before forwarding. The wrapper carries this directory's
    // absolute path — idirectory has no get_path() of its own — so a rule can
    // be matched against the verb's full target path. Returned directory nodes
    // are re-wrapped so the whole subtree stays inside the fault layer with an
    // accurate absolute path; reads forward transparently and files (which
    // carry no faultable verbs of their own) are forwarded unwrapped.
    class directory : public idirectory, public std::enable_shared_from_this<directory>
    {
    public:
        directory() = delete;
        directory(std::shared_ptr<idirectory> const& underlying_directory,
                  std::shared_ptr<fault_script> const& script,
                  file_path                            absolute_path);
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
        std::shared_ptr<idirectory>   m_directory;
        std::shared_ptr<fault_script> m_script;
        file_path                     m_absolute_path;
    };

    class filesystem : public ifilesystem, public std::enable_shared_from_this<filesystem>
    {
    public:
        filesystem() = delete;
        filesystem(std::shared_ptr<ifilesystem> const& underlying_filesystem,
                   std::shared_ptr<fault_script> const& script);
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
        std::shared_ptr<ifilesystem>  m_filesystem;
        std::shared_ptr<fault_script> m_script;
    };

    //
    // Webcore facet (D8 / M-HWC-FACETS-3). The fault-injecting wrapper consults
    // the fault script before each activation; if a rule fires, it throws the
    // mapped exception and the activation never reaches the underlying layer.
    //

    class webcore : public iwebcore, public std::enable_shared_from_this<webcore>
    {
    public:
        webcore() = delete;
        webcore(std::shared_ptr<iwebcore> const&     underlying_webcore,
                std::shared_ptr<fault_script> const& script);
        webcore(webcore const&)           = delete;
        webcore(webcore&& other) noexcept = delete;
        ~webcore()                        = default;

        webcore&
        operator=(webcore const&) = delete;
        webcore&
        operator=(webcore&& other) noexcept = delete;

        activate_disposition
        activate(activate_flags                      flags,
                 activation_request const&           request,
                 std::unique_ptr<iwebcore_instance>& returned_instance,
                 std::error_code&                    ec) override;

        set_metadata_disposition
        set_metadata(set_metadata_flags  flags,
                     std::u16string_view type,
                     std::u16string_view value,
                     std::error_code&    ec) override;

    private:
        std::shared_ptr<iwebcore>     m_webcore;
        std::shared_ptr<fault_script> m_script;
    };

    class platform : public iplatform, public std::enable_shared_from_this<platform>
    {
    public:
        platform() = delete;
        platform(std::shared_ptr<iplatform> const&    underlying_platform,
                 std::shared_ptr<fault_script> const& script);
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

    private:
        std::shared_ptr<iplatform>    m_underlying_platform;
        std::shared_ptr<fault_script> m_script;
        std::shared_ptr<registry>     m_registry;
        std::shared_ptr<filesystem>   m_filesystem;
        std::shared_ptr<webcore>      m_webcore;
    };

    // Wrap underlying_platform with the fault-injecting layer driven by script.
    // Unlike the journaling layer, the script is supplied by the caller (it is a
    // separate parsed input artifact), so it is threaded in rather than created
    // internally.
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const&    underlying_platform,
                    std::shared_ptr<fault_script> const& script);

} // namespace m::pil::impl::fault
