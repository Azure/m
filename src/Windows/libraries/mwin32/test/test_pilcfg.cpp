// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <Windows.h>

#include <gtest/gtest.h>

#include "pilcfg.h"

using namespace std::string_view_literals;
using m::mwin32_impl::parse_pilcfg;
using contract_mode = m::mwin32_impl::pilcfg::webcore_config::contract_mode;

namespace
{
    // RAII helper that sets an environment variable for the duration of a test
    // and restores its prior value (or unsets it) on destruction, so the tests
    // remain reproducible and do not leak process state.
    class scoped_env_var
    {
    public:
        scoped_env_var(wchar_t const* name, wchar_t const* value): m_name(name)
        {
            DWORD const had = ::GetEnvironmentVariableW(name, nullptr, 0);
            if (had != 0)
            {
                m_prior.resize(had);
                DWORD const got = ::GetEnvironmentVariableW(name, m_prior.data(), had);
                m_prior.resize(got);
                m_had_prior = true;
            }
            ::SetEnvironmentVariableW(name, value);
        }

        ~scoped_env_var()
        {
            ::SetEnvironmentVariableW(m_name, m_had_prior ? m_prior.c_str() : nullptr);
        }

        scoped_env_var(scoped_env_var const&)            = delete;
        scoped_env_var& operator=(scoped_env_var const&) = delete;

    private:
        wchar_t const* m_name;
        std::wstring   m_prior;
        bool           m_had_prior = false;
    };
} // namespace

//
// parse_pilcfg unit tests. These exercise only the pure JSON->pilcfg parser;
// load_pilcfg (which touches the filesystem and the host module path) is not
// unit-tested here because it depends on process-external state.
//

TEST(PilcfgParse, EmptyObjectIsPassthrough)
{
    auto const cfg = parse_pilcfg("{}"sv);
    EXPECT_FALSE(cfg.buffer_updates);
    EXPECT_FALSE(cfg.record_modifications);
}

TEST(PilcfgParse, BufferUpdatesTrue)
{
    auto const cfg = parse_pilcfg(R"({ "buffer_updates": true })"sv);
    EXPECT_TRUE(cfg.buffer_updates);
    EXPECT_FALSE(cfg.record_modifications);
}

TEST(PilcfgParse, RecordModificationsTrue)
{
    auto const cfg = parse_pilcfg(R"({ "record_modifications": true })"sv);
    EXPECT_FALSE(cfg.buffer_updates);
    EXPECT_TRUE(cfg.record_modifications);
}

TEST(PilcfgParse, BothTrue)
{
    auto const cfg =
        parse_pilcfg(R"({ "buffer_updates": true, "record_modifications": true })"sv);
    EXPECT_TRUE(cfg.buffer_updates);
    EXPECT_TRUE(cfg.record_modifications);
}

TEST(PilcfgParse, BothExplicitlyFalse)
{
    auto const cfg =
        parse_pilcfg(R"({ "buffer_updates": false, "record_modifications": false })"sv);
    EXPECT_FALSE(cfg.buffer_updates);
    EXPECT_FALSE(cfg.record_modifications);
}

TEST(PilcfgParse, UnknownMembersIgnored)
{
    auto const cfg = parse_pilcfg(
        R"({ "buffer_updates": true, "future_option": 42, "note": "hello" })"sv);
    EXPECT_TRUE(cfg.buffer_updates);
    EXPECT_FALSE(cfg.record_modifications);
}

TEST(PilcfgParse, WhitespaceAndFormattingTolerated)
{
    auto const cfg = parse_pilcfg("\n\t {\r\n  \"record_modifications\" :\ttrue\n}\n"sv);
    EXPECT_FALSE(cfg.buffer_updates);
    EXPECT_TRUE(cfg.record_modifications);
}

TEST(PilcfgParse, MemberOrderDoesNotMatter)
{
    auto const cfg =
        parse_pilcfg(R"({ "record_modifications": true, "buffer_updates": true })"sv);
    EXPECT_TRUE(cfg.buffer_updates);
    EXPECT_TRUE(cfg.record_modifications);
}

TEST(PilcfgParse, InvalidJsonThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg("{ not json"sv));
    EXPECT_ANY_THROW(parse_pilcfg(""sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "buffer_updates": true,, })"sv));
}

TEST(PilcfgParse, NonObjectRootThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg("[]"sv));
    EXPECT_ANY_THROW(parse_pilcfg("true"sv));
    EXPECT_ANY_THROW(parse_pilcfg("42"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"("a string")"sv));
    EXPECT_ANY_THROW(parse_pilcfg("null"sv));
}

TEST(PilcfgParse, NonBooleanRecognizedMemberThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "buffer_updates": "yes" })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "buffer_updates": 1 })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "record_modifications": null })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "record_modifications": [] })"sv));
}

TEST(PilcfgParse, NestedObjectMemberIgnoredWhenNotRecognized)
{
    auto const cfg =
        parse_pilcfg(R"({ "buffer_updates": true, "nested": { "x": 1 } })"sv);
    EXPECT_TRUE(cfg.buffer_updates);
    EXPECT_FALSE(cfg.record_modifications);
}

TEST(PilcfgParse, NoRedirectionsByDefault)
{
    auto const cfg = parse_pilcfg(R"({ "buffer_updates": true })"sv);
    EXPECT_TRUE(cfg.redirections.empty());
}

TEST(PilcfgParse, EmptyRedirectionsArrayIsEmpty)
{
    auto const cfg = parse_pilcfg(R"({ "redirections": [] })"sv);
    EXPECT_TRUE(cfg.redirections.empty());
}

TEST(PilcfgParse, SingleRedirectionParsed)
{
    auto const cfg = parse_pilcfg(
        R"({ "redirections": [ { "from": "HKLM\\Software", "to": "HKCU\\Temp\\Software" } ] })"sv);
    ASSERT_EQ(cfg.redirections.size(), 1u);
    EXPECT_EQ(cfg.redirections[0].first, u"HKLM\\Software");
    EXPECT_EQ(cfg.redirections[0].second, u"HKCU\\Temp\\Software");
}

TEST(PilcfgParse, MultipleRedirectionsPreserveOrder)
{
    auto const cfg = parse_pilcfg(R"({ "redirections": [
        { "from": "HKLM\\A", "to": "HKCU\\X" },
        { "from": "HKLM\\B", "to": "HKCU\\Y" },
        { "from": "HKLM\\C", "to": "HKCU\\Z" }
    ] })"sv);
    ASSERT_EQ(cfg.redirections.size(), 3u);
    EXPECT_EQ(cfg.redirections[0].first, u"HKLM\\A");
    EXPECT_EQ(cfg.redirections[1].first, u"HKLM\\B");
    EXPECT_EQ(cfg.redirections[2].second, u"HKCU\\Z");
}

TEST(PilcfgParse, RedirectionsCombineWithFlags)
{
    auto const cfg = parse_pilcfg(R"({
        "buffer_updates": true,
        "redirections": [ { "from": "HKLM\\Software", "to": "HKCU\\Temp" } ]
    })"sv);
    EXPECT_TRUE(cfg.buffer_updates);
    ASSERT_EQ(cfg.redirections.size(), 1u);
    EXPECT_EQ(cfg.redirections[0].first, u"HKLM\\Software");
}

TEST(PilcfgParse, RedirectionUnicodePreserved)
{
    // from = "HKCU\Ключ" (Cyrillic), to = "HKCU\Schlüssel" (umlaut), written with
    // ASCII-only \u escapes so the test is independent of source-file encoding.
    auto const cfg = parse_pilcfg(
        R"({ "redirections": [ { "from": "HKCU\\\u041a\u043b\u044e\u0447", "to": "HKCU\\Schl\u00fcssel" } ] })"sv);
    ASSERT_EQ(cfg.redirections.size(), 1u);
    EXPECT_EQ(cfg.redirections[0].first, u"HKCU\\\u041a\u043b\u044e\u0447");
    EXPECT_EQ(cfg.redirections[0].second, u"HKCU\\Schl\u00fcssel");
}

TEST(PilcfgParse, RedirectionsNotArrayThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "redirections": {} })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "redirections": "nope" })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "redirections": 7 })"sv));
}

TEST(PilcfgParse, RedirectionElementNotObjectThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "redirections": [ "HKLM\\A" ] })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "redirections": [ 42 ] })"sv));
}

TEST(PilcfgParse, RedirectionMissingFromOrToThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "redirections": [ { "from": "HKLM\\A" } ] })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "redirections": [ { "to": "HKCU\\X" } ] })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "redirections": [ {} ] })"sv));
}

TEST(PilcfgParse, RedirectionNonStringFromOrToThrows)
{
    EXPECT_ANY_THROW(
        parse_pilcfg(R"({ "redirections": [ { "from": 1, "to": "HKCU\\X" } ] })"sv));
    EXPECT_ANY_THROW(
        parse_pilcfg(R"({ "redirections": [ { "from": "HKLM\\A", "to": true } ] })"sv));
}

TEST(PilcfgParse, NoPersistedStateByDefault)
{
    auto const cfg = parse_pilcfg(R"({ "buffer_updates": true })"sv);
    EXPECT_TRUE(cfg.persisted_state.empty());
}

TEST(PilcfgParse, PersistedStateParsed)
{
    auto const cfg =
        parse_pilcfg(R"({ "persisted_state": "C:\\snapshots\\reg.xml" })"sv);
    EXPECT_EQ(cfg.persisted_state, u"C:\\snapshots\\reg.xml");
}

TEST(PilcfgParse, PersistedStateUnicodePreserved)
{
    // "C:\Ключ\reg.xml" written with ASCII-only \u escapes.
    auto const cfg = parse_pilcfg(
        R"({ "persisted_state": "C:\\\u041a\u043b\u044e\u0447\\reg.xml" })"sv);
    EXPECT_EQ(cfg.persisted_state, u"C:\\\u041a\u043b\u044e\u0447\\reg.xml");
}

TEST(PilcfgParse, PersistedStateNonStringThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "persisted_state": 7 })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "persisted_state": true })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "persisted_state": [] })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "persisted_state": null })"sv));
}

TEST(PilcfgParse, NoFaultScriptByDefault)
{
    auto const cfg = parse_pilcfg(R"({ "buffer_updates": true })"sv);
    EXPECT_TRUE(cfg.fault_script.empty());
}

TEST(PilcfgParse, FaultScriptParsed)
{
    auto const cfg =
        parse_pilcfg(R"({ "fault_script": "C:\\faults\\script.xml" })"sv);
    EXPECT_EQ(cfg.fault_script, u"C:\\faults\\script.xml");
}

TEST(PilcfgParse, FaultScriptCombinesWithOtherSettings)
{
    auto const cfg = parse_pilcfg(R"({
        "buffer_updates": true,
        "fault_script": "C:\\faults\\script.xml"
    })"sv);
    EXPECT_TRUE(cfg.buffer_updates);
    EXPECT_EQ(cfg.fault_script, u"C:\\faults\\script.xml");
}

TEST(PilcfgParse, FaultScriptUnicodePreserved)
{
    // "C:\Ключ\fault.xml" written with ASCII-only \u escapes.
    auto const cfg = parse_pilcfg(
        R"({ "fault_script": "C:\\\u041a\u043b\u044e\u0447\\fault.xml" })"sv);
    EXPECT_EQ(cfg.fault_script, u"C:\\\u041a\u043b\u044e\u0447\\fault.xml");
}

TEST(PilcfgParse, FaultScriptNonStringThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "fault_script": 7 })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "fault_script": true })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "fault_script": [] })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "fault_script": null })"sv));
}

//
// Environment-variable expansion in host-path members. A checked-in .pilcfg can
// reference per-machine locations with Windows %VAR% syntax; the parser expands
// those tokens (via ExpandEnvironmentStringsW) for members that denote a host
// filesystem path. Logical namespace identifiers (redirection keys, webcore
// endpoints) are taken literally and are never expanded.
//

constexpr wchar_t k_env_name[]  = L"M_PILCFG_TEST_DIR";
constexpr wchar_t k_env_value[] = L"C:\\expanded";

TEST(PilcfgExpand, CaptureSnapshotExpandsLeadingToken)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "capture_snapshot": "%M_PILCFG_TEST_DIR%\\snap.xml" })"sv);
    EXPECT_EQ(cfg.capture_snapshot, u"C:\\expanded\\snap.xml");
}

TEST(PilcfgExpand, PersistedStateExpandsToken)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "persisted_state": "%M_PILCFG_TEST_DIR%\\reg.xml" })"sv);
    EXPECT_EQ(cfg.persisted_state, u"C:\\expanded\\reg.xml");
}

TEST(PilcfgExpand, DiagnosticLogExpandsToken)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "diagnostic_log": "%M_PILCFG_TEST_DIR%\\trace.log" })"sv);
    EXPECT_EQ(cfg.diagnostic_log, u"C:\\expanded\\trace.log");
}

TEST(PilcfgExpand, FaultScriptExpandsToken)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "fault_script": "%M_PILCFG_TEST_DIR%\\fault.xml" })"sv);
    EXPECT_EQ(cfg.fault_script, u"C:\\expanded\\fault.xml");
}

TEST(PilcfgExpand, TokenInMiddleExpands)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "capture_snapshot": "prefix\\%M_PILCFG_TEST_DIR%\\snap.xml" })"sv);
    EXPECT_EQ(cfg.capture_snapshot, u"prefix\\C:\\expanded\\snap.xml");
}

TEST(PilcfgExpand, MultipleTokensExpand)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "capture_snapshot": "%M_PILCFG_TEST_DIR%\\%M_PILCFG_TEST_DIR%" })"sv);
    EXPECT_EQ(cfg.capture_snapshot, u"C:\\expanded\\C:\\expanded");
}

TEST(PilcfgExpand, NoTokenReturnedUnchanged)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg =
        parse_pilcfg(R"({ "capture_snapshot": "C:\\literal\\snap.xml" })"sv);
    EXPECT_EQ(cfg.capture_snapshot, u"C:\\literal\\snap.xml");
}

TEST(PilcfgExpand, UndefinedTokenLeftVerbatim)
{
    // No such variable is defined; ExpandEnvironmentStringsW leaves the token
    // verbatim, and our specification preserves that behavior.
    ::SetEnvironmentVariableW(L"M_PILCFG_NOT_DEFINED", nullptr);
    auto const cfg = parse_pilcfg(
        R"({ "capture_snapshot": "%M_PILCFG_NOT_DEFINED%\\snap.xml" })"sv);
    EXPECT_EQ(cfg.capture_snapshot, u"%M_PILCFG_NOT_DEFINED%\\snap.xml");
}

TEST(PilcfgExpand, EmptyMemberStaysEmpty)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(R"({ "capture_snapshot": "" })"sv);
    EXPECT_TRUE(cfg.capture_snapshot.empty());
}

TEST(PilcfgExpand, WebcoreMaterializationDirExpands)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "webcore": { "materialization_dir": "%M_PILCFG_TEST_DIR%\\mat" } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    EXPECT_EQ(cfg.webcore->materialization_dir, u"C:\\expanded\\mat");
}

TEST(PilcfgExpand, WebcoreFaultScriptExpands)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "webcore": { "fault_script": "%M_PILCFG_TEST_DIR%\\f.xml" } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    EXPECT_EQ(cfg.webcore->fault_script, u"C:\\expanded\\f.xml");
}

TEST(PilcfgExpand, RedirectionKeysAreNotExpanded)
{
    // Redirection keys are logical namespace identifiers, not host paths, so a
    // '%'-bearing key is preserved exactly.
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "redirections": [ { "from": "HKCU\\%M_PILCFG_TEST_DIR%", "to": "HKLM\\%M_PILCFG_TEST_DIR%" } ] })"sv);
    ASSERT_EQ(cfg.redirections.size(), 1u);
    EXPECT_EQ(cfg.redirections[0].first, u"HKCU\\%M_PILCFG_TEST_DIR%");
    EXPECT_EQ(cfg.redirections[0].second, u"HKLM\\%M_PILCFG_TEST_DIR%");
}

TEST(PilcfgExpand, WebcoreEndpointsAreNotExpanded)
{
    // Endpoint identifiers are logical, not host paths; they are never expanded.
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "webcore": { "endpoints": [ { "public": "%M_PILCFG_TEST_DIR%\\p", "private": "%M_PILCFG_TEST_DIR%\\q" } ] } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    ASSERT_EQ(cfg.webcore->endpoints.size(), 1u);
    EXPECT_EQ(cfg.webcore->endpoints[0].first, u"%M_PILCFG_TEST_DIR%\\p");
    EXPECT_EQ(cfg.webcore->endpoints[0].second, u"%M_PILCFG_TEST_DIR%\\q");
}

//
// webcore.contracts parsing (M-HWC-CONTRACTCFG-4, D-HWC-8). Each entry binds a
// contract spec (a host path, %VAR%-expanded) to a logical endpoint key (taken
// literally) and a mode ("validate" or "drive"). Absent yields an empty vector;
// malformed shapes throw.
//

TEST(PilcfgParse, ContractsAbsentIsEmpty)
{
    auto const cfg = parse_pilcfg(R"({ "webcore": { "materialization_dir": "C:\\m" } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    EXPECT_TRUE(cfg.webcore->contracts.empty());
}

TEST(PilcfgParse, ContractsEmptyArrayIsEmpty)
{
    auto const cfg = parse_pilcfg(R"({ "webcore": { "contracts": [] } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    EXPECT_TRUE(cfg.webcore->contracts.empty());
}

TEST(PilcfgParse, ContractsSingleEntryParsed)
{
    auto const cfg = parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "C:\\specs\\api.yaml", "endpoint": "ping", "mode": "validate" } ] } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    ASSERT_EQ(cfg.webcore->contracts.size(), 1u);
    EXPECT_EQ(cfg.webcore->contracts[0].spec, u"C:\\specs\\api.yaml");
    EXPECT_EQ(cfg.webcore->contracts[0].endpoint, u"ping");
    EXPECT_EQ(cfg.webcore->contracts[0].mode, contract_mode::validate);
}

TEST(PilcfgParse, ContractsDriveModeParsed)
{
    auto const cfg = parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "C:\\specs\\api.yaml", "endpoint": "ping", "mode": "drive" } ] } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    ASSERT_EQ(cfg.webcore->contracts.size(), 1u);
    EXPECT_EQ(cfg.webcore->contracts[0].mode, contract_mode::drive);
}

TEST(PilcfgParse, ContractsMultipleEntriesPreserveOrder)
{
    auto const cfg = parse_pilcfg(
        R"({ "webcore": { "contracts": [
            { "spec": "C:\\a.yaml", "endpoint": "first",  "mode": "validate" },
            { "spec": "C:\\b.yaml", "endpoint": "second", "mode": "drive" },
            { "spec": "C:\\c.yaml", "endpoint": "third",  "mode": "validate" }
        ] } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    ASSERT_EQ(cfg.webcore->contracts.size(), 3u);
    EXPECT_EQ(cfg.webcore->contracts[0].endpoint, u"first");
    EXPECT_EQ(cfg.webcore->contracts[1].endpoint, u"second");
    EXPECT_EQ(cfg.webcore->contracts[2].endpoint, u"third");
    EXPECT_EQ(cfg.webcore->contracts[0].mode, contract_mode::validate);
    EXPECT_EQ(cfg.webcore->contracts[1].mode, contract_mode::drive);
    EXPECT_EQ(cfg.webcore->contracts[2].mode, contract_mode::validate);
}

TEST(PilcfgParse, ContractsSpecExpandsEnvironmentVariable)
{
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "%M_PILCFG_TEST_DIR%\\api.yaml", "endpoint": "ping", "mode": "validate" } ] } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    ASSERT_EQ(cfg.webcore->contracts.size(), 1u);
    EXPECT_EQ(cfg.webcore->contracts[0].spec, u"C:\\expanded\\api.yaml");
}

TEST(PilcfgParse, ContractsEndpointIsNotExpanded)
{
    // The endpoint is a logical key, not a host path; it is taken literally.
    scoped_env_var const env(k_env_name, k_env_value);
    auto const           cfg = parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "C:\\api.yaml", "endpoint": "%M_PILCFG_TEST_DIR%", "mode": "validate" } ] } })"sv);
    ASSERT_TRUE(cfg.webcore.has_value());
    ASSERT_EQ(cfg.webcore->contracts.size(), 1u);
    EXPECT_EQ(cfg.webcore->contracts[0].endpoint, u"%M_PILCFG_TEST_DIR%");
}

TEST(PilcfgParse, ContractsNonArrayThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "webcore": { "contracts": "nope" } })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "webcore": { "contracts": {} } })"sv));
}

TEST(PilcfgParse, ContractsElementNotObjectThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "webcore": { "contracts": [ "nope" ] } })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(R"({ "webcore": { "contracts": [ 42 ] } })"sv));
}

TEST(PilcfgParse, ContractsMissingMemberThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "endpoint": "ping", "mode": "validate" } ] } })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "C:\\a.yaml", "mode": "validate" } ] } })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "C:\\a.yaml", "endpoint": "ping" } ] } })"sv));
}

TEST(PilcfgParse, ContractsEmptySpecOrEndpointThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "", "endpoint": "ping", "mode": "validate" } ] } })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "C:\\a.yaml", "endpoint": "", "mode": "validate" } ] } })"sv));
}

TEST(PilcfgParse, ContractsUnknownModeThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "C:\\a.yaml", "endpoint": "ping", "mode": "observe" } ] } })"sv));
}

TEST(PilcfgParse, ContractsNonStringMemberThrows)
{
    EXPECT_ANY_THROW(parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": 7, "endpoint": "ping", "mode": "validate" } ] } })"sv));
    EXPECT_ANY_THROW(parse_pilcfg(
        R"({ "webcore": { "contracts": [ { "spec": "C:\\a.yaml", "endpoint": "ping", "mode": true } ] } })"sv));
}
