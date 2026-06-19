// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <m/error_handling/macros.h>
#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/pil/file_path.h>
#include <m/utility/enum_operations.h>
#include <m/utility/error_macros.h>

//
// Interface (provider) layer for the Hostable Web Core (HWC) isolation surface
// (D-HWC-1, D-HWC-2). Unlike the registry and filesystem surfaces, HWC models a
// live *engine* (hwebcore.dll), not persistent named state: it owns the
// activation lifecycle and forwards three flat C entry points
// (WebCoreActivate / WebCoreShutdown / WebCoreSetMetadata, all HRESULT). The
// engine's config / registry reads are isolated by *composing* the filesystem /
// registry surfaces it reads, so there is no buffered / journaling state model
// here (those facets are M_NOT_IMPLEMENTED, D-HWC-1).
//
// Two interfaces compose the surface:
//   - iwebcore_instance : an opaque RAII activation token; its destruction shuts
//                         the instance down (analogue of ifilesystem_monitor_token).
//   - iwebcore          : the engine surface; activate(...) yields an instance
//                         token, set_metadata(...) forwards engine metadata.
//
// Error model (D-HWC-2): the std::error_code& channel is the non-throwing
// primitive each provider implements; `disposition` carries only contractual
// non-success (the single HWC contract code `already_activated`, the
// ERROR_SERVICE_ALREADY_RUNNING shape), never errors. A thin throwing overload
// wraps the ec primitive.
//

namespace m::pil
{
    //
    // Forward declaration of the public synthetic-HTTP edge seam (D-HWC-11). An
    // activated instance may expose one via `synthetic_http_edge()`. We only
    // name the pointer here, so this header gains no dependency on the contract
    // message types the seam speaks (it is defined in m/pil/synthetic_http_edge.h).
    //
    struct isynthetic_http_edge;

    //
    // The inputs to a single HWC activation. The config paths are carried as
    // `file_path` values — paths *in the isolated filesystem* (D-HWC-2), not raw
    // OS paths — which is what wires the engine's config reads to the isolated
    // FS surface. The root-web config is optional (HWC accepts a null
    // pszRootWebConfigFile); the instance name names the activation.
    //
    struct activation_request
    {
        file_path                app_host_config;
        std::optional<file_path> root_web_config;
        std::u16string           instance_name;
    };

    //
    // An opaque activation token. Destroying it shuts the activated instance
    // down (RAII, like ifilesystem_monitor_token). Whether the shutdown is
    // immediate is selected by activate_flags::immediate_shutdown_on_release at
    // activation time.
    //
    struct iwebcore_instance
    {
        virtual ~iwebcore_instance() {}

        //
        // The activation's in-process synthetic-HTTP edge (D-HWC-11), or null if
        // this instance has none (e.g. the null engine). The edge is the seam a
        // consumer submits drive traffic into and taps crossing traffic from;
        // see m/pil/synthetic_http_edge.h. The returned edge is owned by the
        // instance and valid for the instance's lifetime.
        //
        virtual isynthetic_http_edge* synthetic_http_edge() { return nullptr; }
    };

    //
    // The HWC engine surface. A provider activates the engine against the active
    // platform; the returned instance token owns the activation lifetime.
    //
    struct iwebcore
    {
        virtual ~iwebcore() = default;

        //
        //  activate
        //
        //  Activates the engine with the supplied request, yielding an instance
        //  token in `returned_instance`. The single contractual non-success
        //  outcome is `already_activated` (the HWC ERROR_SERVICE_ALREADY_RUNNING
        //  contract, D-HWC-5): a second activation while one is live returns the
        //  already_activated disposition with a null token rather than failing
        //  through `ec`. All other failures are reported through `ec`.
        //

        enum class activate_flags : uint64_t
        {
            // The instance token's destructor requests an immediate shutdown
            // (WebCoreShutdown(TRUE)) rather than a graceful one.
            immediate_shutdown_on_release = 1ull << 0,
        };

        enum class activate_result_code : uint32_t
        {
            already_activated = 1,
        };

        enum class activate_result_flags : uint32_t
        {
        };

        using activate_disposition =
            disposition<activate_result_code, activate_result_flags>;

        virtual activate_disposition
        activate(activate_flags                      flags,
                 activation_request const&           request,
                 std::unique_ptr<iwebcore_instance>& returned_instance,
                 std::error_code&                    ec) = 0;

        //
        // Throwing wrapper over the ec-form primitive.
        //
        activate_disposition
        activate(activate_flags                      flags,
                 activation_request const&           request,
                 std::unique_ptr<iwebcore_instance>& returned_instance)
        {
            std::error_code ec;
            auto const      d = activate(flags, request, returned_instance, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        // Convenience: activate with default flags, returning the instance token
        // (null if the contractual already_activated outcome was reported).
        //
        std::unique_ptr<iwebcore_instance>
        activate(activation_request const& request)
        {
            std::unique_ptr<iwebcore_instance> returned_instance;
            activate(activate_flags{}, request, returned_instance);
            return returned_instance;
        }

        //
        //  set_metadata
        //
        //  Forwards engine metadata (WebCoreSetMetadata(type, value)). The type
        //  and value are engine-defined strings (not file paths). Failures are
        //  reported through `ec`.
        //

        enum class set_metadata_flags : uint64_t
        {
        };

        enum class set_metadata_result_code : uint32_t
        {
        };

        enum class set_metadata_result_flags : uint32_t
        {
        };

        using set_metadata_disposition =
            disposition<set_metadata_result_code, set_metadata_result_flags>;

        virtual set_metadata_disposition
        set_metadata(set_metadata_flags     flags,
                     std::u16string_view    type,
                     std::u16string_view    value,
                     std::error_code&       ec) = 0;

        //
        // Throwing wrapper over the ec-form primitive.
        //
        set_metadata_disposition
        set_metadata(set_metadata_flags flags, std::u16string_view type, std::u16string_view value)
        {
            std::error_code ec;
            auto const      d = set_metadata(flags, type, value, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        // Convenience: set metadata with default flags.
        //
        void
        set_metadata(std::u16string_view type, std::u16string_view value)
        {
            set_metadata(set_metadata_flags{}, type, value);
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iwebcore::activate_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iwebcore::activate_result_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iwebcore::set_metadata_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iwebcore::set_metadata_result_flags);

    //
    // A placeholder instance token for the null provider. Holds nothing; its
    // destruction shuts nothing down.
    //
    struct null_webcore_instance final : iwebcore_instance
    {
    };

    //
    // A placeholder engine surface that resolves through the platform stack but
    // has no live engine behind it. Every operation throws "not implemented".
    // The base iplatform wiring hands one of these out (see
    // iplatform::get_webcore) unless a provider overrides it (the direct/Windows
    // platform, M-HWC-DIRECT), mirroring how null_filesystem backs the default
    // get_filesystem (D9 / D-HWC-2).
    //
    struct null_webcore final : iwebcore
    {
        activate_disposition
        activate(activate_flags,
                 activation_request const&,
                 std::unique_ptr<iwebcore_instance>&,
                 std::error_code&) override
        {
            M_NOT_IMPLEMENTED("null_webcore::activate");
        }

        set_metadata_disposition
        set_metadata(set_metadata_flags,
                     std::u16string_view,
                     std::u16string_view,
                     std::error_code&) override
        {
            M_NOT_IMPLEMENTED("null_webcore::set_metadata");
        }
    };

} // namespace m::pil
