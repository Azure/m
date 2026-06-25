# merriam — CHECKLIST

Action-only checklist. Completed groups move to `COMPLETED-CHECKLIST.md`.
The driving milestone (`MW18`) lives in
[`../windows-win32-shim/CHECKLIST.md`](../windows-win32-shim/CHECKLIST.md); this
file scopes the component-local work.

---

## MER1 — Dictionary-store service (MW18-2)

- [x] **MER1-1** Async **content** store (`store.rs`): one newline-delimited
      word-list file per `(locale, user)` via `windows-file-io`;
      `add`/`remove`/`contains`/`list` with path-safe slugs, word normalization,
      and per-key serialization (MER-D1/D2/D3). 14 unit tests.
- [x] **MER1-2** Dispatch core (`routes.rs`) mirroring `wordy`'s custom-dict
      routes 1:1; host-agnostic request/response/outcome; JSON bodies. Unit tests
      per route.
- [ ] **MER1-3** http.sys listener edge + server bin + gated integration test.
