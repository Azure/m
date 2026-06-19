// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

#include <pugixml.hpp>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>

#include "buffered/buffered.h"
#include "journaling/journaling.h"

using namespace std::string_view_literals;

#ifdef WIN32

namespace
{
    // Build a sealed snapshot fixture file holding HKCU with one seed value so
    // both the recording source stack and the replay target start from the same
    // deterministic, win32-free base world.
    void
    write_snapshot_fixture(std::filesystem::path const& p)
    {
        std::error_code ec;
        std::filesystem::remove(p, ec);

        auto pf  = m::pil::make_platform(m::pil::make_platform_flags::buffer_updates);
        auto r   = pf.get_registry();
        auto k1  = r.open_predefined_key(m::pil::predefined_key::current_user);
        auto app = k1.create_key(L"MJOURNAL_Seed"sv);
        app.set_value(L"seed"sv, 1u);

        pf.save(p);
    }

    bool
    has_value_named(m::pil::key& k, std::wstring_view name)
    {
        for (auto const& vt: k.list_value_names_and_types())
            if (std::wstring_view{vt.m_value_name} == name)
                return true;
        return false;
    }
} // namespace

// M-JOURNAL-3: record an order-sensitive mutation sequence through the
// journaling decorator, replay the recorded journal onto a fresh copy of the
// same base world, and assert the replayed world is observably equivalent to
// the one the source produced. This exercises ordered replay end to end:
// repeated SetValue (last writer wins), value deletion, nested key creation,
// and whole-subtree deletion (DeleteTree, including descendants) must all land
// identically after replay.
TEST(Journaling, RecordReplayProducesObservableEquivalence)
{
    auto const snapshot = std::filesystem::temp_directory_path() / "mjournal_snapshot.xml";
    write_snapshot_fixture(snapshot);

    // --- SOURCE: journaling decorator over a snapshot leaf. ---
    auto journaling_plat = std::make_shared<m::pil::impl::journaling::platform>(
        m::pil::impl::buffered::create_platform_from_persisted_xml(snapshot));

    {
        m::pil::platform p{std::shared_ptr<m::pil::iplatform>(journaling_plat)};

        auto r    = p.get_registry();
        auto hkcu = r.open_predefined_key(m::pil::predefined_key::current_user);

        auto app = hkcu.create_key(L"JournalApp"sv);
        app.set_value(L"v"sv, 1u); // overwritten below; replay must honor order
        app.set_value(L"v"sv, 2u); // final winner
        app.set_value(L"doomed"sv, 7u);
        app.delete_value(L"doomed"sv); // value must be absent after replay

        auto child = app.create_key(L"Child"sv);
        child.set_value(L"c"sv, 9u);

        // A non-empty subtree (a key holding a value) deleted wholesale: replay
        // must remove ToDelete and everything under it.
        auto todelete = app.create_key(L"ToDelete"sv);
        todelete.set_value(L"x"sv, 1u);
        app.delete_tree(L"ToDelete"sv); // subtree must be gone after replay
    }

    // Capture the recorded verb stream as a standalone <Journal> artifact.
    pugi::xml_document journal_doc;
    auto               journal_root = journal_doc.append_child(L"Journal");
    journaling_plat->save_journal(journal_root);

    // --- TARGET: a fresh copy of the same base world, with no source state. ---
    auto target_leaf = m::pil::impl::buffered::create_platform_from_persisted_xml(snapshot);
    auto target_reg  = target_leaf->get_registry();

    m::pil::impl::journaling::replay(journal_root, *target_reg);

    // --- Assert observable equivalence on the replayed target. ---
    m::pil::platform tp{std::shared_ptr<m::pil::iplatform>(target_leaf)};
    auto             tr    = tp.get_registry();
    auto             thkcu = tr.open_predefined_key(m::pil::predefined_key::current_user);

    auto tapp_opt = thkcu.try_open_key(L"JournalApp"sv);
    ASSERT_TRUE(tapp_opt.has_value());
    auto tapp = std::move(tapp_opt.value());

    // Last writer wins: v == 2, not 1.
    EXPECT_EQ(tapp.get_uint32_value(L"v"sv), 2u);

    // Deleted value is absent; surviving value is present.
    EXPECT_FALSE(has_value_named(tapp, L"doomed"sv));
    EXPECT_TRUE(has_value_named(tapp, L"v"sv));

    // Nested key and its value replayed.
    auto tchild_opt = tapp.try_open_key(L"Child"sv);
    ASSERT_TRUE(tchild_opt.has_value());
    EXPECT_EQ(tchild_opt.value().get_uint32_value(L"c"sv), 9u);

    // Whole-subtree deletion replayed: ToDelete is gone.
    EXPECT_FALSE(tapp.try_open_key(L"ToDelete"sv).has_value());

    std::error_code ec;
    std::filesystem::remove(snapshot, ec);
}

#endif // WIN32
