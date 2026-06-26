# CHECKLIST — `cartographer` observed-environment descriptor (D-CART-4)

Implements the environment descriptor: an observed topology of **actors**, **roles**,
and **channels**, with roles as the substitution unit. Design: `D-CART-4` in
[DESIGN-NOTES.md](DESIGN-NOTES.md); rationale in
[design-sessions/DESIGN-SESSION-2026-06-26-observed-environment-roles.md](design-sessions/DESIGN-SESSION-2026-06-26-observed-environment-roles.md).

## Milestone EM-A — Descriptor model & serialization

- [x] **EM-A1** Add an `environment` module defining the descriptor model:
      `Environment` (version + reused `Info`), `Actor` (id, title, `bindings`,
      `plays`), `Binding` (scheme/host/port + observed counts), `Role` (id,
      `plays`, `presents`/`requires` security, `children`), `Channel` (from/to
      role, protocol, `contract` ref, transport, observations). Serde derive.
- [x] **EM-A2** Add the `Provenance` annotation (`observed` | `derived` { basis }
      | `asserted`) and attach it to actors, roles, and channels.
- [x] **EM-A3** Serialize/deserialize the descriptor through the existing `format`
      machinery (default YAML, also JSON), reusing the serde model.
- [x] **EM-A4** Tests: a hand-built `Environment` round-trips YAML and JSON
      (parse → equal); provenance and the `$ref` contract links survive.

## Milestone EM-B — Actor & binding derivation from journals

- [x] **EM-B1** Derive egress actors keyed by `(scheme, host, port)` with a
      `Binding`; the observed process becomes a single local actor.
- [x] **EM-B2** Derive inbound actors: the local process is the server actor;
      remote callers default to one `client` actor per inbound seam (an unknown
      server binding is allowed — bindings are optional evidence).
- [x] **EM-B3** Attach observed evidence (interaction counts, first/last
      timestamps) to bindings and actors.
- [ ] **EM-B4** Tests: a small synthetic egress+inbound journal yields the
      expected actor set, bindings, and counts.

## Milestone EM-C — Role & channel derivation + contract linkage

- [ ] **EM-C1** Derive coarse roles from actors (one role per part: server /
      client) with deterministic, stable role ids.
- [ ] **EM-C2** Derive channels as `(client-role → server-role, protocol)` edges
      carrying observation counts.
- [ ] **EM-C3** Link each channel's `contract` to the synthesized OpenAPI document
      (a `$ref` / relative path), reusing existing synthesis output.
- [ ] **EM-C4** Carry `presents` / `requires` security onto roles from observed
      identity/auth header names.
- [ ] **EM-C5** Tests: roles, channels, contract refs, and security derive
      correctly from the synthetic journal.

## Milestone EM-D — Additive role refinement (behavioral subdivision)

- [ ] **EM-D1** Compute a behavioral signature per client actor (the set of
      operations / channels it used).
- [ ] **EM-D2** Subdivide a coarse role into child roles when behaviorally distinct
      sub-populations appear; retain the parent as a group (= union of children);
      child names derive deterministically from the distinguishing trait.
- [ ] **EM-D3** Encode the subdivision-axis priority (behavior > transport/auth >
      identity); keep identity-class subdivision behind the documented rolling-cert
      caveat, off by default.
      > ⛔ **PREREQUISITE — PII-A:** identity-axis subdivision must stay disabled until PII-A
      > (tokenized identity) lands; it must never key on a raw principal identity. Until then
      > this item ships the behavior and transport/auth axes only. See
      > [`../../CHECKLIST-pii-tokenization.md`](../../CHECKLIST-pii-tokenization.md).
- [ ] **EM-D4** Tests: two behaviorally distinct caller populations split into two
      child roles under a retained parent; a homogeneous journal does not split.

## Milestone EM-E — Provenance preservation, CLI emission, end-to-end

- [ ] **EM-E1** Mark derived elements `derived` with their `basis`; on re-synthesis
      over an existing descriptor, preserve `asserted` (human) elements and never
      clobber them (mirrors the D-CART-3 prose-preservation rule).
- [ ] **EM-E2** CLI: emit the environment descriptor alongside the spec
      (`--environment <path>` / `environment.<fmt>`).
- [ ] **EM-E3** Extend the `wordy` example to also emit `wordy-environment.yaml`.
- [ ] **EM-E4** Integration: synthesize the wordy environment descriptor from the
      wordy journal; assert it round-trips, references the wordy OpenAPI contract,
      and that re-synthesis preserves a hand-`asserted` role rename.

## Deferred (intentional, not gaps)

- Richer inbound-caller attribution may want additional shim capture (e.g. an
  authority/identity field); the first pass defaults to one client role per seam.
  Recorded here so the absence is deliberate, per D-CART-4. **Blocked on PII-A
  (tokenized identity):** any caller-distinguishing by principal identity must consume tokens,
  never raw identities — see [`../../CHECKLIST-pii-tokenization.md`](../../CHECKLIST-pii-tokenization.md).
- The recast/plan document (rebinding role → provider) and any replay / fault
  executor are downstream of cartographer and out of scope for these milestones.
- PII tokenization of identities and likely-PII request data (api-journal **D-AJ-4**) is
  deferred and tracked in the root [`CHECKLIST-pii-tokenization.md`](../../CHECKLIST-pii-tokenization.md);
  milestone PII-D covers cartographer not re-leaking identities into specs or the descriptor.
