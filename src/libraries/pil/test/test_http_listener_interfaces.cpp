// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <span>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <m/pil/http_listener_interfaces.h>

//
// Exercises the HTTP listener interface contracts (ihttp_listener /
// ihttp_listener_session) against the null provider. The point is to verify
// that the interface surface, enums, and wrapper types compile and link
// correctly.
//

namespace
{
    using m::pil::endpoint_mapping;
    using m::pil::http_endpoint;
    using m::pil::ihttp_listener;
    using m::pil::ihttp_listener_session;
    using m::pil::null_http_listener;
    using m::pil::null_http_listener_session;

    //--------------------------------------------------------------------------
    // http_endpoint tests
    //--------------------------------------------------------------------------

    TEST(HttpEndpoint, DefaultConstructsEmpty)
    {
        http_endpoint ep;
        EXPECT_TRUE(ep.empty());
        EXPECT_TRUE(ep.host.empty());
        EXPECT_EQ(ep.port, 0);
    }

    TEST(HttpEndpoint, ConstructsWithHostAndPort)
    {
        http_endpoint ep(u"localhost", 8080);
        EXPECT_FALSE(ep.empty());
        EXPECT_EQ(ep.host, u"localhost");
        EXPECT_EQ(ep.port, 8080);
    }

    TEST(HttpEndpoint, ConstructsWithStringViewAndPort)
    {
        std::u16string_view host = u"example.com";
        http_endpoint       ep(host, 443);
        EXPECT_FALSE(ep.empty());
        EXPECT_EQ(ep.host, u"example.com");
        EXPECT_EQ(ep.port, 443);
    }

    TEST(HttpEndpoint, EqualityOperator)
    {
        http_endpoint ep1(u"localhost", 8080);
        http_endpoint ep2(u"localhost", 8080);
        http_endpoint ep3(u"localhost", 9090);
        http_endpoint ep4(u"example.com", 8080);

        EXPECT_EQ(ep1, ep2);
        EXPECT_NE(ep1, ep3);
        EXPECT_NE(ep1, ep4);
    }

    TEST(HttpEndpoint, CopyAndMove)
    {
        http_endpoint original(u"localhost", 8080);
        http_endpoint copy = original;
        EXPECT_EQ(copy, original);

        http_endpoint moved = std::move(copy);
        EXPECT_EQ(moved.host, u"localhost");
        EXPECT_EQ(moved.port, 8080);
    }

    //--------------------------------------------------------------------------
    // endpoint_mapping tests
    //--------------------------------------------------------------------------

    TEST(EndpointMapping, DefaultConstructs)
    {
        endpoint_mapping mapping;
        EXPECT_TRUE(mapping.public_endpoint.empty());
        EXPECT_TRUE(mapping.private_endpoint.empty());
    }

    TEST(EndpointMapping, ConstructsWithEndpoints)
    {
        http_endpoint    pub(u"www.example.com", 443);
        http_endpoint    priv(u"127.0.0.1", 49152);
        endpoint_mapping mapping(pub, priv);

        EXPECT_EQ(mapping.public_endpoint.host, u"www.example.com");
        EXPECT_EQ(mapping.public_endpoint.port, 443);
        EXPECT_EQ(mapping.private_endpoint.host, u"127.0.0.1");
        EXPECT_EQ(mapping.private_endpoint.port, 49152);
    }

    //--------------------------------------------------------------------------
    // null_http_listener_session tests
    //--------------------------------------------------------------------------

    TEST(NullHttpListenerSession, DefaultConstructs)
    {
        null_http_listener_session session;
        // The null session does nothing; this proves construction works.
    }

    TEST(NullHttpListenerSession, MappingsReturnsEmpty)
    {
        null_http_listener_session session;
        auto const&                mappings = session.mappings();
        EXPECT_TRUE(mappings.empty());
    }

    TEST(NullHttpListenerSession, LookupPrivateReturnsNullopt)
    {
        null_http_listener_session session;
        http_endpoint              pub(u"localhost", 80);
        auto                       result = session.lookup_private(pub);
        EXPECT_FALSE(result.has_value());
    }

    TEST(NullHttpListenerSession, LookupPublicReturnsNullopt)
    {
        null_http_listener_session session;
        http_endpoint              priv(u"127.0.0.1", 49152);
        auto                       result = session.lookup_public(priv);
        EXPECT_FALSE(result.has_value());
    }

    //--------------------------------------------------------------------------
    // null_http_listener tests
    //--------------------------------------------------------------------------

    TEST(NullHttpListener, DefaultConstructs)
    {
        null_http_listener listener;
        // The null listener does nothing; construction proves linkage.
    }

    TEST(NullHttpListener, CreateSessionReturnsNotSupported)
    {
        null_http_listener listener;

        std::vector<endpoint_mapping> mappings;
        mappings.emplace_back(http_endpoint(u"localhost", 80),
                              http_endpoint(u"127.0.0.1", 49152));

        std::unique_ptr<ihttp_listener_session> returned_session;
        std::error_code                         ec;

        auto disp = listener.create_session(ihttp_listener::create_session_flags{},
                                            std::span(mappings),
                                            returned_session,
                                            ec);

        EXPECT_TRUE(ec);
        EXPECT_EQ(ec, std::errc::function_not_supported);
        EXPECT_EQ(returned_session, nullptr);
    }

    TEST(NullHttpListener, RemapReturnsNotSupported)
    {
        null_http_listener         listener;
        null_http_listener_session session;
        http_endpoint              pub(u"localhost", 80);
        http_endpoint              returned_priv;
        std::error_code            ec;

        auto disp = listener.remap(ihttp_listener::remap_flags{},
                                   session,
                                   pub,
                                   std::nullopt,
                                   returned_priv,
                                   ec);

        EXPECT_TRUE(ec);
        EXPECT_EQ(ec, std::errc::function_not_supported);
    }

    TEST(NullHttpListener, UnmapReturnsNotSupported)
    {
        null_http_listener         listener;
        null_http_listener_session session;
        http_endpoint              pub(u"localhost", 80);
        std::error_code            ec;

        auto disp = listener.unmap(ihttp_listener::unmap_flags{}, session, pub, ec);

        EXPECT_TRUE(ec);
        EXPECT_EQ(ec, std::errc::function_not_supported);
    }

    //--------------------------------------------------------------------------
    // Enum flag operations
    //--------------------------------------------------------------------------

    TEST(HttpListenerEnums, CreateSessionFlagsOps)
    {
        auto flags = ihttp_listener::create_session_flags{};
        flags      = flags | ihttp_listener::create_session_flags::allocate_ephemeral_ports;
        EXPECT_NE(flags, ihttp_listener::create_session_flags{});
    }

    TEST(HttpListenerEnums, RemapFlagsOps)
    {
        auto flags = ihttp_listener::remap_flags{};
        flags      = flags | ihttp_listener::remap_flags::allocate_ephemeral_port;
        EXPECT_NE(flags, ihttp_listener::remap_flags{});
    }

    TEST(HttpListenerEnums, UnmapFlagsDefaultsEmpty)
    {
        auto flags = ihttp_listener::unmap_flags{};
        EXPECT_EQ(flags, ihttp_listener::unmap_flags{});
    }

    TEST(HttpListenerEnums, ResultCodesExist)
    {
        // Just verify the result code enums compile and have expected values.
        EXPECT_EQ(static_cast<uint32_t>(ihttp_listener::create_session_result_code::endpoint_already_mapped), 1);
        EXPECT_EQ(static_cast<uint32_t>(ihttp_listener::create_session_result_code::private_endpoint_in_use), 2);
        EXPECT_EQ(static_cast<uint32_t>(ihttp_listener::remap_result_code::endpoint_already_mapped), 1);
        EXPECT_EQ(static_cast<uint32_t>(ihttp_listener::remap_result_code::private_endpoint_in_use), 2);
        EXPECT_EQ(static_cast<uint32_t>(ihttp_listener::remap_result_code::no_active_session), 3);
        EXPECT_EQ(static_cast<uint32_t>(ihttp_listener::unmap_result_code::endpoint_not_mapped), 1);
        EXPECT_EQ(static_cast<uint32_t>(ihttp_listener::unmap_result_code::no_active_session), 2);
    }

} // namespace
