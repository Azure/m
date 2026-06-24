// Copyright (c) Microsoft Corporation.

//! End-to-end integration test (M1-7) for the pure safe core.
//!
//! Composes the full stack the way a caller would —
//! [`Registry`] facade → [`Buffered`] decorator → in-memory [`TreeSurface`] —
//! over a few hundred synthetic keys and values, and asserts the isolation
//! semantics hold across the whole stack:
//!
//! * buffered writes leave the underlying base tree unmodified until commit,
//! * reads through the buffered facade reflect the overlay (read-your-writes),
//! * `commit` merges the buffered batch into the base correctly, and
//! * enumeration is ordinal-ordered.
//!
//! This lives as an in-crate `#[cfg(test)]` module (not a `tests/` integration
//! crate) so it can use the test-only `AsciiOrdinalCasing` ordinal seam, which
//! is itself `#[cfg(test)]`-gated and never ships.

#![cfg(test)]

use crate::decorator::Buffered;
use crate::path::KeyPath;
use crate::registry::Registry;
use crate::surface::{Surface, TreeSurface};
use crate::tree::{Hive, OverlayTree, ValueData};
use crate::{OrdinalCasing, Utf16};
use windows_text::AsciiOrdinalCasing;

const BASE_VALUE_COUNT: usize = 200;
const BASE_SUBKEY_COUNT: usize = 50;
const OVERWRITE_COUNT: usize = 50;
const DELETE_START: usize = 150;
const DELETE_COUNT: usize = 30;
const NEW_VALUE_COUNT: usize = 40;

fn w(s: &str) -> Utf16 {
    Utf16::from_utf8(s)
}

fn app_path() -> KeyPath {
    KeyPath::parse("Software\\App")
}

fn base_value_name(i: usize) -> Utf16 {
    w(&format!("Val{i:04}"))
}

fn new_value_name(i: usize) -> Utf16 {
    w(&format!("New{i:04}"))
}

/// Build the synthetic base hive: a few hundred string values plus subkeys
/// under `Software\App`.
fn synthetic_base() -> Hive {
    let casing = AsciiOrdinalCasing;
    let mut base = Hive::new();
    let app = app_path();
    for i in 0..BASE_VALUE_COUNT {
        base.insert_value(
            &casing,
            &app,
            base_value_name(i),
            ValueData::String(w(&format!("base-{i}"))),
        );
    }
    for i in 0..BASE_SUBKEY_COUNT {
        base.insert_key(&casing, &app.child(w(&format!("Sub{i:03}"))));
    }
    base
}

fn fresh_stack() -> Registry<Buffered<TreeSurface<AsciiOrdinalCasing>, AsciiOrdinalCasing>> {
    let tree = OverlayTree::new(AsciiOrdinalCasing, synthetic_base());
    let surface = TreeSurface::new(tree);
    let buffered = Buffered::new(surface, AsciiOrdinalCasing);
    Registry::new(buffered)
}

/// Apply the buffered batch: overwrite some values (with a type change), delete
/// a range, add new values, and create new subkeys — all through the facade.
fn apply_batch(reg: &mut Registry<Buffered<TreeSurface<AsciiOrdinalCasing>, AsciiOrdinalCasing>>) {
    let app = app_path();
    for i in 0..OVERWRITE_COUNT {
        // String -> Dword: both a value change and a type change.
        reg.set_u32(&app, base_value_name(i), i as u32).unwrap();
    }
    for i in DELETE_START..(DELETE_START + DELETE_COUNT) {
        reg.delete_value(&app, &base_value_name(i)).unwrap();
    }
    for i in 0..NEW_VALUE_COUNT {
        reg.set_u64(&app, new_value_name(i), (i as u64) << 40).unwrap();
    }
    for i in 0..20 {
        reg.create_key(&app.child(w(&format!("Fresh{i:03}")))).unwrap();
    }
}

#[test]
fn buffered_batch_is_read_your_writes_and_leaves_base_untouched() {
    let mut reg = fresh_stack();
    apply_batch(&mut reg);
    let app = app_path();

    // Overwritten entries read back as the new typed value through the facade.
    for i in 0..OVERWRITE_COUNT {
        assert_eq!(reg.get_u32(&app, &base_value_name(i)).unwrap(), i as u32);
    }
    // Deleted entries are gone through the facade.
    for i in DELETE_START..(DELETE_START + DELETE_COUNT) {
        assert_eq!(
            reg.get_value(&app, &base_value_name(i)),
            Err(crate::error::RegistryError::ValueNotFound)
        );
    }
    // New entries are visible through the facade.
    for i in 0..NEW_VALUE_COUNT {
        assert_eq!(reg.get_u64(&app, &new_value_name(i)).unwrap(), (i as u64) << 40);
    }
    // Untouched base entries still read their original string.
    for i in OVERWRITE_COUNT..DELETE_START {
        assert_eq!(
            reg.get_value(&app, &base_value_name(i)).unwrap(),
            ValueData::String(w(&format!("base-{i}")))
        );
    }

    // Nothing has reached the base: pull the inner surface out from under the
    // (still dirty) buffer and confirm the originals are intact and the
    // overwritten/new entries never landed.
    let buffered = reg.into_surface();
    assert!(buffered.is_dirty());
    let mut inner = buffered.into_inner();
    for i in 0..OVERWRITE_COUNT {
        assert_eq!(
            inner.invoke(&crate::surface::Request::ReadValue {
                path: app.clone(),
                name: base_value_name(i),
            }),
            Ok(crate::surface::Response::Value(ValueData::String(w(&format!(
                "base-{i}"
            )))))
        );
    }
    // A deleted-in-buffer value still exists in the base.
    assert_eq!(
        inner.invoke(&crate::surface::Request::ReadValue {
            path: app.clone(),
            name: base_value_name(DELETE_START),
        }),
        Ok(crate::surface::Response::Value(ValueData::String(w(
            &format!("base-{DELETE_START}")
        ))))
    );
    // A new-in-buffer value does not exist in the base.
    assert_eq!(
        inner.invoke(&crate::surface::Request::ReadValue {
            path: app,
            name: new_value_name(0),
        }),
        Err(crate::error::RegistryError::ValueNotFound)
    );
}

#[test]
fn commit_merges_batch_into_base() {
    let mut reg = fresh_stack();
    apply_batch(&mut reg);
    let app = app_path();

    reg.surface_mut().commit().unwrap();
    assert!(!reg.surface().is_dirty());

    // After commit the merged state is visible.
    for i in 0..OVERWRITE_COUNT {
        assert_eq!(reg.get_u32(&app, &base_value_name(i)).unwrap(), i as u32);
    }
    for i in DELETE_START..(DELETE_START + DELETE_COUNT) {
        assert_eq!(
            reg.get_value(&app, &base_value_name(i)),
            Err(crate::error::RegistryError::ValueNotFound)
        );
    }
    for i in 0..NEW_VALUE_COUNT {
        assert_eq!(reg.get_u64(&app, &new_value_name(i)).unwrap(), (i as u64) << 40);
    }

    // The value count reflects the merge: base minus deletions plus additions
    // (overwrites do not change the count).
    let values = reg.values(&app).unwrap();
    assert_eq!(
        values.len(),
        BASE_VALUE_COUNT - DELETE_COUNT + NEW_VALUE_COUNT
    );

    // Enumeration is ordinal-ordered: sort keys are non-decreasing.
    let casing = AsciiOrdinalCasing;
    let keys: Vec<Vec<u8>> = values
        .iter()
        .map(|(n, _)| casing.sort_key(n.as_units()))
        .collect();
    assert!(keys.windows(2).all(|w| w[0] <= w[1]), "values not ordinal-ordered");

    // Subkey enumeration likewise ordinal-ordered, with the new keys merged in.
    let subkeys = reg.keys(&app).unwrap();
    assert_eq!(subkeys.len(), BASE_SUBKEY_COUNT + 20);
    let skeys: Vec<Vec<u8>> = subkeys
        .iter()
        .map(|n| casing.sort_key(n.as_units()))
        .collect();
    assert!(skeys.windows(2).all(|w| w[0] <= w[1]), "subkeys not ordinal-ordered");
}

// --- M4-5: load a serialized C++ PIL artifact end to end --------------------

/// The hand-authored, spec-conformant artifact (D18). Embedded so the test is
/// hermetic; swap in a real C++-produced artifact when one is available.
const ARTIFACT_XML: &str = include_str!("../testdata/registry_artifact.xml");

#[test]
fn load_artifact_decodes_tree_and_enumerates_in_ordinal_order() {
    let casing = AsciiOrdinalCasing;
    let hive = crate::load_registry_hive(&casing, ARTIFACT_XML).expect("artifact should parse");
    let tree = OverlayTree::new(casing, hive);

    // Hive names are normalized to their canonical full forms (D19), so paths a
    // Session vends resolve — whether the artifact used an abbreviation (HKLM,
    // HKCC) or a long form (HKEY_CURRENT_USER).
    let lm = crate::Session::new().root(crate::WellKnownRoot::LocalMachine);
    assert!(tree.key_exists(&lm));
    assert!(tree.key_exists(&KeyPath::parse("HKEY_CURRENT_USER\\Environment")));
    assert!(tree.key_exists(&KeyPath::parse("HKEY_CURRENT_CONFIG\\Display")));

    // Every value type decodes (REG_SZ / EXPAND_SZ / MULTI_SZ / DWORD / QWORD /
    // BINARY) including the default (empty-name) value.
    let types = KeyPath::parse("HKEY_LOCAL_MACHINE\\Software\\Types");
    assert_eq!(
        tree.get_value(&types, &w("Name")).unwrap(),
        ValueData::String(w("Srv"))
    );
    assert_eq!(
        tree.get_value(&types, &w("Path")).unwrap(),
        ValueData::ExpandString(w("%TMP%"))
    );
    assert_eq!(
        tree.get_value(&types, &w("Langs")).unwrap(),
        ValueData::MultiString(vec![w("en"), w("fr")])
    );
    assert_eq!(tree.get_value(&types, &w("Count")).unwrap(), ValueData::Dword(0x1234));
    assert_eq!(tree.get_value(&types, &w("Big")).unwrap(), ValueData::Qword(1));
    assert_eq!(
        tree.get_value(&types, &w("Blob")).unwrap(),
        ValueData::Binary(vec![0xca, 0xfe])
    );
    assert_eq!(
        tree.get_value(&types, &w("")).unwrap(),
        ValueData::String(w("def"))
    );

    // A value tombstone folds away: the value is absent in the sealed base.
    assert_eq!(
        tree.get_value(&types, &w("Stale")),
        Err(crate::error::RegistryError::ValueNotFound)
    );

    // Subkeys authored out of order enumerate in ordinal order; the deleted key
    // is absent and the mirrored placeholder is present (empty).
    let order = KeyPath::parse("HKEY_LOCAL_MACHINE\\Software\\Order");
    let names = tree.enum_subkeys(&order).expect("enumerable");
    assert_eq!(
        names,
        vec![w("Alpha"), w("beta"), w("Beta2"), w("Mir"), w("Zeta"), w("_under")]
    );
    assert!(!tree.key_exists(&order.child(w("Del"))));
    let mir = order.child(w("Mir"));
    assert!(tree.key_exists(&mir));
    assert!(tree.enum_subkeys(&mir).expect("enumerable").is_empty());

    // The long-form hive's value decodes too.
    assert_eq!(
        tree.get_value(&KeyPath::parse("HKEY_CURRENT_USER\\Environment"), &w("EDITOR"))
            .unwrap(),
        ValueData::String(w("vi"))
    );
}

// --- M5-5: live capture -> save -> load round-trip parity -------------------

/// End-to-end on the real OS registry (Windows only): write a known subtree to
/// a scratch HKCU key with [`LiveRegistry`], [`capture`] it into a base hive,
/// serialize it with [`save_registry_hive`], reload it with
/// [`load_registry_hive`], and assert the reloaded tree matches what was
/// written — plus that re-serialization is a fixed point. The scratch subtree
/// is removed before and after the test (RAII), so the test is self-cleaning
/// and needs no administrator rights.
///
/// [`LiveRegistry`]: crate::LiveRegistry
/// [`capture`]: crate::LiveRegistry::capture
/// [`save_registry_hive`]: crate::save_registry_hive
/// [`load_registry_hive`]: crate::load_registry_hive
#[cfg(windows)]
#[test]
fn live_capture_round_trips_through_artifact_format() {
    use crate::{LiveRegistry, Win32OrdinalCasing};

    let casing = Win32OrdinalCasing;
    let reg = LiveRegistry::new();
    let root = KeyPath::parse(
        "HKEY_CURRENT_USER\\Software\\windows-platform-isolation-tests\\m5_5_capture",
    );

    /// Removes the scratch subtree on drop so the registry is left clean.
    struct Cleanup<'a>(&'a LiveRegistry, KeyPath);
    impl Drop for Cleanup<'_> {
        fn drop(&mut self) {
            let _ = self.0.delete_key(&self.1);
        }
    }

    // Start from a clean slate, and guarantee teardown.
    let _ = reg.delete_key(&root);
    let _guard = Cleanup(&reg, root.clone());

    // Populate a known subtree: a spread of value types at the root plus a
    // nested subkey with its own value.
    reg.create_key(&root).unwrap();
    reg.write_value(&root, &w("Name"), &ValueData::String(w("Srv"))).unwrap();
    reg.write_value(&root, &w("Path"), &ValueData::ExpandString(w("%TMP%"))).unwrap();
    reg.write_value(&root, &w("Langs"), &ValueData::MultiString(vec![w("en"), w("fr")])).unwrap();
    reg.write_value(&root, &w("Count"), &ValueData::Dword(0x1234)).unwrap();
    reg.write_value(&root, &w("Big"), &ValueData::Qword(1)).unwrap();
    reg.write_value(&root, &w("Blob"), &ValueData::Binary(vec![0xca, 0xfe])).unwrap();
    let child = root.child(w("Child"));
    reg.create_key(&child).unwrap();
    reg.write_value(&child, &w("Leaf"), &ValueData::Dword(7)).unwrap();

    // Capture -> save -> load.
    let captured = reg.capture(&casing, &root).expect("capture subtree");
    let xml = crate::save_registry_hive(casing, &captured);
    let reloaded = crate::load_registry_hive(&casing, &xml).expect("reload captured artifact");
    let tree = OverlayTree::new(casing, reloaded);

    // Parity: every written value reads back identically through the reloaded
    // artifact.
    assert_eq!(tree.get_value(&root, &w("Name")).unwrap(), ValueData::String(w("Srv")));
    assert_eq!(
        tree.get_value(&root, &w("Path")).unwrap(),
        ValueData::ExpandString(w("%TMP%"))
    );
    assert_eq!(
        tree.get_value(&root, &w("Langs")).unwrap(),
        ValueData::MultiString(vec![w("en"), w("fr")])
    );
    assert_eq!(tree.get_value(&root, &w("Count")).unwrap(), ValueData::Dword(0x1234));
    assert_eq!(tree.get_value(&root, &w("Big")).unwrap(), ValueData::Qword(1));
    assert_eq!(
        tree.get_value(&root, &w("Blob")).unwrap(),
        ValueData::Binary(vec![0xca, 0xfe])
    );
    assert!(tree.key_exists(&child));
    assert_eq!(tree.get_value(&child, &w("Leaf")).unwrap(), ValueData::Dword(7));

    // Re-serializing the reloaded hive is a fixed point.
    let xml2 = crate::save_registry_hive(casing, tree.base());
    assert_eq!(xml, xml2, "captured artifact must be stable under re-serialization");
}

// --- M6-6: load a serialized C++ PIL filesystem artifact end to end ----------

/// The hand-authored, spec-conformant filesystem artifact. Embedded so the test
/// is hermetic; swap in a real C++-produced artifact when one is available.
const FILESYSTEM_ARTIFACT_XML: &str = include_str!("../testdata/filesystem_artifact.xml");

#[test]
fn load_filesystem_artifact_decodes_tree_and_enumerates_in_ordinal_order() {
    use crate::fs_tree::{NodeKind, OverlayFileTree};
    use crate::FilePath;

    fn p(s: &str) -> FilePath {
        FilePath::from_utf8(s)
    }

    let casing = AsciiOrdinalCasing;
    let tree = crate::load_filesystem(&casing, FILESYSTEM_ARTIFACT_XML)
        .expect("filesystem artifact should parse");
    let overlay = OverlayFileTree::new(casing, tree);

    // The drive root and its nested directories/files resolve as ordinary path
    // components rooted at the Root's `text` (C:).
    assert!(overlay.dir_exists(&p("C:\\Windows")));
    assert!(overlay.dir_exists(&p("C:\\Windows\\System32")));
    assert!(overlay.file_exists(&p("C:\\Windows\\notepad.exe")));
    assert!(overlay.file_exists(&p("C:\\Windows\\System32\\kernel32.dll")));

    // File metadata decodes whole (size, attributes, and the opaque i64 times).
    let notepad = overlay
        .file_metadata(&p("C:\\Windows\\notepad.exe"))
        .expect("file present");
    assert_eq!(notepad.size, 4096);
    assert_eq!(notepad.attributes, 32);
    assert_eq!(notepad.creation_time, 131000000000000020);
    assert_eq!(notepad.last_write_time, 131000000000000021);
    assert_eq!(notepad.last_access_time, 131000000000000022);

    // Directory metadata is not exposed through file_metadata (that accessor is
    // for files); a directory path is reported NotFound there.
    assert_eq!(
        overlay.file_metadata(&p("C:\\Windows")),
        Err(crate::FilesystemError::NotFound)
    );

    // Entries authored out of order enumerate in ordinal order. Under C:\ the
    // directories Windows, Users, ProgramData were authored out of order; the
    // ASCII ordinal fold orders them ProgramData(P) < Users(U) < Windows(W).
    let root_entries = overlay.read_dir(&p("C:")).expect("enumerable");
    let root_names: Vec<String> = root_entries
        .iter()
        .map(|e| e.name.to_utf8().unwrap())
        .collect();
    assert_eq!(
        root_names,
        vec![
            "ProgramData".to_string(),
            "Users".to_string(),
            "Windows".to_string()
        ]
    );
    assert!(root_entries.iter().all(|e| e.kind == NodeKind::Directory));

    // System32 mixes files: kernel32.dll and ntdll.dll, ordinal-ordered.
    let sys32 = overlay
        .read_dir(&p("C:\\Windows\\System32"))
        .expect("enumerable");
    let sys32_names: Vec<String> = sys32.iter().map(|e| e.name.to_utf8().unwrap()).collect();
    assert_eq!(
        sys32_names,
        vec!["kernel32.dll".to_string(), "ntdll.dll".to_string()]
    );
    assert!(sys32.iter().all(|e| e.kind == NodeKind::File));

    // A deleted directory and a deleted file fold away (absent in the base).
    assert!(!overlay.dir_exists(&p("C:\\Windows\\OldStuff")));
    assert!(!overlay.file_exists(&p("C:\\Windows\\stale.log")));

    // A mirrored placeholder is present but empty: its name enumerates, but its
    // would-be children are not loaded.
    assert!(overlay.dir_exists(&p("C:\\Windows\\Temp")));
    assert!(
        overlay
            .read_dir(&p("C:\\Windows\\Temp"))
            .expect("enumerable")
            .is_empty()
    );
    assert!(!overlay.file_exists(&p("C:\\Windows\\Temp\\should-not-load.tmp")));

    // The mirrored placeholder folds into Windows's enumeration alongside the
    // real entries, ordinal-ordered. The ASCII fold orders them
    // notepad.exe(N) < System32(S) < Temp(T).
    let win_entries = overlay.read_dir(&p("C:\\Windows")).expect("enumerable");
    let win_names: Vec<String> = win_entries
        .iter()
        .map(|e| e.name.to_utf8().unwrap())
        .collect();
    assert_eq!(
        win_names,
        vec![
            "notepad.exe".to_string(),
            "System32".to_string(),
            "Temp".to_string()
        ]
    );
}

// --- M9-5: live filesystem end-to-end lifecycle -----------------------------

/// End-to-end on the real OS filesystem (Windows only): build a deterministic
/// scratch subtree under the OS temp dir entirely through [`LiveFilesystem`]
/// (create directories, write files with known metadata), then drive
/// metadata/enumeration/capture/removal back through the same provider and
/// assert:
///
/// * written attributes and timestamps read back identically (metadata parity),
/// * `read_dir` is ordinal-ordered and typed,
/// * a [`capture`](crate::LiveFilesystem::capture) of the subtree enumerates in
///   the same ordinal order as the live `read_dir` (snapshot parity), and
/// * `remove_dir` deletes the whole subtree.
///
/// The scratch subtree lives under a unique per-process temp path and is removed
/// before and after the test (RAII), so the test is self-cleaning and needs no
/// special rights.
#[cfg(windows)]
#[test]
fn live_filesystem_lifecycle_round_trips_metadata_and_ordering() {
    use crate::fs_tree::{FileMetadata, NodeKind, OverlayFileTree};
    use crate::{FilePath, LiveFilesystem, Win32OrdinalCasing};

    /// A FILETIME (100ns ticks since 1601) circa 2019, for deterministic
    /// timestamp round-tripping.
    const SAMPLE_TIME: i64 = 132_000_000_000_000_000;
    /// `FILE_ATTRIBUTE_HIDDEN`: benign (does not block later deletion).
    const FILE_ATTRIBUTE_HIDDEN: u32 = 0x2;

    let fs = LiveFilesystem::new(Win32OrdinalCasing);

    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let root_buf = std::env::temp_dir().join(format!("wpi-m9-5-{}-{nanos}", std::process::id()));
    let root = FilePath::from_utf8(&root_buf.to_string_lossy());

    /// Removes the scratch subtree on drop so the filesystem is left clean.
    struct Cleanup(std::path::PathBuf);
    impl Drop for Cleanup {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }
    let _ = std::fs::remove_dir_all(&root_buf);
    let _guard = Cleanup(root_buf.clone());

    let child = |rel: &str| FilePath::from_utf8(&root_buf.join(rel).to_string_lossy());

    // --- Build the subtree entirely through the live provider ---------------
    fs.create_dir(&root).unwrap();
    fs.create_dir(&child("Apps")).unwrap();
    fs.create_dir(&child("Data")).unwrap();
    // Nested directory created implicitly by a deep create.
    fs.create_dir(&child("Apps\\zzz")).unwrap();

    let readme_md = FileMetadata {
        size: 999, // not applied: content is out of scope
        creation_time: SAMPLE_TIME,
        last_write_time: SAMPLE_TIME + 7,
        last_access_time: SAMPLE_TIME + 9,
        attributes: FILE_ATTRIBUTE_HIDDEN,
    };
    fs.write_file(&child("readme.md"), readme_md).unwrap();
    fs.write_file(&child("Setup.exe"), FileMetadata::default()).unwrap();
    // A file under a not-yet-created parent: write_file creates ancestors.
    fs.write_file(&child("Apps\\tool.exe"), FileMetadata::default()).unwrap();

    // --- Metadata parity ----------------------------------------------------
    let got = fs.metadata(&child("readme.md")).unwrap();
    assert_eq!(got.creation_time, readme_md.creation_time);
    assert_eq!(got.last_write_time, readme_md.last_write_time);
    assert!(got.attributes & FILE_ATTRIBUTE_HIDDEN != 0);
    // Size is a consequence of content, which the metadata-only provider does
    // not write.
    assert_eq!(got.size, 0);

    assert!(fs.file_exists(&child("readme.md")).unwrap());
    assert!(fs.dir_exists(&child("Apps\\zzz")).unwrap());
    assert!(fs.file_exists(&child("Apps\\tool.exe")).unwrap());

    // --- Ordinal read_dir parity --------------------------------------------
    // Win32 ordinal fold (case-insensitive) orders the root entries
    // Apps(A) < Data(D) < readme.md(R) < Setup.exe(S).
    let entries = fs.read_dir(&root).unwrap();
    let names: Vec<String> = entries.iter().map(|e| e.name.to_utf8().unwrap()).collect();
    assert_eq!(
        names,
        vec![
            "Apps".to_string(),
            "Data".to_string(),
            "readme.md".to_string(),
            "Setup.exe".to_string()
        ]
    );
    let kind = |n: &str| {
        entries
            .iter()
            .find(|e| e.name.to_utf8().unwrap() == n)
            .unwrap()
            .kind
    };
    assert_eq!(kind("Apps"), NodeKind::Directory);
    assert_eq!(kind("readme.md"), NodeKind::File);

    // Apps holds tool.exe(T) < zzz(Z).
    let apps = fs.read_dir(&child("Apps")).unwrap();
    let apps_names: Vec<String> = apps.iter().map(|e| e.name.to_utf8().unwrap()).collect();
    assert_eq!(apps_names, vec!["tool.exe".to_string(), "zzz".to_string()]);

    // --- Capture / snapshot parity ------------------------------------------
    // A capture enumerates in the same ordinal order as the live read_dir.
    let captured = fs.capture(&root).expect("capture subtree");
    let overlay = OverlayFileTree::new(Win32OrdinalCasing, captured);
    let cap_names: Vec<String> = overlay
        .read_dir(&root)
        .unwrap()
        .iter()
        .map(|e| e.name.to_utf8().unwrap())
        .collect();
    assert_eq!(cap_names, names, "capture must match live ordinal ordering");
    assert!(overlay.file_exists(&child("Apps\\tool.exe")));
    assert!(overlay.dir_exists(&child("Apps\\zzz")));
    assert_eq!(
        overlay.file_metadata(&child("readme.md")).unwrap().attributes & FILE_ATTRIBUTE_HIDDEN,
        FILE_ATTRIBUTE_HIDDEN
    );

    // --- Removal ------------------------------------------------------------
    fs.remove_dir(&root).unwrap();
    assert!(!fs.dir_exists(&root).unwrap());
    // Idempotent: removing the already-gone subtree is not an error.
    fs.remove_dir(&root).unwrap();
}

// --- M8-5: web handler surface end-to-end (host-emulating harness) -----------

/// Synthetic request count for the web integration harness. A few hundred
/// exchanges exercises the decorator stack at integration scale.
const WEB_REQUEST_COUNT: usize = 300;

/// A leaf "application" handler that produces a deterministic response from the
/// request — the thing a real host would invoke at the bottom of the stack. It
/// fills the response in `on_send_response`, mirroring how the host hands the
/// handler a blank response to populate.
struct EchoApp;

impl crate::RequestHandler for EchoApp {
    fn on_begin_request(&mut self, _request: &crate::HttpRequest) -> crate::Disposition {
        crate::Disposition::Continue
    }

    fn on_send_response(&mut self, response: &mut crate::HttpResponse) -> crate::Disposition {
        // Derive a deterministic, request-independent body/header so two runs
        // over the same request are byte-identical.
        response.push_header("X-Echo", "served");
        crate::Disposition::Continue
    }
}

/// Build the i-th synthetic request: cycling method, a unique URL, and a header
/// whose *value* carries would-be PII the journal must never capture.
fn web_request(i: usize) -> crate::HttpRequest {
    let method = match i % 3 {
        0 => "GET",
        1 => "POST",
        _ => "PUT",
    };
    crate::HttpRequest::new(method, format!("/res/{i:04}"))
        .with_header("X-User", format!("secret-user-{i}"))
        .with_body(format!("payload-{i}").into_bytes())
}

/// Drive one request through a handler exactly as a host would: notify begin,
/// let the handler populate a blank response, notify send, return the response.
fn drive<H: crate::RequestHandler>(
    handler: &mut H,
    request: &crate::HttpRequest,
) -> crate::HttpResponse {
    let mut response = crate::HttpResponse::new(200);
    let _ = handler.on_begin_request(request);
    let _ = handler.on_send_response(&mut response);
    response
}

/// A sink that records every observed event for later inspection.
#[derive(Default)]
struct CollectingSink {
    events: Vec<crate::ObservedEvent>,
}

impl crate::ObservationSink for CollectingSink {
    fn observe(&mut self, event: crate::ObservedEvent) {
        self.events.push(event);
    }
}

#[test]
fn web_identity_path_is_byte_identical_to_undecorated() {
    let session = crate::WebSession::default();
    assert_eq!(session.mode(), crate::IsolationMode::Off);

    for i in 0..WEB_REQUEST_COUNT {
        let request = web_request(i);

        // Baseline: the bare app with no decorator.
        let baseline = drive(&mut EchoApp, &request);

        // Through the session's Off-mode (identity) stack.
        let mut handler =
            session.wrap(EchoApp, CollectingSink::default(), crate::VolumePolicy::record_all());
        let observed = drive(&mut handler, &request);

        assert_eq!(observed, baseline, "identity path must not alter the response");
        // Off mode selects the identity variant — no observation occurs.
        assert!(matches!(handler, crate::WebHandler::Identity(_)));
    }
}

#[test]
fn web_journaling_path_observes_without_altering_response() {
    let session = crate::WebSession::new(crate::IsolationMode::Record);

    for i in 0..WEB_REQUEST_COUNT {
        let request = web_request(i);
        let baseline = drive(&mut EchoApp, &request);

        let mut handler =
            session.wrap(EchoApp, CollectingSink::default(), crate::VolumePolicy::record_all());
        let observed = drive(&mut handler, &request);

        // The response is byte-identical to the undecorated path — journaling
        // observes, it does not mutate.
        assert_eq!(observed, baseline, "journaling must not alter the response");

        // Exactly one begin and one send were recorded, names only.
        let crate::WebHandler::Journaling(journaling) = handler else {
            panic!("record mode must select the journaling variant");
        };
        let events = &journaling.sink().events;
        assert_eq!(events.len(), 2, "one begin + one send per exchange");
        match &events[0] {
            crate::ObservedEvent::BeginRequest { method, url, header_names } => {
                assert_eq!(url, &format!("/res/{i:04}"));
                assert!(matches!(method.as_str(), "GET" | "POST" | "PUT"));
                // PII-first (D28): header NAMES are observed, values are not.
                assert_eq!(header_names, &vec!["X-User".to_string()]);
            }
            other => panic!("first event should be BeginRequest, got {other:?}"),
        }
        assert!(matches!(events[1], crate::ObservedEvent::SendResponse { .. }));
    }
}

#[test]
fn web_volume_policy_suppresses_observation_but_preserves_response() {
    let session = crate::WebSession::new(crate::IsolationMode::Record);
    let mut policy = crate::VolumePolicy::record_all();
    // Suppress a high-traffic health probe.
    policy.suppress("GET", "/health");

    let probe = crate::HttpRequest::new("GET", "/health");
    let baseline = drive(&mut EchoApp, &probe);

    let mut handler = session.wrap(EchoApp, CollectingSink::default(), policy);
    let observed = drive(&mut handler, &probe);

    assert_eq!(observed, baseline, "suppression must not alter the response");
    let crate::WebHandler::Journaling(journaling) = handler else {
        panic!("record mode must select the journaling variant");
    };
    assert!(
        journaling.sink().events.is_empty(),
        "suppressed exchange records nothing"
    );
}


