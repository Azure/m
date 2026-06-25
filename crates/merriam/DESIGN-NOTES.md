# merriam — Design Notes

Current canonical decisions for `merriam`, the dictionary-store service of the
validation tier (windows-win32-shim **SHIM-D23**). Produced by **MW18-2**.

## MER-D1 — Content store, not a namespace store

`wordy`'s custom dictionary stored each word as a **name-encoded empty file**
(pure namespace/metadata ops, no content — windows-win32-shim SHIM-D6). `merriam`
deliberately does the opposite: each `(locale, user)` is **one file whose
content is the newline-delimited word list**. The reason is the validation tier's
purpose — to exercise `windows-file-io`'s native async overlapped read/write —
so the store must read and rewrite file *bytes*, not just create/stat/delete
names. `list` returns the parsed lines (sorted via a `BTreeSet`); `add`/`remove`
read-modify-rewrite the whole file (`CREATE_ALWAYS` truncates, so a shrink leaves
no stale tail).

## MER-D2 — Owned word + path specification (Design Autonomy)

`merriam` defines its own input contract rather than inheriting one:

- A **word** is trimmed, rejected if empty or if it contains a line break (it
  must be exactly one line of the file), then lowercased — the dictionary is
  case-insensitive, matching `wordy`.
- The **locale** and **user** become path-safe slugs (lower-cased; every byte
  outside `[a-z0-9-_]` percent-encoded). The slug can contain no path separator,
  `.`, or `..`, so a hostile `user` such as `../../etc` is neutralized and can
  never write outside the store root (covered by a unit test).

These rules are the service's contract; the on-disk format and `windows-file-io`
are the implementation that satisfies it.

## MER-D3 — Sync store over async I/O, serialized per `(locale, user)`

The store methods are **synchronous** (`add`/`remove`/`contains`/`list`) but
drive `windows-file-io`'s **async** overlapped operations internally via a
thread-pool `block_on`. This keeps the async file I/O genuine while letting a
plain `std::sync::Mutex` serialize the read-modify-write of a word-list file —
the lock is held only across the synchronous `block_on` driver, **never across
an `.await`** (so the `await_holding_lock` hazard never arises). Each
`(locale, user)` has its own lock (created on demand), so distinct dictionaries
proceed concurrently while a single dictionary's concurrent writers — and a
reader racing a rewrite — are serialized. The dispatch core (MW18-2.2) is
therefore synchronous too, mirroring `wordy::routes::Service`; the http.sys
listener (MW18-2.3) offloads each request to a pool work item, so the inbound
receive loop is never blocked by a store operation.
