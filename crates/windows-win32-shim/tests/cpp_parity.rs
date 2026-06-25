// Copyright (c) Microsoft Corporation.

//! MW7 — C++ registry-artifact parity (SHIM-D13 / SHIM-D5).
//!
//! These drive a golden `persisted_state` artifact authored to the C++ `mwin32`
//! `save_xml` **dialect** (mwin32 DESIGN-NOTES D7 == platform-isolation D18/D19)
//! through the shim's config-driven session ([`ShimSession::from_config`] →
//! [`reg_ops`]) and assert the shim reproduces the C++ shim's *observable*
//! behavior. Where `tests/pilcfg.rs` round-trips the shim's **own**
//! `save_registry_hive` output, this suite proves the shim consumes the C++
//! emission dialect it never produced itself: abbreviated and long-form hive
//! names, a `last_write_time` attribute, every decodable `REG_*` type plus a
//! default (empty-name) value, mixed-case hex, value and key tombstones, a
//! mirrored placeholder, and out-of-order subkeys.
//!
//! Per SHIM-D13/SHIM-D5 the parity contract is the shared on-disk format; a
//! literally-C++-binary-captured artifact is a future swap-in (CHECKLIST MW7).

#![cfg(windows)]

use std::path::PathBuf;

use windows_platform_isolation::{KeyPath, Utf16};
use windows_sys::Win32::System::Registry::{
    REG_BINARY, REG_DWORD, REG_EXPAND_SZ, REG_MULTI_SZ, REG_QWORD, REG_SZ,
};
use windows_win32_shim::{Pilcfg, RawHandle, ShimSession, reg_ops};

/// Reserved predefined `HKEY` values (bare 32-bit form), matching the registry
/// integration suite.
const HKLM: RawHandle = 0x8000_0002;
const HKCU: RawHandle = 0x8000_0001;

fn w(s: &str) -> Utf16 {
    Utf16::from_utf8(s)
}

fn key(s: &str) -> KeyPath {
    KeyPath::parse(s)
}

fn artifact_path() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("testdata").join("cpp_registry_artifact.xml")
}

/// Decode UTF-16LE `bytes` to text, trimming trailing NULs so the assertion is
/// robust to the format's string-terminator convention.
fn utf16le_text(bytes: &[u8]) -> String {
    let units: Vec<u16> =
        bytes.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect();
    String::from_utf16_lossy(&units).trim_end_matches('\0').to_string()
}

/// Split a `REG_MULTI_SZ` payload into its component strings (UTF-16LE, NUL
/// separated, double-NUL terminated), independent of the exact terminator the
/// encoder emits.
fn multi_sz_parts(bytes: &[u8]) -> Vec<String> {
    let units: Vec<u16> =
        bytes.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect();
    let mut parts = Vec::new();
    let mut cur = Vec::new();
    for u in units {
        if u == 0 {
            if cur.is_empty() {
                break;
            }
            parts.push(String::from_utf16_lossy(&cur));
            cur.clear();
        } else {
            cur.push(u);
        }
    }
    parts
}

#[test]
fn cpp_dialect_registry_artifact_decodes_and_enumerates_through_the_shim() {
    let artifact = artifact_path();
    let before = std::fs::read(&artifact).expect("read golden artifact");

    let cfg = Pilcfg {
        persisted_state: artifact.to_string_lossy().into_owned(),
        ..Pilcfg::default()
    };
    let session = ShimSession::from_config(cfg);

    session.with_registry(|reg| {
        let handles = session.handles();

        // --- HKLM (abbreviated hive) values: every decodable REG_* type. ------
        let vendor = reg_ops::open_key(reg, handles, HKLM, &key("Software\\Vendor"))
            .expect("the persisted key opens under the normalized hive");

        let (ty, bytes) = reg_ops::query_value(reg, handles, vendor, &w("Name")).expect("Name");
        assert_eq!(ty, REG_SZ);
        assert_eq!(utf16le_text(&bytes), "Srv");

        let (ty, bytes) = reg_ops::query_value(reg, handles, vendor, &w("Path")).expect("Path");
        assert_eq!(ty, REG_EXPAND_SZ);
        assert_eq!(utf16le_text(&bytes), "%TMP%");

        let (ty, bytes) = reg_ops::query_value(reg, handles, vendor, &w("Langs")).expect("Langs");
        assert_eq!(ty, REG_MULTI_SZ);
        assert_eq!(multi_sz_parts(&bytes), vec!["en".to_string(), "fr".to_string()]);

        let (ty, bytes) = reg_ops::query_value(reg, handles, vendor, &w("Count")).expect("Count");
        assert_eq!(ty, REG_DWORD);
        assert_eq!(bytes, 0x0000_1234u32.to_le_bytes());

        let (ty, bytes) = reg_ops::query_value(reg, handles, vendor, &w("Big")).expect("Big");
        assert_eq!(ty, REG_QWORD);
        assert_eq!(bytes, 1u64.to_le_bytes());

        let (ty, bytes) = reg_ops::query_value(reg, handles, vendor, &w("Blob")).expect("Blob");
        assert_eq!(ty, REG_BINARY);
        assert_eq!(bytes, [0xCA, 0xFE]); // mixed-case hex "CAFE" decoded

        // The default (empty-name) value decodes like any other.
        let (ty, bytes) = reg_ops::query_value(reg, handles, vendor, &w("")).expect("default value");
        assert_eq!(ty, REG_SZ);
        assert_eq!(utf16le_text(&bytes), "def");

        // The value tombstone folded away: it is absent in the sealed snapshot.
        assert!(
            reg_ops::query_value(reg, handles, vendor, &w("Stale")).is_err(),
            "a deleted value must not be observable"
        );

        // --- Out-of-order subkeys enumerate in ordinal order; tombstone gone, --
        //     mirrored placeholder present (empty). -----------------------------
        let order = reg_ops::open_key(reg, handles, HKLM, &key("Software\\Order"))
            .expect("Order opens");
        let mut names = Vec::new();
        let mut i = 0;
        while let Some(name) = reg_ops::enum_key(reg, handles, order, i).expect("enum subkey") {
            names.push(String::from_utf16_lossy(name.as_units()));
            i += 1;
        }
        assert_eq!(names, ["Alpha", "beta", "Beta2", "Mir", "Zeta", "_under"]);

        // The deleted subkey is absent; the mirrored placeholder is an empty key.
        assert!(
            reg_ops::open_key(reg, handles, HKLM, &key("Software\\Order\\Del")).is_err(),
            "a deleted subkey must not be observable"
        );
        let mir = reg_ops::open_key(reg, handles, HKLM, &key("Software\\Order\\Mir"))
            .expect("the mirrored placeholder enumerates");
        assert!(
            reg_ops::enum_key(reg, handles, mir, 0).expect("enum mirrored").is_none(),
            "the mirrored placeholder is empty"
        );

        // --- HKCU (long-form HKEY_CURRENT_USER in the artifact) normalizes. ----
        let env = reg_ops::open_key(reg, handles, HKCU, &key("Environment"))
            .expect("the long-form hive normalizes and opens under HKCU");
        let (ty, bytes) = reg_ops::query_value(reg, handles, env, &w("EDITOR")).expect("EDITOR");
        assert_eq!(ty, REG_SZ);
        assert_eq!(utf16le_text(&bytes), "vi");
    });

    // Loading a persisted snapshot never mutates the source artifact.
    let after = std::fs::read(&artifact).expect("re-read golden artifact");
    assert_eq!(before, after, "the source artifact must be read-only");
}

#[test]
fn cpp_dialect_artifact_writes_are_isolated_in_the_overlay() {
    let artifact = artifact_path();
    let before = std::fs::read(&artifact).expect("read golden artifact");

    let cfg = Pilcfg {
        persisted_state: artifact.to_string_lossy().into_owned(),
        ..Pilcfg::default()
    };
    let session = ShimSession::from_config(cfg);

    session.with_registry(|reg| {
        let handles = session.handles();
        let vendor = reg_ops::open_key(reg, handles, HKLM, &key("Software\\Vendor"))
            .expect("the persisted key opens");

        // A write lands in the overlay and reads back...
        reg_ops::set_value(reg, handles, vendor, &w("Added"), REG_DWORD, &0xBEEFu32.to_le_bytes())
            .expect("overlay write");
        let (ty, bytes) =
            reg_ops::query_value(reg, handles, vendor, &w("Added")).expect("overlay read-back");
        assert_eq!(ty, REG_DWORD);
        assert_eq!(bytes, 0xBEEFu32.to_le_bytes());

        // ...while the snapshot's own values remain intact.
        let (_, name) = reg_ops::query_value(reg, handles, vendor, &w("Name")).expect("Name intact");
        assert_eq!(utf16le_text(&name), "Srv");
    });

    // The isolated write never reaches the source artifact on disk.
    let after = std::fs::read(&artifact).expect("re-read golden artifact");
    assert_eq!(before, after, "an overlay write must not touch the source artifact");
}
