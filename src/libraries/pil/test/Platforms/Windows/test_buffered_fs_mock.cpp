// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <m/pil/filesystem_base_types.h>
#include <m/pil/filesystem_interfaces.h>
#include <m/pil/pil.h>
#include <m/utility/exception.h>

#include "buffered/buffered.h"
#include "mock_idirectory.h"

using namespace std::string_view_literals;

#ifdef WIN32

namespace
{
    namespace bufimpl = m::pil::impl::buffered;

    using m::pil::test::mock_idirectory;
    using tp = m::pil::time_point_type;

    tp
    stamp(std::int64_t ticks)
    {
        return tp(tp::duration(ticks));
    }

    // Build a directory_entry for a child of the given kind with a scripted
    // last_write_time, so a test can observe that the captured child metadata
    // came from the enumeration.
    m::pil::directory_entry
    entry(std::u16string_view name, m::pil::node_kind kind, std::int64_t lwt_ticks)
    {
        m::pil::file_metadata md{};
        md.m_kind            = kind;
        md.m_last_write_time = stamp(lwt_ticks);
        md.m_attributes      = (kind == m::pil::node_kind::directory)
                                   ? m::pil::file_attributes::directory
                                   : m::pil::file_attributes::normal;
        return m::pil::directory_entry(m::pil::file_name_string_type{name}, md);
    }

    // Wrap a mock underlying directory in a buffered overlay (which captures it
    // whole in its constructor) and return it as an idirectory so the
    // convenience read overloads are reachable, keeping the mock alive and
    // reachable for assertions on its recorded capture passes.
    std::shared_ptr<m::pil::idirectory>
    capture(std::shared_ptr<mock_idirectory> const& mock)
    {
        return std::shared_ptr<m::pil::idirectory>(std::make_shared<bufimpl::directory>(mock));
    }

    std::set<std::u16string>
    child_names(m::pil::idirectory& dir)
    {
        std::set<std::u16string> names;
        for (std::size_t index = 0;; ++index)
        {
            auto const e = dir.enumerate_entries(index);
            if (!e.has_value())
                break;
            names.insert(std::u16string(e.value().m_name.view()));
        }
        return names;
    }
} // namespace

// M-FS-BUF-4: a last_write_time that changes across the capture bracket (a "torn
// read") forces the buffered layer to re-capture. The mock scripts the stamp to
// change once (A then B) and then hold steady at B, so capture takes exactly two
// passes and settles on the stabilized stamp B.
TEST(BufferedFsCaptureMock, TornReadTriggersBoundedRetryThenStabilizes)
{
    auto const stamp_a = stamp(1000);
    auto const stamp_b = stamp(2000);

    // query_information sequence across attempts:
    //   attempt 1: before=A, after=B  -> A != B, retry
    //   attempt 2: before=B, after=B  -> stable, stop
    auto mock = std::make_shared<mock_idirectory>(
        std::vector<tp>{stamp_a, stamp_b, stamp_b, stamp_b},
        std::vector<std::vector<m::pil::directory_entry>>{
            {entry(u"alpha", m::pil::node_kind::directory, 1),
             entry(u"file.txt", m::pil::node_kind::file, 2)}});

    auto overlay = capture(mock);

    // Exactly one retry: two capture passes.
    EXPECT_EQ(mock->capture_pass_count(), 2u);

    // The overlay settled on the stabilized stamp, not the torn one.
    EXPECT_EQ(overlay->query_information().m_last_write_time, stamp_b);

    // The captured namespace is intact across the retried pass.
    EXPECT_EQ(child_names(*overlay),
              (std::set<std::u16string>{u"alpha", u"file.txt"}));
}

// M-FS-BUF-4: if the last_write_time never stabilizes, the retry loop is bounded
// (k_max_capture_attempts == 3) and stops after the cap, settling on whatever
// stamp the final bracket observed rather than spinning forever.
TEST(BufferedFsCaptureMock, UnstableDirectoryStopsAtRetryBound)
{
    // Six strictly-increasing stamps: every before/after bracket disagrees, so
    // the loop runs the full three attempts and then stops.
    auto mock = std::make_shared<mock_idirectory>(
        std::vector<tp>{stamp(10), stamp(20), stamp(30), stamp(40), stamp(50), stamp(60)},
        std::vector<std::vector<m::pil::directory_entry>>{
            {entry(u"alpha", m::pil::node_kind::directory, 1)}});

    auto overlay = capture(mock);

    EXPECT_EQ(mock->capture_pass_count(), 3u);

    // The last bracket's "after" read was the sixth scripted stamp.
    EXPECT_EQ(overlay->query_information().m_last_write_time, stamp(60));
}

// M-FS-BUF-4: an entry present in the first (torn) enumeration that is gone by
// the stabilized re-read is dropped from the captured set. Because a child's
// metadata arrives whole with the enumeration (D14, no separate per-entry load),
// a vanished entry is simply absent from the final consistent pass — the
// filesystem analogue of the registry's "value vanished between enumeration and
// load" drop.
TEST(BufferedFsCaptureMock, VanishedEntryDroppedFromCapture)
{
    auto const stamp_a = stamp(1000);
    auto const stamp_b = stamp(2000);

    // Pass 1 enumerates {keep, gone} but the bracket is torn (A then B); pass 2
    // is stable (B then B) and enumerates only {keep}. The capture clears and
    // re-populates from the consistent pass, so "gone" is dropped.
    auto mock = std::make_shared<mock_idirectory>(
        std::vector<tp>{stamp_a, stamp_b, stamp_b, stamp_b},
        std::vector<std::vector<m::pil::directory_entry>>{
            {entry(u"keep", m::pil::node_kind::file, 1),
             entry(u"gone", m::pil::node_kind::file, 2)},
            {entry(u"keep", m::pil::node_kind::file, 1)}});

    auto overlay = capture(mock);

    // One retry: the torn first pass was re-captured.
    EXPECT_EQ(mock->capture_pass_count(), 2u);

    // The vanished entry was dropped; only the surviving child remains.
    EXPECT_EQ(child_names(*overlay), (std::set<std::u16string>{u"keep"}));

    // Opening the dropped entry reports not-found through the tentative path.
    EXPECT_FALSE(overlay->try_open_file(m::pil::file_path(u"gone"sv)));
}

#endif // WIN32
