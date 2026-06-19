# `disposition` — contractual non-success results

## What `disposition` is

`disposition<CodeT, FlagsT>` is the channel an operation uses to report a
**contractual non-success result**: a defined, expected outcome that is part
of the operation's published contract and that is **not an error**.

It is deliberately *not* an error channel. Errors — conditions such as
access-denied, not-found, out-of-memory, or any other "the operation could not
be performed" situation — travel on a different channel:

* `std::error_code&` for the non-throwing API surface, and
* exceptions (e.g. `std::system_error`) for the throwing convenience wrappers.

A `disposition` answers the question *"which of the contractually-defined
outcomes occurred?"* for a call that was otherwise carried out. An
`std::error_code` answers the orthogonal question *"did the operation fail?"*

## Why it exists: replacing brittle error-code sniffing

The motivating purpose of this pattern is to **stop clients from inspecting
specific, low-level status/error values** to infer what happened.

Without it, a client ends up writing fragile code like:

```cpp
// Brittle: the client reverse-engineers meaning from a raw status value.
auto status = SomeRegistryCall(...);
if (status == ERROR_MORE_DATA) { /* ... */ }
```

This couples the client to the exact status values a particular platform
happens to return, and to the assumption that those values mean the same thing
everywhere. It also conflates "an error happened" with "a defined alternative
outcome happened."

`disposition` inverts that relationship. The API offers a **contractual
vocabulary**: the caller opts in with **input flags**, and in return the API
guarantees to express the information the caller asked for as **disposition
codes/flags**. The client switches on the API's own contractual outcomes —
never on raw platform status values.

```cpp
// Contractual: the client asks for the outcome it cares about and
// switches on the API's own vocabulary, not on raw status values.
auto d = some_operation(enable_existing_vs_created, ..., out, ec);
throw_if_failed(ec);                       // errors are a separate channel
if (d.code() == create_key_result_code::opened_existing) { /* ... */ }
```

### Error codes are ambiguous; dispositions are not

Avoiding hard-coded status *values* is only half the problem. The deeper issue
is that an `error_code` is **provenance-blind**: it tells you *that* something
failed, but not *which resource* the failure pertains to, nor *which internal
step* produced it.

Consider "file not found" (`ERROR_FILE_NOT_FOUND` / `ENOENT`). When a client
receives it from a single logical operation, it cannot safely conclude that the
resource it asked about is the thing that was missing. One operation may, under
the hood, touch many resources — a parent key, a transaction or redirection
layer, a backing side-file, a security-descriptor lookup, a reparse/symlink
target. Any of those could be the "not found." So even a client that matches
the error value *correctly* can reach the *wrong* conclusion: "X does not
exist," when in fact X was fine and some internal dependency was missing.

The API is the only party that knows which internal call failed and what it
meant. A contractual disposition lets it take responsibility for that
distinction:

* If the client's named resource is genuinely the missing one, the API reports
  a contractual outcome that says exactly that (e.g.
  `open_key_result_code::target_key_not_found`) — a guarantee *about the
  resource the client named*.
* If instead some internal dependency was missing, that is an **error** about an
  internal condition — surfaced on the `error_code` channel — not a statement
  that the client's resource is absent.

A raw "file not found" code conflates those two completely different situations.
A disposition keeps them separate. This makes error-code inspection not just
brittle (coupled to values) but **unsound** (it cannot establish that the
failure even pertains to the caller's resource). `disposition` replaces both
problems with an outcome the API contractually vouches for.

## The opt-in gate (preserved)

A core invariant of `disposition` is **"simple callers get simple results."**

* If a caller passes input `flags{0}` (i.e. opts into nothing), the returned
  `disposition` is nominal — `operator bool()` is `false`, both `code()` and
  `flags()` are their zero values.
* A provider may **not** surface a richer or newly-added contractual outcome to
  a caller that did not enable it via an input flag.

This gate is what makes the contract forward-compatible: an operation can grow
new contractual outcomes over time without ever blindsiding existing callers.
Old callers, having opted into nothing, continue to see only nominal results
and the success-or-throw (or success-or-`ec`) behavior they were written
against. A caller observes a new outcome **only** because it deliberately
opted in to it.

### Reconciling "contractual" with "gated"

These two ideas fit together as **two tiers of contract**:

* **Simple contract (no opt-in):** the operation guarantees success, or it
  reports an error on the `error_code`/exception channel. Any richer
  alternative outcome is collapsed to nominal. The caller has no additional
  obligations.
* **Richer contract (opt-in):** by setting an input flag, the caller signs up
  for — and therefore becomes obligated to handle — the specific contractual
  outcomes that flag enables.

A caller cannot "miss" something it was required to handle, because it only
becomes responsible for an outcome by explicitly opting into it.

## `code` versus `flags`

A `disposition` carries two scoped-enum components, and both are subject to the
opt-in gate:

* **`code` (`CodeT`)** — the **mutually-exclusive** contractual outcome: *which*
  one of the defined alternatives occurred (e.g. "created new" vs. "opened
  existing"). Zero means the nominal/ordinary outcome.
* **`flags` (`FlagsT`)** — **orthogonal** additional bits that may accompany the
  outcome, each independently meaningful.

`operator bool()` is `true` when *anything* out of the ordinary is present —
that is, when `code != 0` **or** `flags != 0`.

## How the gate maps to inputs (designer's choice)

How an input flag enables an output disposition is left to the **operation's
designer**. Two common shapes:

* **Bit-for-bit:** each input flag enables one specific corresponding output
  disposition bit/code. (This is the usual case.)
* **Gate:** a single input flag turns on a broader vocabulary of contractual
  outcomes for that call.

Either is valid; the choice is part of designing the individual operation's
contract.

## Relationship to the error channel

`disposition` and `std::error_code` are **independent** and coexist in the same
provider primitive. The canonical provider signature is:

```cpp
// Provider primitive (each concrete ikey implementation must supply this):
//   * ec        -> error channel (failure / no failure)
//   * return    -> disposition (which contractual outcome, gated by input flags)
//   * out&       -> the produced result on success
virtual op_disposition
op(op_flags flags, /* inputs... */, result_type& out, std::error_code& ec) = 0;
```

* `ec` reports whether the operation **failed**.
* the returned `disposition` reports **which contractual outcome** occurred,
  honoring the opt-in gate implied by `flags`.
* the throwing convenience overload is a thin wrapper: it calls the `ec` form
  and then `throw_if_failed(ec)`, returning the same `disposition`.

The two channels never overlap: an error is never encoded as a disposition, and
a contractual outcome is never encoded as an `error_code`.

## A note on irony

It is worth acknowledging a tension at the heart of pairing these two channels.

The `disposition` pattern was conceived — and refined over the course of
decades — precisely to *eliminate* fragile status-comparison code: the sprawl of
`if (status == THIS) ... else if (status == THAT) ...` that couples callers to
raw values and to guesses about what those values mean. Exceptions complement
that goal nicely: on the throwing surface, the error path simply propagates, and
the caller writes none of that branching at all.

Offering an `std::error_code&` overload deliberately reintroduces exactly the
thing the pattern set out to abolish — an out-parameter the caller must inspect
and branch on after every single call. There is no escaping the irony: the
non-throwing surface trades the cleanliness of propagation for the manual,
check-after-every-call style that motivated `disposition` in the first place.

The resolution is that the two channels answer different questions and serve
different callers, on purpose:

* `disposition` still does its original job — it keeps callers from sniffing
  status *values* to recover **contractual meaning**. That benefit holds whether
  errors propagate as exceptions or are handed back as an `error_code`.
* `error_code` exists for callers (and boundaries — such as a C ABI like the
  `mReg*` shims) that **cannot or must not let exceptions propagate**. For them,
  manual inspection is not a regression; it is the only option, and it is still
  far better than inspecting raw platform status, because the *contractual*
  meaning continues to arrive via the `disposition`.

So the irony is real but not a contradiction: `error_code` reintroduces
status-checking only for the *error* axis, and only where exceptions are not
viable — while `disposition` continues to keep the *contractual-outcome* axis
free of value-sniffing on both surfaces.

### What is actually objectionable: *inspecting*

It is tempting to read all of this as a dislike of exceptions, or of error
codes. It is neither. The objection is narrower and more precise: it is the act
of **stopping to inspect an error condition**.

There is an old exchange that captures it. Asked *"do you hate C++
exceptions?"*, the answer was: *"No — I don't hate exceptions. I hate
**catching** them."* The same sentiment applies here. A `catch` block and an
`if (ec) { ... }` are the same gesture wearing different clothes: control flow
halts and interrogates a failure. **Any code that stops to inspect an error
condition is, in this view, suspect** — not wrong in every case, but a smell to
be justified rather than assumed.

This is the deeper reason `disposition` exists, and why it is framed as
*contractual* rather than as a softer error report. A disposition does not ask
the caller to investigate what went wrong; it hands back a positive, named
statement of **what contractually happened**, so the caller can make a forward
decision instead of pausing to diagnose. Catching, inspecting, and sniffing are
all the same backward-looking move; `disposition` is the attempt to replace that
move with a forward-looking one wherever the outcome is part of the contract.

Errors — and the occasional unavoidable `catch` or `if (ec)` at a boundary —
remain a fact of life, especially at places like the C ABI of the `mReg*` shims.
The goal is not to pretend they do not exist; it is to confine the
stop-and-inspect gesture to the narrow error axis where it is truly unavoidable,
and to keep everything that *can* be expressed contractually out of that world
entirely.

### Sitting atop three worlds

This design does not exist in a vacuum; it sits at the meeting point of three
different worlds, each with its own native philosophy about how an operation
reports what happened. Much of the tension above is simply the friction between
them.

1. **The status-returning provider underneath.** One of the most important PIL
   providers only ever returns status codes — it has no exceptions, no
   `error_code`, just integer results. Crucially, over many years that provider
   has had to *carefully curate* those return codes so that specific values
   carry **contractual obligations**, not merely "it failed." In other words,
   the `disposition` idea is not new to this layer; the underlying world already
   discovered, the hard way, that some status values must be promoted to
   contractual meaning. It simply expresses that through hand-curated codes
   rather than a typed channel.

2. **The PIL (`ikey`) interfaces in the middle.** The PIL attempts to present a
   *modern C++* interface over a virtualized notion of the Windows registry. So
   C++ sensibilities apply here: exceptions for the error axis, RAII, typed
   results, and `disposition` as the typed successor to that lower layer's
   curated status codes. This is where the curated-but-untyped contractual
   meaning from below is lifted into an explicit, typed, opt-in contract.

3. **The naive Win32 clients on top.** The newest work asks whether all of this
   can be made available *back* to ordinary Win32 clients — callers who cannot
   change their programming model at all, who expect `LSTATUS` and a C ABI and
   nothing more. They cannot catch a C++ exception and would not know what to do
   with a `disposition` type. For them the `mReg*` shims must collapse the rich
   middle-world contract back down into the flat status world they came from.

`disposition` and the `error_code` overloads are what let a single body of logic
serve all three: it preserves the lower layer's hard-won contractual meaning,
expresses it in modern-C++ terms in the middle, and can still be flattened back
to a bare status for clients who live in the world the whole effort started in.
The irony noted above is, in the end, the price of bridging three worlds that
disagree about how to say "here is what happened."

## Summary of the contract

1. `disposition` carries **contractual, non-error** outcomes only.
2. Errors travel on `std::error_code` (non-throwing) or exceptions (throwing).
3. **Opt-in gate:** input `flags{0}` ⇒ nominal disposition; a provider must not
   return an outcome the caller did not enable.
4. The pattern exists to let clients consume a **contractual vocabulary**
   instead of inspecting raw platform status/error values. This is both because
   raw values are brittle to match against, and because a raw error code is
   **provenance-blind** — it cannot establish that the failure pertains to the
   resource the caller named, whereas a disposition is a guarantee about that
   specific resource.
5. `code` = mutually-exclusive outcome (0 = nominal); `flags` = orthogonal bits;
   both gated.
6. The input-flag → output-disposition mapping (bit-for-bit vs. master gate) is
   the operation designer's choice.
7. `disposition` and `error_code` are independent channels carried by the same
   provider primitive.
