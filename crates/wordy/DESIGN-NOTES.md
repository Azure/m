# wordy — Design Notes

`wordy` is a "shared dictionary" REST service implemented as a Rust IIS native
module. Within this repository it serves two purposes: it is the proof harness
for the link-time host-call redirection built in
[`windows-win32-shim`](../windows-win32-shim) (see that crate's SHIM-D19), and it
is where genuine Hostable Web Core (HWC) business logic is grown. This file
records `wordy`'s own design decisions; the surrounding isolation strategy lives
in the shim's design notes.

## WD-D1 — The shim-unaware contract

`wordy` is **deliberately unaware** of any isolation machinery. This is a
load-bearing property, not an accident:

- `wordy` has **no dependency** on `windows-win32-shim` and references none of its
  types, exports, or concepts.
- `wordy` contains **no isolation code** — no aliasing, no shimming, no `.pilcfg`,
  no awareness that its host calls might be redirected.
- The **only** external artifacts the isolation harness may attach to `wordy` are
  *link inputs* supplied through the environment at build time (see WD-D2). The
  decision to isolate `wordy` is therefore made entirely from the outside, exactly
  as it would be for a third-party application whose source we do not control.

A plain build with no environment variables set produces an ordinary IIS native
module; that plain build *is* the evidence that the crate carries no isolation
knowledge.

## WD-D2 — Generic env-driven `build.rs`

`build.rs` injects extra link inputs **only** when `WORDY_EXTRA_LINK_SEARCH`,
`WORDY_EXTRA_LINK_OBJ`, and/or `WORDY_EXTRA_LINK_LIB` are set, and otherwise does
nothing. These names are generic ("extra link object", "extra link lib"); the
script carries no knowledge of aliases or shims. Build-script directives are used
rather than external `RUSTFLAGS` / `.cargo/config.toml` because they are scoped to
this crate's final artifact and cache deterministically, without leaking link
flags to sibling crates.

## WD-D3 — `wordy` declares its own IIS ABI (peer of `mwinweb`)

The IIS `httpserv.h` native-module interfaces are not part of `windows-sys`, so
the ABI is modeled as hand-rolled `#[repr(C)]` vtables. `wordy` declares its
**own** copy of the minimal subset it uses (`src/iis.rs`) rather than reusing the
shim's `mwinweb` module — reusing `mwinweb` would couple `wordy` to the shim and
break WD-D1. The modest duplication is intentional and acceptable; extracting a
neutral `iis-native-module` crate that both could share is deferred. The modeled
vtable layouts are pinned precisely when a genuine host is bound (MW13-5).

## WD-D4 — `RegisterModule` is exported under its real name

IIS loads `wordy.dll` directly and calls its `RegisterModule` export by name, so
`wordy` exports `RegisterModule` (not the shim's internal `mRegisterModule`). All
`unsafe` lives in `src/iis.rs`, which the crate root opts into explicitly while
denying `unsafe_code` everywhere else.

## WD-D5 — Safe routing core split from the ABI boundary

Request handling is split so the decision logic is testable without a host:
`src/routes.rs` is pure, safe, and platform-independent (method + URL → outcome),
while `src/iis.rs` only decodes the host request into strings and realizes the
outcome against the host response. MW13-1 seeds the dispatcher with a single
health route (`GET /healthz` → 200); later milestones grow it into the full
dictionary surface.
