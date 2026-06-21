# windows-platform-isolation — CHECKLIST

Action-only checklist. Completed groups move to `COMPLETED-CHECKLIST.md`.
Decision references point at `DESIGN-NOTES.md` (D-numbers).

Milestone M1 (pure safe core) is complete — see `COMPLETED-CHECKLIST.md`.

## Deferred (queued as later milestones)

- **M2 — C++ artifact loader (D5 read side / D15 ingress).** Safe deserializer
  that loads state captured by the C++ PIL providers into the M1 tree. Gated on
  documenting the C++ saved-state format (read the C++ serialization code).
  Ends in an interop test loading a real C++-produced artifact.
- **M3 — FFI leaf & live/"direct" provider.** The single `unsafe` module:
  `windows-sys` bindings, RAII handle wrappers, the production Win32 ordinal
  comparator / sort-key (D6/D8), and a live registry provider. The write/capture
  side of the artifact format.
