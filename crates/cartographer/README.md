# cartographer

Offline tool that turns **journaled HTTP API interactions** into **OpenAPI 3.1**.

The win32 shim observes request/response traffic and journals it as NDJSON (the
shared [`api-journal`](../api-journal) schema). Those journals are gathered
off-machine and handed to `cartographer`, which:

1. loads the project's existing OpenAPI specs (if any),
2. **validates** the observed interactions against them, reporting any deviation
   (an undocumented path, an undeclared status code, a body-schema mismatch, …)
   as a diagnostic, and
3. **synthesizes** updated OpenAPI 3.1 specs from what was observed, merging into
   the existing specs rather than overwriting human-authored prose.

The tool is pure data — no Win32, no `unsafe`.

## CLI

```text
cartographer <journal.ndjson>... [--spec <openapi.(yaml|json)>]... [--update] [--overwrite] [--format text|ndjson]
```

- Default run validates the journals against the supplied specs and prints
  diagnostics; the exit code is `0` (clean), `1` (findings), or `2` (usage error).
- `--update` writes the synthesized/merged spec to `openapi.<fmt>`.
- `--overwrite` replaces existing operations instead of additively merging.

## Try it — generate a sample journal + spec

A runnable example builds a representative `wordy` journal and the OpenAPI spec
cartographer synthesizes from it, so you can eyeball what capture + synthesis
produce end to end:

```text
cargo run -p cartographer --example wordy -- <out_dir>
```

`<out_dir>` defaults to the current directory. It writes:

- `wordy-journal.ndjson` — the captured journal (one interaction per line),
- `wordy-openapi.yaml` / `wordy-openapi.json` — the synthesized OpenAPI 3.1 doc.

For example, to drop the samples into a scratch folder:

```text
cargo run -p cartographer --example wordy -- .scratch/wordy-openapi
```

The sample exercises path-template inference (`/custom/frobnicate` and
`/custom/wat` collapse to `/custom/{id}`), nested body schemas, literal
`example`s carried from a `bodies: full` capture, and name-only retention of
identity headers. The generator lives at
[`examples/wordy.rs`](examples/wordy.rs).
