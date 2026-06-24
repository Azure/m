# Design session — 2026-06-23 — aliasobj shim: off / record / replay over a host process

**Status: promoted (2026-06-24).** The principles below were reviewed and
promoted to Tier-1 decisions **D24–D29** in `DESIGN-NOTES.md`; this file is
retained as the rationale/record of how they were reached. Where a principle
extends or refines an existing decision it says so.

**Promoted to (see `DESIGN-NOTES.md`):**

- Refinement of **D17** — the redirection model is generalized from "HWC surface,
  out-of-process" to "the whole set of import-based platform APIs a host process
  uses," unified by one interception technique.
- **D24** — interception is **link-time aliasobj symbol redirection**, never
  runtime patching.
- **D25** — the shim presents **three modes** (off / record / replay) that are
  exactly the D4 decorator stack applied to the full surface.
- **D26** — the shim provides **dynamic-loader shims** (`mLoadLibrary{A,W}`,
  `mLoadLibraryEx{A,W}`, `mGetProcAddress`) so runtime-bound symbols and
  substitutable engines are reachable by the same technique.
- **D27** — **coordinated multi-surface isolation**: network, filesystem, and
  registry are recorded/replayed as one coherent world.
- **D28** — **PII discipline** is a first-class constraint of record mode:
  payload data is not captured without audit.
- **D29** — **observe unaddressed seams**: surfaces we choose not to virtualize,
  but whose entry points we can name, are still logged as call-events so their
  use is visible and reviewable later.

---

## Scenario — the thing that must work

A complex host process is run under the shim **with no edits to the host's naive
call sites**. The host author wrote ordinary C++ that calls the platform the
ordinary way — registry reads, file I/O, an HTTP client, an RPC channel, and a
dynamically loaded web engine. We want that same process, **relinked** against our
shim, to run with its platform interactions diverted through a layer we control.

The bulk of the interaction we care about is **network traffic** (an outbound HTTP
client surface and a bidirectional RPC surface). A smaller but essential part is
**local filesystem and registry access that is coordinated with** that network
traffic — state that must be consistent with the recorded exchange for a replay to
be coherent.

The discussion settled that this is **clearly achievable**: the process runs, so
*something* binds the platform surfaces into it; therefore we can sit in that
binding. The only real work is enumerating the import-based APIs the naive code
uses and redirecting them into our DLL. *Which* of the host's modules relink, and
*which* symbols and interfaces are taken over, are implementation details to be
solved — not architectural questions to be re-litigated.

## Principle 1 *(provisional D24)* — interception = aliasobj symbol redirection

"Intercept" here means exactly one technique: an **alias object** placed in the
link of a module we build so that the **naive symbol the C++ author wrote**
(`RegOpenKeyExW`, `CreateFileW`, the HTTP-client entry points, the RPC entry
points, `LoadLibraryW`, `GetProcAddress`, …) resolves to **our** implementation
instead of the OS import. This is a **link-time** decision in the consuming
module's import table. It is **not** Detours, IAT hot-patching, or any runtime
code modification.

Consequence to keep in mind: aliasobj redirects calls a module **makes** (its
import table). It does not redirect virtual calls **made into** a module through
an interface pointer it was handed — that is a vtable dispatch owned by whoever
constructed the object. The two are handled differently (see "import-vs-vtable"
below), but both reduce to the same family of shims; no second technique is
introduced.

## Principle 2 *(provisional D25)* — three modes = the D4 decorator stack

The shim is **mode-switchable**, and the modes are the existing policy-intent
decorator stack (**D4**) applied to this surface:

| Mode | D4 layer | Behavior |
|---|---|---|
| **off** | pass-through | every shimmed symbol forwards to the real implementation; the shim is a faithful **identity**. Off must be indistinguishable from not being shimmed at all. |
| **record** | journaling | forward to the real implementation **and** append an ordered, replayable verb stream of the API activity, subject to the PII constraint (Principle 5). |
| **replay** | replay-consumer (**D15**) | service calls from the recorded journal **instead of** forwarding; the real platform is not touched. |

Pass-through being a true identity is the load-bearing requirement: it is what lets
the shim be linked in permanently and toggled, rather than being a separate build.

## Principle 3 *(provisional D26)* — dynamic-loader shims

Some symbols and whole engines are bound **at runtime** rather than through the
static import table — `GetProcAddress` against a `LoadLibrary`'d module, and
delay-loaded DLLs resolved on first use. To bring those under the **same**
aliasobj technique, the shim ships:

- `mLoadLibraryA` / `mLoadLibraryW`
- `mLoadLibraryExA` / `mLoadLibraryExW`
- `mGetProcAddress`

A module relinked against the shim has its naive `LoadLibrary*` / `GetProcAddress`
calls redirected to these. The shim then decides, per request, whether to return a
real module/proc (off / pass-through) or a **shim-supplied** one (record/replay, or
to substitute an engine entirely). This is what makes a **dynamically loaded
engine** (e.g. an HWC web core resolved via `GetProcAddress` for `WebCoreActivate`)
reachable: we intercept the *loader call the host makes*, not the engine's own
internals.

## Principle 4 *(provisional D27)* — coordinated multi-surface isolation

Network is the majority of the problem, but it is **not** isolated in a vacuum.
The shim treats **network + filesystem + registry as one coordinated surface**:
the registry values and files the host reads around a network exchange are part of
the same recorded world. A replay must present filesystem/registry state that is
**consistent with** the network journal it is replaying; record mode must capture
the coordinated reads alongside the traffic. The surfaces share the journal model
and the mode switch so they move together.

## Principle 5 *(provisional D28)* — PII discipline in record mode

Recording is **PII-first**. The default posture is:

- Capture **shape / control / metadata** (which call, ordering, sizes, status,
  non-sensitive headers/keys) freely.
- **Do not** capture payload / body / value data **without an audit** of its
  contents. Most payloads will require redaction or will not be recordable at all.
- Reuse the host product's **existing data-cleanup / scrubbing** machinery rather
  than inventing a parallel one (such machinery is expected to already exist in
  the product whose traffic we record).

This is a constraint on the journaling layer, to be specified in detail during
implementation review; it is recorded here so the journal format is designed with
redaction as a primary concern, not an afterthought.

## Principle 6 — layered like the PIL config (pilcfg) work

The whole arrangement takes the **same form as the pilcfg decorator stack**: the
isolation / record / replay logic is **layered beneath the naive call sites**, and
the consumer code is unaware of which layer is active. This is the established PIL
shape (D4 decorator stack, D10 provider composition); this session asserts that the
network and loader surfaces adopt that **same** shape rather than a bespoke one.

## Principle 7 *(provisional D29)* — observe unaddressed seams, don't silently ignore them

For any surface we **choose not to virtualize** but whose **entry point we can
name** (`CoCreateInstance`, `GetProcAddress` / `LoadLibrary*`,
`HttpSetServiceConfiguration`, the CRT `FILE*` family, …), the shim still **records
that the entry point was used** — which API, against which target (CLSID,
module/proc name, config key) — even when it passes the call straight through
untouched. That yields an **inventory of what the host actually exercised** without
committing to handle it. Later review decides, per entry point, whether the use was
impactful and whether to promote it from *observed-and-passed-through* to
*virtualized*. This is what makes the tracked-risk list below **measurable** rather
than speculative: every risk that has a known entry point also has an observation
hook, so we learn from real runs whether it bites.

This is deliberately coupled to a **volume policy** (open question 5): once a given
entry-point use is known-safe and happens constantly, logging every occurrence is
noise. The intent is to keep the *capability* to observe every seam while letting a
known-safe allowlist suppress routine logging (still counting occurrences) so the
journal stays about data worth processing.

## The import-vs-vtable boundary (why the loader shims matter)

A recurring point worth recording so it is not re-discovered:

- Everything the host **calls out to** — registry, file, HTTP client, RPC,
  loader — is an **import**, and is redirected by Principle 1 directly.
- A surface delivered **into** the host through an interface pointer it was handed
  (the canonical case: a hosted web request arriving as an `IHttpContext*` whose
  methods dispatch through the **web engine's** vtable) is **not** an import and is
  **not** reachable by relinking the receiving module.

The resolution uses the loader shims (Principle 3), not a new technique: by
intercepting the host's **engine-load** calls, the shim becomes the party that
**activates the engine** and therefore **supplies the registrar and the request
context**. The host's own registration entry point (the public IIS-native-module
contract — `RegisterModule` → `SetRequestNotifications(factory, …)` →
`CHttpModule::OnBeginRequest`) then hands the shim its factory voluntarily, and the
shim drives that factory with a shim-supplied context. The receiving module is
**unmodified**; the single link-time hook is the aliased loader call in the host
executable. "Take over the registration API and shim the real handlers" is exactly
this: the shim supplies the `IHttpModuleRegistrationInfo` by being the activator,
wraps the factory the module registers, and decorates the real handler.

Honest fork, recorded so it is not re-argued:

- **Synthetic context (hermetic).** The shim is the engine; it constructs the
  request/response objects. Pure link-time; no real engine, no admin, no kernel
  HTTP stack. This is the path that composes with record/replay.
- **Real engine fidelity.** If the *real* engine must run and supply the real
  context, the engine — not the shim — owns the registrar, and interposing on its
  internal calls would require runtime patching. That is **out of scope** for the
  aliasobj technique by definition.

## Tracked risks and unaddressed seams

Kept as a living list (Principle 7). Each risk names the **entry point(s)** to
observe so its real-world impact can be measured before we decide whether to act.
"Disposition" is the current intent, not a commitment.

| Risk / seam | Entry point(s) to observe | Current disposition |
|---|---|---|
| **CRT `FILE*` family coherence** — redirecting one CRT file call without the rest yields half-virtualized handles | `fopen_s` / `_wfopen_s`, `fread`, `fwrite`, `fseek`, `fclose` | Virtualize as a coherent set only when CRT-fs isolation is in scope; until then observe the open calls. Reachable because the UCRT is dynamically linked (`api-ms-win-crt-*` import thunks), so these are aliasable IAT entries. |
| **Lazy request-wrapper evaluation** — the inbound request/response wrapper is lazy; eager capture would miss fields the handler never pulled, or pull fields it never would | the handler's field reads on the request/response wrapper | Design the journal at **field-pull granularity**, not eager snapshot. |
| **Async completion ordering** — completions arrive on threadpool/callback threads, not in call order | `WinHttpSetStatusCallback` and the RPC async-callback registrations | Key the journal by **request identity**, not call order (D12 async-at-edges). |
| **Config-plane side effects** — provisioning APIs mutate a machine-global store | `HttpSetServiceConfiguration` / `HttpInitialize` | Observe, and **stub** in record/replay so a test host's persistent config is not mutated. |
| **COM activation** — object construction is not caught by the loader shims | `CoCreateInstance` (ole32) | Startup/host-plane only (not per-request); **observe everywhere**, virtualize only in the dev-replay bootstrap (e.g. a faked config admin manager). |
| **Dynamic engine / proc resolution** | `LoadLibrary*` / `GetProcAddress` (module, proc) pairs | Already shimmed (D26); observe the resolved pairs so engine/proc substitution is auditable. |

## Relationship to existing decisions

- **D4** (decorator stack) — the three modes *are* this stack; nothing new in the
  layer semantics, only the surface they apply to.
- **D15** (replay consumer) — replay mode is the D15 "consume saved state" posture
  extended from registry to the coordinated network/fs/registry world.
- **D17** (HWC redirection, out-of-process) — this session **generalizes** D17: the
  out-of-process capture pipeline remains the heavy-traffic path, but the
  *interception mechanism* across all surfaces is unified as the aliasobj +
  loader-shim technique, and the HWC inbound surface is reached via the loader
  shims (the engine-activation route above).
- **D12** (sync core, async at the edge) — unchanged: capture stays thread-frugal
  on the intercepted thread and hands off; async lives in the sibling crates.
- **D10** (provider composition) — the per-surface providers compose behind the
  same facade/seam; the network and loader surfaces add provider kinds, not a new
  composition model.

## Open questions (for review)

1. **Fidelity vs observability.** Pass-through-to-real maximizes fidelity;
   record/replay maximizes observability and hermeticity. How per-surface and how
   dynamic should the mode switch be (global, per-surface, per-call-class)?
2. **In-process synthetic engine vs child process.** The synthetic-context path can
   run in-process or behind a cleaner child-process capture boundary (the D17
   service). Which boundary for which surface?
3. **Journal coordination.** Exactly how the network journal and the
   filesystem/registry snapshots are keyed together so a replay is provably
   coherent (Principle 4).
4. **Redaction contract.** The concrete PII rules and the integration point with
   the product's existing scrubbing (Principle 5).
5. **Collection volume vs known-OK suppression.** Record mode can produce
   voluminous data. We will need a policy that distinguishes (a) data collected
   for later processing from (b) events that are merely *odd-but-known-safe* and
   need not be logged on every occurrence — e.g. `GetProcAddress(knownImport)`
   against a module already known to be safe. Likely shape: an **allowlist of
   known-safe `(api, target)` pairs** that suppresses routine logging while still
   counting occurrences, so the journal stays focused on data worth processing.
   Deferred until more implementation experience tells us which seams actually
   fire constantly. (Pairs with Principle 7.)
