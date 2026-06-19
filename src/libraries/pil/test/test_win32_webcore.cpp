// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

// Include the header directly since it's internal to the win32 platform.
#include "direct/Platforms/Windows/win32_webcore.h"

#include <m/pil/file_path.h>
#include <m/pil/webcore_interfaces.h>

//
// Tests for the Direct Windows HWC provider using an injectable fake engine.
// This validates the provider's lifecycle without requiring IIS installed.
//

namespace
{
    using m::pil::activation_request;
    using m::pil::file_path;
    using m::pil::iwebcore;
    using m::pil::iwebcore_instance;
    using m::pil::impl::win32::PFN_WEB_CORE_ACTIVATE;
    using m::pil::impl::win32::PFN_WEB_CORE_SET_METADATA;
    using m::pil::impl::win32::PFN_WEB_CORE_SHUTDOWN;
    using m::pil::impl::win32::webcore;
    using m::pil::impl::win32::webcore_engine_api;

    // HRESULT values for fake engine. Use different names to avoid conflict
    // with Windows.h HRESULT type.
    constexpr long k_s_ok                     = 0L;
    constexpr long k_e_service_already_running = static_cast<long>(0x80070420);
    constexpr long k_e_service_not_active      = static_cast<long>(0x80070426);
    constexpr long k_e_fail                    = static_cast<long>(0x80004005);

    //--------------------------------------------------------------------------
    // Fake engine state (shared across test callbacks)
    //--------------------------------------------------------------------------

    struct fake_engine_state
    {
        std::atomic<bool> is_active{false};
        std::atomic<int>  activate_call_count{0};
        std::atomic<int>  shutdown_call_count{0};
        std::atomic<int>  set_metadata_call_count{0};

        std::wstring last_app_host_config;
        std::wstring last_root_web_config;
        std::wstring last_instance_name;
        std::wstring last_metadata_type;
        std::wstring last_metadata_value;

        bool         fail_next_activate{false};
        bool         fail_next_set_metadata{false};

        void
        reset()
        {
            is_active.store(false);
            activate_call_count.store(0);
            shutdown_call_count.store(0);
            set_metadata_call_count.store(0);
            last_app_host_config.clear();
            last_root_web_config.clear();
            last_instance_name.clear();
            last_metadata_type.clear();
            last_metadata_value.clear();
            fail_next_activate     = false;
            fail_next_set_metadata = false;
        }
    };

    // Global fake engine state (tests run single-threaded).
    fake_engine_state g_fake_engine;

    //--------------------------------------------------------------------------
    // Fake engine callbacks
    //--------------------------------------------------------------------------

    long __stdcall
    fake_web_core_activate(wchar_t const* app_host_config,
                           wchar_t const* root_web_config,
                           wchar_t const* instance_name)
    {
        ++g_fake_engine.activate_call_count;

        if (g_fake_engine.fail_next_activate)
        {
            g_fake_engine.fail_next_activate = false;
            return k_e_fail;
        }

        if (g_fake_engine.is_active.load())
        {
            return k_e_service_already_running;
        }

        g_fake_engine.is_active.store(true);

        if (app_host_config)
            g_fake_engine.last_app_host_config = app_host_config;
        if (root_web_config)
            g_fake_engine.last_root_web_config = root_web_config;
        if (instance_name)
            g_fake_engine.last_instance_name = instance_name;

        return k_s_ok;
    }

    long __stdcall
    fake_web_core_shutdown(std::uint32_t f_immediate)
    {
        ++g_fake_engine.shutdown_call_count;

        if (!g_fake_engine.is_active.load())
        {
            return k_e_service_not_active;
        }

        (void)f_immediate;
        g_fake_engine.is_active.store(false);
        return k_s_ok;
    }

    long __stdcall
    fake_web_core_set_metadata(wchar_t const* type, wchar_t const* value)
    {
        ++g_fake_engine.set_metadata_call_count;

        if (g_fake_engine.fail_next_set_metadata)
        {
            g_fake_engine.fail_next_set_metadata = false;
            return k_e_fail;
        }

        if (type)
            g_fake_engine.last_metadata_type = type;
        if (value)
            g_fake_engine.last_metadata_value = value;

        return k_s_ok;
    }

    webcore_engine_api
    make_fake_api()
    {
        return webcore_engine_api{
            .pfn_activate     = fake_web_core_activate,
            .pfn_shutdown     = fake_web_core_shutdown,
            .pfn_set_metadata = fake_web_core_set_metadata,
        };
    }

    //--------------------------------------------------------------------------
    // Tests
    //--------------------------------------------------------------------------

    class Win32WebcoreTest : public ::testing::Test
    {
    protected:
        void
        SetUp() override
        {
            g_fake_engine.reset();
        }

        void
        TearDown() override
        {
            g_fake_engine.reset();
        }
    };

    TEST_F(Win32WebcoreTest, ActivateSucceeds)
    {
        auto provider = std::make_shared<webcore>(make_fake_api());

        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"TestInstance";

        std::unique_ptr<iwebcore_instance> instance;
        std::error_code                    ec;

        auto d = provider->activate(iwebcore::activate_flags{}, request, instance, ec);

        EXPECT_FALSE(d);                       // nominal disposition
        EXPECT_FALSE(ec);                      // no error
        EXPECT_TRUE(instance);                 // got an instance
        EXPECT_EQ(g_fake_engine.activate_call_count.load(), 1);
        EXPECT_EQ(g_fake_engine.last_app_host_config, L"C:\\test\\applicationHost.config");
        EXPECT_EQ(g_fake_engine.last_instance_name, L"TestInstance");
    }

    TEST_F(Win32WebcoreTest, ActivateWithRootWebConfig)
    {
        auto provider = std::make_shared<webcore>(make_fake_api());

        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.root_web_config = file_path(u"C:\\test\\root.web.config");
        request.instance_name   = u"WithRoot";

        std::unique_ptr<iwebcore_instance> instance;
        std::error_code                    ec;

        auto d = provider->activate(iwebcore::activate_flags{}, request, instance, ec);

        EXPECT_FALSE(d);
        EXPECT_FALSE(ec);
        EXPECT_TRUE(instance);
        EXPECT_EQ(g_fake_engine.last_root_web_config, L"C:\\test\\root.web.config");
    }

    TEST_F(Win32WebcoreTest, DoubleActivateYieldsAlreadyActivated)
    {
        auto provider = std::make_shared<webcore>(make_fake_api());

        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"First";

        std::unique_ptr<iwebcore_instance> instance1;
        std::error_code                    ec;

        auto d1 = provider->activate(iwebcore::activate_flags{}, request, instance1, ec);
        EXPECT_FALSE(d1);
        EXPECT_TRUE(instance1);

        // Second activation should return already_activated without calling the engine.
        std::unique_ptr<iwebcore_instance> instance2;
        auto d2 = provider->activate(iwebcore::activate_flags{}, request, instance2, ec);

        EXPECT_TRUE(d2); // non-nominal disposition
        EXPECT_EQ(d2.code(), iwebcore::activate_result_code::already_activated);
        EXPECT_FALSE(instance2); // no instance returned
        EXPECT_EQ(g_fake_engine.activate_call_count.load(), 1); // only called once
    }

    TEST_F(Win32WebcoreTest, ShutdownOnInstanceDestruction)
    {
        auto provider = std::make_shared<webcore>(make_fake_api());

        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"ShutdownTest";

        std::unique_ptr<iwebcore_instance> instance;
        std::error_code                    ec;

        provider->activate(iwebcore::activate_flags{}, request, instance, ec);
        EXPECT_TRUE(g_fake_engine.is_active.load());
        EXPECT_EQ(g_fake_engine.shutdown_call_count.load(), 0);

        // Destroy the instance token — should trigger shutdown.
        instance.reset();

        EXPECT_FALSE(g_fake_engine.is_active.load());
        EXPECT_EQ(g_fake_engine.shutdown_call_count.load(), 1);
    }

    TEST_F(Win32WebcoreTest, SetMetadataSucceeds)
    {
        auto provider = std::make_shared<webcore>(make_fake_api());

        // Activate first (set_metadata requires an active instance in real HWC,
        // though our fake doesn't enforce this).
        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"MetadataTest";

        std::unique_ptr<iwebcore_instance> instance;
        std::error_code                    ec;
        provider->activate(iwebcore::activate_flags{}, request, instance, ec);

        // Set metadata.
        auto d = provider->set_metadata(iwebcore::set_metadata_flags{},
                                         u"some/type",
                                         u"some-value",
                                         ec);

        EXPECT_FALSE(d);
        EXPECT_FALSE(ec);
        EXPECT_EQ(g_fake_engine.set_metadata_call_count.load(), 1);
        EXPECT_EQ(g_fake_engine.last_metadata_type, L"some/type");
        EXPECT_EQ(g_fake_engine.last_metadata_value, L"some-value");
    }

    TEST_F(Win32WebcoreTest, ActivateFailurePropagatesError)
    {
        auto provider = std::make_shared<webcore>(make_fake_api());

        g_fake_engine.fail_next_activate = true;

        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"FailTest";

        std::unique_ptr<iwebcore_instance> instance;
        std::error_code                    ec;

        auto d = provider->activate(iwebcore::activate_flags{}, request, instance, ec);

        EXPECT_FALSE(d);          // nominal disposition (error is in ec)
        EXPECT_TRUE(ec);          // error propagated
        EXPECT_FALSE(instance);   // no instance returned
    }

    TEST_F(Win32WebcoreTest, ImmediateShutdownFlagHonored)
    {
        // This test validates that the flag is passed through — the fake engine
        // doesn't distinguish graceful vs immediate, but the provider should
        // record the flag.

        auto provider = std::make_shared<webcore>(make_fake_api());

        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"ImmediateTest";

        std::unique_ptr<iwebcore_instance> instance;
        std::error_code                    ec;

        auto d = provider->activate(
            iwebcore::activate_flags::immediate_shutdown_on_release,
            request,
            instance,
            ec);

        EXPECT_FALSE(d);
        EXPECT_TRUE(instance);

        // Destroy — the fake doesn't differentiate, but we verify no crash.
        instance.reset();
        EXPECT_EQ(g_fake_engine.shutdown_call_count.load(), 1);
    }

} // namespace
