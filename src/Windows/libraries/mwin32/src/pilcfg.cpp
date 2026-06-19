// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <m/strings/convert.h>

#include "pilcfg.h"

namespace m::mwin32_impl
{
    namespace
    {
        // JSON member names recognized in a .pilcfg file. Adding, removing, or
        // renaming any of these is a breaking change to the sidecar format.
        constexpr std::string_view k_buffer_updates       = "buffer_updates";
        constexpr std::string_view k_record_modifications = "record_modifications";
        constexpr std::string_view k_redirections         = "redirections";
        constexpr std::string_view k_from                 = "from";
        constexpr std::string_view k_to                   = "to";
        constexpr std::string_view k_persisted_state      = "persisted_state";
        constexpr std::string_view k_capture_snapshot     = "capture_snapshot";
        constexpr std::string_view k_diagnostic_log       = "diagnostic_log";
        constexpr std::string_view k_fault_script         = "fault_script";

        // Webcore configuration JSON member names.
        constexpr std::string_view k_webcore              = "webcore";
        constexpr std::string_view k_interception         = "interception";
        constexpr std::string_view k_endpoints            = "endpoints";
        constexpr std::string_view k_public               = "public";
        constexpr std::string_view k_private              = "private";
        constexpr std::string_view k_materialization_dir  = "materialization_dir";

        // Webcore contract-binding JSON member names (D-HWC-8).
        constexpr std::string_view k_contracts            = "contracts";
        constexpr std::string_view k_spec                 = "spec";
        constexpr std::string_view k_endpoint             = "endpoint";
        constexpr std::string_view k_mode                 = "mode";
        constexpr std::string_view k_mode_validate        = "validate";
        constexpr std::string_view k_mode_drive           = "drive";

        // Wire-capture configuration JSON member names (WC-4). The "mode",
        // "spec", and "validate" names are shared with the webcore contract
        // schema above; "record" and "host" are specific to capture.
        constexpr std::string_view k_capture              = "capture";
        constexpr std::string_view k_host                 = "host";
        constexpr std::string_view k_mode_record          = "record";

        // Upper bound on the module path length we will accept, to bound the
        // GetModuleFileNameW growth loop. Far larger than any real path.
        constexpr DWORD k_max_module_path_chars = 0x10000;

        // Expands Windows %VAR% environment-variable references in a host-path
        // member so a checked-in .pilcfg can resolve to per-machine locations
        // (e.g. "%TEMP%\\snapshot.xml"). Specified behavior (owned by us): a
        // %NAME% token is replaced by the value of environment variable NAME; an
        // undefined token is left verbatim, and a string with no % tokens is
        // returned unchanged. Only members that denote a host filesystem path
        // are expanded; logical namespace identifiers (redirection keys, webcore
        // endpoints) are taken literally so a legitimate '%' in a key is never
        // disturbed. Implemented with ExpandEnvironmentStringsW; on any failure
        // the literal value is returned.
        std::u16string
        expand_environment_path(std::u16string const& value)
        {
            if (value.empty())
                return value;

            auto const* src = reinterpret_cast<wchar_t const*>(value.c_str());

            // The first call returns the required buffer size in characters,
            // including the terminating null.
            DWORD const needed = ::ExpandEnvironmentStringsW(src, nullptr, 0);
            if (needed == 0)
                return value;

            std::wstring expanded(needed, L'\0');
            DWORD const  written = ::ExpandEnvironmentStringsW(src, expanded.data(), needed);
            if (written == 0 || written > needed)
                return value;

            // `written` counts the terminating null; drop it.
            expanded.resize(written - 1);
            return std::u16string(reinterpret_cast<char16_t const*>(expanded.data()),
                                  expanded.size());
        }

        bool
        read_bool_member(nlohmann::json const& j, std::string_view name)
        {
            auto const it = j.find(name);
            if (it == j.end())
                return false;

            if (!it->is_boolean())
                throw std::runtime_error(std::string("pilcfg: '") + std::string(name) +
                                         "' must be a boolean");

            return it->get<bool>();
        }

        // Parse the optional "persisted_state" string member. Absent yields an
        // empty string; present but non-string throws.
        std::u16string
        read_persisted_state_member(nlohmann::json const& j)
        {
            auto const it = j.find(k_persisted_state);
            if (it == j.end())
                return {};

            if (!it->is_string())
                throw std::runtime_error("pilcfg: 'persisted_state' must be a string");

            auto const& s = it->get_ref<nlohmann::json::string_t const&>();
            auto const  u8 =
                std::u8string_view(reinterpret_cast<char8_t const*>(s.data()), s.size());
            return expand_environment_path(m::to_u16string(u8));
        }

        // Parse the optional "capture_snapshot" string member. Absent yields an
        // empty string; present but non-string throws.
        std::u16string
        read_capture_snapshot_member(nlohmann::json const& j)
        {
            auto const it = j.find(k_capture_snapshot);
            if (it == j.end())
                return {};

            if (!it->is_string())
                throw std::runtime_error("pilcfg: 'capture_snapshot' must be a string");

            auto const& s = it->get_ref<nlohmann::json::string_t const&>();
            auto const  u8 =
                std::u8string_view(reinterpret_cast<char8_t const*>(s.data()), s.size());
            return expand_environment_path(m::to_u16string(u8));
        }

        // Parse the optional "diagnostic_log" string member. Absent yields an
        // empty string; present but non-string throws.
        std::u16string
        read_diagnostic_log_member(nlohmann::json const& j)
        {
            auto const it = j.find(k_diagnostic_log);
            if (it == j.end())
                return {};

            if (!it->is_string())
                throw std::runtime_error("pilcfg: 'diagnostic_log' must be a string");

            auto const& s = it->get_ref<nlohmann::json::string_t const&>();
            auto const  u8 =
                std::u8string_view(reinterpret_cast<char8_t const*>(s.data()), s.size());
            return expand_environment_path(m::to_u16string(u8));
        }

        // Parse the optional "fault_script" string member. Absent yields an
        // empty string; present but non-string throws.
        std::u16string
        read_fault_script_member(nlohmann::json const& j)
        {
            auto const it = j.find(k_fault_script);
            if (it == j.end())
                return {};

            if (!it->is_string())
                throw std::runtime_error("pilcfg: 'fault_script' must be a string");

            auto const& s = it->get_ref<nlohmann::json::string_t const&>();
            auto const  u8 =
                std::u8string_view(reinterpret_cast<char8_t const*>(s.data()), s.size());
            return expand_environment_path(m::to_u16string(u8));
        }

        // JSON text is UTF-8 by definition, so the bytes are reinterpreted as
        // char8_t before transcoding to char16_t.
        std::u16string
        json_string_to_u16(nlohmann::json const& value, std::string_view member)
        {
            if (!value.is_string())
                throw std::runtime_error(std::string("pilcfg: '") +
                                         std::string(member) + "' must be a string");

            auto const& s = value.get_ref<nlohmann::json::string_t const&>();
            auto const  u8 =
                std::u8string_view(reinterpret_cast<char8_t const*>(s.data()), s.size());
            return m::to_u16string(u8);
        }

        // Parse an optional string member from a JSON object. Returns empty
        // string if absent; throws if present but not a string.
        std::u16string
        read_optional_string_member(nlohmann::json const& j, std::string_view name)
        {
            auto const it = j.find(name);
            if (it == j.end())
                return {};

            return json_string_to_u16(*it, name);
        }

        // Parse the optional "webcore.endpoints" array. Absent yields an empty
        // vector. Present must be an array whose every element is an object
        // carrying string "public" and "private" members; anything else throws.
        std::vector<std::pair<std::u16string, std::u16string>>
        read_endpoints_member(nlohmann::json const& j)
        {
            std::vector<std::pair<std::u16string, std::u16string>> endpoints;

            auto const it = j.find(k_endpoints);
            if (it == j.end())
                return endpoints;

            if (!it->is_array())
                throw std::runtime_error("pilcfg: 'webcore.endpoints' must be an array");

            for (auto const& element: *it)
            {
                if (!element.is_object())
                    throw std::runtime_error(
                        "pilcfg: each 'webcore.endpoints' element must be an object");

                auto const public_it  = element.find(k_public);
                auto const private_it = element.find(k_private);

                if (public_it == element.end() || private_it == element.end())
                    throw std::runtime_error(
                        "pilcfg: each 'webcore.endpoints' element must have "
                        "'public' and 'private' members");

                endpoints.emplace_back(json_string_to_u16(*public_it, k_public),
                                       json_string_to_u16(*private_it, k_private));
            }

            return endpoints;
        }

        // Parse the optional "webcore.contracts" array (D-HWC-8). Absent yields an
        // empty vector. Present must be an array whose every element is an object
        // carrying non-empty string "spec" and "endpoint" members and a "mode"
        // member that is "validate" or "drive"; anything else throws. The "spec"
        // host path is %VAR%-expanded; "endpoint" is a logical key taken
        // literally (like the keys in "endpoints"). Order is preserved.
        std::vector<pilcfg::webcore_config::contract_binding>
        read_contracts_member(nlohmann::json const& j)
        {
            std::vector<pilcfg::webcore_config::contract_binding> contracts;

            auto const it = j.find(k_contracts);
            if (it == j.end())
                return contracts;

            if (!it->is_array())
                throw std::runtime_error("pilcfg: 'webcore.contracts' must be an array");

            for (auto const& element: *it)
            {
                if (!element.is_object())
                    throw std::runtime_error(
                        "pilcfg: each 'webcore.contracts' element must be an object");

                auto const spec_it     = element.find(k_spec);
                auto const endpoint_it = element.find(k_endpoint);
                auto const mode_it     = element.find(k_mode);

                if (spec_it == element.end() || endpoint_it == element.end() ||
                    mode_it == element.end())
                    throw std::runtime_error(
                        "pilcfg: each 'webcore.contracts' element must have "
                        "'spec', 'endpoint', and 'mode' members");

                pilcfg::webcore_config::contract_binding binding;
                binding.spec     = expand_environment_path(json_string_to_u16(*spec_it, k_spec));
                binding.endpoint = json_string_to_u16(*endpoint_it, k_endpoint);

                if (binding.spec.empty())
                    throw std::runtime_error(
                        "pilcfg: 'webcore.contracts' 'spec' must be a non-empty string");
                if (binding.endpoint.empty())
                    throw std::runtime_error(
                        "pilcfg: 'webcore.contracts' 'endpoint' must be a non-empty string");

                if (!mode_it->is_string())
                    throw std::runtime_error(
                        "pilcfg: 'webcore.contracts' 'mode' must be a string");

                auto const& mode_s = mode_it->get_ref<nlohmann::json::string_t const&>();
                if (mode_s == k_mode_validate)
                    binding.mode = pilcfg::webcore_config::contract_mode::validate;
                else if (mode_s == k_mode_drive)
                    binding.mode = pilcfg::webcore_config::contract_mode::drive;
                else
                    throw std::runtime_error(
                        "pilcfg: 'webcore.contracts' 'mode' must be 'validate' or 'drive'");

                contracts.push_back(std::move(binding));
            }

            return contracts;
        }

        std::optional<pilcfg::webcore_config>
        read_webcore_member(nlohmann::json const& j)
        {
            auto const it = j.find(k_webcore);
            if (it == j.end())
                return std::nullopt;

            if (!it->is_object())
                throw std::runtime_error("pilcfg: 'webcore' must be an object");

            pilcfg::webcore_config cfg;
            cfg.interception       = read_bool_member(*it, k_interception);
            cfg.endpoints          = read_endpoints_member(*it);
            cfg.contracts          = read_contracts_member(*it);
            cfg.materialization_dir =
                expand_environment_path(read_optional_string_member(*it, k_materialization_dir));
            cfg.fault_script =
                expand_environment_path(read_optional_string_member(*it, k_fault_script));
            return cfg;
        }

        // Parse the optional "capture" object (WC-4). Absent yields
        // std::nullopt. Present must be an object with a required "mode" string
        // ("record" or "validate"), a required non-empty "spec" string (the
        // contract path, %VAR%-expanded), and an optional "host" string filter;
        // anything else throws.
        std::optional<pilcfg::capture_config>
        read_capture_member(nlohmann::json const& j)
        {
            auto const it = j.find(k_capture);
            if (it == j.end())
                return std::nullopt;

            if (!it->is_object())
                throw std::runtime_error("pilcfg: 'capture' must be an object");

            auto const& obj      = *it;
            auto const  mode_it  = obj.find(k_mode);
            auto const  spec_it  = obj.find(k_spec);

            if (mode_it == obj.end() || spec_it == obj.end())
                throw std::runtime_error(
                    "pilcfg: 'capture' must have 'mode' and 'spec' members");

            pilcfg::capture_config cfg;

            if (!mode_it->is_string())
                throw std::runtime_error("pilcfg: 'capture' 'mode' must be a string");

            auto const& mode_s = mode_it->get_ref<nlohmann::json::string_t const&>();
            if (mode_s == k_mode_record)
                cfg.mode = pilcfg::capture_config::capture_mode::record;
            else if (mode_s == k_mode_validate)
                cfg.mode = pilcfg::capture_config::capture_mode::validate;
            else
                throw std::runtime_error(
                    "pilcfg: 'capture' 'mode' must be 'record' or 'validate'");

            cfg.spec = expand_environment_path(json_string_to_u16(*spec_it, k_spec));
            if (cfg.spec.empty())
                throw std::runtime_error(
                    "pilcfg: 'capture' 'spec' must be a non-empty string");

            // Optional Host-header filter: a literal UTF-8 value (not
            // %VAR%-expanded) so it compares directly against reassembled
            // header values.
            auto const host_it = obj.find(k_host);
            if (host_it != obj.end())
            {
                if (!host_it->is_string())
                    throw std::runtime_error("pilcfg: 'capture' 'host' must be a string");
                cfg.host = host_it->get_ref<nlohmann::json::string_t const&>();
            }

            return cfg;
        }

        // Parse the optional "redirections" array. Absent yields an empty
        // vector. Present must be an array whose every element is an object
        // carrying string "from" and "to" members; anything else throws.
        std::vector<std::pair<std::u16string, std::u16string>>
        read_redirections_member(nlohmann::json const& j)
        {
            std::vector<std::pair<std::u16string, std::u16string>> redirections;

            auto const it = j.find(k_redirections);
            if (it == j.end())
                return redirections;

            if (!it->is_array())
                throw std::runtime_error("pilcfg: 'redirections' must be an array");

            for (auto const& element: *it)
            {
                if (!element.is_object())
                    throw std::runtime_error(
                        "pilcfg: each 'redirections' element must be an object");

                auto const from_it = element.find(k_from);
                auto const to_it   = element.find(k_to);

                if (from_it == element.end() || to_it == element.end())
                    throw std::runtime_error(
                        "pilcfg: each 'redirections' element must have 'from' and 'to' members");

                redirections.emplace_back(json_string_to_u16(*from_it, k_from),
                                          json_string_to_u16(*to_it, k_to));
            }

            return redirections;
        }

        // Full path of the host executable's `.pilcfg` sidecar, or empty on
        // failure. Uses GetModuleFileNameW(nullptr, ...) so the path is the
        // process executable, not this DLL.
        std::filesystem::path
        sidecar_path()
        {
            std::wstring buffer;
            DWORD        capacity = MAX_PATH;

            for (;;)
            {
                buffer.resize(capacity);

                DWORD const written = GetModuleFileNameW(nullptr, buffer.data(), capacity);
                if (written == 0)
                    return {};

                if (written < capacity)
                {
                    buffer.resize(written);
                    break;
                }

                // Truncated: the return value equals the buffer size. Grow and
                // retry, but give up rather than loop unboundedly.
                if (capacity >= k_max_module_path_chars)
                    return {};

                capacity *= 2;
            }

            auto path = std::filesystem::path(buffer);
            path += L".pilcfg";
            return path;
        }
    } // namespace

    pilcfg
    parse_pilcfg(std::string_view json_text)
    {
        auto const j = nlohmann::json::parse(json_text);

        if (!j.is_object())
            throw std::runtime_error("pilcfg: root must be a JSON object");

        pilcfg cfg;
        cfg.buffer_updates       = read_bool_member(j, k_buffer_updates);
        cfg.record_modifications = read_bool_member(j, k_record_modifications);
        cfg.redirections         = read_redirections_member(j);
        cfg.persisted_state      = read_persisted_state_member(j);
        cfg.capture_snapshot     = read_capture_snapshot_member(j);
        cfg.diagnostic_log       = read_diagnostic_log_member(j);
        cfg.fault_script         = read_fault_script_member(j);
        cfg.webcore              = read_webcore_member(j);
        cfg.capture              = read_capture_member(j);
        return cfg;
    }

    pilcfg
    load_pilcfg()
    {
        try
        {
            auto const path = sidecar_path();
            if (path.empty())
                return pilcfg{};

            std::ifstream file(path, std::ios::binary);
            if (!file)
                return pilcfg{};

            std::string text((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

            return parse_pilcfg(text);
        }
        catch (...)
        {
            // A missing or malformed sidecar must never break the host process;
            // fall back to passthrough.
            return pilcfg{};
        }
    }

} // namespace m::mwin32_impl
