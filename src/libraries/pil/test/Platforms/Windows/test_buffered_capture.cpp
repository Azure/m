// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <exception>
#include <string>
#include <string_view>

#include <m/pil/pil.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>

using namespace std::string_view_literals;

#ifdef WIN32

namespace
{
    // Remove a test subtree from the *real* registry under HKCU, ignoring any
    // failure (the subtree may not exist). Uses a direct platform so the change
    // hits the real registry rather than a buffer.
    void
    remove_real_hkcu_subkey(std::wstring_view subkey)
    {
        try
        {
            auto p    = m::pil::make_platform();
            auto r    = p.get_registry();
            auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
            hkcu.delete_tree(subkey);
        }
        catch (std::exception const&)
        {
        }
    }
} // namespace

// M-PS-2: a buffered key captures its underlying values WHOLE at materialization
// (eager), not lazily on read. Proof: after the buffered key is opened, mutate
// the underlying value through a separate direct platform; the buffered key must
// still return the value captured at open time, not the mutated one.
TEST(BufferedCapture, ValuesAreCapturedWholeAtMaterialization)
{
    static constexpr auto k_subkey = L"MPS2_EagerCapture"sv;
    remove_real_hkcu_subkey(k_subkey);

    // Stage known state in the real registry via a direct platform.
    {
        auto p    = m::pil::make_platform();
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k    = hkcu.create_key(k_subkey);
        k.set_value(L"counter"sv, 100u);
    }

    // Open through a buffered platform; materializing the mirror should eagerly
    // capture the value whole.
    auto buffered = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto br       = buffered.get_registry();
    auto bhkcu    = br.open_predefined_key(m::pil::predefined_key::current_user);
    auto bk       = bhkcu.open_key(k_subkey);

    // Mutate the underlying value via a fresh direct platform.
    {
        auto p    = m::pil::make_platform();
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k    = hkcu.open_key(k_subkey);
        k.set_value(L"counter"sv, 999u);
    }

    // The buffered key must still serve the value captured at open time. If
    // capture were lazy, this read would observe the mutated underlying value.
    EXPECT_EQ(bk.get_uint32_value(L"counter"sv), 100u);

    remove_real_hkcu_subkey(k_subkey);
}

// M-PS-2: a buffered key captures the underlying key's metadata (last_write_time)
// at materialization. Previously a mirrored key carried no timestamp (min); now
// it must match the underlying key's last_write_time.
TEST(BufferedCapture, MetadataLastWriteTimeIsCaptured)
{
    static constexpr auto k_subkey = L"MPS2_Metadata"sv;
    remove_real_hkcu_subkey(k_subkey);

    m::pil::time_point_type direct_lwt{};

    {
        auto p    = m::pil::make_platform();
        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto k    = hkcu.create_key(k_subkey);
        k.set_value(L"v"sv, 1u);
        direct_lwt = k.last_write_time();
    }

    auto buffered = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto br       = buffered.get_registry();
    auto bhkcu    = br.open_predefined_key(m::pil::predefined_key::current_user);
    auto bk       = bhkcu.open_key(k_subkey);

    auto const buffered_lwt = bk.last_write_time();

    EXPECT_NE(buffered_lwt, (m::pil::time_point_type::min)());
    EXPECT_EQ(buffered_lwt, direct_lwt);

    remove_real_hkcu_subkey(k_subkey);
}

// M-BUFTREE-1: buffered::key::delete_tree with a named subkey removes that
// subkey together with all of its descendants, even when the subtree is not
// empty (unlike delete_key, which requires an empty subkey). The deletion is
// recorded as a tombstone in the overlay, shadowing the underlying registry.
TEST(BufferedDeleteTree, NamedSubtreeWithDescendantsIsRemoved)
{
    static constexpr auto k_subkey = L"MBUFTREE_Named"sv;
    remove_real_hkcu_subkey(k_subkey);

    // Stage a non-empty subtree in the real registry: a parent holding a value,
    // a child subkey, and a grandchild subkey with its own value.
    {
        auto p     = m::pil::make_platform();
        auto r     = p.get_registry();
        auto hkcu  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto root  = hkcu.create_key(k_subkey);
        auto child = root.create_key(L"Child"sv);
        child.set_value(L"v"sv, 7u);
        auto grand = child.create_key(L"Grand"sv);
        grand.set_value(L"g"sv, 8u);
    }

    // Delete the whole non-empty subtree through a buffered overlay.
    auto buffered = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
    auto br       = buffered.get_registry();
    auto bhkcu    = br.open_predefined_key(m::pil::predefined_key::current_user);
    auto broot    = bhkcu.open_key(k_subkey);

    broot.delete_tree(L"Child"sv); // non-empty subtree must vanish wholesale

    // The subtree and everything under it are gone in the overlay.
    EXPECT_FALSE(broot.try_open_key(L"Child"sv).has_value());

    remove_real_hkcu_subkey(k_subkey);
}

#endif // WIN32

