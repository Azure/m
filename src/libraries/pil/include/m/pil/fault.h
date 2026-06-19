// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include <m/pil/file_path.h>
#include <m/pil/key_path.h>
#include <m/pil/platform.h>
#include <m/pil/registry_base_types.h>

#include <pugixml.hpp>

//
// Public surface for the fault-injecting layer (D8). The fault layer is a
// transparent decorator stack driven by a declarative, stateful fault script:
// a separate input artifact (never part of a persisted <Platform>) whose
// counted rules map an operation on a target path to an injected failure.
//
// This header mirrors the make_platform_interface / load_platform_interface
// surface in <m/pil/pil.h>: a fault script is constructed (programmatically or
// parsed from XML) and then layered over an existing platform-interface stack
// with apply_fault_layer.
//

namespace m::pil
{
    namespace impl::fault
    {
        // Internal representation; defined in src/fault/fault.h. Only an opaque
        // shared_ptr crosses the public boundary.
        class fault_script;
    } // namespace impl::fault

    // The registry operation a fault rule targets. The spellings accepted in the
    // <FaultScript> artifact are fixed by the grammar; changing any value is a
    // breaking change to the artifact.
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

        // Filesystem operations (M-FS-FAULT). These target a file_path; their
        // <FaultScript> spellings are the filesystem verbs (create_directory,
        // create_file, open_directory, open_file, remove_entry,
        // delete_tree_entry, rename_entry), distinct from the registry verbs.
        create_directory,
        create_file,
        open_directory,
        open_file,
        remove_entry,
        delete_tree_entry,
        rename_entry,
    };

    // The failure a fired rule injects. Each maps to the real m:: exception the
    // platform raises for that status, so a consumer exercises its genuine
    // error-handling path. Changing any value is a breaking change to the
    // artifact grammar.
    enum class fault_action : std::uint32_t
    {
        not_found,
        access_denied,
        out_of_resources,
        sharing_violation,
        already_exists,
        not_supported,
    };

    //
    // A counted-rule fault script (D8). Build one programmatically with
    // add_rule, or parse one from the <FaultScript> grammar via
    // parse_fault_script / load_fault_script. A script can be shared by exactly
    // one fault layer at a time; its rule counters are advanced as the layered
    // platform is exercised.
    //
    class fault_script
    {
    public:
        // Construct an empty script (no rules).
        fault_script();

        fault_script(fault_script const&)            = default;
        fault_script(fault_script&&) noexcept         = default;
        fault_script& operator=(fault_script const&)  = default;
        fault_script& operator=(fault_script&&) noexcept = default;
        ~fault_script()                               = default;

        //
        // Append a counted rule. The rule fires on exactly the occurrence-th
        // matching operation (1-based; occurrence must be >= 1) and not again.
        // A non-null value_name additionally constrains value operations to
        // that value name. target is the absolute, root-prefixed key path the
        // rule matches (case-insensitively).
        //
        void
        add_rule(fault_operation                       op,
                 key_path const&                       target,
                 std::optional<value_name_string_type> value_name,
                 std::uint64_t                         occurrence,
                 fault_action                          action);

        //
        // Append a counted filesystem rule. The rule fires on exactly the
        // occurrence-th matching filesystem operation (1-based; occurrence must
        // be >= 1) on target, an absolute, root-prefixed file_path matched
        // case-insensitively. Filesystem operations carry no value-name
        // constraint.
        //
        void
        add_rule(fault_operation  op,
                 file_path const& target,
                 std::uint64_t    occurrence,
                 fault_action     action);

        //
        // Accessor for the internal script used by apply_fault_layer. Not part
        // of the stable contract; present because the public type is a thin
        // handle over the internal representation.
        //
        std::shared_ptr<impl::fault::fault_script> const&
        get_impl() const noexcept;

    private:
        explicit fault_script(std::shared_ptr<impl::fault::fault_script> impl) noexcept;

        friend fault_script
        parse_fault_script(pugi::xml_node const& fault_script_node);

        std::shared_ptr<impl::fault::fault_script> m_impl;
    };

    //
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
    // optional valueName further constrains value operations. An unknown
    // operation/action spelling, a missing required attribute, or occurrence < 1
    // throws m::invalid_parameter.
    //
    fault_script
    parse_fault_script(pugi::xml_node const& fault_script_node);

    //
    // Load and parse a fault script from a file whose document element is a
    // <FaultScript>. Throws if the file cannot be loaded; parse failures throw
    // as parse_fault_script.
    //
    fault_script
    load_fault_script(std::filesystem::path const& path);

    //
    // Wrap an underlying platform-interface stack with the fault-injecting layer
    // driven by script, returning the wrapped interface. Mirrors
    // make_platform_interface / load_platform_interface in <m/pil/pil.h>.
    //
    std::shared_ptr<iplatform>
    apply_fault_layer(std::shared_ptr<iplatform> const& underlying_platform,
                      fault_script const&               script);
} // namespace m::pil
