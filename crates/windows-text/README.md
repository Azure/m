# windows-text

A safe, reusable Windows string layer: UTF-16 storage, Windows **ordinal**
case-insensitive comparison + binary sort keys, and code-page transcoding.

This crate is unconditionally `#![forbid(unsafe_code)]`. All `unsafe` Win32
calls are confined to the sibling [`windows-text-sys`](../windows-text-sys) leaf
crate.

## What it provides

- `Utf16` — an owned UTF-16 string (UTF-8 ingress, lossless `u16` storage,
  fallible UTF-8 egress).
- Ordinal casing — `Utf16::compare_ignore_case` / `Utf16::sort_key` (Windows),
  and the `OrdinalCasing` trait as an off-Windows dependency-injection seam.
- `CodePage` — `MultiByteToWideChar` / `WideCharToMultiByte` conversions
  (Windows).

## Features

- `testing` — exposes the pure-Rust `AsciiOrdinalCasing` reference so downstream
  crates can unit-test case-insensitive logic off Windows.

See `DESIGN-NOTES.md` for the casing/sort-key semantics and rationale. The crate
charter (porting more of the C++ `m` string libraries over time) is recorded as
D16 in `../windows-platform-isolation/DESIGN-NOTES.md`.
