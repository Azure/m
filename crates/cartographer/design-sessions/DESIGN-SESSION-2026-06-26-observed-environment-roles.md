# Design session — observed-environment descriptor (roles as substitution units)

- **Date:** 2026-06-26
- **Status:** **Approved 2026-06-26 as `D-CART-4`** (see [`../DESIGN-NOTES.md`](../DESIGN-NOTES.md)).
  Work tracked in [`../CHECKLIST.md`](../CHECKLIST.md) (milestones EM-A..EM-E). The
  body below is the original exploration, retained as the historical record.
- **Participants:** repo owner + assistant.
- **Topic:** A new "observed environment" descriptor that maps the *participants
  and channels* around journaled HTTP traffic — not the messages (cartographer
  already synthesizes those as OpenAPI). The driving requirement is that
  **roles are abstractable, replaceable-as-a-unit handles** so a later phase can
  recast them for automated replay or fault injection.

This file is a faithful record of the design discussion, not a polished spec.
If/when settled, the canonical decision moves to `DESIGN-NOTES.md` (`D-CART-4`)
and the implied work moves to a `CHECKLIST`.

---

## 1. Problem & goal

cartographer today answers "what do the messages on a channel look like?"
(OpenAPI synthesis from `api-journal` NDJSON). It does **not** describe the
*channels themselves* — the participants, their addresses/ports, their
authentication, and how they relate.

The owner's longer-term goal: drive automated tests — both normal patterns and
**fault injection** — from synthesized descriptions. That needs more than
messages. Crucially, the owner's framing:

> "having this as an abstractable notion that we can replace as a unit is most
> important … just noting that 'ip address such-and-such connects on port x' is
> interesting, but [the abstraction] is most important."

So the unit of description must be a **role**, not an endpoint. A concrete
`(ip, port, tls, auth)` is *evidence* that some participant played a role; the
role name is the stable substitution boundary.

cartographer's job stops at producing the **map** (Q4, confirmed). What to *do*
with the map (replay, fault injection) is a separate downstream tool/phase.

## 2. Core abstraction — three layers: actor → role → channel

Initially proposed two layers (roles + endpoint evidence). The owner's Q2 answer
("the actor may have multiple roles") splits this into three:

- **Actor** — a concrete observed participant (e.g. the wordy process; a remote
  client population). Carries the binding evidence: scheme/host/port, TLS, cert,
  observed counts/timing. The "who we actually saw."
- **Role** — an abstract *part* an actor plays. **This is the substitution unit.**
  - One actor plays **≥1** roles (the wordy process is a *server* on its inbound
    seam and a *client* on its egress seam → one actor, multiple roles).
  - One role can be recast independently of the actor's other roles (e.g. fault
    only the actor's `spellcheck` server role, leave `anagram` live).
  - Carries the security it `presents` (as a client) and/or `requires` (as a
    server), reusing OpenAPI `securityScheme` shapes.
- **Channel** — a directed edge `from` one role `to` another, carrying a
  `contract` (`$ref` to the OpenAPI doc cartographer already synthesizes) plus
  protocol/transport bindings and provenance.

The substitution contract for a role is **derived** — the union of channels the
role participates in (and their contracts) — not hand-authored. (Owner Q3: it's a
combination; derivation first, expert refinement second.)

## 3. Role derivation — additive refinement hierarchy

Owner Q1: start trivially and subdivide only as evidence demands. The analysis is
offline ("meaty"), so it may use expensive clustering and rearrange roles.

- **Start coarse:** one role, e.g. `wordy-client`.
- **Subdivide on evidence:** when a sub-population shows distinct behavior, split
  → `wordy-client-spellcheck`, `wordy-client-anagram`.
- **Subdivision axis priority:** behavior (which operations/channels the actor
  uses) **>** transport / authentication scheme **>** identity. Client *identity*
  is the least useful axis (you usually can't replicate a principal), with one
  caveat the owner raised: a long-lived principal managed via **rolling certs**
  could make identity-class a legitimate axis.
- **Stability guard (renames are breaking):** the owner noted role-name changes
  break downstream test configs. Mitigation chosen: **splits are additive** — the
  parent role persists as a *group* equal to the union of its children. A config
  targeting `wordy-client` still resolves after the split; experts may target a
  child for finer-grained recast. New evidence adds children; it does not rename
  existing handles.
- Names should be a **deterministic function of the traits** that justified the
  split, so the same evidence reproduces the same names across capture sessions.

## 4. Provenance & the hand-tuning feedback loop (owner side note)

Owner flow: link the library → enable journaling → get journals → cartographer
derives contracts + environment map → **experts hand-tune** → then either (a)
additional capture runs to verify the tuning, or (b) machine-local testing of
model accuracy.

Owner side note: *"I would really like it if we could use the experts' hand
tuning as feedback to improve our derivations … if there is [something to plan
beforehand], please do plan for it."*

**We can't build the learner now, but we can guarantee the data it will need
exists.** Plannable pre-work, baked into the model from day one:

- **Three provenance tiers per element:** `observed` (immutable fact) →
  `derived` (cartographer's interpretation, **carrying its `basis`** — which
  heuristic produced it and why, e.g. "split: distinct operation set") →
  `asserted` (human override, authoritative).
- **Re-synthesis preserves `asserted` elements.** This is the same principle that
  already preserves a renamed `{id}` template once a human names it (D-CART-3):
  human edits are *inputs* to the next derivation, never clobbered.
- **The delta is the signal.** Because `derived` carries its basis and `asserted`
  is stored distinctly, the difference between "what we would derive fresh" and
  "what the expert kept/changed" is always computable. That diff is the future
  training signal. Keeping derivations **deterministic and explainable** is the
  whole pre-investment; the learner itself stays out of scope.
- Optional later: emit a "corrections" record when re-synthesis notices an
  `asserted` value disagreeing with the freshly `derived` one.

## 5. Scope (confirmed)

cartographer **only maps**. It reads journals, derives actors/roles/channels and
their contracts, and emits the environment descriptor (observed + derived,
refinable, re-verifiable). It does **not** execute replay or fault injection —
that is a separate downstream tool/phase the owner will design later once the
API map is solid.

## 6. Document shape (illustrative, wordy)

A new top-level document — working name **Environment** descriptor — reusing
OpenAPI vocabulary where it fits and adding the participant/edge layer OpenAPI
lacks. (Field names illustrative, not final.)

```yaml
environment: 0.1.0
info:                              # OpenAPI Info object, reused verbatim
  title: wordy — observed environment
  version: 2026-06-26
  description: Synthesized from 11 observed inbound interactions.

actors:                            # concrete observed participants (the evidence)
  proc-wordy:
    title: wordy service process
    bindings:
      - { scheme: https, host: wordy.internal, port: 443, observed: { interactions: 11 } }
    plays: [srv-wordy]             # one actor → many roles
  pop-callers:
    title: observed caller population
    plays: [wordy-client]

roles:                             # abstract, recastable parts — the substitution unit
  srv-wordy:
    plays: [server]
    requires: { identity: { type: apiKey, in: header, names: [X-Wordy-User, X-Wordy-Locale] } }
    provenance: derived
  wordy-client:                    # coarse to start; children added additively on evidence
    plays: [client]
    presents: { identity: { type: apiKey, in: header, names: [X-Wordy-User, X-Wordy-Locale] } }
    provenance: derived
    children: []                   # e.g. wordy-client-spellcheck, wordy-client-anagram

channels:                          # role → role edges, each carrying a contract
  wordy-client→srv-wordy:
    from: wordy-client
    to: srv-wordy
    protocol: http/1.1+tls
    contract: { $ref: ./wordy-openapi.yaml }
    transport: { tls: { observed: true } }
    observations: { interactions: 11, operations: 7 }
    provenance: { tier: derived, basis: "single observed inbound seam" }
```

The recast/plan document (replay vs fault) is **separate and downstream** — it
rebinds role → provider without touching this map. Out of cartographer scope.

## 7. Prior-art mapping

| Concern | Borrowed from | Gap we fill |
|---|---|---|
| `info`, `securityScheme` shapes | OpenAPI | — (reuse verbatim) |
| named servers + protocol bindings | AsyncAPI | AsyncAPI has no *client* participant, no substitution notion |
| named consumer/provider roles, contract-per-pair | Pact | Pact = authored expectations for one pair; ours = *observed multi-role topology* |
| stable role name in front of changing instances | service mesh (k8s Service → pods) | mesh is infra config, not a portable observed contract |
| per-edge message contract | cartographer's own OpenAPI synthesis | the channel just `$ref`s it |

Net: *observed topology of named, recastable roles, each channel carrying an
OpenAPI contract, with provenance back to raw bindings* — a combination none of
the individual schemes cover. The role-as-substitution-unit is ours to define.

## 8. Open implementation questions (deferred to the build phase)

1. Concrete clustering algorithm for the behavioral signature that drives role
   splits (and the determinism guarantee for names).
2. Exact field vocabulary (`actors`/`roles`/`channels`/`bindings`/`plays`/
   `presents`/`requires`/`provenance`) — naming bikeshed to settle at first use.
3. How inbound client actors are distinguished at all when anonymous (default:
   one client role per inbound seam unless a configured selector or a behavioral
   split applies).
4. Whether the descriptor is its own crate/module in cartographer and whether the
   `api-journal` record needs any new captured field to support role attribution
   (likely already sufficient: seam, host, path, identity-header names).

## 9. Decision & milestone outline (committed)

Promoted to **`D-CART-4`** (environment descriptor: three-layer actor/role/channel
model; additive role-refinement hierarchy; three-tier provenance enabling later
feedback learning; cartographer maps only). Work is tracked in
[`../CHECKLIST.md`](../CHECKLIST.md) as milestones EM-A..EM-E, roughly:

- Define the environment descriptor model (actors/roles/channels + provenance),
  serialize to YAML/JSON (reuse the existing `format` machinery).
- Derive actors + bindings from journal records (group by seam + host/selector).
- Derive coarse roles + channels; attach each channel's OpenAPI contract.
- Additive role subdivision on behavioral signature (parent retained as group).
- Preserve `asserted` (human) elements on re-synthesis; record derivation basis.
- End-to-end: synthesize the wordy environment doc from the wordy journal and
  assert it round-trips + re-validates.
