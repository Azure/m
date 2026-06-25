# talky — Design Notes

`talky` is a configurable HTTP stress client for `wordy`. Its defining choice is
to talk over **Microsoft-native web APIs** rather than a Rust HTTP stack, so it
exercises `wordy` the way native Windows clients do — and so it can grow native
authentication later. It is the native-stack sibling of the Rust-native
`squeaky` client; the two are deliberately **self-contained** (no shared crate),
per the owner's request.

## TK-D1 — Native transport: WinHTTP

The transport is the WinHTTP API (`src/winhttp.rs`), bound through `windows-sys`.
This mirrors `wordy`'s own use of native Win32 surfaces and is the seam where
future native authentication (Negotiate / NTLM / Basic / Digest, via
`WinHttpSetCredentials` / `WinHttpQueryAuthSchemes`) will be added without
touching the workload or driver. A single session handle is opened once and
shared across all workers (WinHTTP handles are thread-safe); each request opens
a connection + request, sends, reads the status, drains the body, and closes the
per-request handles so the session's connection pool is reused. All `unsafe`
lives in `winhttp.rs`; the crate root denies `unsafe_code` otherwise.

## TK-D2 — Concurrency on the Windows thread pool

Workers run on `windows-threadpool` (`submit_once`), not `std::thread` — keeping
`talky` on the Microsoft-native stack end-to-end and reusing an in-house crate.
Each worker loops over synthesized requests until the configured time and/or
count bound, accumulating per-route statistics that are merged at the end.

## TK-D3 — JSON configuration, owned schema

All execution parameters come from a JSON file (`src/config.rs`), validated on
load (`serde` + `serde_json`). The schema is intentionally the same shape as
`squeaky`'s, so one config can drive either client. Unknown fields are rejected
(`deny_unknown_fields`) to catch typos. The run is bounded by `duration_secs`
and/or `max_requests` (at least one required).

## TK-D4 — IPv4 and IPv6 endpoints

Endpoints are full base URLs; `Endpoint::parse` handles IPv4 literals, bracketed
IPv6 literals (`http://[::1]:8080`), and hostnames, classifying each by family.
The `ip_version` config (`v4` | `v6` | `both`) filters which endpoints are
driven; hostnames are always eligible (their family is resolved at connect
time). WinHTTP connects to the literal host + port directly, so both families
work without special handling.

## TK-D5 — Deterministic, weighted workload

`src/workload.rs` is a pure, host-agnostic generator: a small seeded xorshift
PRNG drives a weighted route mix and synthesizes each request (random words,
anagram templates, regex patterns, custom-word paths). Being seeded makes a
given config reproducible, and being pure makes the request shapes unit-testable
without a network.

## TK-D6 — Target availability

`wordy` is not yet reachable over genuine HTTP (live Hostable Web Core hosting is
deferred to windows-win32-shim **MW16**). `talky` therefore takes its endpoints
from config and is ready to point at the live service when it lands; until then
it drives load against whatever endpoint the config names (e.g. a stand-in
server). The transport, concurrency, workload, and reporting are all exercised
today; only the live `wordy` target awaits MW16.
