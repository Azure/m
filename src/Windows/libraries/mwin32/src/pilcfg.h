// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace m::mwin32_impl
{
    //
    // The parsed contents of a `<executable>.pilcfg` sidecar file. Each field
    // maps directly onto a PIL stack layer that the session can request from
    // m::pil::make_platform_interface. The all-false default is "passthrough":
    // calls flow straight through to the live Win32 registry.
    //
    // This struct deliberately has no dependency on the PIL headers so it can be
    // unit-tested in isolation; the session translates it into
    // m::pil::make_platform_flags.
    //
    struct pilcfg
    {
        // Interpose a buffering layer: registry mutations are captured in memory
        // and are not written through to the live registry. (Mode "buffered".)
        bool buffer_updates = false;

        // Interpose a logging layer that records every registry modification.
        // (Mode "logging".)
        bool record_modifications = false;

        // Registry path redirections. Each pair maps a public path prefix
        // (.first) to the private path (.second) it is transparently rewritten
        // to. Built from the optional "redirections" array; empty by default
        // (no redirection). Interposes a redirecting layer when non-empty. The
        // strings are owned here so they outlive the views handed to PIL.
        std::vector<std::pair<std::u16string, std::u16string>> redirections;

        // Path to a persisted registry-state XML file. When non-empty the
        // session runs entirely against this loaded snapshot and never touches
        // the live registry (mode (c)); the buffer/redirection settings above
        // are ignored in that case. Empty by default. Built from the optional
        // "persisted_state" string member.
        std::u16string persisted_state;

        // Path to which the session writes a snapshot of its registry state when
        // the host process exits. Empty by default (no capture). Built from the
        // optional "capture_snapshot" string member. Intended to be paired with
        // "buffer_updates" so a run's writes are captured into an overlay and
        // persisted to this file without touching the live registry; the file can
        // then be replayed via "persisted_state". A best-effort save: a failure to
        // write the snapshot never crashes the host at exit.
        std::u16string capture_snapshot;

        // Path to which the session writes its diagnostic modification log (the
        // ordered requested-vs-done trace of registry operations) when the host
        // process exits. Empty by default (no log). Built from the optional
        // "diagnostic_log" string member. Intended to be paired with
        // "record_modifications" so the run's writes and deletes are recorded in
        // order and emitted to this file. A best-effort save: a failure to write
        // the log never crashes the host at exit.
        std::u16string diagnostic_log;

        // Path to a `<FaultScript>` XML file (the PIL fault-layer artifact).
        // When non-empty the session layers the fault-injecting platform on top
        // of whatever base stack the other settings selected (live, buffered,
        // redirected, or a persisted snapshot), so configured registry
        // operations fail with the scripted error. Empty by default (no fault
        // injection). Built from the optional "fault_script" string member.
        // Loading the referenced file is best-effort: a missing or malformed
        // fault script leaves the base stack unwrapped rather than breaking the
        // host (tolerant load, per D5/D7).
        std::u16string fault_script;

        // Optional webcore configuration (D-HWC-4, D-HWC-6, D-HWC-7). When
        // present, configures how the webcore surface is accessed.
        struct webcore_config
        {
            // If true, use Detours-based interception to intercept the engine's
            // outbound Reg*/CreateFileW calls (D-HWC-7). If false (default), use
            // materialization: project isolated configs to a temp directory
            // before calling the real engine (D-HWC-4).
            bool interception = false;

            // URL namespace mapping (D-HWC-6): maps public URLs to private
            // sandboxed URLs. Each pair maps a public URL prefix (.first) to the
            // private URL prefix (.second). Empty by default (no remapping).
            std::vector<std::pair<std::u16string, std::u16string>> endpoints;

            // Optional directory for materialized configs. If empty, a per-
            // instance temp directory is created. Only used when interception is
            // false.
            std::u16string materialization_dir;

            // Optional webcore-specific fault script path. When non-empty, the
            // webcore surface is additionally wrapped with fault injection
            // driven by this script (separate from the global fault_script).
            std::u16string fault_script;
        };

        // Optional webcore configuration. std::nullopt means "not configured" —
        // the webcore surface is accessed through the underlying platform with
        // no additional wrapping. A present-but-default webcore_config enables
        // the webcore surface with materialization mode and no endpoints.
        std::optional<webcore_config> webcore;
    };

    //
    // Parse the JSON text of a `.pilcfg` file into a pilcfg. The accepted schema
    // is an object with optional boolean members "buffer_updates" and
    // "record_modifications", an optional "redirections" array of objects
    // each carrying string members "from" and "to", and an optional
    // "persisted_state" string naming a snapshot file, and an optional
    // "fault_script" string naming a `<FaultScript>` file. Absent members keep
    // their default and unknown members are ignored. Throws if the text is not
    // valid JSON, is not a JSON object, a recognized member is present with the
    // wrong type, or a "redirections" element is not an object with string
    // "from" and "to" members.
    //
    pilcfg
    parse_pilcfg(std::string_view json_text);

    //
    // Locate the `<host-executable>.pilcfg` file next to the running module,
    // read it, and parse it. Any failure — file absent, unreadable, or
    // malformed — yields the default (passthrough) configuration rather than
    // throwing, so a missing or broken sidecar never breaks the host process.
    //
    pilcfg
    load_pilcfg();

} // namespace m::mwin32_impl
