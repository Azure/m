// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/utility/exception.h>

//
// Shape / compile-level tests for the filesystem convenience wrappers
// (filesystem_class / directory / file). The platform stack now forwards
// get_filesystem through to a live Windows provider (M-FS-DIRECT and the
// decorator facet milestones), so the null provider -- whose operations all
// report "not implemented" -- must be constructed explicitly to exercise the
// wrapper's forwarding to it. These tests build that genuinely-null provider
// directly; live behavior is covered by the direct-provider tests.
//

namespace
{
    using m::pil::directory;
    using m::pil::file;
    using m::pil::file_root;
    using m::pil::file_root_kind;
    using m::pil::filesystem_class;
    using m::pil::null_filesystem;

    file_root
    drive_c()
    {
        return file_root(file_root_kind::drive, std::u16string_view(u"C:"));
    }

    // A genuinely-null filesystem wrapper: built directly over the null
    // provider whose operations all report "not implemented". The live platform
    // stack now forwards get_filesystem to a real provider, so the null
    // provider must be constructed explicitly to exercise the wrapper's
    // forwarding to it.
    filesystem_class
    null_filesystem_class()
    {
        std::shared_ptr<m::pil::ifilesystem> sp = std::make_shared<null_filesystem>();
        return filesystem_class(std::move(sp));
    }

#ifdef WIN32
    // A live decorated platform (record_modifications) for shape tests that
    // only need a resolvable filesystem wrapper.
    m::pil::platform
    live_platform()
    {
        return m::pil::make_platform(m::pil::make_platform_flags::record_modifications);
    }
#endif

    TEST(TestFilesystemWrappers, PlatformGetFilesystemReturnsWrapper)
    {
#ifndef WIN32
        GTEST_SKIP() << "platform creation is not implemented on this platform yet";
#else
        auto platform = live_platform();
        auto fs       = platform.get_filesystem();
        // Resolving the wrapper through the value platform must not throw.
        SUCCEED();
        (void)fs;
#endif
    }

    TEST(TestFilesystemWrappers, OpenRootNotImplementedAgainstNullProvider)
    {
        auto fs = null_filesystem_class();
        EXPECT_THROW((void)fs.open_root(drive_c()), m::not_implemented);
    }

    TEST(TestFilesystemWrappers, DefaultDirectoryIsFalse)
    {
        directory d;
        EXPECT_FALSE(static_cast<bool>(d));
    }

    TEST(TestFilesystemWrappers, DefaultFileIsFalse)
    {
        file f;
        EXPECT_FALSE(static_cast<bool>(f));
    }

    TEST(TestFilesystemWrappers, DirectorySwap)
    {
        directory a;
        directory b;
        swap(a, b);
        EXPECT_FALSE(static_cast<bool>(a));
        EXPECT_FALSE(static_cast<bool>(b));
    }

    TEST(TestFilesystemWrappers, FileSwap)
    {
        file a;
        file b;
        swap(a, b);
        EXPECT_FALSE(static_cast<bool>(a));
        EXPECT_FALSE(static_cast<bool>(b));
    }

    TEST(TestFilesystemWrappers, FilesystemClassCopyAndMove)
    {
        filesystem_class fs = null_filesystem_class();

        filesystem_class copy(fs);
        filesystem_class moved(std::move(fs));

        filesystem_class assigned;
        assigned = copy;

        filesystem_class move_assigned;
        move_assigned = std::move(moved);

        // All resolve to the null provider; open_root still reports unimplemented.
        EXPECT_THROW((void)assigned.open_root(drive_c()), m::not_implemented);
        EXPECT_THROW((void)move_assigned.open_root(drive_c()), m::not_implemented);
    }

    TEST(TestFilesystemWrappers, FilesystemClassSwap)
    {
        filesystem_class a = null_filesystem_class();
        filesystem_class b;

        a.swap(b);

        EXPECT_THROW((void)b.open_root(drive_c()), m::not_implemented);
    }

} // namespace
