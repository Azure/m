# CHECKLIST — PII tokenization + encrypted identity map (DEFERRED)

> ⛔ **DEFERRED — do NOT start during the current development push.** Queued per the repo
> owner's directive (2026-06-26): capturing principal identities (even their *names*) and other
> likely-PII request data in plaintext is unacceptable, but implementing tokenization now would
> hamper active work. This plan exists so the requirement is not lost.
>
> Policy: api-journal **D-AJ-4** ([crates/api-journal/DESIGN-NOTES.md](crates/api-journal/DESIGN-NOTES.md)).
> Affects cartographer **D-CART-4** ([crates/cartographer/DESIGN-NOTES.md](crates/cartographer/DESIGN-NOTES.md)).

Cross-cutting across **api-journal** (schema), **windows-win32-shim** (capture), and
**cartographer** (output). Milestones are dependency-ordered; each ends with tests when built.

## Milestone PII-A — Tokenized-identity schema + sidecar map (api-journal)

> **Hard prerequisite for identity features.** PII-A blocks any work that would first introduce
> a raw principal-identity path: cartographer's deferred *richer inbound-caller attribution* and
> *identity-axis role subdivision* (EM-D3, in [crates/cartographer/CHECKLIST.md](crates/cartographer/CHECKLIST.md)).
> Neither may distinguish callers by a raw identity until PII-A lands.

- [ ] **PII-A1** Define a stable opaque **identity token** type and have `JournalRecord`
      reference tokens in place of principal identities.
- [ ] **PII-A2** Define the **token → identity map** sidecar file format (separate from the
      journal; one map per capture session).
- [ ] **PII-A3** Tests: a tokenized record round-trips and carries no raw identity; the map
      resolves tokens back to identities.

## Milestone PII-B — PII-candidate detection + tokenization at capture (windows-win32-shim)

- [ ] **PII-B1** Tokenize principal identity at capture time (replace identity with a stable
      token; record token → identity in the sidecar map).
- [ ] **PII-B2** PII-candidate detection over request data (body fields, query values,
      headers): pattern-based where patterns are clear (emails, GUIDs, bearer tokens, …).
- [ ] **PII-B3** Manual-intervention hooks: per-field allow / deny / tokenize overrides via
      `.pilcfg`, since detection is fuzzy.
- [ ] **PII-B4** Tests: identities and detected PII are tokenized in the emitted journal; the
      sidecar map is complete; overrides honored.

## Milestone PII-C — Encrypted identity map (key use is an explicit opt-in)

- [ ] **PII-C1** Encrypt the token → identity sidecar at rest; never store plaintext PII.
- [ ] **PII-C2** Decryption requires an **explicit developer choice** (no implicit/automatic
      decrypt in tooling).
- [ ] **PII-C3** Adopt a vetted key containment / selection / exchange standard (do **not**
      invent); record the choice as a decision. Surveying/choosing the standard is itself a
      design task within this item.
- [ ] **PII-C4** Tests: the map at rest is ciphertext; decrypt is gated on an explicit key
      choice.

## Milestone PII-D — cartographer never re-leaks identities / PII

- [ ] **PII-D1** Synthesis and the environment descriptor (D-CART-4) consume tokenized
      journals; roles, selectors, and security carry tokens, never raw identities.
- [ ] **PII-D2** `full`-mode example bodies (D-AJ-3) are PII-scrubbed / tokenized before they
      reach specs.
- [ ] **PII-D3** Tests: no raw identity or detected-PII string appears in any synthesized spec
      or environment descriptor.

## Near-term safety rail (may be implemented independently of the deferral)

The rest of this plan is deferred, but this one item directly addresses the production-capture
risk and is a cheap safety rail (not tokenization), so it may be done at any time.

- [ ] **PII-SAFE-1** Warn when `bodies: full` is enabled: it captures literal request/response
      bodies that can contain customer PII, so it must not be run against production traffic
      until PII-D2 (body scrub / tokenization) lands. The default (`shapes`/`none`) stays
      PII-safe and needs no warning.

## Open design questions (resolve when this is undeferred)

- Token stability across capture sessions (same identity → same token, or per-session tokens?).
- Whether tokenization happens only at capture (shim), or also as an offline re-tokenization
  pass over already-gathered journals.
- The key-management standard for PII-C3 — survey existing standards before choosing; do not
  invent a bespoke scheme.
