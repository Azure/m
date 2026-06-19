// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

#include <m/pil/file_path.h>
#include <m/utility/exception.h>

using m::pil::file_path;
using m::pil::file_root;
using m::pil::file_root_kind;

using path_surface = m::pil::path_surface;

using namespace std::string_view_literals;

//
// M-FS-PATH-1: type definition, root-family parsing, and relative/absolute
// classification. No canonicalization yet (M-FS-PATH-2): every input must
// round-trip verbatim through native().
//

TEST(TestFilePath, EmptyIsRootlessRelative)
{
    auto p = file_path(u""sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::none);
    EXPECT_FALSE(p.has_root());
    EXPECT_FALSE(p.is_absolute());
    EXPECT_TRUE(p.is_relative());
    EXPECT_EQ(p.native(), u""sv);
    EXPECT_EQ(p.relative_path(), u""sv);
}

TEST(TestFilePath, RelativePathHasNoRoot)
{
    auto p = file_path(u"foo\\bar\\baz"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::none);
    EXPECT_FALSE(p.has_root());
    EXPECT_FALSE(p.is_absolute());
    EXPECT_TRUE(p.is_relative());
    EXPECT_EQ(p.native(), u"foo\\bar\\baz"sv);
    EXPECT_EQ(p.relative_path(), u"foo\\bar\\baz"sv);
    EXPECT_TRUE(p.root().is_none());
}

TEST(TestFilePath, PosixRootIsAbsolute)
{
    auto p = file_path(u"/usr/local/bin"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::posix);
    EXPECT_TRUE(p.has_root());
    EXPECT_TRUE(p.is_absolute());
    EXPECT_FALSE(p.is_relative());
    EXPECT_EQ(p.native(), u"/usr/local/bin"sv);
    EXPECT_EQ(p.root().text(), u"/"sv);
    EXPECT_EQ(p.relative_path(), u"usr/local/bin"sv);
}

TEST(TestFilePath, BarePosixRoot)
{
    auto p = file_path(u"/"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::posix);
    EXPECT_TRUE(p.is_absolute());
    EXPECT_EQ(p.native(), u"/"sv);
    EXPECT_EQ(p.root().text(), u"/"sv);
    EXPECT_EQ(p.relative_path(), u""sv);
}

TEST(TestFilePath, SingleLeadingBackslashIsPosixStyleRoot)
{
    // A single leading separator (either form) is treated as a POSIX-style
    // absolute-from-root; the original separator character round-trips.
    auto p = file_path(u"\\foo"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::posix);
    EXPECT_TRUE(p.is_absolute());
    EXPECT_EQ(p.native(), u"\\foo"sv);
    EXPECT_EQ(p.root().text(), u"\\"sv);
    EXPECT_EQ(p.relative_path(), u"foo"sv);
}

TEST(TestFilePath, DriveAbsolute)
{
    auto p = file_path(u"C:\\Windows\\System32"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::drive);
    EXPECT_TRUE(p.has_root());
    EXPECT_TRUE(p.is_absolute());
    EXPECT_FALSE(p.is_relative());
    EXPECT_EQ(p.native(), u"C:\\Windows\\System32"sv);
    EXPECT_EQ(p.root().text(), u"C:\\"sv);
    EXPECT_EQ(p.relative_path(), u"Windows\\System32"sv);
}

TEST(TestFilePath, BareDriveIsDriveRelative)
{
    auto p = file_path(u"C:"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::drive);
    EXPECT_TRUE(p.has_root());
    EXPECT_FALSE(p.is_absolute()); // no terminating separator => drive-relative
    EXPECT_TRUE(p.is_relative());
    EXPECT_EQ(p.native(), u"C:"sv);
    EXPECT_EQ(p.root().text(), u"C:"sv);
    EXPECT_EQ(p.relative_path(), u""sv);
}

TEST(TestFilePath, DriveRelativeWithRemainder)
{
    auto p = file_path(u"C:foo\\bar"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::drive);
    EXPECT_FALSE(p.is_absolute());
    EXPECT_TRUE(p.is_relative());
    EXPECT_EQ(p.native(), u"C:foo\\bar"sv); // round-trips without inserting a separator
    EXPECT_EQ(p.root().text(), u"C:"sv);
    EXPECT_EQ(p.relative_path(), u"foo\\bar"sv);
}

TEST(TestFilePath, UncShare)
{
    auto p = file_path(u"\\\\server\\share\\dir\\file"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::unc);
    EXPECT_TRUE(p.is_absolute());
    EXPECT_EQ(p.native(), u"\\\\server\\share\\dir\\file"sv);
    EXPECT_EQ(p.root().text(), u"\\\\server\\share\\"sv);
    EXPECT_EQ(p.relative_path(), u"dir\\file"sv);
}

TEST(TestFilePath, BareUncShareRoot)
{
    auto p = file_path(u"\\\\server\\share"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::unc);
    EXPECT_TRUE(p.is_absolute());
    EXPECT_EQ(p.native(), u"\\\\server\\share"sv);
    EXPECT_EQ(p.root().text(), u"\\\\server\\share"sv);
    EXPECT_EQ(p.relative_path(), u""sv);
}

TEST(TestFilePath, ForwardSlashUnc)
{
    auto p = file_path(u"//server/share/dir"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::unc);
    EXPECT_TRUE(p.is_absolute());
    EXPECT_EQ(p.native(), u"//server/share/dir"sv);
    EXPECT_EQ(p.root().text(), u"//server/share/"sv);
    EXPECT_EQ(p.relative_path(), u"dir"sv);
}

TEST(TestFilePath, DeviceNamespace)
{
    auto p = file_path(u"\\\\.\\PhysicalDrive0"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::device);
    EXPECT_TRUE(p.is_absolute());
    EXPECT_FALSE(p.root().suppresses_normalization());
    EXPECT_EQ(p.native(), u"\\\\.\\PhysicalDrive0"sv);
    EXPECT_EQ(p.root().text(), u"\\\\.\\"sv);
    EXPECT_EQ(p.relative_path(), u"PhysicalDrive0"sv);
}

TEST(TestFilePath, ExtendedLengthIsVerbatim)
{
    // The "\\?\" prefix is recognized; the remainder is verbatim (D11), so the
    // ".." is NOT resolved and the path round-trips exactly.
    auto p = file_path(u"\\\\?\\C:\\a\\..\\b"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::extended);
    EXPECT_TRUE(p.is_absolute());
    EXPECT_TRUE(p.root().suppresses_normalization());
    EXPECT_EQ(p.native(), u"\\\\?\\C:\\a\\..\\b"sv);
    EXPECT_EQ(p.root().text(), u"\\\\?\\"sv);
    EXPECT_EQ(p.relative_path(), u"C:\\a\\..\\b"sv);
}

TEST(TestFilePath, ExtendedLengthUnc)
{
    auto p = file_path(u"\\\\?\\UNC\\server\\share\\x"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::extended_unc);
    EXPECT_TRUE(p.is_absolute());
    EXPECT_TRUE(p.root().suppresses_normalization());
    EXPECT_EQ(p.native(), u"\\\\?\\UNC\\server\\share\\x"sv);
    EXPECT_EQ(p.root().text(), u"\\\\?\\UNC\\"sv);
    EXPECT_EQ(p.relative_path(), u"server\\share\\x"sv);
}

TEST(TestFilePath, ExtendedUncTokenIsCaseInsensitive)
{
    auto p = file_path(u"\\\\?\\unc\\server\\share"sv);

    EXPECT_EQ(p.root_kind(), file_root_kind::extended_unc);
    EXPECT_EQ(p.native(), u"\\\\?\\unc\\server\\share"sv); // stored case preserved
    EXPECT_EQ(p.root().text(), u"\\\\?\\unc\\"sv);
}

TEST(TestFilePath, EqualityIsExact)
{
    EXPECT_EQ(file_path(u"C:\\Foo"sv), file_path(u"C:\\Foo"sv));
    EXPECT_FALSE(file_path(u"C:\\Foo"sv) == file_path(u"C:\\foo"sv));
    EXPECT_FALSE(file_path(u"C:\\Foo"sv) == file_path(u"C:/Foo"sv));
}

TEST(TestFilePath, CopyMoveAssignSwapClear)
{
    auto a = file_path(u"\\\\server\\share\\dir"sv);

    auto b = a; // copy
    EXPECT_EQ(b, a);
    EXPECT_EQ(b.root_kind(), file_root_kind::unc);

    auto c = std::move(b); // move
    EXPECT_EQ(c, a);
    EXPECT_EQ(c.root_kind(), file_root_kind::unc);

    file_path d;
    d = a; // copy-assign
    EXPECT_EQ(d, a);

    file_path e;
    e = u"/etc/hosts"sv; // view assign re-parses
    EXPECT_EQ(e.root_kind(), file_root_kind::posix);
    EXPECT_EQ(e.relative_path(), u"etc/hosts"sv);

    a.swap(e);
    EXPECT_EQ(a.root_kind(), file_root_kind::posix);
    EXPECT_EQ(e.root_kind(), file_root_kind::unc);

    e.clear();
    EXPECT_EQ(e.root_kind(), file_root_kind::none);
    EXPECT_EQ(e.native(), u""sv);
    EXPECT_TRUE(e.is_relative());
}

TEST(TestFilePath, ConstructFromPointer)
{
    auto p = file_path(u"C:\\Temp");
    EXPECT_EQ(p.root_kind(), file_root_kind::drive);
    EXPECT_EQ(p.native(), u"C:\\Temp"sv);

    auto q = file_path(u"relative\\path");
    EXPECT_EQ(q.root_kind(), file_root_kind::none);
    EXPECT_EQ(q.native(), u"relative\\path"sv);
}

//
// M-FS-PATH-2: lexical canonicalization (lexically_normal), parent/leaf
// splitting, and path joining (operator/). The surface is an explicit argument
// because the PIL models a chosen platform that need not be the host.
//

TEST(TestFilePath, NormalizeForwardSlashesToBackslashWindows)
{
    auto const p = file_path(u"C:/Windows/System32"sv).lexically_normal(path_surface::windows);
    EXPECT_EQ(p.native(), u"C:\\Windows\\System32"sv);
}

TEST(TestFilePath, CollapseRepeatedSeparatorsWindows)
{
    auto const p = file_path(u"C:\\\\Windows\\\\\\System32"sv).lexically_normal(path_surface::windows);
    EXPECT_EQ(p.native(), u"C:\\Windows\\System32"sv);
}

TEST(TestFilePath, StripTrailingSeparatorWindows)
{
    auto const p = file_path(u"C:\\Windows\\"sv).lexically_normal(path_surface::windows);
    EXPECT_EQ(p.native(), u"C:\\Windows"sv);
}

TEST(TestFilePath, BareRootKeepsTrailingSeparator)
{
    auto const p = file_path(u"C:\\"sv).lexically_normal(path_surface::windows);
    EXPECT_EQ(p.native(), u"C:\\"sv);
}

TEST(TestFilePath, ResolveDotAndDotDotWindows)
{
    auto const p = file_path(u"C:\\a\\.\\b\\..\\c"sv).lexically_normal(path_surface::windows);
    EXPECT_EQ(p.native(), u"C:\\a\\c"sv);
}

TEST(TestFilePath, RelativeLeadingDotDotPreserved)
{
    auto const p = file_path(u"..\\..\\a\\b"sv).lexically_normal(path_surface::windows);
    EXPECT_EQ(p.native(), u"..\\..\\a\\b"sv);
}

TEST(TestFilePath, DotDotUnderflowAbsoluteWindowsThrows)
{
    EXPECT_THROW((void)file_path(u"C:\\.."sv).lexically_normal(path_surface::windows),
                 m::invalid_parameter);
}

TEST(TestFilePath, DotDotUnderflowPosixThrows)
{
    EXPECT_THROW((void)file_path(u"/.."sv).lexically_normal(path_surface::posix),
                 m::invalid_parameter);
}

TEST(TestFilePath, ExtendedLengthNotNormalized)
{
    // Win32 treats "\\?\C:\a\..\b" as a literally distinct object: nothing past
    // the prefix is normalized.
    auto const verbatim = u"\\\\?\\C:\\a\\..\\b"sv;
    auto const p        = file_path(verbatim).lexically_normal(path_surface::windows);
    EXPECT_EQ(p.native(), verbatim);
}

TEST(TestFilePath, PosixSurfaceTreatsBackslashAsName)
{
    // On the POSIX surface a backslash is an ordinary filename character; only
    // "/" separates and only a leading "/" is a root.
    auto const p = file_path(u"/usr//local/../bin"sv).lexically_normal(path_surface::posix);
    EXPECT_EQ(p.native(), u"/usr/bin"sv);
}

TEST(TestFilePath, PosixRelativeDotResolution)
{
    auto const p = file_path(u"a/./b/../c"sv).lexically_normal(path_surface::posix);
    EXPECT_EQ(p.native(), u"a/c"sv);
}

TEST(TestFilePath, SplitDriveAbsolute)
{
    auto const [parent, leaf] = file_path(u"C:\\Windows\\System32"sv).split_parent_path_and_leaf_name();
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent->native(), u"C:\\Windows"sv);
    EXPECT_EQ(leaf.native(), u"System32"sv);
}

TEST(TestFilePath, SplitSingleComponentUnderRoot)
{
    auto const [parent, leaf] = file_path(u"C:\\Windows"sv).split_parent_path_and_leaf_name();
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent->native(), u"C:\\"sv);
    EXPECT_EQ(leaf.native(), u"Windows"sv);
}

TEST(TestFilePath, SplitBareRootHasNoParentOrLeaf)
{
    auto const [parent, leaf] = file_path(u"C:\\"sv).split_parent_path_and_leaf_name();
    EXPECT_FALSE(parent.has_value());
    EXPECT_TRUE(leaf.native().empty());
}

TEST(TestFilePath, SplitRootlessSingleComponent)
{
    auto const [parent, leaf] = file_path(u"foo"sv).split_parent_path_and_leaf_name();
    EXPECT_FALSE(parent.has_value());
    EXPECT_EQ(leaf.native(), u"foo"sv);
}

TEST(TestFilePath, SplitPosixPath)
{
    auto const [parent, leaf] = file_path(u"/usr/bin"sv).split_parent_path_and_leaf_name();
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent->native(), u"/usr"sv);
    EXPECT_EQ(leaf.native(), u"bin"sv);
}

TEST(TestFilePath, SplitTrailingSeparatorIgnored)
{
    auto const [parent, leaf] = file_path(u"C:\\Windows\\"sv).split_parent_path_and_leaf_name();
    ASSERT_TRUE(parent.has_value());
    EXPECT_EQ(parent->native(), u"C:\\"sv);
    EXPECT_EQ(leaf.native(), u"Windows"sv);
}

TEST(TestFilePath, ParentPathAndHasParent)
{
    auto const p = file_path(u"C:\\Windows\\System32"sv);
    EXPECT_TRUE(p.has_parent_path());
    auto const parent = p.parent_path();
    EXPECT_EQ(parent.native(), u"C:\\Windows"sv);

    auto const root = file_path(u"C:\\"sv);
    EXPECT_FALSE(root.has_parent_path());
    auto const root_parent = root.parent_path();
    EXPECT_TRUE(root_parent.native().empty());
}

TEST(TestFilePath, AppendRelativeComponent)
{
    auto const a = file_path(u"C:\\x"sv) / file_path(u"y"sv);
    EXPECT_EQ(a.native(), u"C:\\x\\y"sv);
    auto const b = file_path(u"C:\\"sv) / file_path(u"y"sv);
    EXPECT_EQ(b.native(), u"C:\\y"sv);
    auto const c = file_path(u"/usr"sv) / file_path(u"bin"sv);
    EXPECT_EQ(c.native(), u"/usr/bin"sv);
    auto const d = file_path(u""sv) / file_path(u"y"sv);
    EXPECT_EQ(d.native(), u"y"sv);
}

TEST(TestFilePath, AppendAbsoluteReplaces)
{
    auto const result = file_path(u"C:\\x"sv) / file_path(u"D:\\y"sv);
    EXPECT_EQ(result.native(), u"D:\\y"sv);
}

//
// M-FS-PATH-3: name comparison by surface (D12). Windows folds case ordinally,
// POSIX is ordinal case-sensitive; the stored case is always preserved.
//

TEST(TestFilePath, WindowsEquivalenceFoldsCase)
{
    auto const a = file_path(u"Foo"sv);
    auto const b = file_path(u"foo"sv);

    EXPECT_TRUE(a.equivalent(b, path_surface::windows));
    EXPECT_FALSE(a.precedes(b, path_surface::windows));
    EXPECT_FALSE(b.precedes(a, path_surface::windows));
}

TEST(TestFilePath, PosixEquivalenceIsCaseSensitive)
{
    auto const a = file_path(u"Foo"sv);
    auto const b = file_path(u"foo"sv);

    EXPECT_FALSE(a.equivalent(b, path_surface::posix));
    // Distinct under POSIX: exactly one of the two orderings holds.
    EXPECT_NE(a.precedes(b, path_surface::posix), b.precedes(a, path_surface::posix));
}

TEST(TestFilePath, ComparisonPreservesStoredCase)
{
    auto const a = file_path(u"C:\\Windows\\System32"sv);
    auto const b = file_path(u"c:\\windows\\system32"sv);

    // Equivalent on Windows, yet each keeps its own original casing.
    EXPECT_TRUE(a.equivalent(b, path_surface::windows));
    EXPECT_EQ(a.native(), u"C:\\Windows\\System32"sv);
    EXPECT_EQ(b.native(), u"c:\\windows\\system32"sv);
}

TEST(TestFilePath, EquivalenceConsistentWithOrdering)
{
    auto const a = file_path(u"alpha"sv);
    auto const b = file_path(u"beta"sv);

    // Distinct names: ordered (one precedes the other) and not equivalent.
    EXPECT_FALSE(a.equivalent(b, path_surface::windows));
    EXPECT_TRUE(a.precedes(b, path_surface::windows));
    EXPECT_FALSE(b.precedes(a, path_surface::windows));

    // Equivalent names are unordered in both directions.
    auto const c = file_path(u"GAMMA"sv);
    auto const d = file_path(u"gamma"sv);
    EXPECT_TRUE(c.equivalent(d, path_surface::windows));
    EXPECT_FALSE(c.precedes(d, path_surface::windows));
    EXPECT_FALSE(d.precedes(c, path_surface::windows));
}

TEST(TestFilePath, PosixOrderingIsOrdinal)
{
    // Uppercase letters sort before lowercase in ordinal (code-unit) order.
    auto const upper = file_path(u"Z"sv);
    auto const lower = file_path(u"a"sv);

    EXPECT_TRUE(upper.precedes(lower, path_surface::posix));
    EXPECT_FALSE(lower.precedes(upper, path_surface::posix));
}

//
// M-FS-PATH-4: edge-case sweep and table-driven canonicalization integration.
// Each row asserts that lexically_normal(surface) maps `input` to `expected`.
// The table mixes ≥10 ordinary cases with the surface's edge cases: mixed
// separators, UNC vs drive, extended-length verbatim, empty/relative, deeply
// nested dot resolution, and trailing-dot/space preservation.
//

namespace
{
    struct canon_row
    {
        path_surface           surface;
        std::u16string_view    input;
        std::u16string_view    expected;
    };

    constexpr std::array<canon_row, 28> canon_table{{
        // --- Windows: ordinary cases ---
        {path_surface::windows, u"C:\\Windows\\System32"sv, u"C:\\Windows\\System32"sv},
        {path_surface::windows, u"C:/Windows/System32"sv, u"C:\\Windows\\System32"sv},
        {path_surface::windows, u"C:\\\\Windows\\\\\\System32"sv, u"C:\\Windows\\System32"sv},
        {path_surface::windows, u"C:\\Windows\\"sv, u"C:\\Windows"sv},
        {path_surface::windows, u"C:\\"sv, u"C:\\"sv},
        {path_surface::windows, u"C:\\a\\.\\b\\..\\c"sv, u"C:\\a\\c"sv},
        {path_surface::windows, u"relative\\path"sv, u"relative\\path"sv},
        {path_surface::windows, u"foo\\\\bar"sv, u"foo\\bar"sv},
        {path_surface::windows, u".\\foo"sv, u"foo"sv},
        {path_surface::windows, u"a\\b\\..\\..\\c"sv, u"c"sv},
        {path_surface::windows, u"C:foo\\bar"sv, u"C:foo\\bar"sv},
        // --- Windows: edges ---
        {path_surface::windows, u"..\\..\\a\\b"sv, u"..\\..\\a\\b"sv}, // leading .. preserved
        {path_surface::windows,
         u"\\\\server\\share\\dir\\file"sv,
         u"\\\\server\\share\\dir\\file"sv}, // UNC
        {path_surface::windows, u"\\\\server\\share\\"sv, u"\\\\server\\share\\"sv}, // bare UNC root
        {path_surface::windows, u"\\\\.\\PhysicalDrive0"sv, u"\\\\.\\PhysicalDrive0"sv}, // device
        {path_surface::windows,
         u"\\\\?\\C:\\a\\..\\b"sv,
         u"\\\\?\\C:\\a\\..\\b"sv}, // extended-length verbatim
        {path_surface::windows, u""sv, u""sv}, // empty
        {path_surface::windows,
         u"C:\\a\\b\\c\\d\\e\\..\\..\\f"sv,
         u"C:\\a\\b\\c\\f"sv}, // deeply nested
        {path_surface::windows, u"C:\\foo."sv, u"C:\\foo."sv},   // trailing dot preserved (lexical)
        {path_surface::windows, u"C:\\foo "sv, u"C:\\foo "sv},   // trailing space preserved (lexical)
        // --- POSIX: ordinary cases ---
        {path_surface::posix, u"/usr/bin"sv, u"/usr/bin"sv},
        {path_surface::posix, u"/usr//local/../bin"sv, u"/usr/bin"sv},
        {path_surface::posix, u"a/./b/../c"sv, u"a/c"sv},
        {path_surface::posix, u"/"sv, u"/"sv},
        {path_surface::posix, u"usr/local/bin"sv, u"usr/local/bin"sv},
        // --- POSIX: edges ---
        {path_surface::posix, u"//foo/bar"sv, u"/foo/bar"sv}, // leading slashes collapse
        {path_surface::posix, u"a\\b"sv, u"a\\b"sv},          // backslash is an ordinary char
        {path_surface::posix, u"/a/b/c/../../d"sv, u"/a/d"sv}, // deeply nested
    }};
} // namespace

TEST(TestFilePath, CanonicalizationTable)
{
    for (std::size_t i = 0; i < canon_table.size(); ++i)
    {
        auto const& row    = canon_table[i];
        auto const  result = file_path(row.input).lexically_normal(row.surface);
        EXPECT_EQ(result.native(), row.expected) << "canon_table row " << i;
    }
}

TEST(TestFilePath, ExtendedLiteralDiffersFromNormalizedSibling)
{
    // The extended-length form is a literally distinct object from its
    // normalized sibling: "\\?\C:\a\..\b" stays verbatim while "C:\a\..\b"
    // resolves to "C:\b".
    auto const literal = file_path(u"\\\\?\\C:\\a\\..\\b"sv).lexically_normal(path_surface::windows);
    auto const sibling = file_path(u"C:\\a\\..\\b"sv).lexically_normal(path_surface::windows);

    EXPECT_EQ(literal.native(), u"\\\\?\\C:\\a\\..\\b"sv);
    EXPECT_EQ(sibling.native(), u"C:\\b"sv);
    EXPECT_NE(literal.native(), sibling.native());
}

TEST(TestFilePath, DotDotPastRootRejectedBothSurfaces)
{
    EXPECT_THROW((void)file_path(u"C:\\a\\..\\..\\b"sv).lexically_normal(path_surface::windows),
                 m::invalid_parameter);
    EXPECT_THROW((void)file_path(u"/a/../../b"sv).lexically_normal(path_surface::posix),
                 m::invalid_parameter);
}

TEST(TestFilePath, CanonicalizationIsIdempotent)
{
    // Normalizing an already-normal path is a no-op for every table row.
    for (std::size_t i = 0; i < canon_table.size(); ++i)
    {
        auto const& row   = canon_table[i];
        auto const  once  = file_path(row.input).lexically_normal(row.surface);
        auto const  twice = once.lexically_normal(row.surface);
        EXPECT_EQ(once.native(), twice.native()) << "canon_table row " << i;
    }
}
