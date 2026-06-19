// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <m/pil/pil.h>
#include <m/pil/registry.h>

using namespace std::string_view_literals;

#ifdef WIN32

namespace
{
    bool
    has_subkey(m::pil::key& k, std::wstring_view name)
    {
        for (auto&& n: k.list_subkey_names())
            if (n == m::pil::key_path(name))
                return true;
        return false;
    }
} // namespace

// M-BUFCREATE-1: live RegCreateKeyExW auto-creates every intermediate key in a
// multi-component path. The buffered overlay must do the same: creating
// "A\\B\\C" materializes A, B, and C, each openable, with the right nesting.
TEST(BufferedCreateKey, MultiLevelCreateMaterializesAllIntermediates)
{
    auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    auto leaf = hkcu.create_key(L"A\\B\\C"sv);

    // Every level is openable through its full path.
    auto a = hkcu.open_key(L"A"sv);
    auto b = hkcu.open_key(L"A\\B"sv);
    auto c = hkcu.open_key(L"A\\B\\C"sv);

    // The nesting is correct: A contains B, B contains C.
    EXPECT_TRUE(has_subkey(a, L"B"sv));
    EXPECT_TRUE(has_subkey(b, L"C"sv));

    // The returned key is the leaf C: a value set through it reads back through
    // a freshly opened "A\\B\\C".
    leaf.set_value(L"marker"sv, 42u);
    EXPECT_EQ(c.get_uint32_value(L"marker"sv), 42u);
}

// M-BUFCREATE-1: re-creating an already-existing multi-component path is
// idempotent — it opens the existing keys rather than duplicating them and
// preserves values already written to the leaf.
TEST(BufferedCreateKey, ReCreatingExistingPathIsIdempotent)
{
    auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    auto first = hkcu.create_key(L"X\\Y\\Z"sv);
    first.set_value(L"seed"sv, 7u);

    // Re-create the same path; the leaf and its value must survive.
    auto second = hkcu.create_key(L"X\\Y\\Z"sv);
    EXPECT_EQ(second.get_uint32_value(L"seed"sv), 7u);

    // No duplicate intermediates were created.
    auto x = hkcu.open_key(L"X"sv);
    auto y = hkcu.open_key(L"X\\Y"sv);

    EXPECT_EQ(x.list_subkey_names().size(), 1u);
    EXPECT_EQ(y.list_subkey_names().size(), 1u);
    EXPECT_TRUE(has_subkey(x, L"Y"sv));
    EXPECT_TRUE(has_subkey(y, L"Z"sv));
}

// M-BUFCREATE-1: a partially-existing path extends the existing prefix rather
// than recreating it — creating "P\\Q" then "P\\Q\\R" adds R under the existing
// Q without disturbing Q's prior contents.
TEST(BufferedCreateKey, PartiallyExistingPathExtendsExistingPrefix)
{
    auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    auto q = hkcu.create_key(L"P\\Q"sv);
    q.set_value(L"q_value"sv, 11u);

    hkcu.create_key(L"P\\Q\\R"sv);

    auto q_again = hkcu.open_key(L"P\\Q"sv);
    EXPECT_EQ(q_again.get_uint32_value(L"q_value"sv), 11u);
    EXPECT_TRUE(has_subkey(q_again, L"R"sv));

    // P still has exactly one child Q.
    auto pk = hkcu.open_key(L"P"sv);
    EXPECT_EQ(pk.list_subkey_names().size(), 1u);
}

// M-BUFCREATE-1: the existing single-component create behavior is unchanged.
TEST(BufferedCreateKey, SingleComponentCreateUnchanged)
{
    auto p    = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto r    = p.get_registry();
    auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

    auto k = hkcu.create_key(L"Solo"sv);
    k.set_value(L"v"sv, 3u);

    EXPECT_TRUE(has_subkey(hkcu, L"Solo"sv));

    auto reopened = hkcu.open_key(L"Solo"sv);
    EXPECT_EQ(reopened.get_uint32_value(L"v"sv), 3u);
}

#endif // WIN32
