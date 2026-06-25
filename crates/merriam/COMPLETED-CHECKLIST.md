# merriam — COMPLETED-CHECKLIST

Append-only record of completed checklist groups (most recent at the bottom).

## Moved 2026-06-25 — MER1: dictionary-store service (MW18-2)

- [x] **MER1-1** Async **content** store (`store.rs`): one newline-delimited
      word-list file per `(locale, user)` via `windows-file-io`;
      `add`/`remove`/`contains`/`list` with path-safe slugs, word normalization,
      and per-key serialization (MER-D1/D2/D3). 14 unit tests.
- [x] **MER1-2** Dispatch core (`routes.rs`) mirroring `wordy`'s custom-dict
      routes 1:1; host-agnostic request/response/outcome; JSON bodies. 13 route
      unit tests (MER-D4).
- [x] **MER1-3** http.sys listener edge (`http_sys.rs`, `Server`) + `merriam-host`
      bin + a gated listener integration test (`tests/listener.rs`: binds a free
      loopback port, drives every route over real HTTP, SKIPs on
      `ERROR_ACCESS_DENIED`). Synchronous dispatch (MER-D5).
