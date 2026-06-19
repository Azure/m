// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string_view>

#include <m/exception/exception.h>
#include <m/pil/file_path.h>
#include <m/pil/filesystem_interfaces.h>

#include "buffered/buffered.h"
#include "fault/fault.h"

using namespace std::string_view_literals;

#ifdef WIN32

//
// M-FS-FAULT-1: the filesystem facet of the fault layer. These tests drive the
// fault directory decorator directly over a sealed (no live underlying)
// buffered filesystem overlay, so the matching/counting behavior is exercised
// deterministically and without touching the host filesystem. Rule targets are
// the absolute file_path the decorator computes for each operation (the base
// label joined with the relative argument).
//

namespace
{
    namespace fault   = m::pil::impl::fault;
    namespace bufimpl = m::pil::impl::buffered;

    m::pil::file_path
    rel(std::u16string_view s)
    {
        return m::pil::file_path(m::pil::file_path::view_type(s));
    }

    // The synthetic absolute path the fault root is told it lives at; rule
    // targets are formed by joining this with the relative operation argument.
    m::pil::file_path
    base_path()
    {
        return m::pil::file_path(m::pil::file_path::view_type(u"C:\\fault_fs_base"));
    }

    std::shared_ptr<m::pil::idirectory>
    empty_overlay()
    {
        m::pil::file_metadata md;
        md.m_kind       = m::pil::node_kind::directory;
        md.m_attributes = m::pil::file_attributes::directory;
        return std::make_shared<bufimpl::directory>(md, nullptr);
    }

    std::shared_ptr<m::pil::idirectory>
    make_fault_root(std::shared_ptr<fault::fault_script> const& script)
    {
        return std::make_shared<fault::directory>(empty_overlay(), script, base_path());
    }
} // namespace

// M-FS-FAULT-1: a counted filesystem rule fires on exactly the Nth matching
// occurrence, not before, and (being one-shot) not again afterward.
TEST(FaultFilesystem, CountedMatchFiresOnNthOccurrence)
{
    auto script = std::make_shared<fault::fault_script>();
    auto root   = make_fault_root(script);

    // Created before any rule is installed, so this create passes through.
    ASSERT_TRUE(static_cast<bool>(root->create_directory(rel(u"target"))));

    script->add_rule(fault::fault_rule(fault::fault_operation::open_directory,
                                       base_path() / rel(u"target"),
                                       3,
                                       fault::fault_action::out_of_resources));

    EXPECT_TRUE(static_cast<bool>(root->try_open_directory(rel(u"target")))); // 1st
    EXPECT_TRUE(static_cast<bool>(root->try_open_directory(rel(u"target")))); // 2nd
    EXPECT_THROW(static_cast<void>(root->try_open_directory(rel(u"target"))),
                 m::out_of_resources); // 3rd fires
    EXPECT_TRUE(static_cast<bool>(root->try_open_directory(rel(u"target")))); // 4th: one-shot
}

// M-FS-FAULT-1: multiple filesystem rules compose; each counts its own matching
// operations independently, and one rule firing does not consume another's
// counter.
TEST(FaultFilesystem, MultipleRulesComposeIndependently)
{
    auto script = std::make_shared<fault::fault_script>();
    auto root   = make_fault_root(script);

    ASSERT_TRUE(static_cast<bool>(root->create_directory(rel(u"a"))));
    ASSERT_TRUE(static_cast<bool>(root->create_directory(rel(u"b"))));

    script->add_rule(fault::fault_rule(fault::fault_operation::open_directory,
                                       base_path() / rel(u"a"),
                                       1,
                                       fault::fault_action::access_denied));
    script->add_rule(fault::fault_rule(fault::fault_operation::open_directory,
                                       base_path() / rel(u"b"),
                                       2,
                                       fault::fault_action::out_of_resources));

    // Rule A fires on its first open of "a".
    EXPECT_THROW(static_cast<void>(root->try_open_directory(rel(u"a"))), m::access_denied);

    // Rule B is unaffected by A: its first open of "b" succeeds, its second fires.
    EXPECT_TRUE(static_cast<bool>(root->try_open_directory(rel(u"b"))));
    EXPECT_THROW(static_cast<void>(root->try_open_directory(rel(u"b"))), m::out_of_resources);
}

// M-FS-FAULT-1: operations that match no rule pass through unchanged and mutate
// the overlay as normal; only the matching operation fires.
TEST(FaultFilesystem, NonMatchingOperationsPassThrough)
{
    auto script = std::make_shared<fault::fault_script>();
    auto root   = make_fault_root(script);

    // The only rule targets create_directory of "blocked".
    script->add_rule(fault::fault_rule(fault::fault_operation::create_directory,
                                       base_path() / rel(u"blocked"),
                                       1,
                                       fault::fault_action::access_denied));

    // Unrelated paths and operations pass through and take effect in the overlay.
    ASSERT_TRUE(static_cast<bool>(root->create_directory(rel(u"allowed"))));
    ASSERT_TRUE(static_cast<bool>(root->create_file(rel(u"file.txt"))));
    EXPECT_TRUE(static_cast<bool>(root->try_open_directory(rel(u"allowed"))));
    EXPECT_TRUE(static_cast<bool>(root->try_open_file(rel(u"file.txt"))));

    // A remove of an untargeted entry passes through.
    root->remove_entry(rel(u"file.txt"));
    EXPECT_FALSE(static_cast<bool>(root->try_open_file(rel(u"file.txt"))));

    // The matching operation finally fires, and because the fault is raised
    // before forwarding, the overlay is left unmutated by it.
    EXPECT_THROW(static_cast<void>(root->create_directory(rel(u"blocked"))), m::access_denied);
    EXPECT_FALSE(static_cast<bool>(root->try_open_directory(rel(u"blocked"))));
}

// M-FS-FAULT-1: every filesystem verb in the vocabulary is matched on its own
// operation and target, confirming the operation-to-verb mapping.
TEST(FaultFilesystem, EachFilesystemVerbCanFire)
{
    // create_file
    {
        auto script = std::make_shared<fault::fault_script>();
        auto root   = make_fault_root(script);
        script->add_rule(fault::fault_rule(fault::fault_operation::create_file,
                                           base_path() / rel(u"f"),
                                           1,
                                           fault::fault_action::access_denied));
        EXPECT_THROW(static_cast<void>(root->create_file(rel(u"f"))), m::access_denied);
    }

    // open_file
    {
        auto script = std::make_shared<fault::fault_script>();
        auto root   = make_fault_root(script);
        ASSERT_TRUE(static_cast<bool>(root->create_file(rel(u"f"))));
        script->add_rule(fault::fault_rule(fault::fault_operation::open_file,
                                           base_path() / rel(u"f"),
                                           1,
                                           fault::fault_action::not_found));
        EXPECT_THROW(static_cast<void>(root->try_open_file(rel(u"f"))), m::not_found);
    }

    // create_directory
    {
        auto script = std::make_shared<fault::fault_script>();
        auto root   = make_fault_root(script);
        script->add_rule(fault::fault_rule(fault::fault_operation::create_directory,
                                           base_path() / rel(u"d"),
                                           1,
                                           fault::fault_action::already_exists));
        EXPECT_THROW(static_cast<void>(root->create_directory(rel(u"d"))), m::already_exists);
    }

    // open_directory
    {
        auto script = std::make_shared<fault::fault_script>();
        auto root   = make_fault_root(script);
        ASSERT_TRUE(static_cast<bool>(root->create_directory(rel(u"d"))));
        script->add_rule(fault::fault_rule(fault::fault_operation::open_directory,
                                           base_path() / rel(u"d"),
                                           1,
                                           fault::fault_action::access_denied));
        EXPECT_THROW(static_cast<void>(root->try_open_directory(rel(u"d"))), m::access_denied);
    }

    // remove_entry
    {
        auto script = std::make_shared<fault::fault_script>();
        auto root   = make_fault_root(script);
        ASSERT_TRUE(static_cast<bool>(root->create_file(rel(u"g"))));
        script->add_rule(fault::fault_rule(fault::fault_operation::remove_entry,
                                           base_path() / rel(u"g"),
                                           1,
                                           fault::fault_action::sharing_violation));
        EXPECT_THROW(root->remove_entry(rel(u"g")), m::sharing_violation);
    }

    // delete_tree_entry (named subtree)
    {
        auto script = std::make_shared<fault::fault_script>();
        auto root   = make_fault_root(script);
        ASSERT_TRUE(static_cast<bool>(root->create_file(rel(u"tree\\leaf"))));
        script->add_rule(fault::fault_rule(fault::fault_operation::delete_tree_entry,
                                           base_path() / rel(u"tree"),
                                           1,
                                           fault::fault_action::out_of_resources));
        EXPECT_THROW(root->delete_tree(std::optional<m::pil::file_path>(rel(u"tree"))),
                     m::out_of_resources);
    }

    // rename_entry (matched on the source name)
    {
        auto script = std::make_shared<fault::fault_script>();
        auto root   = make_fault_root(script);
        ASSERT_TRUE(static_cast<bool>(root->create_file(rel(u"old"))));
        script->add_rule(fault::fault_rule(fault::fault_operation::rename_entry,
                                           base_path() / rel(u"old"),
                                           1,
                                           fault::fault_action::not_supported));
        EXPECT_THROW(root->rename_entry(rel(u"old"), rel(u"new")), m::not_supported);
    }
}

#endif // WIN32
