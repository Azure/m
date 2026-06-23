// Copyright (c) Microsoft Corporation.

//! Win32→`m` link-time alias generator (MW5, SHIM-D4).
//!
//! The export manifest [`EXPORT_DEF`] (`windows_win32_shim.def`) is the single
//! source of truth for the set of `m<Name>` entry points the shim exports. This
//! module is the Rust counterpart of the C++ `generate_mwin32_alias.cmake`: it
//! parses the manifest and, for every aliased export `m<Name>`, emits the
//! redirect of the genuine Win32 name `<Name>` (the map is mechanical — strip
//! the leading `m`) onto the shim:
//!
//! ```c
//! extern "C" void m<Name>();                                  // import thunk
//! extern "C" void (*__imp_<Name>)() = &m<Name>;               // the decisive redirect
//! #pragma comment(linker, "/alternatename:<Name>=m<Name>")    // best-effort fallback
//! ```
//!
//! A `.def` line may carry a trailing `; noalias` comment: such a name is still
//! exported by the DLL (so a client may call it explicitly) but is deliberately
//! **not** auto-redirected here. That is how `mCloseHandle` opts out —
//! redirecting every `CloseHandle` in a client would capture non-shim OS
//! handles. A line whose first non-blank character is `;` is fully commented out
//! (a not-yet-implemented export) and is skipped entirely.
//!
//! The generator is intentionally signature-free: the IAT slot is pointer-sized
//! and the client casts through its own declared signature at the call site, so
//! the uniform `void(*)()` slot type is sufficient (the `.def` carries no
//! signatures). `&m<Name>` is a link-time address constant, so the slot is
//! initialized at load time, before any client call.
//!
//! This module has no `unsafe` and no platform dependency: it is pure text
//! processing over the manifest, usable from a build script or a test on any
//! host. Producing the alias object / import library and the C++ link-proof are
//! the deferred cross-toolchain MW5-3 work.

use std::collections::HashSet;
use std::fmt::Write as _;

/// The export manifest, embedded at compile time. The single source of truth
/// for the shim's exported `m<Name>` set and this generator's input.
pub const EXPORT_DEF: &str = include_str!("../windows_win32_shim.def");

/// Error produced when the manifest contains a malformed export name.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AliasError {
    /// An active export line is not a valid `m<Name>` shim name (`m` followed by
    /// an uppercase ASCII letter or an underscore). Carries the offending token.
    InvalidExportShape(String),
}

impl std::fmt::Display for AliasError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            AliasError::InvalidExportShape(name) => {
                write!(f, "export '{name}' does not match the m<Name> shim shape")
            }
        }
    }
}

impl std::error::Error for AliasError {}

/// The parsed manifest: the exported names and the aliased subset.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Manifest {
    /// Every active (uncommented) exported shim name, in file order, deduplicated.
    pub exports: Vec<String>,
    /// The subset of [`Manifest::exports`] that participates in auto-redirection
    /// (i.e. excludes any name marked `; noalias`), in file order.
    pub aliased: Vec<String>,
}

/// The genuine Win32 name for a shim name: the mechanical strip of the leading
/// `m` (`mRegOpenKeyExW` → `RegOpenKeyExW`, `m_lopen` → `_lopen`).
///
/// `shim` must begin with the ASCII byte `m`; every name that reaches this
/// helper has already passed [`is_shim_shape`].
#[must_use]
pub fn win32_name(shim: &str) -> &str {
    &shim[1..]
}

/// Whether `name` is a valid shim export shape: `m` followed by an uppercase
/// ASCII letter, or `m` followed by `_` for the dusty-deck legacy primitives
/// whose genuine names start with an underscore (`_lopen`, `_lread`, …).
#[must_use]
pub fn is_shim_shape(name: &str) -> bool {
    let mut chars = name.chars();
    if chars.next() != Some('m') {
        return false;
    }
    matches!(chars.next(), Some(c) if c.is_ascii_uppercase() || c == '_')
}

/// Parse the export manifest into its active and aliased name sets.
///
/// Mirrors `generate_mwin32_alias.cmake`: split each line at the first `;` (the
/// `.def` comment marker); a name is the trimmed text before it. Empty names and
/// the `EXPORTS` keyword are skipped; the first occurrence of each name wins
/// (duplicates are dropped). A name whose trailing comment contains `noalias` is
/// recorded as an export but excluded from the aliased set.
///
/// # Errors
///
/// Returns [`AliasError::InvalidExportShape`] if an active export name does not
/// match [`is_shim_shape`].
pub fn parse_manifest(def: &str) -> Result<Manifest, AliasError> {
    let mut exports = Vec::new();
    let mut aliased = Vec::new();
    let mut seen: HashSet<&str> = HashSet::new();

    for raw_line in def.lines() {
        let (name_part, comment_part) = match raw_line.find(';') {
            Some(i) => (&raw_line[..i], &raw_line[i..]),
            None => (raw_line, ""),
        };
        let shim_name = name_part.trim();
        if shim_name.is_empty() || shim_name == "EXPORTS" {
            continue;
        }
        if !is_shim_shape(shim_name) {
            return Err(AliasError::InvalidExportShape(shim_name.to_string()));
        }
        if !seen.insert(shim_name) {
            continue;
        }
        exports.push(shim_name.to_string());
        if !comment_part.contains("noalias") {
            aliased.push(shim_name.to_string());
        }
    }

    Ok(Manifest { exports, aliased })
}

/// Generate the alias translation unit from the export manifest.
///
/// Emits the generated-file header followed by, for each aliased export, the
/// import-thunk declaration, the decisive `__imp_<Name>` IAT-slot definition,
/// and the `/alternatename` fallback pragma.
///
/// # Errors
///
/// Propagates [`parse_manifest`] errors.
pub fn generate_alias_tu(def: &str) -> Result<String, AliasError> {
    let manifest = parse_manifest(def)?;
    let mut out = String::new();

    let _ = write!(
        out,
        "// Copyright (c) Microsoft Corporation.\n\
         //\n\
         // GENERATED FILE - DO NOT EDIT.\n\
         // Produced by windows_win32_shim::alias_gen from windows_win32_shim.def.\n\
         //\n\
         // Link this object into a client to redirect its genuine Win32\n\
         // registry/filesystem calls to the windows-win32-shim DLL. {count} functions\n\
         // are redirected. See SHIM-D4.\n\
         //\n\
         // Each entry defines the __imp_ IAT slot the client's <windows.h> dllimport\n\
         // call goes through (the decisive redirect) and a /alternatename fallback for\n\
         // plain references. The uniform void(*)() slot type is intentional: the slot\n\
         // is only a pointer value; the client casts through its own signature at the\n\
         // call site.\n\n",
        count = manifest.aliased.len()
    );

    for shim in &manifest.aliased {
        let win32 = win32_name(shim);
        let _ = write!(
            out,
            "extern \"C\" void {shim}();\n\
             extern \"C\" void (*__imp_{win32})() = &{shim};\n\
             #pragma comment(linker, \"/alternatename:{win32}={shim}\")\n\n"
        );
    }

    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    // --- win32_name / is_shim_shape -----------------------------------------

    #[test]
    fn win32_name_strips_leading_m() {
        assert_eq!(win32_name("mRegOpenKeyExW"), "RegOpenKeyExW");
        assert_eq!(win32_name("mCreateFileW"), "CreateFileW");
        assert_eq!(win32_name("m_lopen"), "_lopen");
    }

    #[test]
    fn is_shim_shape_accepts_uppercase_and_underscore() {
        assert!(is_shim_shape("mRegCloseKey"));
        assert!(is_shim_shape("m_lread"));
    }

    #[test]
    fn is_shim_shape_rejects_malformed() {
        assert!(!is_shim_shape("madeup")); // m + lowercase
        assert!(!is_shim_shape("m")); // no second char
        assert!(!is_shim_shape("m1Foo")); // m + digit
        assert!(!is_shim_shape("CreateFileW")); // no leading m
    }

    // --- synthetic manifests -------------------------------------------------

    #[test]
    fn skips_comment_and_exports_keyword() {
        let def = "EXPORTS\n; a header comment\n mRegCloseKey\n;mCreateFileA\n";
        let m = parse_manifest(def).unwrap();
        assert_eq!(m.exports, vec!["mRegCloseKey"]);
        assert_eq!(m.aliased, vec!["mRegCloseKey"]);
    }

    #[test]
    fn dedupes_repeated_names() {
        let def = " mRegOpenKeyExW\n mRegOpenKeyExW\n mRegOpenKeyExW\n";
        let m = parse_manifest(def).unwrap();
        assert_eq!(m.exports, vec!["mRegOpenKeyExW"]);
    }

    #[test]
    fn noalias_trailing_comment_exports_but_excludes_alias() {
        let def = " mCloseHandle ; noalias\n mCreateFileW\n";
        let m = parse_manifest(def).unwrap();
        assert_eq!(m.exports, vec!["mCloseHandle", "mCreateFileW"]);
        assert_eq!(m.aliased, vec!["mCreateFileW"]);
    }

    #[test]
    fn dusty_deck_underscore_name_aliases_to_underscore_win32() {
        let def = " m_lopen\n";
        let tu = generate_alias_tu(def).unwrap();
        assert!(tu.contains("extern \"C\" void (*__imp__lopen)() = &m_lopen;"));
        assert!(tu.contains("/alternatename:_lopen=m_lopen"));
    }

    #[test]
    fn invalid_active_name_errors() {
        let def = " mRegCloseKey\n madeup\n";
        assert_eq!(
            parse_manifest(def),
            Err(AliasError::InvalidExportShape("madeup".to_string()))
        );
    }

    #[test]
    fn generate_emits_slot_and_pragma_for_each_alias() {
        let def = " mRegOpenKeyExW\n";
        let tu = generate_alias_tu(def).unwrap();
        assert!(tu.contains("extern \"C\" void mRegOpenKeyExW();"));
        assert!(tu.contains("extern \"C\" void (*__imp_RegOpenKeyExW)() = &mRegOpenKeyExW;"));
        assert!(tu.contains("#pragma comment(linker, \"/alternatename:RegOpenKeyExW=mRegOpenKeyExW\")"));
    }

    #[test]
    fn generate_header_reports_alias_count() {
        let def = " mCloseHandle ; noalias\n mRegOpenKeyExW\n mCreateFileW\n";
        let tu = generate_alias_tu(def).unwrap();
        // Two aliased (mCloseHandle is noalias, so excluded).
        assert!(tu.contains("2 functions"));
        assert!(!tu.contains("__imp_CloseHandle"));
    }

    // --- the real embedded manifest -----------------------------------------

    #[test]
    fn real_manifest_active_and_aliased_counts() {
        let m = parse_manifest(EXPORT_DEF).unwrap();
        // 72 active exports today (W forms + NOT_SUPPORTED stubs + the 10
        // registry A forms, MW6-2); mCloseHandle is the sole noalias export, so
        // 71 are aliased.
        assert_eq!(m.exports.len(), 72);
        assert_eq!(m.aliased.len(), 71);
    }

    #[test]
    fn real_manifest_noalias_close_handle() {
        let m = parse_manifest(EXPORT_DEF).unwrap();
        assert!(m.exports.iter().any(|n| n == "mCloseHandle"));
        assert!(!m.aliased.iter().any(|n| n == "mCloseHandle"));
    }

    #[test]
    fn real_manifest_find_ex_w_is_active() {
        let m = parse_manifest(EXPORT_DEF).unwrap();
        assert!(m.aliased.iter().any(|n| n == "mFindFirstFileExW"));
        assert!(m.aliased.iter().any(|n| n == "mFindFirstFileTransactedW"));
    }

    #[test]
    fn real_manifest_unimplemented_names_are_commented_out() {
        let m = parse_manifest(EXPORT_DEF).unwrap();
        for absent in [
            "mCreateFileA",          // ANSI form, MW6-3
            "mFindFirstFileExA",     // find-Ex ANSI form, MW6-3
            "mWebCoreActivate",      // out of scope
            "mLZOpenFileW",          // dusty-deck, unimplemented
        ] {
            assert!(
                !m.exports.iter().any(|n| n == absent),
                "{absent} should be commented out in the manifest"
            );
        }
    }

    #[test]
    fn real_manifest_generates_without_error_and_aliases_a_known_name() {
        let tu = generate_alias_tu(EXPORT_DEF).unwrap();
        assert!(tu.contains("extern \"C\" void (*__imp_CreateFileW)() = &mCreateFileW;"));
        assert!(tu.contains("/alternatename:RegSetValueExW=mRegSetValueExW"));
    }
}
