// Copyright (c) Microsoft Corporation.

//! `gen-alias-obj` — emit the Win32→`m` alias COFF object from an NDJSON manifest.
//!
//! Produces the link-time redirect object described in SHIM-D4 with **no C++
//! compiler and no MSVC tool**: it reads the NDJSON alias manifest and writes the
//! COFF object bytes directly (see [`windows_win32_shim::alias_obj`]). A client
//! that links the emitted `.obj` together with the shim's import library has its
//! genuine `<windows.h>` Win32 calls (`RegOpenKeyExW`, `CreateFileW`, …)
//! redirected to the shim's `m<Name>` exports.
//!
//! Usage:
//!
//! ```text
//! gen-alias-obj [--manifest <ndjson>] [--out <obj>]
//! ```
//!
//! `--manifest` defaults to the manifest embedded in the crate
//! (`windows_win32_shim_aliases.ndjson`); `--out` defaults to
//! `windows_win32_shim_alias.obj` in the current directory.

use std::process::ExitCode;

use windows_win32_shim::alias_obj::{ALIAS_MANIFEST, generate_alias_object};

/// Default output path when `--out` is not supplied.
const DEFAULT_OUT: &str = "windows_win32_shim_alias.obj";

fn main() -> ExitCode {
    match run() {
        Ok(report) => {
            println!("{report}");
            ExitCode::SUCCESS
        }
        Err(message) => {
            eprintln!("gen-alias-obj: error: {message}");
            ExitCode::FAILURE
        }
    }
}

/// Parse arguments, emit the object, and return the human-readable report (the
/// single content sink; `main` is the only place that touches stdout/stderr).
fn run() -> Result<String, String> {
    let mut manifest_path: Option<String> = None;
    let mut out_path: Option<String> = None;

    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-h" | "--help" => return Ok(usage()),
            "--manifest" => {
                manifest_path = Some(args.next().ok_or("--manifest requires a path")?);
            }
            "-o" | "--out" => {
                out_path = Some(args.next().ok_or("--out requires a path")?);
            }
            other => return Err(format!("unexpected argument '{other}'\n\n{}", usage())),
        }
    }

    let (manifest, source) = match &manifest_path {
        Some(path) => (
            std::fs::read_to_string(path).map_err(|e| format!("reading '{path}': {e}"))?,
            path.clone(),
        ),
        None => (
            ALIAS_MANIFEST.to_string(),
            "<embedded manifest>".to_string(),
        ),
    };

    let out = out_path.unwrap_or_else(|| DEFAULT_OUT.to_string());
    let bytes = generate_alias_object(&manifest).map_err(|e| e.to_string())?;
    std::fs::write(&out, &bytes).map_err(|e| format!("writing '{out}': {e}"))?;

    Ok(format!(
        "wrote {} bytes to '{out}' from {source}",
        bytes.len()
    ))
}

/// The usage / help text.
fn usage() -> String {
    format!(
        "gen-alias-obj — emit the Win32->m alias COFF object\n\
         \n\
         USAGE:\n    \
         gen-alias-obj [--manifest <ndjson>] [--out <obj>]\n\
         \n\
         OPTIONS:\n    \
         --manifest <ndjson>  NDJSON alias manifest (default: embedded)\n    \
         -o, --out <obj>      output object path (default: {DEFAULT_OUT})\n    \
         -h, --help           print this help"
    )
}
