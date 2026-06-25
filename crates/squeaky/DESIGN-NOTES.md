# squeaky — Design Notes

`squeaky` is a configurable HTTP stress client for `wordy`, built on the
**native Rust async web stack** (Tokio + `reqwest`). It is the Rust-native
counterpart to the WinHTTP-based `talky` client; the two are deliberately
**self-contained** (no shared crate), per the owner's request, and share only a
config schema.

## SQ-D1 — Rust-native transport: Tokio + reqwest

The transport is `reqwest` on a multi-threaded Tokio runtime — the idiomatic
Rust async web client stack. A single `reqwest::Client` (cheap to clone;
internally reference-counted, with connection pooling) is shared across worker
tasks. `reqwest` is configured with `default-features = false` + `json`: `wordy`
is served over plain HTTP, so no TLS backend is pulled in. The whole crate is
`#![deny(unsafe_code)]` — there is no `unsafe` to quarantine.

## SQ-D2 — Concurrency on Tokio tasks

Workers are `tokio::spawn`ed tasks (not OS threads): each loops over synthesized
requests until the configured time and/or count bound, awaiting `reqwest`
between requests, and returns its per-route statistics, which are merged at the
end. This contrasts with `talky`'s Windows-thread-pool workers — the same
workload over two runtimes.

## SQ-D3 — Shared config schema, owned here

Execution parameters come from a JSON file (`src/config.rs`), validated on load
(`serde` + `serde_json`), with the same shape as `talky`'s so one config drives
either. Unknown fields are rejected; the run is bounded by `duration_secs`
and/or `max_requests`.

## SQ-D4 — IPv4 and IPv6 endpoints

`Endpoint::parse` handles IPv4 literals, bracketed IPv6 literals
(`http://[::1]:8080`), and hostnames. The `ip_version` filter (`v4` | `v6` |
`both`) selects which to drive; `base_url()` renders the scheme + (bracketed for
IPv6) host + port into a URL `reqwest` parses directly, so both families work.

## SQ-D5 — Deterministic, weighted workload

`src/workload.rs` is a pure, host-agnostic generator: a seeded xorshift PRNG
drives a weighted route mix and synthesizes each request (random words, anagram
templates, regex patterns, custom-word paths). Seeded for reproducibility and
pure for unit-testability without a network. (This module and `src/stats.rs` are
intentionally identical to `talky`'s — duplicated, not shared.)

## SQ-D6 — Target availability

`wordy` is not yet reachable over genuine HTTP (live Hostable Web Core hosting is
deferred to windows-win32-shim **MW16**). `squeaky` takes its endpoints from
config and is ready to point at the live service when it lands; until then it
drives load against whatever endpoint the config names.
