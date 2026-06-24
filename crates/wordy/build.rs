// Copyright (c) Microsoft Corporation.

//! Generic, isolation-agnostic build script for `wordy`.
//!
//! `wordy` is deliberately unaware of any isolation machinery (windows-win32-shim
//! SHIM-D19). This script knows nothing about aliases, shims, or `.pilcfg`; it
//! only injects *extra link inputs* when the environment asks it to, and
//! otherwise builds a perfectly ordinary binary. That "plain build with nothing
//! set" is the proof that the crate carries no isolation knowledge — the act of
//! isolating `wordy` is performed entirely from the outside (by whoever sets
//! these variables at build time), exactly as it would be for a real third-party
//! application whose source we do not control.
//!
//! Build-script directives are used (rather than external `RUSTFLAGS` /
//! `.cargo/config.toml`) because they are scoped to this crate's final artifact
//! and cache deterministically.
//!
//! Recognized variables (all optional):
//! - `WORDY_EXTRA_LINK_SEARCH` — one or more native library search directories
//!   (platform path-list separated).
//! - `WORDY_EXTRA_LINK_OBJ` — one or more object files to add to the link
//!   (platform path-list separated); passed through as raw linker arguments.
//! - `WORDY_EXTRA_LINK_LIB` — one or more library names to link (`;`-separated).

use std::env;

const ENV_SEARCH: &str = "WORDY_EXTRA_LINK_SEARCH";
const ENV_OBJ: &str = "WORDY_EXTRA_LINK_OBJ";
const ENV_LIB: &str = "WORDY_EXTRA_LINK_LIB";

fn main() {
    println!("cargo:rerun-if-env-changed={ENV_SEARCH}");
    println!("cargo:rerun-if-env-changed={ENV_OBJ}");
    println!("cargo:rerun-if-env-changed={ENV_LIB}");

    if let Some(value) = non_empty(ENV_SEARCH) {
        for dir in env::split_paths(&value) {
            println!("cargo:rustc-link-search=native={}", dir.display());
        }
    }

    if let Some(value) = non_empty(ENV_OBJ) {
        for obj in env::split_paths(&value) {
            // An object file is added to the link as a raw linker argument; it is
            // not a named library, so `rustc-link-lib` does not apply.
            println!("cargo:rustc-link-arg={}", obj.display());
        }
    }

    if let Some(value) = non_empty(ENV_LIB) {
        for lib in value.split(';').filter(|s| !s.is_empty()) {
            println!("cargo:rustc-link-lib={lib}");
        }
    }
}

/// Read an environment variable, returning `Some` only when it is present and
/// not empty.
fn non_empty(name: &str) -> Option<String> {
    match env::var(name) {
        Ok(value) if !value.trim().is_empty() => Some(value),
        _ => None,
    }
}
