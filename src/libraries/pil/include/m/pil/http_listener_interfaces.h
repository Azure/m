// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/utility/enum_operations.h>
#include <m/utility/error_macros.h>

//
// Interface (provider) layer for the HTTP listener isolation surface (D-HWC-6).
// This surface models the HTTP Server API (http.sys) namespace — specifically
// the URL reservation and request handling lifecycle. It enables redirection of
// public endpoints (host:port) to private endpoints (loopback + ephemeral port)
// so that HWC activations do not reserve production URLs on the box.
//
// Two tiers of implementation (D-HWC-6):
//   - Tier A: Real http.sys with private namespace. The public endpoint is
//             rewritten to loopback + ephemeral port; URL-ACL and cert bindings
//             are synthesized for the private prefix. Requests reach http.sys.
//   - Tier B: Fake http.sys. The HTTP Server API (receive/send) is intercepted
//             and requests are fed from an in-process queue. No http.sys, no
//             admin, fully deterministic unit-test edge.
//
// Error model: like iwebcore, the std::error_code& channel is the non-throwing
// primitive; `disposition` carries only contractual non-success. The null
// provider returns M_NOT_IMPLEMENTED for all operations.
//

namespace m::pil
{
    //--------------------------------------------------------------------------
    // http_endpoint — a host:port pair identifying an HTTP endpoint
    //--------------------------------------------------------------------------

    struct http_endpoint
    {
        std::u16string host;
        std::uint16_t  port{0};

        // Default-constructed endpoint is empty.
        http_endpoint() = default;

        http_endpoint(std::u16string_view h, std::uint16_t p)
            : host(h)
            , port(p)
        {
        }

        http_endpoint(char16_t const* h, std::uint16_t p)
            : host(h)
            , port(p)
        {
        }

        bool
        empty() const noexcept
        {
            return host.empty() && port == 0;
        }

        bool
        operator==(http_endpoint const& other) const noexcept
        {
            return host == other.host && port == other.port;
        }

        bool
        operator!=(http_endpoint const& other) const noexcept
        {
            return !(*this == other);
        }
    };

    //--------------------------------------------------------------------------
    // endpoint_mapping — a public↔private endpoint mapping
    //--------------------------------------------------------------------------

    struct endpoint_mapping
    {
        http_endpoint public_endpoint;
        http_endpoint private_endpoint;

        endpoint_mapping() = default;

        endpoint_mapping(http_endpoint pub, http_endpoint priv)
            : public_endpoint(std::move(pub))
            , private_endpoint(std::move(priv))
        {
        }
    };

    //--------------------------------------------------------------------------
    // ihttp_listener_session — RAII token for an HTTP listener session
    //--------------------------------------------------------------------------
    //
    // An activation token representing an active HTTP listening session. When
    // destroyed, the session is torn down and any remappings released.
    //

    struct ihttp_listener_session
    {
        virtual ~ihttp_listener_session() = default;

        //
        // Returns the active endpoint mappings for this session.
        //
        virtual std::vector<endpoint_mapping> const&
        mappings() const = 0;

        //
        // Look up the private endpoint for a given public endpoint.
        // Returns nullopt if no mapping exists.
        //
        virtual std::optional<http_endpoint>
        lookup_private(http_endpoint const& public_ep) const = 0;

        //
        // Look up the public endpoint for a given private endpoint.
        // Returns nullopt if no mapping exists.
        //
        virtual std::optional<http_endpoint>
        lookup_public(http_endpoint const& private_ep) const = 0;
    };

    //--------------------------------------------------------------------------
    // ihttp_listener — the HTTP listener surface
    //--------------------------------------------------------------------------

    struct ihttp_listener
    {
        virtual ~ihttp_listener() = default;

        //
        //  create_session
        //
        //  Creates a new HTTP listener session with the given endpoint mappings.
        //  Each mapping specifies a public endpoint that should be remapped to a
        //  private endpoint. If no private endpoint is specified for a mapping,
        //  one is allocated automatically (loopback + ephemeral port).
        //

        enum class create_session_flags : uint64_t
        {
            // Allocate ephemeral ports for any mappings without explicit private endpoints.
            allocate_ephemeral_ports = 1ull << 0,
        };

        enum class create_session_result_code : uint32_t
        {
            // A session already exists for one of the requested public endpoints.
            endpoint_already_mapped = 1,

            // The requested private endpoint is already in use.
            private_endpoint_in_use = 2,
        };

        enum class create_session_result_flags : uint32_t
        {
        };

        using create_session_disposition =
            disposition<create_session_result_code, create_session_result_flags>;

        virtual create_session_disposition
        create_session(create_session_flags                       flags,
                       std::span<endpoint_mapping const>          mappings,
                       std::unique_ptr<ihttp_listener_session>&   returned_session,
                       std::error_code&                           ec) = 0;

        //
        // Throwing wrapper.
        //
        create_session_disposition
        create_session(create_session_flags                       flags,
                       std::span<endpoint_mapping const>          mappings,
                       std::unique_ptr<ihttp_listener_session>&   returned_session)
        {
            std::error_code ec;
            auto const      d = create_session(flags, mappings, returned_session, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        // Convenience: create session with default flags.
        //
        std::unique_ptr<ihttp_listener_session>
        create_session(std::span<endpoint_mapping const> mappings)
        {
            std::unique_ptr<ihttp_listener_session> returned_session;
            create_session(create_session_flags::allocate_ephemeral_ports,
                           mappings,
                           returned_session);
            return returned_session;
        }

        //
        //  remap
        //
        //  Adds a single endpoint mapping to an existing session. This is a
        //  convenience for adding mappings one at a time rather than all at
        //  session creation.
        //

        enum class remap_flags : uint64_t
        {
            // Allocate an ephemeral private port if none specified.
            allocate_ephemeral_port = 1ull << 0,
        };

        enum class remap_result_code : uint32_t
        {
            // The public endpoint is already mapped.
            endpoint_already_mapped = 1,

            // The private endpoint is already in use.
            private_endpoint_in_use = 2,

            // No active session.
            no_active_session = 3,
        };

        enum class remap_result_flags : uint32_t
        {
        };

        using remap_disposition = disposition<remap_result_code, remap_result_flags>;

        virtual remap_disposition
        remap(remap_flags                     flags,
              ihttp_listener_session&         session,
              http_endpoint const&            public_endpoint,
              std::optional<http_endpoint>    private_endpoint,
              http_endpoint&                  returned_private_endpoint,
              std::error_code&                ec) = 0;

        //
        // Throwing wrapper.
        //
        remap_disposition
        remap(remap_flags                     flags,
              ihttp_listener_session&         session,
              http_endpoint const&            public_endpoint,
              std::optional<http_endpoint>    private_endpoint,
              http_endpoint&                  returned_private_endpoint)
        {
            std::error_code ec;
            auto const      d = remap(flags, session, public_endpoint, private_endpoint,
                                      returned_private_endpoint, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        // Convenience: remap with ephemeral port allocation.
        //
        http_endpoint
        remap(ihttp_listener_session& session, http_endpoint const& public_endpoint)
        {
            http_endpoint returned;
            remap(remap_flags::allocate_ephemeral_port,
                  session,
                  public_endpoint,
                  std::nullopt,
                  returned);
            return returned;
        }

        //
        //  unmap
        //
        //  Removes a mapping from a session.
        //

        enum class unmap_flags : uint64_t
        {
        };

        enum class unmap_result_code : uint32_t
        {
            // The public endpoint was not mapped.
            endpoint_not_mapped = 1,

            // No active session.
            no_active_session = 2,
        };

        enum class unmap_result_flags : uint32_t
        {
        };

        using unmap_disposition = disposition<unmap_result_code, unmap_result_flags>;

        virtual unmap_disposition
        unmap(unmap_flags              flags,
              ihttp_listener_session&  session,
              http_endpoint const&     public_endpoint,
              std::error_code&         ec) = 0;

        //
        // Throwing wrapper.
        //
        unmap_disposition
        unmap(unmap_flags              flags,
              ihttp_listener_session&  session,
              http_endpoint const&     public_endpoint)
        {
            std::error_code ec;
            auto const      d = unmap(flags, session, public_endpoint, ec);
            if (ec)
                throw std::system_error(ec);
            return d;
        }

        //
        // Convenience: unmap with default flags.
        //
        void
        unmap(ihttp_listener_session& session, http_endpoint const& public_endpoint)
        {
            unmap(unmap_flags{}, session, public_endpoint);
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_listener::create_session_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_listener::create_session_result_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_listener::remap_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_listener::remap_result_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_listener::unmap_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(ihttp_listener::unmap_result_flags);

    //--------------------------------------------------------------------------
    // null_http_listener_session — placeholder session for the null provider
    //--------------------------------------------------------------------------

    class null_http_listener_session final : public ihttp_listener_session
    {
    public:
        null_http_listener_session()  = default;
        ~null_http_listener_session() override = default;

        std::vector<endpoint_mapping> const&
        mappings() const override
        {
            return m_empty_mappings;
        }

        std::optional<http_endpoint>
        lookup_private(http_endpoint const&) const override
        {
            return std::nullopt;
        }

        std::optional<http_endpoint>
        lookup_public(http_endpoint const&) const override
        {
            return std::nullopt;
        }

    private:
        std::vector<endpoint_mapping> m_empty_mappings;
    };

    //--------------------------------------------------------------------------
    // null_http_listener — the null provider (M_NOT_IMPLEMENTED)
    //--------------------------------------------------------------------------

    class null_http_listener final : public ihttp_listener
    {
    public:
        null_http_listener()  = default;
        ~null_http_listener() override = default;

        create_session_disposition
        create_session(create_session_flags,
                       std::span<endpoint_mapping const>,
                       std::unique_ptr<ihttp_listener_session>&,
                       std::error_code& ec) override
        {
            ec = std::make_error_code(std::errc::function_not_supported);
            return {};
        }

        remap_disposition
        remap(remap_flags,
              ihttp_listener_session&,
              http_endpoint const&,
              std::optional<http_endpoint>,
              http_endpoint&,
              std::error_code& ec) override
        {
            ec = std::make_error_code(std::errc::function_not_supported);
            return {};
        }

        unmap_disposition
        unmap(unmap_flags,
              ihttp_listener_session&,
              http_endpoint const&,
              std::error_code& ec) override
        {
            ec = std::make_error_code(std::errc::function_not_supported);
            return {};
        }
    };

} // namespace m::pil
