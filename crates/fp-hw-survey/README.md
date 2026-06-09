# fp-hw-survey

A small, dependency-free Rust program that captures **native floating-point
hardware behavior** across many machines and architectures, then merges those
captures to find the rows where the hardware actually *disagrees*.

It exists to answer empirical questions like:

- Do different AArch64 vendors (Qualcomm Snapdragon, Apple M-series, Ampere)
  produce different bit patterns for the estimate instructions `frecpe` /
  `frsqrte` / `frecpx` / `fmulx`? (Spoiler: the architecture says they
  *shouldn't* — see [What we expect to find](#what-we-expect-to-find) — and the
  survey's job is to confirm that empirically.)
- Where exactly do x86-64 and AArch64 differ — `fmax`/`fmin` semantics,
  float→int out-of-range results, NaN propagation, flush-to-zero edges?

Each machine runs a **deterministic** operand × rounding-mode corpus through the
real scalar instructions (via per-architecture inline asm — *not* Rust libcore
math, which would hide the divergence), and records the native
`(result_bits, exception_flags)`. Because the corpus is identical on every
machine, captures align row-for-row and the merge step can emit just the
disagreements.

## What we expect to find

A late but important correction to the tool's original premise: on **AArch64**,
the scalar estimate instructions are **not** loosely "implementation-defined."
The Arm Architecture Reference Manual defines `frecpe`/`frsqrte` via exact
shared pseudocode (`FPRecipEstimate` → `RecipEstimate`), a deterministic
fixed-point integer computation; `frecpx` and `fmulx` are likewise fully
specified. The result bits are reproducible from the spec alone — e.g.
`vrecpe(1,2,3,4)` is architecturally required to yield
`0.99805, 0.49902, 0.33301, 0.24951` on every conforming ARMv8 part. So:

- **Intra-ARMv8 agreement is the predicted result.** Apple vs Snapdragon vs
  Ampere *should* produce bit-identical estimates. A merge that reports **zero**
  divergences across AArch64 machines is therefore the *informative,* expected
  outcome — a positive confirmation of conformance, not a wasted run.
- **One legitimate intra-ARMv8 estimate divergence exists — and it is a
  *feature* difference, not a conformance bug.** `FEAT_RPRES` selects a 12-bit
  reciprocal / reciprocal-sqrt estimate table (when `FPCR.AH==1`, single
  precision) where a non-RPRES part returns 8 bits. So a `frecpe.s` /
  `frsqrte.s` disagreement between two machines is expected **only** when their
  `rpres`/`AH` context differs; both answers are deterministic, just from
  different mandated tables. The capture records `rpres`/`afp` in the header
  `features` precisely so the merge can attribute such a row to the feature gap
  rather than flag it as an erratum.
- **The genuinely divergent axes lie elsewhere:** ARMv7 NEON `VRECPE`/`VRSQRTE`
  (pre-v8, genuinely looser — "read from a ROM with limited bins"), and
  **cross-architecture** x86-64 ↔ AArch64 semantics (`fmax`/`fmin` NaN handling,
  float→int saturation, flush-to-zero edges).
- **Spec ≠ proof of correct silicon.** The survey still earns its keep by
  catching a part that *deviates* from the pseudocode (an erratum, a botched
  subnormal/`FEAT_FP16`/sign-of-zero edge). "Should agree per spec" becomes
  "does agree, measured."

### Consequence for the downstream reference set

This sharply shrinks the golden/reference data a consumer like `rook::fp` must
carry. If AArch64 estimates are architecturally fixed, the consumer needs **one
architecturally-derived table per architecture**, not a per-vendor / per-SKU
golden captured from every machine. The survey does **not** ship a multi-SKU ×
full-corpus blob (which is where the tens-to-hundreds-of-MB figures came from);
it ships only the **divergence set** — the handful of keys that are genuinely
not unanimous (cross-arch corners, any conformance outlier). In the expected
case that intra-ARMv8 is unanimous, the AArch64 contribution to that reference
set is **empty**, and the per-machine captures are intermediate verification
artifacts, not data the consumer retains.

## Build

Stable Rust, `std` only, **no external crates**. Builds out of the box on:

- macOS arm64 (Apple Silicon)
- Windows on ARM (Snapdragon, e.g. Surface / Volterra / Lenovo X13s)
- Linux/Windows x86-64

```sh
cargo build --release
```

The binary is `target/release/fp-hw-survey` (`.exe` on Windows).

## Supported architectures

| Arch | Backend | Notes |
|------|---------|-------|
| `aarch64` | full scalar oracle | All 77 catalogued ops; half-precision (`.h`) ops require `FEAT_FP16`. |
| `x86_64`  | SSE/SSE2 (+FMA3) | The SSE-mappable subset only: arithmetic, `fmax`/`fmin`, `fsqrt`, `fma`, f32↔f64, truncating signed float→int, signed int→float. Ops with no scalar SSE form (`fmaxnm`, `fmulx`, `fabd`, the estimate family, directed-rounding/unsigned conversions, all half ops) are skipped. |
| other | none | Produces only a header line. |

> **x86-64 validation caveat:** the x86-64 inline asm was authored on an arm64
> host and could not be executed there during development. Every `capture` run
> first executes a **known-answer self-test** for the local architecture and
> **aborts** if any check fails, so a broken oracle never emits untrustworthy
> data. Still, the first time you run this on real x64 hardware, eyeball the
> `selftest` output.

## Usage

### Generating a capture (run on each machine)

A capture is a single self-describing NDJSON file. Its **header line records the
hardware identity and the capture date/time automatically** — you do not have to
supply them — so a capture file is always traceable back to the machine and the
moment it was produced.

**Step 1 — build the release binary on the target machine.**

```sh
# from the workspace root (c:\github\m or your clone)
cargo build --release -p fp-hw-survey
```

Use `--release`: a capture runs a large operand × mode sweep, and the debug
build is much slower. Optimization level does **not** change the captured
results (each op runs inside its own inline-asm block), only the wall-clock
time. The binary lands at:

- Linux/macOS: `target/release/fp-hw-survey`
- Windows: `target\release\fp-hw-survey.exe`

**Step 2 — sanity-check the host (optional but recommended).**

```sh
fp-hw-survey info        # arch, OS, CPU brand, detected features, supported-op count
fp-hw-survey selftest    # known-answer checks for this machine's oracle
```

`info` is also the quickest way to confirm the tool detected the CPU and
features (e.g. `fp16` on AArch64) correctly before you commit to a full run.

**Step 3 — run the capture.**

```sh
fp-hw-survey capture --label "snapdragon-x-elite-win"
```

`capture` first runs the self-test and **aborts without writing if any
known-answer check fails**, so a broken oracle never produces untrustworthy
data. On success it writes `capture-<label>.ndjson` in the current directory and
prints the op/row counts and output path to stderr.

**Step 4 — keep / send the file.** The resulting `capture-<label>.ndjson` is the
artifact. It is self-contained: machine identity and timestamp are in the header
(see [Record format](#record-format)).

#### Capture options

| Flag | Default | Meaning |
|------|---------|---------|
| `--label` | *(required)* | Human name for this machine; goes in the header and the default filename. Pick something that identifies the CPU **and** OS, e.g. `m2-macbook-air`, `ampere-altra-linux`, `snapdragon-x-elite-win`. |
| `--out` | `capture-<label>.ndjson` | Output file path. |
| `--pairs` | `2000` | Random operand draws per op (on top of the curated edge cases). Higher = denser coverage. |
| `--budget-mb` | `150` | Hard output-size cap in MB; capture stops once exceeded. |
| `--ops` | *(all)* | Comma-separated op labels to restrict the capture to, e.g. `frecpe.s,frsqrte.s`. |

The defaults target the ~100–200 MB per-machine budget the survey was designed
around. Increase `--pairs` for denser random coverage (bounded by
`--budget-mb`); narrow with `--ops` when you only care about specific
instructions.

Examples:

```sh
# Full default capture, explicit output path
fp-hw-survey capture --label ampere-altra-linux --out /tmp/altra.ndjson

# Dense capture of just the estimate family, larger budget
fp-hw-survey capture --label m2-mac --ops frecpe.s,frsqrte.s,frecpx.s,fmulx.s \
                     --pairs 50000 --budget-mb 50
```

### Merge (offline, on one machine)

```sh
fp-hw-survey merge --out divergences.ndjson capture-*.ndjson
```

Aligns every capture on the logical key `(op, a, b, c, mode, flush)` and writes
only the keys where the `(result, flags)` pair is **not unanimous** across the
machines that produced it. Each divergence row lists every machine's label,
arch, result bits, and decoded flags. A summary (machine count, aligned-key
count, divergences, per-op breakdown) prints to stderr.

> Merge currently holds all aligned rows in memory; run it on a box with enough
> RAM for the combined capture set.

### Other subcommands

```sh
fp-hw-survey selftest   # run known-answer checks for this host
fp-hw-survey info       # arch, OS, CPU brand, features, supported-op count
```

## Collecting captures across the fleet

Captures are gathered **manually** — each machine owner runs the tool and
submits one NDJSON file. There is intentionally no CI capture job: the
interesting SKUs (specific Snapdragon / Apple / Ampere / Graviton parts) are
physical hardware a hosted runner does not represent, and a capture is a
one-shot artifact, not something that needs to run on every push.

Coordination and provenance live in **GitHub issues on `azure/m`**, while the
*data* lives in a small committed artifact (issues are not a data store):

1. **Tracking issue** — one epic, *"FP hardware survey — fleet capture
   campaign"*, carries a checklist of target SKUs (Apple M-series, Snapdragon
   X-series, Ampere Altra, AWS Graviton, x86-64 Intel/AMD; Microsoft Cobalt 100
   is explicitly **deferred**). Each SKU is a per-platform capture issue.
2. **Submitting a capture** — on the target machine, build `--release`, then:

   ```sh
   fp-hw-survey info        # confirm CPU brand + features (fp16/rpres/afp) detected
   fp-hw-survey selftest    # must pass; capture refuses to write otherwise
   fp-hw-survey capture --label <cpu-and-os>
   ```

   Paste the `info` and `selftest` output **and the one-line NDJSON header**
   into the platform issue (the header is small, human-readable, and records the
   CPU / OS / features / date — durable provenance even after the file itself is
   gone), then attach or link the `capture-<label>.ndjson`.
3. **Ingest (offline, on one machine)** — collect the submitted captures and
   merge:

   ```sh
   fp-hw-survey merge --out divergences.ndjson capture-*.ndjson
   ```

   Commit **only** two things, never the raw multi-hundred-MB captures:

   - **`divergences.ndjson`** — the merge output. For the intra-ARMv8 estimate
     ops this file is *expected to be empty*; an empty file is the positive
     conformance result, not a missing one. A non-empty AArch64 row is a real
     finding **unless** the contributing machines differ in `rpres`/`afp`/`AH`
     context (see [What we expect to find](#what-we-expect-to-find)).
   - a **provenance table** — one row per contributing capture, copied from each
     header: `label`, `cpu`, `os`, `features`, `captured_utc`, row count,
     `tool_version`. This is what lets a future reader know exactly which silicon
     backs the divergence set.

   Raw captures are retained out-of-band (issue attachments / artifact storage),
   not in git.
4. **Close-out** — paste the `merge` stderr summary (machine count, aligned-key
   count, divergence count, per-op breakdown) into the tracking issue and check
   off the contributing SKUs.

## Record format

NDJSON, one object per line. The first line is a header that **identifies the
hardware and stamps the capture time automatically**:

```json
{"kind":"header","schema":1,"arch":"aarch64","arch_tag":"aarch64","os":"macos","cpu":"Apple M2","label":"m2-macbook-air","features":["fp16"],"captured_unix":1780995871,"captured_utc":"2026-06-09T09:04:31Z","tool_version":"0.1.0"}
```

Header fields:

| Field | Meaning |
|-------|---------|
| `arch` / `arch_tag` | Target architecture (`aarch64`, `x86_64`, …). |
| `os` | Operating system (`macos`, `windows`, `linux`, …). |
| `cpu` | Best-effort CPU brand string, detected natively per platform: CPUID on x86; the registry `ProcessorNameString` on Windows (incl. Windows-on-ARM); `sysctl machdep.cpu.brand_string` on macOS/iOS; `/proc/cpuinfo` on Linux. `"unknown"` only if none apply — the `--label` always disambiguates. |
| `features` | Detected FP-relevant features (e.g. `fp16`, `rpres`, `afp` on AArch64; `avx`, `fma` on x86-64). `rpres`/`afp` are the IMPLEMENTATION_DEFINED knobs that can change an estimate *value*, so a `frecpe.s`/`frsqrte.s` divergence is expected only when they differ between machines. Best-effort on Windows-on-ARM (no `PF_*` flag exposes them). |
| `label` | The `--label` you supplied. |
| `captured_unix` | Capture time, seconds since the Unix epoch (UTC). |
| `captured_utc` | Capture time as ISO-8601 UTC, e.g. `2026-06-09T09:04:31Z`. |
| `tool_version` | `fp-hw-survey` version that produced the file. |

Each data row stores operands and results as raw bit patterns (`u64`; the low
bits hold f32/f16/i32 values), the logical rounding mode, the flush flag, and
the **normalized** exception flags:

```json
{"op":"frecpe.s","a":0,"b":0,"c":0,"mode":"RN","flush":false,"res":2139095040,"flags":2}
```

Exception flags use the AArch64 `FPSR` cumulative layout
(`IOC=1, DZC=2, OFC=4, UFC=8, IXC=16, IDC=128`); x86-64 `MXCSR` status bits are
translated into this layout so flags compare directly across architectures.

## Contributing captures

1. `cargo build --release -p fp-hw-survey` on the target machine.
2. `fp-hw-survey capture --label "<distinctive-name>"` (use a name that
   identifies the CPU and OS, e.g. `snapdragon-x-elite-win`,
   `ampere-altra-linux`).
3. Send back the `capture-<label>.ndjson` file. It already carries the machine
   identity and capture timestamp in its header — nothing else to record.

Captures from the same CPU model on different OSes are still useful — OS-level
defaults (e.g. denormal handling) can differ.

## License

MIT. See the repository root [LICENSE](../../LICENSE). Copyright (c) Microsoft Corporation.
