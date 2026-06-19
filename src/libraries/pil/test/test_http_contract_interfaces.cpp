// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <span>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <pugixml.hpp>

#include <m/pil/http_contract.h>
#include <m/pil/http_contract_interfaces.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/platform_interfaces.h>

//
// Exercises the HTTP contract interface surface (ihttp_contract /
// ihttp_contract_document) against the null provider, the public-façade
// `contract_mode` re-declaration and its mapping onto the interface enum, the
// `iplatform::get_http_contract` default, and decorator forwarding
// (M-HWC-CONTRACT-IFACE).
//

namespace
{
    using m::pil::contract_facet_mode;
    using m::pil::contract_mode;
    using m::pil::ihttp_contract;
    using m::pil::ihttp_contract_document;
    using m::pil::iplatform;
    using m::pil::null_http_contract;
    using m::pil::to_facet_mode;

    //--------------------------------------------------------------------------
    // contract_mode façade <-> interface mapping
    //--------------------------------------------------------------------------

    TEST(HttpContractMode, PublicEnumMatchesInterfaceBitForBit)
    {
        EXPECT_EQ(static_cast<std::uint32_t>(contract_mode::validate),
                  static_cast<std::uint32_t>(contract_facet_mode::validate));
        EXPECT_EQ(static_cast<std::uint32_t>(contract_mode::drive),
                  static_cast<std::uint32_t>(contract_facet_mode::drive));
    }

    TEST(HttpContractMode, MapsPublicModeOntoInterfaceMode)
    {
        EXPECT_EQ(to_facet_mode(contract_mode::validate), contract_facet_mode::validate);
        EXPECT_EQ(to_facet_mode(contract_mode::drive), contract_facet_mode::drive);
    }

    //--------------------------------------------------------------------------
    // null_http_contract — load surfaces not-implemented
    //--------------------------------------------------------------------------

    TEST(NullHttpContract, LoadSurfacesNotImplementedThroughEc)
    {
        null_http_contract                              contract;
        std::unique_ptr<ihttp_contract_document>        document;
        std::error_code                                 ec;
        auto const                                      d =
            contract.load(ihttp_contract::load_flags{}, "openapi: \"3.0.0\"\npaths: {}", document, ec);

        EXPECT_TRUE(ec);
        EXPECT_EQ(ec, std::make_error_code(std::errc::function_not_supported));
        EXPECT_FALSE(d);
        EXPECT_EQ(document, nullptr);
    }

    TEST(NullHttpContract, ThrowingLoadWrapperThrows)
    {
        null_http_contract contract;
        EXPECT_THROW((void)contract.load("paths: {}"), std::system_error);
    }

    //--------------------------------------------------------------------------
    // ihttp_contract_document — a hand-written document validates the surface
    //--------------------------------------------------------------------------
    //
    // A minimal document that flags one operation ("GET /known") as conforming
    // and everything else as an unknown operation. This exercises the
    // disposition violation-code channel without the live provider.
    //
    class stub_document final : public ihttp_contract_document
    {
    public:
        validate_request_disposition
        validate_request(std::string_view              method,
                         std::string_view              path,
                         std::span<m::pil::http_header const>,
                         std::span<std::uint8_t const>,
                         std::error_code&              ec) override
        {
            ec.clear();
            if (method == "GET" && path == "/known")
                return {};
            return validate_request_result_code::unknown_operation;
        }

        validate_response_disposition
        validate_response(std::string_view              method,
                          std::string_view              path,
                          std::uint16_t                 status,
                          std::span<m::pil::http_header const>,
                          std::span<std::uint8_t const>,
                          std::error_code&              ec) override
        {
            ec.clear();
            if (!(method == "GET" && path == "/known"))
                return validate_response_result_code::unknown_operation;
            if (status != 200)
                return validate_response_result_code::undeclared_status;
            return {};
        }
    };

    TEST(HttpContractDocument, ConformingRequestYieldsFalseDisposition)
    {
        stub_document               doc;
        std::vector<m::pil::http_header> headers;
        std::vector<std::uint8_t>   body;
        std::error_code             ec;
        auto const                  d = doc.validate_request("GET", "/known", headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_FALSE(d);
        EXPECT_TRUE(d.code_ok());
    }

    TEST(HttpContractDocument, UnknownOperationSurfacesViolationCode)
    {
        stub_document               doc;
        std::vector<m::pil::http_header> headers;
        std::vector<std::uint8_t>   body;
        std::error_code             ec;
        auto const                  d = doc.validate_request("POST", "/missing", headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(), ihttp_contract_document::validate_request_result_code::unknown_operation);
    }

    TEST(HttpContractDocument, UndeclaredStatusSurfacesViolationCode)
    {
        stub_document               doc;
        std::vector<m::pil::http_header> headers;
        std::vector<std::uint8_t>   body;
        std::error_code             ec;
        auto const                  d = doc.validate_response("GET", "/known", 503, headers, body, ec);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(d);
        EXPECT_EQ(d.code(),
                  ihttp_contract_document::validate_response_result_code::undeclared_status);
    }

    //--------------------------------------------------------------------------
    // iplatform::get_http_contract default + decorator forwarding (IFACE-4)
    //--------------------------------------------------------------------------
    //
    // A minimal base platform implementing only the pure virtuals; it inherits
    // the default get_http_contract (null provider). A decorator forwards
    // get_http_contract to its underlying. The top of the stack must surface a
    // usable null provider whose load reports not-implemented.
    //
    class base_platform : public iplatform
    {
    public:
        get_registry_disposition
        get_registry(get_registry_flags, std::shared_ptr<m::pil::iregistry>& returned_registry) override
        {
            returned_registry.reset();
            return {};
        }

        save_disposition
        save(save_flags, save_contents, pugi::xml_node&) override
        {
            return {};
        }
    };

    class decorator_platform final : public base_platform
    {
    public:
        explicit decorator_platform(std::shared_ptr<iplatform> underlying)
            : m_underlying(std::move(underlying))
        {
        }

        using base_platform::get_http_contract;

        get_http_contract_disposition
        get_http_contract(get_http_contract_flags          flags,
                          std::shared_ptr<ihttp_contract>& returned_http_contract) override
        {
            return m_underlying->get_http_contract(flags, returned_http_contract);
        }

    private:
        std::shared_ptr<iplatform> m_underlying;
    };

    TEST(PlatformHttpContract, DefaultYieldsNullProvider)
    {
        base_platform plat;
        auto const    contract = plat.get_http_contract();
        ASSERT_NE(contract, nullptr);

        std::unique_ptr<ihttp_contract_document> document;
        std::error_code                          ec;
        contract->load(ihttp_contract::load_flags{}, "paths: {}", document, ec);
        EXPECT_EQ(ec, std::make_error_code(std::errc::function_not_supported));
    }

    TEST(PlatformHttpContract, DecoratorForwardsToUnderlyingWithoutCrashing)
    {
        auto base      = std::make_shared<base_platform>();
        auto decorated = std::make_shared<decorator_platform>(base);

        auto const contract = decorated->get_http_contract();
        ASSERT_NE(contract, nullptr);

        std::unique_ptr<ihttp_contract_document> document;
        std::error_code                          ec;
        contract->load(ihttp_contract::load_flags{}, "paths: {}", document, ec);
        EXPECT_EQ(ec, std::make_error_code(std::errc::function_not_supported));
        EXPECT_EQ(document, nullptr);
    }

    //--------------------------------------------------------------------------
    // Live platform stack surfaces a working contract provider (EXPOSE-1)
    //--------------------------------------------------------------------------
    //
    // make_platform_interface() builds the real decorator stack down to the
    // bottom live platform, which now returns make_http_contract_provider().
    // Loading a spec and validating a conforming request through that provider
    // proves get_http_contract is wired end to end (not the null default).
    //
    TEST(PlatformHttpContract, LiveStackProviderLoadsAndValidates)
    {
        constexpr std::string_view spec = R"(
openapi: 3.0.0
info:
  title: expose-test
  version: "1.0"
paths:
  /ping:
    get:
      responses:
        '200':
          description: ok
)";

        auto platform = m::pil::make_platform_interface();
        auto contract = platform->get_http_contract();
        ASSERT_NE(contract, nullptr);

        std::unique_ptr<ihttp_contract_document> document;
        std::error_code                          ec;
        contract->load(ihttp_contract::load_flags{}, spec, document, ec);
        ASSERT_FALSE(ec) << ec.message();
        ASSERT_NE(document, nullptr);

        // A conforming request: known operation, no params/body required.
        auto const d = document->validate_request("GET", "/ping", {}, {}, ec);
        EXPECT_FALSE(ec) << ec.message();
        EXPECT_FALSE(d);

        // An unknown path is reported as a contract violation, not an error.
        auto const d2 = document->validate_request("GET", "/nope", {}, {}, ec);
        EXPECT_FALSE(ec) << ec.message();
        EXPECT_TRUE(d2);
        EXPECT_EQ(d2.code(),
                  ihttp_contract_document::validate_request_result_code::unknown_operation);
    }

} // namespace
