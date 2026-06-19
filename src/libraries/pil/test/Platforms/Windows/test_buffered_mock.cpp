// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <m/pil/registry.h>
#include <m/pil/registry_base_types.h>
#include <m/pil/registry_interfaces.h>
#include <m/utility/exception.h>

#include "buffered/buffered.h"
#include "mock_ikey.h"

using namespace std::string_view_literals;

#ifdef WIN32

namespace
{
    using m::pil::test::mock_ikey;
    using tp = m::pil::time_point_type;

    tp
    stamp(std::int64_t ticks)
    {
        return tp(tp::duration(ticks));
    }

    m::pil::value_name_string_type
    vname(std::u16string_view s)
    {
        return m::pil::value_name_string_type{s};
    }

    // Encode a uint32 as a 4-byte little-endian REG_DWORD payload.
    std::vector<std::byte>
    dword_bytes(std::uint32_t v)
    {
        return {static_cast<std::byte>(v & 0xFFu),
                static_cast<std::byte>((v >> 8) & 0xFFu),
                static_cast<std::byte>((v >> 16) & 0xFFu),
                static_cast<std::byte>((v >> 24) & 0xFFu)};
    }

    // Wrap a mock underlying key in a buffered overlay (which captures it whole
    // in its constructor) and return a friendly key for observation, keeping the
    // mock alive and reachable for assertions on its recorded capture passes.
    m::pil::key
    capture(std::shared_ptr<mock_ikey> const& mock)
    {
        auto buffered_impl = std::make_shared<m::pil::impl::buffered::key>(mock);
        return m::pil::key{buffered_impl};
    }
} // namespace

// M-PS-MOCK: a last_write_time that changes across the capture bracket (a "torn
// read") forces the buffered layer to re-capture. The mock scripts the stamp to
// change once (A then B) and then hold steady at B, so capture takes exactly two
// passes and settles on the stabilized stamp B.
TEST(BufferedCaptureMock, TornReadTriggersBoundedRetryThenStabilizes)
{
    auto const stamp_a = stamp(1000);
    auto const stamp_b = stamp(2000);

    // query_information_key sequence across attempts:
    //   attempt 1: before=A, after=B  -> A != B, retry
    //   attempt 2: before=B, after=B  -> stable, stop
    auto mock = std::make_shared<mock_ikey>(
        std::vector<tp>{stamp_a, stamp_b, stamp_b, stamp_b},
        std::vector<m::pil::value_name_string_type>{},
        std::vector<mock_ikey::value_spec>{
            {vname(u"v"), m::pil::reg_value_type::uint32, dword_bytes(7u), false}});

    auto k = capture(mock);

    // Exactly one retry: two capture passes.
    EXPECT_EQ(mock->capture_pass_count(), 2u);

    // The overlay settled on the stabilized stamp, not the torn one.
    EXPECT_EQ(k.last_write_time(), stamp_b);

    // The value still captured correctly through the retried pass.
    EXPECT_EQ(k.get_uint32_value(L"v"sv), 7u);
}

// M-PS-MOCK: if the last_write_time never stabilizes, the retry loop is bounded
// (k_max_capture_attempts == 3) and stops after the cap, settling on whatever
// stamp the final bracket observed rather than spinning forever.
TEST(BufferedCaptureMock, UnstableKeyStopsAtRetryBound)
{
    // Six strictly-increasing stamps: every before/after bracket disagrees, so
    // the loop runs the full three attempts and then stops.
    auto mock = std::make_shared<mock_ikey>(
        std::vector<tp>{stamp(10), stamp(20), stamp(30), stamp(40), stamp(50), stamp(60)},
        std::vector<m::pil::value_name_string_type>{},
        std::vector<mock_ikey::value_spec>{});

    auto k = capture(mock);

    EXPECT_EQ(mock->capture_pass_count(), 3u);

    // The last bracket's "after" read was the sixth scripted stamp.
    EXPECT_EQ(k.last_write_time(), stamp(60));
}

// M-PS-MOCK: a value that is enumerated but then fails to load (it vanished from
// the underlying registry between enumeration and load) is dropped from the
// captured set rather than treated as an error; sibling values are unaffected.
TEST(BufferedCaptureMock, VanishedValueIsDroppedFromCapture)
{
    auto const steady = stamp(5000);

    auto mock = std::make_shared<mock_ikey>(
        std::vector<tp>{steady}, // stable: a single capture pass
        std::vector<m::pil::value_name_string_type>{},
        std::vector<mock_ikey::value_spec>{
            {vname(u"keep"), m::pil::reg_value_type::uint32, dword_bytes(42u), false},
            {vname(u"gone"), m::pil::reg_value_type::uint32, dword_bytes(99u), true}});

    auto k = capture(mock);

    // Stable key: captured in one pass.
    EXPECT_EQ(mock->capture_pass_count(), 1u);

    // The surviving value is present and correct.
    EXPECT_EQ(k.get_uint32_value(L"keep"sv), 42u);

    // The vanished value was dropped: exactly one value remains, and it is "keep".
    auto const values = k.list_value_names_and_types();
    EXPECT_EQ(values.size(), 1u);

    bool gone_present{false};
    for (auto const& vt: values)
        if (std::wstring_view{vt.m_value_name} == L"gone"sv)
            gone_present = true;
    EXPECT_FALSE(gone_present);

    // Reading the dropped value reports not-found.
    EXPECT_THROW(static_cast<void>(k.get_uint32_value(L"gone"sv)), m::not_found);
}

#endif // WIN32
