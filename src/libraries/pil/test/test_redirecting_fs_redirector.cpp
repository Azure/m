// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string_view>
#include <utility>

#include <m/pil/file_path.h>
#include <m/pil/pil.h>

#include "redirecting/redirecting.h"

using m::pil::file_path;

using namespace std::string_view_literals;

namespace
{
    using P = std::pair<std::u16string_view, std::u16string_view>;

    std::array<P, 3> const fs_il_1 = {{
        P{u"Public\\Documents"sv, u"Private\\Sandbox1234\\Documents"sv},
        P{u"Public\\Documents\\Secret\\Xyz"sv, u"Private\\Vault987\\Pdq\\Here"sv},
        P{u"Shared\\Media"sv, u"Private\\Sandbox1234\\Media"sv},
    }};

    std::shared_ptr<m::pil::impl::redirecting::fs_redirector>
    make_redirector()
    {
        return std::make_shared<m::pil::impl::redirecting::fs_redirector>(fs_il_1);
    }

    TEST(TestRedirectingFsRedirector, NonMatchingPrefixPassesThrough)
    {
        auto const r = make_redirector();

        // A prefix with no redirection entry is returned unchanged.
        EXPECT_EQ(r->map_public_to_private(file_path(u"Public"sv)), file_path(u"Public"sv));
        EXPECT_EQ(r->map_public_to_private(file_path(u"Other\\Place"sv)),
                  file_path(u"Other\\Place"sv));
    }

    TEST(TestRedirectingFsRedirector, RedirectedPrefixMapsToTargetSubtree)
    {
        auto const r = make_redirector();

        // Exact prefix maps.
        EXPECT_EQ(r->map_public_to_private(file_path(u"Public\\Documents"sv)),
                  file_path(u"Private\\Sandbox1234\\Documents"sv));

        // Longer paths under the prefix carry the remainder across.
        EXPECT_EQ(r->map_public_to_private(file_path(u"Public\\Documents\\Letter.txt"sv)),
                  file_path(u"Private\\Sandbox1234\\Documents\\Letter.txt"sv));

        // The longest matching prefix wins.
        EXPECT_EQ(r->map_public_to_private(file_path(u"Public\\Documents\\Secret\\Xyz"sv)),
                  file_path(u"Private\\Vault987\\Pdq\\Here"sv));

        EXPECT_EQ(r->map_public_to_private(file_path(u"Shared\\Media\\song.mp3"sv)),
                  file_path(u"Private\\Sandbox1234\\Media\\song.mp3"sv));
    }

    TEST(TestRedirectingFsRedirector, OriginalCaseOfRemainderPreserved)
    {
        auto const r = make_redirector();

        // The prefix is matched case-insensitively (D12) but the unmatched
        // remainder keeps the caller's exact case.
        EXPECT_EQ(r->map_public_to_private(file_path(u"public\\DOCUMENTS\\MixedCase.TXT"sv)),
                  file_path(u"Private\\Sandbox1234\\Documents\\MixedCase.TXT"sv));
    }

    TEST(TestRedirectingFsRedirector, ReverseMappingPrivateToPublic)
    {
        auto const r = make_redirector();

        EXPECT_EQ(r->map_private_to_public(file_path(u"Private\\Sandbox1234\\Documents\\a.txt"sv)),
                  file_path(u"Public\\Documents\\a.txt"sv));

        // Non-matching private path passes through.
        EXPECT_EQ(r->map_private_to_public(file_path(u"Private\\Unknown"sv)),
                  file_path(u"Private\\Unknown"sv));
    }

    // M-FS-MONITOR-REDIR-1: Rooted (absolute) paths are mapped by extracting
    // the relative portion and matching against the redirection table. This
    // supports watch paths that are fully qualified (e.g. from Win32 APIs)
    // even when the redirection table uses relative keys.
    TEST(TestRedirectingFsRedirector, RootedPathMatchesRelativePortion)
    {
        auto const r = make_redirector();

        // A rooted path whose relative portion matches a redirection key maps
        // to the target, preserving the root and any prefix before the match.
        EXPECT_EQ(r->map_public_to_private(file_path(u"C:\\Users\\Test\\Public\\Documents"sv)),
                  file_path(u"C:\\Users\\Test\\Private\\Sandbox1234\\Documents"sv));

        // Sub-paths also work — the remainder is carried across.
        EXPECT_EQ(
            r->map_public_to_private(file_path(u"C:\\Users\\Test\\Public\\Documents\\file.txt"sv)),
            file_path(u"C:\\Users\\Test\\Private\\Sandbox1234\\Documents\\file.txt"sv));

        // UNC paths work too.
        EXPECT_EQ(r->map_public_to_private(
                      file_path(u"\\\\server\\share\\Public\\Documents\\report.docx"sv)),
                  file_path(u"\\\\server\\share\\Private\\Sandbox1234\\Documents\\report.docx"sv));
    }

    TEST(TestRedirectingFsRedirector, RootedPathWithNoMatchPassesThrough)
    {
        auto const r = make_redirector();

        // A rooted path whose relative portion doesn't match passes through unchanged.
        EXPECT_EQ(r->map_public_to_private(file_path(u"C:\\Windows\\System32"sv)),
                  file_path(u"C:\\Windows\\System32"sv));

        EXPECT_EQ(r->map_public_to_private(file_path(u"D:\\Other\\Path"sv)),
                  file_path(u"D:\\Other\\Path"sv));
    }

    TEST(TestRedirectingFsRedirector, RootedPathReverseMappingPrivateToPublic)
    {
        auto const r = make_redirector();

        // Reverse mapping also works for rooted paths.
        EXPECT_EQ(r->map_private_to_public(
                      file_path(u"C:\\Backing\\Private\\Sandbox1234\\Documents\\a.txt"sv)),
                  file_path(u"C:\\Backing\\Public\\Documents\\a.txt"sv));
    }

} // namespace
