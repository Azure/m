// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string_view>

#include <gtest/gtest.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/utility/exception.h>

//
// Verifies that iplatform::get_filesystem() resolves through the live platform
// stack. The bare direct platform serves a live Windows provider (M-FS-DIRECT),
// and the decorator layers (logging / buffered / redirecting) now forward
// get_filesystem through to it (the buffered / redirecting facet milestones),
// so a decorated stack resolves to the live provider rather than the
// null_filesystem.
//

namespace
{
    TEST(TestFilesystemPlatform, GetFilesystemResolvesThroughStack)
    {
#ifndef WIN32
        GTEST_SKIP() << "platform creation is not implemented on this platform yet";
#else
        auto const platform = m::pil::make_platform_interface();
        ASSERT_NE(platform, nullptr);

        auto const fs = platform->get_filesystem();
        EXPECT_NE(fs, nullptr);
#endif
    }

    TEST(TestFilesystemPlatform, DecoratorStackForwardsToLiveFilesystem)
    {
#ifndef WIN32
        GTEST_SKIP() << "platform creation is not implemented on this platform yet";
#else
        // A decorated stack (record_modifications puts a logging::platform on
        // top) now forwards get_filesystem through to the live provider, so
        // open_root resolves against the real filesystem instead of throwing
        // the null provider's "not implemented".
        auto const platform =
            m::pil::make_platform_interface(m::pil::make_platform_flags::record_modifications);
        auto const fs = platform->get_filesystem();
        ASSERT_NE(fs, nullptr);

        auto const root = fs->open_root(
            m::pil::file_root(m::pil::file_root_kind::drive, std::u16string_view(u"C:")));
        EXPECT_NE(root, nullptr);
#endif
    }

} // namespace
