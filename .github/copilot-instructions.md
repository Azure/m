# Repository-wide instructions for copilot

<!-- NOTE: The "Terminal / Git rules" section previously here was a subset of
     "## Terminal / Git rules — hang prevention" further down and has been removed
     to avoid duplication. See that section for the authoritative rules. -->

## ?? CRITICAL: PROJECT BUILD SYSTEM ??

**THIS IS A CMAKE C++ PROJECT - NOT A .NET PROJECT**

### ? NEVER DO THIS:
- DO NOT run `dotnet clean`, `dotnet build`, `dotnet test`, or ANY `dotnet` commands
- DO NOT look for or reference `.sln` files or `.csproj` files  
- DO NOT assume this is a Visual Studio .NET/C# project
- DO NOT use MSBuild commands

### ? ALWAYS USE CMAKE:
This project ONLY uses **CMake** for building and **CTest** for testing.

See the "CMake Build Commands Reference" section below for correct commands.

---

Ensure that the code always builds clean for debug and retail builds.

Unit tests must pass to consider a unit of work complete once the code has built.

---

## Definition of "Complete" for refactorings and code changes

**MANDATORY**: Work is NOT complete until ALL of the following criteria are met:
1. **Debug build**: Code builds successfully with `--preset x64-debug` (or equivalent)
2. **Retail/Release build**: Code builds successfully with `--preset x64-release` (or equivalent)
3. **Debug tests**: All unit tests pass in debug configuration
4. **Retail tests**: All unit tests pass in release/retail configuration

**CRITICAL REQUIREMENT**: Do NOT declare work "complete" or "successful" until all four criteria above are verified.

**For ALL refactorings**: These verification steps MUST be completed as part of the refactoring work. 
Refactoring is not complete until all builds pass and all tests pass in both debug and release configurations.

If time constraints prevent full verification, explicitly state which configurations remain untested and why the work should NOT be considered complete.

---

## CMake Build Commands Reference

### When User Says "Rebuild Clean" or "Clean Build and Test"

**ALWAYS use these CMake commands:**

1. **Clean the build:**
   ```bash
   cmake --build Q:\github\m\out\build\x64-debug --target clean
   ```

2. **Rebuild:**
   ```bash
   cmake --build Q:\github\m\out\build\x64-debug
   ```

3. **Run tests:**
   ```bash
   ctest --test-dir Q:\github\m\out\build\x64-debug
   ```

### Standard CMake Workflow

**Configure** the build using one of the presets:
```bash
cmake --preset x64-debug
# or
cmake --preset x64-release
```

**Build** using the configured output directory:
```bash
cmake --build Q:\github\m\out\build\x64-debug
# or
cmake --build Q:\github\m\out\build\x64-release
```

**Test** using CTest:
```bash
ctest --test-dir Q:\github\m\out\build\x64-debug
```

---

## User preferences

### Concise responses (no excessive praise or commentary)

Users with GitHub identifiers in this list prefer concise, direct responses:

- EmJayGee

**To add yourself**: Add your GitHub username to the list above with a single line: `- YourGitHubUsername`

# C++ language guidance

See `.github/instructions/cxx.instructions.md` for detailed C++ coding standards including:
- Language standard usage (C++20/C++23)
- Casting rules
- Memory safety requirements
- Commenting guidelines

# Component layering guidance
See `.github/instructions/layering.instructions.md` for guidance on component layering, including:
- Allowed dependencies between layers
- Best practices for maintaining clean architecture
- Examples of proper layering and dependency management

# Building and other context

## vcpkg

The vcpkg github repository is a submodule of `m` immediately at the top level of the repo.

It has to be "bootstrapped" by running either "vcpkg/bootstrap.cmd" on Windows or "vcpkg/bootstrap.sh" on Linux.

# Copilot Instructions

Use CRLF line endings.

## Line endings in tool parameters

All text content passed to tpu tools (`content`, `replacement`, `data` in edit ops) is
automatically normalized to LF before processing. You do not need to worry about whether
the text you send uses LF or CRLF — tpu handles the conversion. The file's existing
line-ending convention (LF or CRLF) is preserved on disk automatically.

## Terminal / Git rules — hang prevention

**These rules prevent terminal hangs that freeze the session.**

- Every `git` command that can produce paged output **must** be run with
  `git --no-pager <subcommand>`. The list below is illustrative, not exhaustive:
  `diff`, `show`, `log`, `blame`, `reflog`, `stash list`, `branch -v`,
  `shortlog`, `tag -n`, `whatchanged`, `grep`. **If unsure whether a `git`
  subcommand may page, use `--no-pager`.**
- Never run `git commit` without `-m "…"`. Commit messages must be a **single
  line** when supplied via `-m`. For longer messages write the message to a
  file under `.scratch/` and use `git commit -F .scratch/<file>`. Never use
  PowerShell here-strings (`@"…"@`) or embedded newlines inside `-m` —
  PowerShell will either hang waiting for terminator or pass `\n` literally.
- Never run `git pull` or `git merge` without `--no-edit`.
- Never run interactive commands: `git rebase -i`, `git add -p`, etc.
- Do not use `less`, `more`, or any other interactive pager.
- Never use PowerShell multi-line string operators (`@"…"@`) in terminal commands.

## Scratch directory for temporary files

When you need to capture command output, test results, debug logs, build warnings, or any
other temporary/diagnostic data to a file, **always write it under the `.scratch/` directory**
at the repository root. This directory is git-ignored.

- Create `.scratch/` if it does not exist.
- Use descriptive filenames (e.g., `.scratch/test_parser_output.txt`, `.scratch/build_warnings.txt`).
- **Never** write scratch or debug files to the repository root or any source directory.

## General instructions for this repository
- All code is Copyright Microsoft Corporation.
- All source code should include a copyright statement. The statement should be brief, a single line comment as the first line of the file which reads something like: Copyright (c) Microsoft Corporation.
- If the source file is also part of an open source library, there may be additional lines giving the details, but in general, open source content should not be checked in to this source repository except as part of a patching process to provide a patch over defective open source dependencies which have to be addressed for security or business continuity reasons.

## Interaction Guidelines
- Prefer concise responses: minimize verbosity, reduce repetition, and avoid excessive formatting/emojis. Get straight to the point in all interactions.

## Checklist execution discipline

When executing checklist items (CHECKLIST.md files):

- **Decide which mode you are in *before* you commit (read first).** Every time you are about to commit checklist work, first determine which situation you are in:
  - **(a) Recording work that is already finished** in the working tree (the items were implemented before this rule was applied, the work arrived as one chunk, or a single coherent change happened to satisfy several items at once). **Action:** commit the completed items together in **one** commit that cites every item ID it satisfies, check them all off in the same commit, and move on. Do **not** invent extra work to retroactively tease the change apart into one-commit-per-item — that artificial "commit surgery" is exactly the end-of-stream bookkeeping this rule is meant to avoid.
  - **(b) Implementing items forward, one after another.** **Action:** follow the one-item-then-commit loop below.

  This rule exists to enforce *implementation sequencing*, **not** to dictate how finished history is sliced. Its single purpose is to stop work item N+1's concerns from leaking into work item N's implementation *while you are still writing item N* — so each item is implemented against a clean, already-landed predecessor. Separate commits are a *byproduct* of separate implementation episodes (you implemented item N, committed, then started item N+1); they are never a goal pursued on their own.
  - If finished work in the tree spans multiple items and those items were genuinely independent, that independence was already preserved by how the code was written — re-slicing the commit adds nothing, so don't spend effort classifying: just commit together (mode a). If the items were *not* independent (item N+1's concerns flowed into item N), that is a sequencing/planning error: merge the items if the coupling is minor, or re-plan if the sequencing was seriously wrong (see the re-plan bullet below). The response is to fix the plan, never cosmetic commit surgery.
- **One item at a time (mode b — implementing forward).** When you are implementing items one after another, implement exactly one checklist item, then **stop and commit**, then move on. "Stop" means: do not begin reading, planning, or editing for the next item until the current one's commit has succeeded. This is the mechanism that delivers the sequencing guarantee above; it does **not** apply retroactively to work that is already done (mode a).
- **A checklist item may legitimately be large.** "One item, one commit" is a sequencing rule, **not** a commit-size rule. There is no upper bound on the diff size, file count, or scope of a single item's commit. If an item's work is genuinely coupled — for example, an IR-schema change that requires updates across lowering, codegen, freezer, pretty-printer, and tests to compile at all — do the whole thing in one commit. Do **not** invent sub-items (`M1.1.1`, `M1.1.2`, …) to make the commit feel smaller; that is artificial work that violates the "one item, one commit" rule by turning one item into several.
- **If items are mis-structured, say so; do not paper over it.** If you discover during execution that two checklist items cannot be implemented independently (one cannot compile or pass tests without the other), that is a checklist-structuring defect. Be honest: name the defect, then either (a) commit both items together in one commit referencing both IDs (acknowledged defect), or (b) restructure the checklist first (in its own commit) so the items become independent. Do **not** silently split, tease apart, or interleave commits to disguise the coupling.
- **No batching for convenience (mode b — implementing forward).** While you are still implementing, do not *start* work on item N+1 before item N is committed *just because* the work is similar, the context is loaded, or it feels efficient to do both at once. Convenience, similarity, or shared context across adjacent items is **not** sufficient justification to pull future work forward into the current item. (This forbids *reaching ahead* during forward implementation; it does **not** require *re-slicing* work that is already finished — that is mode a above.)
- **Re-plan when execution reveals planning was wrong.** A checklist is a hopeful projection, not a contract. When execution surfaces information that invalidates the plan — items that turn out to be coupled, an item that decomposes into work the original plan didn't anticipate, an item that turns out to already be done, an item whose scope expands or contracts based on what you now know — **stop and update the checklist before continuing.** Restructuring a checklist mid-execution is normal and expected; pretending the original plan was correct and silently working around it is not. The restructure itself is a commit (with a message explaining what new information forced the change), and then the revised plan governs.
- **If items must be done together, say so and do it; don't tease apart.** Once you have decided (and recorded in the checklist if the structure is wrong) that two items must land together, commit them together in one commit citing both IDs. Do **not** try to "unthread" a coupled implementation into per-item commits after the fact — that is fiction, not history.
- **Commit immediately after each item.** In mode b (implementing forward), the commit must happen before moving to the next item. In mode a (recording already-finished work), a single commit citing all the item IDs satisfies this.
- **Commit message format:** `Completed item: <item-id>: <full item text>` (e.g. `Completed item: SF-1: Add extensible FunctionCall variant to FilterExpr`). When one commit records several already-finished items together (mode a), use one `Completed item:` line per item ID, e.g.:
  ```
  Completed items: SF-1, SF-2, SF-3

  Completed item: SF-1: Add extensible FunctionCall variant to FilterExpr
  Completed item: SF-2: Wire FunctionCall through the evaluator
  Completed item: SF-3: Add FunctionCall parser support
  ```
- **Check the item off** in CHECKLIST.md (change `- [ ]` to `- [x]`) and include that change in the same commit.
- After the commit, pull / rebase from origin then push back to origin
- **Tests must pass** before committing. Run the appropriate test command (per the language-specific instructions) after each item and fix failures before committing. Pre-existing failures unrelated to the current item do not block the commit, but must be recorded in `UNRESOLVED-TEST-FAILURES.md` (see language-specific instructions for the convention) before committing.
- **When the last item in a CHECKLIST file is completed**, update its PLANS.md entry to "completed" in the same commit.
- **Cross-component handoff callouts.** When the next required action in a checklist sequence shifts to a different source-component (see "Source-Components" above) — i.e. the next dependency-ordered item cannot be worked in the current component because it lives in another component — the item whose completion triggers the shift must end with an explicit handoff callout naming the destination component, milestone, and work item ID. Use the reciprocal form on the destination side: the destination's first dependent item must carry a `CROSS-COMPONENT PREREQUISITE` callout naming the source component / item that must land first, and (if control returns) a `CROSS-COMPONENT HANDOFF` callout at the end pointing back. Recommended format (markdown blockquote so it stands out when scanning):
  > **➡ CROSS-COMPONENT HANDOFF:** next work is in component `<component-path>` → `<milestone-id>` → `<work-item-id>` (`<short title>`). See [`<path-to-CHECKLIST.md>`](...).
  The goal is that a reader executing a checklist linearly never has to infer cross-component dependencies from surrounding prose — the boundary is always called out at the exact item where the handoff occurs.

## Coding conventions

### No manifest numeric constants in source code

Never write bare integer or byte literals as discriminants or protocol tags inline in logic code.
Instead, use a named scoped enum **or** a set of typed named constants, and use those named
identifiers everywhere — in switch/match arms, buffer pushes, assertions, and doc tables.
Both approaches are acceptable; consistency within a single file or module is what matters.

#### C++

**Bad:**
```cpp
buf.push_back(4);            // what is 4?
buf.push_back(255);          // magic
assert(key == std::byte{0});
```

**Good (enum approach):**
```cpp
enum class value_key_tag : std::uint8_t { db_null = 0, text = 4, err = 255 };

buf.push_back(std::to_underlying(value_key_tag::text));
buf.push_back(std::to_underlying(value_key_tag::err));
assert(key == std::to_underlying(value_key_tag::db_null));
```

**Good (constant approach):**
```cpp
namespace tags {
    inline constexpr std::uint8_t db_null = 0;
    inline constexpr std::uint8_t text    = 4;
    inline constexpr std::uint8_t err     = 255;
}

buf.push_back(tags::text);
buf.push_back(tags::err);
assert(key == tags::db_null);
```

#### Rust

**Bad:**
```rust
v.push(4u8);   // what is 4?
vec![255u8]    // magic
assert_eq!(key, vec![0u8]);
```

**Good (enum approach):**
```rust
#[repr(u8)]
enum ValueKeyTag { DbNull = 0, Text = 4, Err = 255, ... }

v.push(ValueKeyTag::Text as u8);
vec![ValueKeyTag::Err as u8]
assert_eq!(key, vec![ValueKeyTag::DbNull as u8]);
```

**Good (const approach):**
```rust
mod tags {
    pub const DBNULL: u8 = 0;
    pub const TEXT:   u8 = 4;
    pub const ERR:    u8 = 255;
}

v.push(tags::TEXT);
vec![tags::ERR]
assert_eq!(key, vec![tags::DBNULL]);
```

This rule applies to all binary encoding schemes, wire protocols, file format tags, sort-key type
bytes, and any other place where a numeric value carries identity meaning. The enum or constant
module is defined in the same file or module as the logic that uses it, and its doc comment must
note that changing any value is a breaking change.

## Design Autonomy — Behavior is owned, never inherited from dependencies

We **define** our behavior. We **choose** dependencies that can satisfy our definition.

It is never acceptable to describe our behavior as "whatever crate X does" or "we delegate to
library Y." That framing surrenders our autonomy to decide what is correct for our users and makes
it impossible to reason about correctness, versioning risk, or future migration.

The correct framing is always:
1. State **what our specified behavior is** (inputs we accept, outputs we produce, errors we raise).
2. Note **which dependency is used to achieve it** and that the dependency was chosen because its
   behavior matches our specification.
3. If a dependency's actual behavior diverges from our specification, the dependency is wrong,
   not our specification. We either constrain the dependency, wrap it, or replace it.

We may align our specification with a dependency's behavior when that behavior is sensible for our
users — but the specification must still be written down explicitly and owned by us. When a
dependency is upgraded or replaced, our specification does not change; only the implementation does.

This applies everywhere: file formats, parse rules, error messages, wire protocols, encoding choices.

## Mono-repo bug policy — fix the layer, don't work around it

All crates in this repository are under active development. When work in one component
reveals a bug or deficiency in an underlying layer (another crate in the repo), **fix it
at the source** rather than working around it in the consuming crate. The whole point of
the mono-repo is that we own every layer and can change them together.

If the fix demands significant refactoring that would derail the current task, raise the
issue back to the engineer driving forward progress so we can decide together whether to
fix it now or defer it. But the default is always: fix the bug where it lives.

## Source-Components

- Source-Components are directory hierarchies in the repository rooted at some directory.
- Source-Components are identified by the presence of either a Cargo.toml file or a COMPONENT.md file in the directory.
- The root of the repository contains a Cargo.toml file, so the entire repository is a source-component, but there are also smaller source-components within the repository which may have their own Cargo.toml or COMPONENT.md files.

Examples:
- `src/tools/csv/` (has COMPONENT.md)
- `src/tools/csv/csv/` (has Cargo.toml)

## Always plan
- Always form a plan in the form of a CHECKLIST.md, at the lowest common source-component for the change
- Keep the plan up to date as you execute on the plan
- Keep a file at the root of each component, called PLANS.md, which tracks all the CHECKLIST.md files in the repository and their status (not started, in progress, completed). If it does not exist, create it. If it does exist, update it with the new CHECKLIST.md file and its status.
- When a CHECKLIST.md file is completed, move it to a table in a different file called COMPLETED-PLANS.md in the same directory, with a brief summary of the work completed, and remove it from PLANS.md.



PLANS.md format (markdown table):
| Path to CHECKLIST.md | Status | Brief description | Design Notes |
|---|---|---|---|

COMPLETED-PLANS.md format (markdown table):
| Path to CHECKLIST.md | Completion Date | Brief description | Design Notes |
|---|---|---|---|

Status values: "not started", "in progress", "completed"

Design Notes column: Path(s) to DESIGN-NOTES.md file(s) that document the work, or "N/A" if none exist

## Plan sizing

If a plan exceeds roughly 10 work items or 3 levels of grouping/nesting, checkpoint it
into a CHECKLIST.md file in the repository before continuing. The goal is that the plan
survives a lost session — if the plan only exists in the chat, it will be lost.

## Design notes are not a work queue

Design notes (DESIGN-NOTES.md, DESIGN-RATIONALE.md, and related files) record *decisions*
— what was chosen and why. They steer future work, but they do **not** schedule it. The
only mechanism that queues work on existing code is a CHECKLIST.md item. A decision that is
recorded only in a design note, with the work it implies never transcribed into a checklist
item, is effectively orphaned: nothing will ever cause that work to be picked up.

This matters because the repository is worked by multiple people and multiple automated
agent sessions, often in parallel and on different machines. None of them share local or
session-private memory. The only directive any contributor — human or agent — can rely on
seeing is what is committed to the repository. Therefore work must be queued in committed
CHECKLIST.md files, never parked in an agent's memory, a chat thread, or a design note that
no one is obligated to act on.

When recording a decision that implies a change to existing code:

- In the same change that records the decision, ensure the implied work exists as a
  CHECKLIST.md item (creating or updating the checklist as needed), and reference the
  decision from that item so the two can be traced to each other.
- If a decision deliberately schedules **no** work — a reservation, a deferral, or a
  "leave as-is for now" choice — state that explicitly in the decision so the absence of a
  checklist item is visibly intentional rather than an oversight.

A component may layer additional, stricter conventions on top of this rule (for example, a
required cross-reference syntax between decision IDs and checklist items). Follow the
nearest applicable component instructions in addition to this baseline.

## CHECKLIST file hygiene

CHECKLIST files are **action-only**: they contain pending, in-progress, and recently
completed (`[x]`) items awaiting migration to `COMPLETED-CHECKLIST.md`. Completed items
must be moved to `COMPLETED-CHECKLIST.md` when a group is fully done (see below).
Never leave historical records, prose summaries, rationale, or context in a CHECKLIST file.

Checklists for work more than 2-3 items long should be organized into milestones.
Milestones should generally be sized to about 5 work items (suggestion, not a rule) and
should end with integration tests when possible.

At the end of every milestone, the following steps are **implicit** and must NOT be written
as checklist items:

1. **Clean compile (no warnings), both debug and release.**
   "Clean" means: discard prior build artifacts first, then build, so that all warnings
   are re-emitted (not suppressed by incremental caching). Fix **all** warnings that
   appear, even those unrelated to the milestone's changes. Build only the in-scope
   source-component, not the entire repository. The exact commands depend on the language
   toolchain — see the language-specific instructions for the mapping.
2. **Test only the in-scope source-component**, not the whole repository.
3. **Sync with origin**: `git fetch`, then merge or rebase the current branch on top
   of the updated upstream tip (`--no-edit`), resolving any conflicts, then push.
   Pushing is permitted at milestone boundaries without further confirmation; outside
   milestone boundaries, follow the standard "ask before pushing" rule.

These are standard procedure, not work items. Checklists contain only substantive work.

Work items in a milestone must be self-contained and all work items must be in dependency order.

### Sub-step notation

When a checklist step is broken into sub-steps, always use decimal notation: `RC-1.1`, `RC-1.2`,
`RC-1.3`, etc. (or whatever prefix is in use). Never use lettered sub-items (`RC-1a`, `RC-1b`) or
nested bullet lists to represent sub-steps. This applies both to CHECKLIST files and to any inline
step breakdowns described during planning.

When a group of related items is fully complete:
1. Move the completed group to `COMPLETED-CHECKLIST.md` in the same directory.
2. Prefix the moved block with a heading: `## Moved YYYY-MM-DD — <brief description of what was done>`.
3. `COMPLETED-CHECKLIST.md` is **append-only**; always add new groups at the bottom.
4. Leave only the remaining pending or in-progress items in the source `CHECKLIST.md`.

Named feature files (`CHECKLIST-<feature>.md`) should be **deleted entirely** once all items are
complete. Move their content to `COMPLETED-CHECKLIST.md` in the same directory before deleting.

## Design note files

Any directory in the repository may have a DESIGN-NOTES.md file.

The DESIGN-NOTES.md file should record design decisions about the code in that directory and its children.

If a decision should be recorded, it should be recorded in a DESIGN-NOTES.md file. The DESIGN-NOTES.md
file to use is either the DESIGN-NOTES.md file in the source-component directory which should be created
if it does not already exist, or if there is an already existing DESIGN-NOTES.md file in any ancestor
directory between the file being changed and the source-component root, use that one instead.

### What to include

The design note files should include anything that a future developer should or may want to know about the
code to help them "get up to speed" or diagnose interesting or bad behaviors.

### What not to include

Like with code comments, don't include super obvious things.

Example: A query processor design note must describe its intent and unique approach in a paragraph, not provide a comprehensive tutorial on the underlying technology or theory. It may include links to external resources for further reading, but should not attempt to teach the reader about query processing in general.

### Three-tier design documentation

Source-components with substantial design history should separate current decisions from
historical rationale using three tiers:

- **Tier 1: `DESIGN-NOTES.md`** — Current canonical decisions. Contains decision indexes,
  compact detail sections stating what was decided and why. Every paragraph must answer
  "what is the decision?" or "what constraint forced this choice?" — not "what else did
  we consider?" Content that answers the latter belongs in Tier 2.

- **Tier 2: `DESIGN-RATIONALE.md`** — Historical record of how decisions were reached.
  Alternatives considered, prior art, design session summaries, evolutionary reasoning.
  Cross-referenced by decision ID from Tier 1. This file is consulted for "why" questions,
  not for forward implementation work.

- **Tier 3: `design-sessions/DESIGN-SESSION-<date>-<topic>.md`** — Raw design session
  transcripts, dated by session. Reference material, not routinely loaded. Stored in a
  `design-sessions/` subdirectory under the source-component root.

When recording a new decision, write to both Tier 1 and Tier 2 in the same commit.
**Never treat Tier 2 or Tier 3 as authoritative for current decisions.** If there is a
conflict, Tier 1 wins.

A source-component may have a `DESIGN-INSTRUCTIONS.md` file specifying additional design
rules — including how these tiers are used — for that component and everything below it.
When working in a directory, locate and follow the nearest `DESIGN-INSTRUCTIONS.md` in
that directory or any ancestor up to the source-component root. These directives are
binding for all work under that directory.

Not all source-components need all three tiers. Small components may have only DESIGN-NOTES.md.

### Design session files

When a design conversation produces extended discussion, exploration, or working-through of a
topic — beyond what fits in a Tier 2 rationale section — capture it as a design session file.

**When to create a session file:**
- The conversation explores a topic in depth over multiple exchanges
- The discussion covers alternatives, trade-offs, or implications that would be valuable
  context for a future reader trying to understand the design landscape
- The topic warrants a standalone record beyond the decision summary in DESIGN-RATIONALE.md

**Naming:** `DESIGN-SESSION-<YYYY-MM-DD>-<topic-slug>.md` (e.g.,
`DESIGN-SESSION-2026-04-06-task-floating.md`)

**Location:** `design-sessions/` subdirectory under the source-component root. Create the
directory if it does not exist. This prevents session files from accumulating in the
component's top-level directory.

**Content:** The session file should be a faithful record of the design discussion — the
reasoning, alternatives, and conclusions as they unfolded. It does not need to be polished
prose, but should be readable by a future developer. Include a brief summary at the top
noting which decisions (D-numbers) resulted from the session.

### Historical Record

As features age out of a source-component, at the very least, move notes which are no longer relevant to a
different file, DESIGN-NOTES-AGED-OUT.md.

When moving the section to DESIGN-NOTES-AGED-OUT.md, include the date of the move, in YYYY/MM/DD format.

## Quality

When providing testing, always provide extensive testing to test at least 10 normal cases, as well as all identifiable edge cases, unless the
computation required to test the edge cases would be excessive on a modern system. The unit tests for a submodule should be able to complete
in under one second of elapsed time on an AMD Ryzen R7 processor running at 1.5ghz with 16gb of memory.

If there are tests which seem vital that would take longer, put an item in a CHECKLIST.md file with special importance for the user to
decide on whether to include them or not.

In any case, if the test is vital it must be authored and be run as part of the integration tests rather than the unit tests.

### Milestone vs sub-milestone checklist work

When working on checklist items organized into milestones, build and test only the
source-component in scope, not the entire repository.

To complete the milestone, perform the implicit end-of-milestone steps described under
"CHECKLIST file hygiene" above (clean repo-wide build with zero warnings, in-scope tests,
sync with origin and push).

### Unit tests

Unit tests should always be reproducible and not use random sampling techniques at runtime without the developer's explicit approval and then
it should be recorded in a design note.

### Integration tests

Integration tests should use larger scale data.

There is no required minimum, but in general should start with data volume in the hundreds or thousands.

The data does not have to be necessarily stable. A guideline might be that smaller data sets (<10kb) should be checked in whether in
a separate file or somehow encoded in source files. Larger data sets may be generated at run time, whether exhaustively or
via random techniques.

## Architectural pre-steps

**Never call `stdout`/`stderr`/`print`/`eprintln` (or the language equivalent) from
more than one site in a tool.** At the first occurrence, introduce an output
abstraction — a writer trait, a sink, or a formatter — and route every subsequent
output through it. The abstraction need not be elaborate (a single trait with one
`write_str` method, or a UTF-8 character stream, is enough); the requirement is that
the storage target (file, channel, stdout, stderr) and the formatting concern be
separable from the call sites that produce content.

This applies to any feature whose output may plausibly need to be retargeted later:
CLI output, log output, diagnostic output, generated artifacts.

<!-- tpu-mcp:setup:begin -->
## File I/O — use `tpu_*` MCP tools, never PowerShell or shell

This workspace runs the **tpu-mcp** MCP server which exposes encoding-aware
file primitives as first-class tools. Plain `Get-Content` / `Set-Content` /
`Out-File` / `>` / `cat` / `sed` round-trip files through the active code
page and silently corrupt UTF-8, UTF-16, smart quotes, em-dashes, and
box-drawing characters. Use the MCP tools instead — they detect, preserve,
and round-trip the file's native encoding and line endings safely.

**Rule:** when working in any project that has the tpu-mcp server registered,
ALWAYS prefer the `tpu_*` tools over PowerShell or shell file commands.

| MCP tool | Use it for |
|---|---|
| `tpu_read_file` | reading text files (UTF-8, UTF-16, Windows-1252, Shift-JIS, …) |
| `tpu_read_head` / `tpu_read_tail` | first/last N lines or bytes |
| `tpu_read_file_binary` | inspecting raw bytes of binary files |
| `tpu_read_file_escaped` | reading text as a single 7-bit-clean escaped line |
| `tpu_write_file` | replacing a text file's full contents |
| `tpu_append_file` | appending text to an existing file |
| `tpu_replace_in_file` | regex / fixed-string substitution (use `fixed_strings: true` for literal targets) |
| `tpu_edit_file` | targeted insert/delete/splice at known line numbers |
| `tpu_validate_file` | pre-flight assertion that a file is in the expected state |
| `tpu_count_file` | line / word / char / byte / pattern counts |
| `tpu_find` | encoding-aware grep across files and globs (pass `glob` to filter a directory walk, e.g. `path: "DIR", glob: "**/*.ndjson"`) |
| `tpu_copy_file` | copy a file or recursively copy a tree (resilient: per-entry warnings, never aborts mid-walk by default) |
| `tpu_render_file` | populate a file from a `{{TOKEN}}` template |
| `tpu_stat_file` | verify a write actually persisted (mtime / size) |
| `tpu_doctor` | scan files/dirs/globs for mojibake or encoding damage; optionally repair with `fix: "peel"` |
| `tpu_setup` | (re)write this guidance block into the active `copilot-instructions.md` |

### When to use each

- **Reads** — always use `tpu_read_file`. Never use PowerShell `Get-Content`
  for code review or content inspection.
- **Edits** — prefer `tpu_replace_in_file` with `fixed_strings: true` over
  `tpu_edit_file` when the target text is unique, because line numbers can
  shift between reads. Use `tpu_edit_file` when you have just read the file
  and know exact line offsets.
- **Writes that should be guarded** — pass `validate: [{ "selector":
  "line-contains:N", "value": "..." }]` to refuse the write if the file is
  not in the expected state.
- **Globs / recursion** — `tpu_find` and `tpu_copy_file` accept glob
  patterns and tolerate inaccessible directories by emitting warning
  records (configurable via the `on_error` argument). To search a directory
  tree with `tpu_find`, pass the directory as `path` and the filename
  pattern as `glob` (e.g. `path: "q:/src/foo/.scratch", glob: "**/*.ndjson"`)
  — this is the `find DIR -name PAT` shape and is the only way to recurse
  into an absolute directory.
- **Dependency-free templating** — `tpu_render_file` substitutes
  `{{NAME}}`-style tokens. Use `\{{` to emit literal braces.

### Tool output format

Every tool response uses a **mixed format**: a JSON invocation header,
a content-type-dependent body, and (for most tools) a JSON status trailer.
Not every line is JSON — read tools and `find` return raw content
between the header and trailer.

- **Line 1** — invocation header:
  `{"reason":"x-tpu-mcp-invocation","tool":"tpu_NAME","args":{...}}`
  Large `content`/`replacement`/`template` fields appear as `"<N bytes>"` placeholders.
- **Mutating tools** (write, replace, edit, append) — normal write:
  `{"status":"success","file":"...","mtime_epoch_ms":N,"size":N}`
  Preview modes do not stamp the file and return a reduced trailer:
  `diff:true` adds unified diff lines before the status (full stamp still present for write/replace/edit).
  `dry_run:true` (replace only): optional diff lines, then `{"status":"success","changed":true|false}`.
  `count:true` (replace only): `{"status":"success","count":N}`.
  `append diff:true`: diff lines when changed, then `{"status":"success","file":"...","changed":true|false}`.
- **Structured tools** (count_file, stat_file, copy_file, render_file,
  setup+target, doctor) — result line
  `{"reason":"x-tpu-mcp-result",...}` followed by `{"status":"success"}`.
- **Read tools** (read_file, read_head, read_tail, read_file_escaped) — header then raw content; no JSON trailer on success.
  **Exception** — `tpu_read_file_binary` with a non-empty `hash` arg acts like a structured tool:
  `{"reason":"x-tpu-mcp-result","encoding":"bytes-base64","content":"<base64>","hashes":[...]}` followed by `{"status":"success"}`.
  Without `hash`, `tpu_read_file_binary` returns header + 7-bit-clean escaped bytes (no trailer).
- **Find tool** (find) — header, then matching lines as plain text, then `{"status":"success","warnings":[...]}` trailer.
- **Errors** — `{"status":"error","message":"..."}` as the final line;
  `isError: true` in the MCP wrapper.

### When a file looks corrupted (mojibake)

Symptoms: `Ã©` where `é` should be, `â€"` where `—` should be, `â"€` instead
of `─`, stray `Â ` before numbers, `ð\u009f...` blobs instead of emoji.
This is *mojibake* — text that was decoded in the wrong encoding and then
re-encoded as UTF-8. It is almost always caused by a non-tpu writer round-
tripping the file through the OS code page (PowerShell `Get-Content` /
`Set-Content` / `Out-File` / `>` / `Add-Content`, a misconfigured editor,
a generator that assumed ASCII).

Workflow:

1. **Diagnose**: call `tpu_doctor` with the suspect file (or the
   surrounding directory / glob). It returns a JSON report listing every
   flagged file, its detected encoding, per-pattern match counts, exact
   line/column locations, and whether a one-layer "peel" repair would
   strictly improve the file (`peel_suggested: true`).
2. **Identify the offender**: when a file is corrupted in a git repo, run
   `git log -p -- <file>` (or `git blame -- <file>`) to find the
   introducing commit. The commit reveals which tool wrote the damage so
   you can stop the leak at the source rather than only repairing
   downstream.
3. **Repair (conservative)**: call `tpu_doctor` again with
   `fix: "peel"`. Only files whose peel produces *strictly fewer* mojibake
   matches are rewritten; the prior content is preserved at `<file>.bak`.
   Re-run `tpu_doctor` after the repair to confirm the report is clean.
4. **Don't paper over it**: if a file legitimately contains mojibake
   digraphs (test fixtures, regex sources, documentation about mojibake),
   add the line `encoding-check: allow-mojibake` (typically inside a
   comment) — `tpu_doctor` and the write-time guard will treat it as
   clean.

The write-time guard in `tpu_write_file` / `tpu_append_file` /
`tpu_replace_in_file` / `tpu_edit_file` already refuses to *introduce* new
mojibake (pre-existing damage passes through). If you genuinely intend to
write curated mojibake fixtures, pass `allow_mojibake: true`.

### File encoding

When you must fall back to PowerShell, never round-trip non-ASCII files
through `Get-Content` / `Set-Content` — read and write via
`[System.IO.File]::ReadAllBytes` / `WriteAllBytes` and validate with
`tools/check-encoding.ps1` afterwards.
<!-- tpu-mcp:setup:end -->

<!-- cargo-mcp:setup:begin -->
## Cargo commands — use `cargo_*` MCP tools, never the terminal

This repository builds Rust code in addition to the CMake/C++ build. When
working in the Rust/Cargo portion of the tree, ALWAYS use the `cargo_*` MCP
tools instead of running `cargo` in a PowerShell or bash terminal. This holds
even inside a larger workflow — do not switch to the terminal for cargo just
because a previous step used the terminal.

Always pass `working_dir` set to the absolute path of the workspace root
(`q:\github\m`); the tool default is the cargo-mcp server's own directory and
will usually fail manifest/toolchain resolution.

| MCP tool | Replaces |
|---|---|
| `cargo_metadata` | `cargo metadata` |
| `cargo_check` | `cargo check` |
| `cargo_build` | `cargo build` |
| `cargo_test` | `cargo test` |
| `cargo_clippy` | `cargo clippy` |
| `cargo_fmt_check` | `cargo fmt --check` |
| `cargo_fmt` | `cargo fmt` |
| `cargo_tree` | `cargo tree` |
| `cargo_doc` | `cargo doc` |
| `cargo_clean` | `cargo clean` |
| `cargo_update` | `cargo update` |
| `cargo_fix` | `cargo fix` |
| `cargo_add` | `cargo add` |
| `cargo_remove` | `cargo remove` |
| `cargo_publish` | `cargo publish` |
| `cargo_diagnostic` | *(no terminal equivalent)* |

### Boolean arguments

Boolean flags (`all_targets`, `release`, `workspace`, `lib`, `bins`, `tests`,
`benches`, `examples`, `all_features`, `no_default_features`, `frozen`,
`locked`, `offline`, …) expect a JSON boolean (`true` / `false`). Prefer the
native boolean. If an expected CLI flag (`--release`, `--all-targets`, …) is
missing from the echoed `x-cargo-mcp-invocation` argv, you probably sent the
boolean in an unrecognised shape — check for a `warning` notification.

### cargo_test — timeouts

`cargo_test` has two timeout knobs, both applied only to the test **execution**
phase (the clock arms when the build finishes, so a slow build never trips
them):

- **`timeout_secs`** — hard overall wall-clock cap. Defaults to 30 s in
  unfiltered mode; no default in `test_filter` mode. Pass `0` to disable.
- **`per_test_timeout_secs`** — per-test budget, only meaningful with
  `test_filter`. Guards against a single hung test. Pass `0` to disable.

Raise or disable these for slow/integration suites that poll internally;
lower them when sanity-checking a fix to fail fast on an infinite loop.

### Redirecting full output to a file (`output_path`)

`cargo_check`, `cargo_build`, `cargo_test`, `cargo_clippy`, and `cargo_doc`
accept an optional `output_path` (relative to `working_dir`, no `..`, parent
must exist) that receives the complete NDJSON output while the tool result is
a compact summary keeping errors, the `build-finished` record, stderr, and
(for `cargo_test`) libtest failure markers. Use it instead of piping to a
temp file when the full transcript would bloat context; then open the file
only if the summary shows a non-zero exit code or failure markers.

### Environment variables (`env`)

Each cargo-spawning tool accepts an optional `env` object that sets (string
value) or unsets (`null` value) environment variables on the cargo subprocess
for that one call, layered over the defaults. Use it for one-shot debug knobs
(`RUSTFLAGS`, `RUST_LOG`, `RUST_BACKTRACE`, `RUSTC_BOOTSTRAP`) instead of
shelling out. Put permanent config in `Cargo.toml`, `.cargo/config.toml`, or
`rust-toolchain.toml`; never pass secrets via `env`.
<!-- cargo-mcp:setup:end -->
