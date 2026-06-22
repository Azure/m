// Copyright (c) Microsoft Corporation.

//! MW4 `.pilcfg` integration tests.
//!
//! These mirror the C++ `test_pilcfg` (schema parse + env-expansion) and the
//! buffered `test_mwinreg_value_ops` (writes isolated from the live registry),
//! exercised end-to-end through the session wiring ([`ShimSession::from_config`]
//! → [`reg_ops`]). The `persisted_state` backing is a fully sandboxed in-memory
//! snapshot loaded from an artifact on disk: reads observe the snapshot, writes
//! are isolated in the overlay (the source artifact is never mutated), and
//! `capture_snapshot` serializes the resulting state to a separate artifact on
//! teardown. No test touches the live OS registry, so the suite is reproducible.

#![cfg(windows)]

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};

use windows_platform_isolation::{
    Hive, KeyPath, Registry, Utf16, ValueData, Win32OrdinalCasing, load_registry_hive,
    save_registry_hive,
};
use windows_sys::Win32::System::Registry::REG_DWORD;
use windows_win32_shim::{Pilcfg, ShimSession, expand_environment_path, parse_pilcfg, reg_ops};

// Predefined `HKEY` reserved value (bare 32-bit form), matching the registry
// integration suite.
const HKLM: windows_win32_shim::RawHandle = 0x8000_0002;

/// The canonical root the persisted snapshot is built under.
const LOCAL_MACHINE: &str = "HKEY_LOCAL_MACHINE";

fn w(s: &str) -> Utf16 {
    Utf16::from_utf8(s)
}

fn key(s: &str) -> KeyPath {
    KeyPath::parse(s)
}

/// A unique scratch directory under the OS temp dir, removed on drop so the
/// suite leaves no artifacts behind.
struct TempDir {
    path: PathBuf,
}

impl TempDir {
    fn new() -> Self {
        static COUNTER: AtomicU32 = AtomicU32::new(0);
        let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
        let mut path = std::env::temp_dir();
        path.push(format!("mw4-pilcfg-{}-{}", std::process::id(), unique));
        std::fs::create_dir_all(&path).expect("create scratch dir");
        Self { path }
    }

    fn child(&self, name: &str) -> PathBuf {
        self.path.join(name)
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}

/// Serialize a one-key snapshot (`HKLM\Software\Vendor\App` with `Seed` = the
/// given DWORD) to `path`, returning the produced XML.
fn write_seed_artifact(path: &Path, seed: u32) -> String {
    let casing = Win32OrdinalCasing;
    let mut hive = Hive::new();
    hive.insert_value(
        &casing,
        &key(&format!("{LOCAL_MACHINE}\\Software\\Vendor\\App")),
        w("Seed"),
        ValueData::Dword(seed),
    );
    let xml = save_registry_hive(casing, &hive);
    std::fs::write(path, &xml).expect("write seed artifact");
    xml
}

#[test]
fn schema_parses_and_expands_paths_like_the_cpp() {
    // Mirror of `test_pilcfg`: recognized members are read, `webcore` is
    // ignored, and `%VAR%` tokens expand in path members but not redirection
    // keys. `SystemRoot` is always defined on Windows.
    let system_root = std::env::var("SystemRoot").expect("SystemRoot is set on Windows");
    let json = r#"{
        "buffer_updates": true,
        "persisted_state": "%SystemRoot%\\state.xml",
        "redirections": [ { "from": "%SystemRoot%", "to": "HKCU\\X" } ],
        "webcore": { "ignored": true }
    }"#;
    let cfg = parse_pilcfg(json).expect("well-formed config parses");
    assert!(cfg.buffer_updates);
    assert_eq!(cfg.persisted_state, format!("{system_root}\\state.xml"));
    assert_eq!(cfg.redirections[0].0, "%SystemRoot%");
    assert_eq!(
        expand_environment_path("%SystemRoot%\\x"),
        format!("{system_root}\\x")
    );
}

#[test]
fn persisted_state_reads_the_snapshot_and_isolates_writes() {
    let dir = TempDir::new();
    let state = dir.child("state.xml");
    let original_xml = write_seed_artifact(&state, 0x1234);

    let cfg = Pilcfg {
        persisted_state: state.to_string_lossy().into_owned(),
        ..Pilcfg::default()
    };
    let session = ShimSession::from_config(cfg);

    session.with_registry(|reg| {
        let app = reg_ops::open_key(reg, session.handles(), HKLM, &key("Software\\Vendor\\App"))
            .expect("the persisted key opens");

        // The seed value from the snapshot is observable.
        let (ty, bytes) =
            reg_ops::query_value(reg, session.handles(), app, &w("Seed")).expect("seed reads back");
        assert_eq!(ty, REG_DWORD);
        assert_eq!(bytes, 0x1234u32.to_le_bytes());

        // A write through the session is isolated in the overlay.
        reg_ops::set_value(
            reg,
            session.handles(),
            app,
            &w("Added"),
            REG_DWORD,
            &0xBEEFu32.to_le_bytes(),
        )
        .expect("write to the overlay");
        let (_, added) = reg_ops::query_value(reg, session.handles(), app, &w("Added"))
            .expect("the overlay write reads back");
        assert_eq!(added, 0xBEEFu32.to_le_bytes());
    });

    // The source artifact on disk is untouched by the isolated write.
    assert_eq!(
        std::fs::read_to_string(&state).expect("re-read artifact"),
        original_xml
    );
}

#[test]
fn capture_snapshot_persists_overlay_writes_on_teardown() {
    let dir = TempDir::new();
    let state = dir.child("state.xml");
    let snapshot = dir.child("snapshot.xml");
    write_seed_artifact(&state, 0x1234);

    let cfg = Pilcfg {
        persisted_state: state.to_string_lossy().into_owned(),
        capture_snapshot: snapshot.to_string_lossy().into_owned(),
        ..Pilcfg::default()
    };
    let session = ShimSession::from_config(cfg);

    session.with_registry(|reg| {
        let app = reg_ops::open_key(reg, session.handles(), HKLM, &key("Software\\Vendor\\App"))
            .expect("the persisted key opens");
        reg_ops::set_value(
            reg,
            session.handles(),
            app,
            &w("Added"),
            REG_DWORD,
            &0xBEEFu32.to_le_bytes(),
        )
        .expect("write to the overlay");
    });

    // Teardown capture writes the snapshot artifact.
    assert!(session.capture_snapshot(), "a snapshot is written");

    // Reloading the captured artifact round-trips both the seed and the write.
    let casing = Win32OrdinalCasing;
    let xml = std::fs::read_to_string(&snapshot).expect("read captured snapshot");
    let hive = load_registry_hive(&casing, &xml).expect("captured snapshot reloads");
    let mut reloaded = Registry::in_memory(hive);
    let handles = windows_win32_shim::HandleTable::new();

    let app = reg_ops::open_key(&mut reloaded, &handles, HKLM, &key("Software\\Vendor\\App"))
        .expect("the captured key opens");
    let (_, seed) =
        reg_ops::query_value(&mut reloaded, &handles, app, &w("Seed")).expect("seed survived");
    assert_eq!(seed, 0x1234u32.to_le_bytes());
    let (_, added) =
        reg_ops::query_value(&mut reloaded, &handles, app, &w("Added")).expect("write survived");
    assert_eq!(added, 0xBEEFu32.to_le_bytes());
}

#[test]
fn capture_snapshot_without_a_path_is_a_noop() {
    let dir = TempDir::new();
    let state = dir.child("state.xml");
    write_seed_artifact(&state, 0x1234);

    // A persisted backing but no capture path: capture is a no-op.
    let cfg = Pilcfg {
        persisted_state: state.to_string_lossy().into_owned(),
        ..Pilcfg::default()
    };
    let session = ShimSession::from_config(cfg);
    assert!(!session.capture_snapshot(), "no path means no snapshot");
}

#[test]
fn malformed_persisted_state_falls_back_to_passthrough() {
    let dir = TempDir::new();
    let bogus = dir.child("missing.xml");

    // A persisted_state path that does not exist must not fail the host; the
    // session falls back to live passthrough (tolerant load, SHIM-D5). It also
    // cannot capture (live backings are not serializable), and the host stays
    // usable.
    let cfg = Pilcfg {
        persisted_state: bogus.to_string_lossy().into_owned(),
        capture_snapshot: dir.child("snapshot.xml").to_string_lossy().into_owned(),
        ..Pilcfg::default()
    };
    let session = ShimSession::from_config(cfg);
    assert!(session.handles().is_empty());
    assert!(
        !session.capture_snapshot(),
        "a live backing cannot be captured"
    );
}
