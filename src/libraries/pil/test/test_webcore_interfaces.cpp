// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

#include <m/pil/file_path.h>
#include <m/pil/webcore.h>
#include <m/pil/webcore_interfaces.h>

//
// Exercises the webcore interface contracts (iwebcore / iwebcore_instance)
// against the null provider. The point is to verify that the interface
// surface, enums, and wrapper types compile and link correctly.
//

namespace
{
    using m::pil::activation_request;
    using m::pil::file_path;
    using m::pil::iwebcore;
    using m::pil::iwebcore_instance;
    using m::pil::null_webcore;
    using m::pil::null_webcore_instance;
    using m::pil::webcore_host;
    using m::pil::webcore_instance;

    //--------------------------------------------------------------------------
    // null_webcore_instance tests
    //--------------------------------------------------------------------------

    TEST(NullWebcoreInstance, DefaultConstructs)
    {
        null_webcore_instance instance;
        // The null instance does nothing; this just proves construction works.
    }

    //--------------------------------------------------------------------------
    // null_webcore tests
    //--------------------------------------------------------------------------

    TEST(NullWebcore, DefaultConstructs)
    {
        null_webcore wc;
        // The null engine surface does nothing; construction proves linkage.
    }

    TEST(NullWebcore, ActivateThrowsNotImplemented)
    {
        null_webcore wc;

        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"TestInstance";

        std::unique_ptr<iwebcore_instance> returned_instance;
        std::error_code                    ec;

        // null_webcore::activate calls M_NOT_IMPLEMENTED, which throws.
        EXPECT_ANY_THROW(wc.activate(iwebcore::activate_flags{}, request, returned_instance, ec));
    }

    TEST(NullWebcore, SetMetadataThrowsNotImplemented)
    {
        null_webcore wc;

        // null_webcore::set_metadata calls M_NOT_IMPLEMENTED, which throws.
        std::error_code ec;
        EXPECT_ANY_THROW(wc.set_metadata(iwebcore::set_metadata_flags{},
                                         u"some_type",
                                         u"some_value",
                                         ec));
    }

    //--------------------------------------------------------------------------
    // webcore_instance wrapper tests
    //--------------------------------------------------------------------------

    TEST(WebcoreInstance, DefaultConstructsEmpty)
    {
        webcore_instance inst;
        EXPECT_FALSE(inst);
    }

    TEST(WebcoreInstance, MovableAndResetable)
    {
        webcore_instance inst;
        inst.reset();
        EXPECT_FALSE(inst);

        webcore_instance inst2 = std::move(inst);
        EXPECT_FALSE(inst2);
    }

    //--------------------------------------------------------------------------
    // webcore_host wrapper tests
    //--------------------------------------------------------------------------

    TEST(WebcoreHost, DefaultConstructsEmpty)
    {
        webcore_host host;
        EXPECT_FALSE(host);
    }

    TEST(WebcoreHost, ConstructsFromNullWebcore)
    {
        auto sp = std::make_shared<null_webcore>();
        webcore_host host(std::move(sp));
        EXPECT_TRUE(host);
    }

    TEST(WebcoreHost, CopyAssignable)
    {
        auto sp = std::make_shared<null_webcore>();
        webcore_host host1(std::move(sp));
        webcore_host host2;
        host2 = host1;
        EXPECT_TRUE(host2);
    }

    TEST(WebcoreHost, MoveAssignable)
    {
        auto sp = std::make_shared<null_webcore>();
        webcore_host host1(std::move(sp));
        webcore_host host2;
        host2 = std::move(host1);
        EXPECT_TRUE(host2);
    }

    TEST(WebcoreHost, Swappable)
    {
        auto sp1 = std::make_shared<null_webcore>();
        auto sp2 = std::make_shared<null_webcore>();
        webcore_host host1(std::move(sp1));
        webcore_host host2(std::move(sp2));

        host1.swap(host2);

        EXPECT_TRUE(host1);
        EXPECT_TRUE(host2);
    }

    //--------------------------------------------------------------------------
    // Enum flag operations
    //--------------------------------------------------------------------------

    TEST(WebcoreEnums, ActivateFlagsOps)
    {
        auto flags = iwebcore::activate_flags{};
        flags = flags | iwebcore::activate_flags::immediate_shutdown_on_release;
        EXPECT_NE(flags, iwebcore::activate_flags{});
    }

    TEST(WebcoreEnums, SetMetadataFlagsDefaultsEmpty)
    {
        auto flags = iwebcore::set_metadata_flags{};
        EXPECT_EQ(flags, iwebcore::set_metadata_flags{});
    }

} // namespace
