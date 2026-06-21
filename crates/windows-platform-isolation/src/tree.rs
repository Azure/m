// Copyright (c) Microsoft Corporation.

//! The registry overlay / copy-on-write tree (D11 shape; pure safe-half logic,
//! D13).
//!
//! An [`OverlayTree`] layers a mutable *overlay* over an immutable *base*
//! [`Hive`]. Reads see the overlay union-mounted over the base; writes land only
//! in the overlay (copy-on-write) so the base is never mutated; deletions are
//! recorded as tombstones that shadow the base. All key/value ordering and
//! case-insensitive matching go through the ordinal-casing seam (D6/D8): every
//! map is keyed by the binary sort key, so iteration is ordinal-ordered and
//! lookups are ordinal case-insensitive.

use std::collections::BTreeMap;

use crate::error::{RegistryError, Result};
use crate::path::KeyPath;
use crate::wstr::{OrdinalCasing, Utf16};

/// The type tag of a registry value (D11 `Type`).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum ValueType {
    /// A single string (`REG_SZ`).
    String,
    /// An expandable string (`REG_EXPAND_SZ`).
    ExpandString,
    /// A list of strings (`REG_MULTI_SZ`).
    MultiString,
    /// A 32-bit integer (`REG_DWORD`).
    Dword,
    /// A 64-bit integer (`REG_QWORD`).
    Qword,
    /// Raw bytes (`REG_BINARY`).
    Binary,
}

/// A typed registry value payload.
#[derive(Clone, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum ValueData {
    /// `REG_SZ`.
    String(Utf16),
    /// `REG_EXPAND_SZ`.
    ExpandString(Utf16),
    /// `REG_MULTI_SZ`.
    MultiString(Vec<Utf16>),
    /// `REG_DWORD`.
    Dword(u32),
    /// `REG_QWORD`.
    Qword(u64),
    /// `REG_BINARY`.
    Binary(Vec<u8>),
}

impl ValueData {
    /// The type tag for this value.
    #[must_use]
    pub fn value_type(&self) -> ValueType {
        match self {
            Self::String(_) => ValueType::String,
            Self::ExpandString(_) => ValueType::ExpandString,
            Self::MultiString(_) => ValueType::MultiString,
            Self::Dword(_) => ValueType::Dword,
            Self::Qword(_) => ValueType::Qword,
            Self::Binary(_) => ValueType::Binary,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct NamedValue {
    name: Utf16,
    data: ValueData,
}

// --- Base hive: immutable captured content ----------------------------------

/// An immutable tree of keys and values — the base layer an [`OverlayTree`]
/// shadows. In the first cut this is built from synthetic data (or, in M2, from
/// state captured by the C++ providers, D15).
#[derive(Clone, Debug, Default)]
pub struct Hive {
    root: HiveNode,
}

#[derive(Clone, Debug, Default)]
struct HiveNode {
    /// sort_key(name) -> (original name, child)
    subkeys: BTreeMap<Vec<u8>, (Utf16, HiveNode)>,
    /// sort_key(name) -> value
    values: BTreeMap<Vec<u8>, NamedValue>,
}

impl Hive {
    /// An empty hive.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Insert (or replace) a value at `path`, creating intermediate keys.
    pub fn insert_value<C: OrdinalCasing>(
        &mut self,
        casing: &C,
        path: &KeyPath,
        name: Utf16,
        data: ValueData,
    ) {
        let node = self.root.ensure_path(casing, path.components());
        let key = casing.sort_key(name.as_units());
        node.values.insert(key, NamedValue { name, data });
    }

    /// Ensure a key exists at `path`, creating intermediate keys.
    pub fn insert_key<C: OrdinalCasing>(&mut self, casing: &C, path: &KeyPath) {
        self.root.ensure_path(casing, path.components());
    }

    fn node_at<C: OrdinalCasing>(&self, casing: &C, comps: &[Utf16]) -> Option<&HiveNode> {
        let mut node = &self.root;
        for c in comps {
            let key = casing.sort_key(c.as_units());
            node = &node.subkeys.get(&key)?.1;
        }
        Some(node)
    }
}

impl HiveNode {
    fn ensure_path<C: OrdinalCasing>(&mut self, casing: &C, comps: &[Utf16]) -> &mut HiveNode {
        let mut node = self;
        for c in comps {
            let key = casing.sort_key(c.as_units());
            node = &mut node
                .subkeys
                .entry(key)
                .or_insert_with(|| (c.clone(), HiveNode::default()))
                .1;
        }
        node
    }
}

// --- Overlay: modifications layered over the base ---------------------------

#[derive(Debug)]
enum ValueState {
    Set(NamedValue),
    Deleted,
}

/// A node in the overlay. A present (non-deleted) node asserts the key exists in
/// the overlay; `deleted` is a tombstone that shadows the base subtree.
#[derive(Debug, Default)]
struct OverlayNode {
    deleted: bool,
    values: BTreeMap<Vec<u8>, ValueState>,
    subkeys: BTreeMap<Vec<u8>, (Utf16, OverlayNode)>,
}

enum Resolve<'a> {
    /// A tombstone (this key or an ancestor) shadows the base: key absent.
    Deleted,
    /// The overlay has a live node for this exact path.
    Overlay(&'a OverlayNode),
    /// The overlay says nothing about this path; defer to the base.
    NotInOverlay,
}

/// An overlay / copy-on-write view over an immutable [`Hive`] base.
pub struct OverlayTree<C: OrdinalCasing> {
    casing: C,
    base: Hive,
    overlay: OverlayNode,
}

impl<C: OrdinalCasing> OverlayTree<C> {
    /// Create an overlay over `base` using `casing` for ordinal matching.
    pub fn new(casing: C, base: Hive) -> Self {
        Self {
            casing,
            base,
            overlay: OverlayNode::default(),
        }
    }

    /// The immutable base layer (never mutated by writes — exposed for tests and
    /// callers that need to prove copy-on-write isolation).
    #[must_use]
    pub fn base(&self) -> &Hive {
        &self.base
    }

    fn resolve(&self, comps: &[Utf16]) -> Resolve<'_> {
        let mut node = &self.overlay;
        for c in comps {
            let key = self.casing.sort_key(c.as_units());
            match node.subkeys.get(&key) {
                Some((_, child)) => {
                    if child.deleted {
                        return Resolve::Deleted;
                    }
                    node = child;
                }
                None => return Resolve::NotInOverlay,
            }
        }
        Resolve::Overlay(node)
    }

    fn overlay_path(&mut self, comps: &[Utf16]) -> &mut OverlayNode {
        let casing = &self.casing;
        let mut node = &mut self.overlay;
        for c in comps {
            let key = casing.sort_key(c.as_units());
            let entry = node
                .subkeys
                .entry(key)
                .or_insert_with(|| (c.clone(), OverlayNode::default()));
            // Navigating through a node asserts it exists (clears any tombstone).
            entry.1.deleted = false;
            node = &mut entry.1;
        }
        node
    }

    /// Whether a key exists at `path`.
    #[must_use]
    pub fn key_exists(&self, path: &KeyPath) -> bool {
        match self.resolve(path.components()) {
            Resolve::Deleted => false,
            Resolve::Overlay(_) => true,
            Resolve::NotInOverlay => self.base.node_at(&self.casing, path.components()).is_some(),
        }
    }

    /// Create a key at `path` (and any missing ancestors).
    pub fn create_key(&mut self, path: &KeyPath) {
        if path.is_empty() {
            return;
        }
        self.overlay_path(path.components());
    }

    /// Delete the key at `path` and its subtree (tombstone shadowing the base).
    pub fn delete_key(&mut self, path: &KeyPath) {
        if path.is_empty() {
            return;
        }
        let node = self.overlay_path(path.components());
        node.deleted = true;
        node.values.clear();
        node.subkeys.clear();
    }

    /// Set (or replace) a value at `path`/`name`. Copy-on-write: only the overlay
    /// is touched; the base is unchanged.
    pub fn set_value(&mut self, path: &KeyPath, name: Utf16, data: ValueData) {
        let key = self.casing.sort_key(name.as_units());
        let node = self.overlay_path(path.components());
        node.values.insert(key, ValueState::Set(NamedValue { name, data }));
    }

    /// Delete a value at `path`/`name` (tombstone shadowing the base value).
    pub fn delete_value(&mut self, path: &KeyPath, name: &Utf16) {
        let key = self.casing.sort_key(name.as_units());
        let node = self.overlay_path(path.components());
        node.values.insert(key, ValueState::Deleted);
    }

    fn base_value(&self, comps: &[Utf16], vkey: &[u8]) -> Result<ValueData> {
        match self.base.node_at(&self.casing, comps) {
            None => Err(RegistryError::KeyNotFound),
            Some(node) => node
                .values
                .get(vkey)
                .map(|nv| nv.data.clone())
                .ok_or(RegistryError::ValueNotFound),
        }
    }

    /// Read a value at `path`/`name`.
    pub fn get_value(&self, path: &KeyPath, name: &Utf16) -> Result<ValueData> {
        let comps = path.components();
        let vkey = self.casing.sort_key(name.as_units());
        match self.resolve(comps) {
            Resolve::Deleted => Err(RegistryError::KeyNotFound),
            Resolve::NotInOverlay => self.base_value(comps, &vkey),
            Resolve::Overlay(node) => match node.values.get(&vkey) {
                Some(ValueState::Set(nv)) => Ok(nv.data.clone()),
                Some(ValueState::Deleted) => Err(RegistryError::ValueNotFound),
                None => match self.base_value(comps, &vkey) {
                    // The overlay asserts the key exists, so a base "key not
                    // found" collapses to "value not found".
                    Err(RegistryError::KeyNotFound) => Err(RegistryError::ValueNotFound),
                    other => other,
                },
            },
        }
    }

    /// Enumerate the immediate subkey names at `path`, ordinal-ordered, with the
    /// overlay merged over the base (created keys added, deleted keys removed).
    pub fn enum_subkeys(&self, path: &KeyPath) -> Result<Vec<Utf16>> {
        let comps = path.components();
        if !self.key_exists(path) {
            return Err(RegistryError::KeyNotFound);
        }
        let mut merged: BTreeMap<Vec<u8>, Utf16> = BTreeMap::new();
        if let Some(node) = self.base.node_at(&self.casing, comps) {
            for (skey, (name, _)) in &node.subkeys {
                merged.insert(skey.clone(), name.clone());
            }
        }
        if let Resolve::Overlay(node) = self.resolve(comps) {
            for (skey, (name, child)) in &node.subkeys {
                if child.deleted {
                    merged.remove(skey);
                } else {
                    merged.insert(skey.clone(), name.clone());
                }
            }
        }
        Ok(merged.into_values().collect())
    }

    /// Enumerate the values at `path`, ordinal-ordered, with the overlay merged
    /// over the base (set values override/added, deleted values removed).
    pub fn enum_values(&self, path: &KeyPath) -> Result<Vec<(Utf16, ValueData)>> {
        let comps = path.components();
        if !self.key_exists(path) {
            return Err(RegistryError::KeyNotFound);
        }
        let mut merged: BTreeMap<Vec<u8>, NamedValue> = BTreeMap::new();
        if let Some(node) = self.base.node_at(&self.casing, comps) {
            for (vkey, nv) in &node.values {
                merged.insert(vkey.clone(), nv.clone());
            }
        }
        if let Resolve::Overlay(node) = self.resolve(comps) {
            for (vkey, state) in &node.values {
                match state {
                    ValueState::Set(nv) => {
                        merged.insert(vkey.clone(), nv.clone());
                    }
                    ValueState::Deleted => {
                        merged.remove(vkey);
                    }
                }
            }
        }
        Ok(merged
            .into_values()
            .map(|nv| (nv.name, nv.data))
            .collect())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wstr::AsciiOrdinalCasing;

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    /// Build a small base hive:
    ///   Software\App  : Name="base", Count=1
    ///   Software\Other: (empty)
    fn base_tree() -> Hive {
        let c = AsciiOrdinalCasing;
        let mut h = Hive::new();
        let app = KeyPath::parse("Software\\App");
        h.insert_value(&c, &app, w("Name"), ValueData::String(w("base")));
        h.insert_value(&c, &app, w("Count"), ValueData::Dword(1));
        h.insert_key(&c, &KeyPath::parse("Software\\Other"));
        h
    }

    fn tree() -> OverlayTree<AsciiOrdinalCasing> {
        OverlayTree::new(AsciiOrdinalCasing, base_tree())
    }

    #[test]
    fn reads_through_to_base() {
        let t = tree();
        let app = KeyPath::parse("Software\\App");
        assert_eq!(
            t.get_value(&app, &w("Name")),
            Ok(ValueData::String(w("base")))
        );
        assert_eq!(t.get_value(&app, &w("Count")), Ok(ValueData::Dword(1)));
    }

    #[test]
    fn lookup_is_case_insensitive() {
        let t = tree();
        // Path and value name differ only in case from the base.
        let app = KeyPath::parse("SOFTWARE\\app");
        assert_eq!(
            t.get_value(&app, &w("NAME")),
            Ok(ValueData::String(w("base")))
        );
    }

    #[test]
    fn overlay_shadows_base_value() {
        let mut t = tree();
        let app = KeyPath::parse("Software\\App");
        t.set_value(&app, w("Name"), ValueData::String(w("overlaid")));
        assert_eq!(
            t.get_value(&app, &w("Name")),
            Ok(ValueData::String(w("overlaid")))
        );
    }

    #[test]
    fn write_is_copy_on_write_base_unchanged() {
        let mut t = tree();
        let app = KeyPath::parse("Software\\App");
        t.set_value(&app, w("Name"), ValueData::String(w("overlaid")));
        // The base layer still holds the original value.
        let c = AsciiOrdinalCasing;
        let node = t.base().node_at(&c, app.components()).unwrap();
        let vkey = c.sort_key(w("Name").as_units());
        assert_eq!(
            node.values.get(&vkey).map(|nv| nv.data.clone()),
            Some(ValueData::String(w("base")))
        );
    }

    #[test]
    fn create_new_key_and_value() {
        let mut t = tree();
        let fresh = KeyPath::parse("Software\\Fresh");
        assert!(!t.key_exists(&fresh));
        t.set_value(&fresh, w("V"), ValueData::Qword(42));
        assert!(t.key_exists(&fresh));
        assert_eq!(t.get_value(&fresh, &w("V")), Ok(ValueData::Qword(42)));
    }

    #[test]
    fn missing_value_on_existing_key_is_value_not_found() {
        let t = tree();
        let app = KeyPath::parse("Software\\App");
        assert_eq!(
            t.get_value(&app, &w("Nope")),
            Err(RegistryError::ValueNotFound)
        );
    }

    #[test]
    fn missing_key_is_key_not_found() {
        let t = tree();
        let gone = KeyPath::parse("Software\\Gone");
        assert_eq!(
            t.get_value(&gone, &w("V")),
            Err(RegistryError::KeyNotFound)
        );
    }

    #[test]
    fn delete_value_tombstones_base() {
        let mut t = tree();
        let app = KeyPath::parse("Software\\App");
        t.delete_value(&app, &w("Name"));
        assert_eq!(
            t.get_value(&app, &w("Name")),
            Err(RegistryError::ValueNotFound)
        );
        // The sibling value is untouched.
        assert_eq!(t.get_value(&app, &w("Count")), Ok(ValueData::Dword(1)));
    }

    #[test]
    fn delete_key_shadows_base_subtree() {
        let mut t = tree();
        let app = KeyPath::parse("Software\\App");
        t.delete_key(&app);
        assert!(!t.key_exists(&app));
        assert_eq!(
            t.get_value(&app, &w("Name")),
            Err(RegistryError::KeyNotFound)
        );
    }

    #[test]
    fn enum_subkeys_merges_ordinal_ordered() {
        let mut t = tree();
        let software = KeyPath::parse("Software");
        // Base has App, Other; add Apple (overlay), delete Other.
        t.create_key(&KeyPath::parse("Software\\Apple"));
        t.delete_key(&KeyPath::parse("Software\\Other"));
        let names: Vec<String> = t
            .enum_subkeys(&software)
            .unwrap()
            .iter()
            .map(|n| n.to_utf8().unwrap())
            .collect();
        // Ordinal order: App, Apple (Other removed).
        assert_eq!(names, vec!["App".to_string(), "Apple".to_string()]);
    }

    #[test]
    fn enum_values_merges_overlay_over_base() {
        let mut t = tree();
        let app = KeyPath::parse("Software\\App");
        t.set_value(&app, w("Name"), ValueData::String(w("new")));
        t.set_value(&app, w("Added"), ValueData::Dword(7));
        t.delete_value(&app, &w("Count"));
        let got: Vec<(String, ValueData)> = t
            .enum_values(&app)
            .unwrap()
            .into_iter()
            .map(|(n, d)| (n.to_utf8().unwrap(), d))
            .collect();
        // Ordinal order by name: Added, Name (Count deleted).
        assert_eq!(
            got,
            vec![
                ("Added".to_string(), ValueData::Dword(7)),
                ("Name".to_string(), ValueData::String(w("new"))),
            ]
        );
    }

    #[test]
    fn enum_on_missing_key_is_key_not_found() {
        let t = tree();
        let gone = KeyPath::parse("Nope");
        assert_eq!(t.enum_subkeys(&gone), Err(RegistryError::KeyNotFound));
        assert_eq!(t.enum_values(&gone), Err(RegistryError::KeyNotFound));
    }
}
