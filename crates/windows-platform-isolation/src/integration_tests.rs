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

