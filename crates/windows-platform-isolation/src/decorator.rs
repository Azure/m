// Copyright (c) Microsoft Corporation.

//! Cross-cutting decorators written once over the [`Surface`] seam (D4).
//!
//! Because every operation is a [`Request`] value, the layers that cut across
//! all surfaces — pass-through, write buffering, journaling — are written a
//! single time against the [`Surface`] trait, independent of whatever concrete
//! provider sits underneath. The buffered decorator's journal *is* the
//! `Request` verb stream (D4): a buffered write is nothing more than a
//! `Request` appended to a list, and `commit` is replaying that list onto the
//! inner surface.

use std::cmp::Ordering;
use std::collections::BTreeMap;

use crate::error::{RegistryError, Result};
use crate::path::KeyPath;
use crate::surface::{Request, Response, Surface};
use crate::tree::ValueData;
use crate::{OrdinalCasing, Utf16};

/// A transparent decorator: forwards every request to the inner surface
/// unchanged. Useful as a seam for inserting behavior without altering call
/// sites, and as the trivial baseline that proves the decorator pattern.
pub struct PassThrough<S: Surface> {
    inner: S,
}

impl<S: Surface> PassThrough<S> {
    /// Wrap an inner surface.
    pub fn new(inner: S) -> Self {
        Self { inner }
    }

    /// Recover the inner surface.
    pub fn into_inner(self) -> S {
        self.inner
    }
}

impl<S: Surface> Surface for PassThrough<S> {
    fn invoke(&mut self, req: &Request) -> Result<Response> {
        self.inner.invoke(req)
    }
}

/// A write-buffering decorator (D4). Mutations are captured in an in-memory
/// journal and are **not** applied to the inner surface until [`commit`] is
/// called; reads see the buffered mutations layered over the inner surface
/// (read-your-writes), leaving the inner surface untouched until commit.
///
/// The journal is the `Request` verb stream: each buffered mutation is the
/// originating `Request`, and `commit` simply replays them in order.
///
/// [`commit`]: Buffered::commit
pub struct Buffered<S: Surface, C: OrdinalCasing> {
    inner: S,
    casing: C,
    journal: Vec<Request>,
}

impl<S: Surface, C: OrdinalCasing> Buffered<S, C> {
    /// Wrap an inner surface with an empty write buffer.
    pub fn new(inner: S, casing: C) -> Self {
        Self {
            inner,
            casing,
            journal: Vec::new(),
        }
    }

    /// The pending verb stream (the buffered mutations, in order).
    #[must_use]
    pub fn journal(&self) -> &[Request] {
        &self.journal
    }

    /// Whether any mutations are buffered.
    #[must_use]
    pub fn is_dirty(&self) -> bool {
        !self.journal.is_empty()
    }

    /// Replay the buffered mutations onto the inner surface, then clear the
    /// buffer. After this the inner surface reflects all buffered writes.
    pub fn commit(&mut self) -> Result<()> {
        for req in std::mem::take(&mut self.journal) {
            self.inner.invoke(&req)?;
        }
        Ok(())
    }

    /// Discard all buffered mutations without touching the inner surface.
    pub fn rollback(&mut self) {
        self.journal.clear();
    }

    /// Recover the inner surface, discarding any uncommitted buffer.
    pub fn into_inner(self) -> S {
        self.inner
    }

    fn comps_eq(&self, a: &[Utf16], b: &[Utf16]) -> bool {
        a.len() == b.len()
            && a.iter().zip(b).all(|(x, y)| {
                self.casing.compare_ignore_case(x.as_units(), y.as_units()) == Ordering::Equal
            })
    }

    fn path_eq(&self, a: &KeyPath, b: &KeyPath) -> bool {
        self.comps_eq(a.components(), b.components())
    }

    /// Is `anc` an ancestor of, or equal to, `path` (ordinal comparison)?
    fn is_ancestor_or_self(&self, anc: &KeyPath, path: &KeyPath) -> bool {
        let a = anc.components();
        let p = path.components();
        a.len() <= p.len() && self.comps_eq(a, &p[..a.len()])
    }

    /// If `child`'s immediate parent is `parent`, return the child's leaf name.
    fn child_leaf_of(&self, child: &KeyPath, parent: &KeyPath) -> Option<Utf16> {
        let c = child.components();
        if c.len() == parent.len() + 1 && self.comps_eq(&c[..parent.len()], parent.components()) {
            Some(c[c.len() - 1].clone())
        } else {
            None
        }
    }

    fn name_eq(&self, a: &Utf16, b: &Utf16) -> bool {
        self.casing.compare_ignore_case(a.as_units(), b.as_units()) == Ordering::Equal
    }

    fn read_value(&mut self, path: &KeyPath, name: &Utf16) -> Result<Response> {
        // Undecided until the journal says otherwise.
        let mut value: Option<Option<ValueData>> = None;
        let mut key_present: Option<bool> = None;
        for req in &self.journal {
            match req {
                Request::WriteValue {
                    path: p,
                    name: n,
                    data,
                } if self.path_eq(p, path) && self.name_eq(n, name) => {
                    value = Some(Some(data.clone()));
                    key_present = Some(true);
                }
                Request::DeleteValue { path: p, name: n }
                    if self.path_eq(p, path) && self.name_eq(n, name) =>
                {
                    value = Some(None);
                }
                Request::CreateKey { path: p } if self.path_eq(p, path) => {
                    key_present = Some(true);
                }
                Request::DeleteKey { path: p } if self.is_ancestor_or_self(p, path) => {
                    key_present = Some(false);
                    value = Some(None);
                }
                _ => {}
            }
        }
        if key_present == Some(false) {
            return Err(RegistryError::KeyNotFound);
        }
        match value {
            Some(Some(d)) => Ok(Response::Value(d)),
            Some(None) => Err(RegistryError::ValueNotFound),
            None => self.inner.invoke(&Request::ReadValue {
                path: path.clone(),
                name: name.clone(),
            }),
        }
    }

    fn key_exists(&mut self, path: &KeyPath) -> Result<Response> {
        let mut decided: Option<bool> = None;
        for req in &self.journal {
            match req {
                Request::CreateKey { path: p } if self.is_ancestor_or_self(path, p) => {
                    decided = Some(true);
                }
                Request::WriteValue { path: p, .. } if self.is_ancestor_or_self(path, p) => {
                    decided = Some(true);
                }
                Request::DeleteKey { path: p } if self.is_ancestor_or_self(p, path) => {
                    decided = Some(false);
                }
                _ => {}
            }
        }
        match decided {
            Some(b) => Ok(Response::Exists(b)),
            None => self.inner.invoke(&Request::KeyExists { path: path.clone() }),
        }
    }

    fn enum_values(&mut self, path: &KeyPath) -> Result<Response> {
        let mut exists = matches!(
            self.inner.invoke(&Request::KeyExists { path: path.clone() })?,
            Response::Exists(true)
        );
        let mut map: BTreeMap<Vec<u8>, (Utf16, ValueData)> = BTreeMap::new();
        if exists
            && let Response::Values(v) =
                self.inner.invoke(&Request::EnumValues { path: path.clone() })?
        {
            for (n, d) in v {
                map.insert(self.casing.sort_key(n.as_units()), (n, d));
            }
        }
        for req in &self.journal {
            match req {
                Request::DeleteKey { path: p } if self.is_ancestor_or_self(p, path) => {
                    map.clear();
                    exists = false;
                }
                Request::CreateKey { path: p } if self.path_eq(p, path) => exists = true,
                Request::CreateKey { path: p } | Request::WriteValue { path: p, .. }
                    if self.is_ancestor_or_self(path, p) && !self.path_eq(p, path) =>
                {
                    exists = true;
                }
                Request::WriteValue {
                    path: p,
                    name,
                    data,
                } if self.path_eq(p, path) => {
                    map.insert(self.casing.sort_key(name.as_units()), (name.clone(), data.clone()));
                    exists = true;
                }
                Request::DeleteValue { path: p, name } if self.path_eq(p, path) => {
                    map.remove(&self.casing.sort_key(name.as_units()));
                }
                _ => {}
            }
        }
        if !exists {
            return Err(RegistryError::KeyNotFound);
        }
        Ok(Response::Values(map.into_values().collect()))
    }

    fn enum_keys(&mut self, path: &KeyPath) -> Result<Response> {
        let mut exists = matches!(
            self.inner.invoke(&Request::KeyExists { path: path.clone() })?,
            Response::Exists(true)
        );
        let mut map: BTreeMap<Vec<u8>, Utf16> = BTreeMap::new();
        if exists
            && let Response::Names(v) =
                self.inner.invoke(&Request::EnumKeys { path: path.clone() })?
        {
            for n in v {
                map.insert(self.casing.sort_key(n.as_units()), n);
            }
        }
        for req in &self.journal {
            match req {
                Request::DeleteKey { path: p } if self.is_ancestor_or_self(p, path) => {
                    map.clear();
                    exists = false;
                }
                Request::DeleteKey { path: p } => {
                    if let Some(leaf) = self.child_leaf_of(p, path) {
                        map.remove(&self.casing.sort_key(leaf.as_units()));
                    }
                }
                Request::CreateKey { path: p } if self.path_eq(p, path) => exists = true,
                Request::CreateKey { path: p } | Request::WriteValue { path: p, .. }
                    if self.is_ancestor_or_self(path, p) =>
                {
                    exists = true;
                    if let Some(leaf) = self.child_leaf_of(p, path) {
                        map.insert(self.casing.sort_key(leaf.as_units()), leaf);
                    }
                }
                _ => {}
            }
        }
        if !exists {
            return Err(RegistryError::KeyNotFound);
        }
        Ok(Response::Names(map.into_values().collect()))
    }
}

impl<S: Surface, C: OrdinalCasing> Surface for Buffered<S, C> {
    fn invoke(&mut self, req: &Request) -> Result<Response> {
        match req {
            // Mutations are captured, never forwarded until commit.
            Request::CreateKey { .. }
            | Request::DeleteKey { .. }
            | Request::WriteValue { .. }
            | Request::DeleteValue { .. } => {
                self.journal.push(req.clone());
                Ok(Response::Unit)
            }
            Request::ReadValue { path, name } => self.read_value(path, name),
            Request::KeyExists { path } => self.key_exists(path),
            Request::EnumKeys { path } => self.enum_keys(path),
            Request::EnumValues { path } => self.enum_values(path),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::surface::TreeSurface;
    use crate::tree::Hive;
    use windows_text::AsciiOrdinalCasing;

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    fn base_surface() -> TreeSurface<AsciiOrdinalCasing> {
        let c = AsciiOrdinalCasing;
        let mut base = Hive::new();
        base.insert_value(
            &c,
            &KeyPath::parse("Software\\App"),
            w("Name"),
            ValueData::String(w("base")),
        );
        TreeSurface::new(crate::tree::OverlayTree::new(c, base))
    }

    #[test]
    fn pass_through_is_transparent() {
        let mut p = PassThrough::new(base_surface());
        assert_eq!(
            p.invoke(&Request::ReadValue {
                path: KeyPath::parse("Software\\App"),
                name: w("Name"),
            }),
            Ok(Response::Value(ValueData::String(w("base"))))
        );
        p.invoke(&Request::WriteValue {
            path: KeyPath::parse("Software\\App"),
            name: w("Name"),
            data: ValueData::Dword(7),
        })
        .unwrap();
        // Pass-through forwards the write straight to the inner surface.
        assert_eq!(
            p.invoke(&Request::ReadValue {
                path: KeyPath::parse("Software\\App"),
                name: w("Name"),
            }),
            Ok(Response::Value(ValueData::Dword(7)))
        );
    }

    #[test]
    fn buffered_write_invisible_to_inner_until_commit() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        let app = KeyPath::parse("Software\\App");
        b.invoke(&Request::WriteValue {
            path: app.clone(),
            name: w("Name"),
            data: ValueData::Dword(42),
        })
        .unwrap();
        assert!(b.is_dirty());
        // Inner surface still holds the original base value.
        let mut inner = b.into_inner();
        assert_eq!(
            inner.invoke(&Request::ReadValue {
                path: app,
                name: w("Name"),
            }),
            Ok(Response::Value(ValueData::String(w("base"))))
        );
    }

    #[test]
    fn buffered_read_your_writes() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        let app = KeyPath::parse("Software\\App");
        b.invoke(&Request::WriteValue {
            path: app.clone(),
            name: w("Name"),
            data: ValueData::Dword(42),
        })
        .unwrap();
        // Read through the buffered surface reflects the buffered write...
        assert_eq!(
            b.invoke(&Request::ReadValue {
                path: app.clone(),
                name: w("Name"),
            }),
            Ok(Response::Value(ValueData::Dword(42)))
        );
        // ...even with a case-different name (ordinal case-insensitive).
        assert_eq!(
            b.invoke(&Request::ReadValue {
                path: app,
                name: w("NAME"),
            }),
            Ok(Response::Value(ValueData::Dword(42)))
        );
    }

    #[test]
    fn buffered_delete_value_is_read_your_writes() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        let app = KeyPath::parse("Software\\App");
        b.invoke(&Request::DeleteValue {
            path: app.clone(),
            name: w("Name"),
        })
        .unwrap();
        assert_eq!(
            b.invoke(&Request::ReadValue {
                path: app,
                name: w("Name"),
            }),
            Err(RegistryError::ValueNotFound)
        );
    }

    #[test]
    fn commit_applies_writes_to_inner() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        let app = KeyPath::parse("Software\\App");
        b.invoke(&Request::WriteValue {
            path: app.clone(),
            name: w("Name"),
            data: ValueData::Dword(42),
        })
        .unwrap();
        b.commit().unwrap();
        assert!(!b.is_dirty());
        assert_eq!(
            b.invoke(&Request::ReadValue {
                path: app,
                name: w("Name"),
            }),
            Ok(Response::Value(ValueData::Dword(42)))
        );
    }

    #[test]
    fn buffered_create_key_visible_through_buffer_only() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        let k = KeyPath::parse("Software\\New");
        b.invoke(&Request::CreateKey { path: k.clone() }).unwrap();
        assert_eq!(
            b.invoke(&Request::KeyExists { path: k.clone() }),
            Ok(Response::Exists(true))
        );
        // Ancestor existence is implied by the buffered create.
        assert_eq!(
            b.invoke(&Request::KeyExists {
                path: KeyPath::parse("Software"),
            }),
            Ok(Response::Exists(true))
        );
        b.commit().unwrap();
        assert_eq!(
            b.invoke(&Request::KeyExists { path: k }),
            Ok(Response::Exists(true))
        );
    }

    #[test]
    fn buffered_delete_key_shadows_inner() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        let app = KeyPath::parse("Software\\App");
        b.invoke(&Request::DeleteKey { path: app.clone() })
            .unwrap();
        assert_eq!(
            b.invoke(&Request::KeyExists { path: app.clone() }),
            Ok(Response::Exists(false))
        );
        assert_eq!(
            b.invoke(&Request::ReadValue {
                path: app,
                name: w("Name"),
            }),
            Err(RegistryError::KeyNotFound)
        );
    }

    #[test]
    fn buffered_enum_values_merges_over_inner() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        let app = KeyPath::parse("Software\\App");
        b.invoke(&Request::WriteValue {
            path: app.clone(),
            name: w("Alpha"),
            data: ValueData::Dword(1),
        })
        .unwrap();
        b.invoke(&Request::DeleteValue {
            path: app.clone(),
            name: w("Name"),
        })
        .unwrap();
        match b.invoke(&Request::EnumValues { path: app }).unwrap() {
            Response::Values(v) => {
                let got: Vec<String> = v.iter().map(|(n, _)| n.to_utf8().unwrap()).collect();
                assert_eq!(got, vec!["Alpha".to_string()]);
            }
            other => panic!("expected Values, got {other:?}"),
        }
    }

    #[test]
    fn buffered_enum_keys_merges_over_inner() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        b.invoke(&Request::CreateKey {
            path: KeyPath::parse("Software\\Beta"),
        })
        .unwrap();
        match b
            .invoke(&Request::EnumKeys {
                path: KeyPath::parse("Software"),
            })
            .unwrap()
        {
            Response::Names(v) => {
                let got: Vec<String> = v.iter().map(|n| n.to_utf8().unwrap()).collect();
                assert_eq!(got, vec!["App".to_string(), "Beta".to_string()]);
            }
            other => panic!("expected Names, got {other:?}"),
        }
    }

    #[test]
    fn rollback_discards_buffer() {
        let mut b = Buffered::new(base_surface(), AsciiOrdinalCasing);
        let app = KeyPath::parse("Software\\App");
        b.invoke(&Request::WriteValue {
            path: app.clone(),
            name: w("Name"),
            data: ValueData::Dword(99),
        })
        .unwrap();
        b.rollback();
        assert!(!b.is_dirty());
        assert_eq!(
            b.invoke(&Request::ReadValue {
                path: app,
                name: w("Name"),
            }),
            Ok(Response::Value(ValueData::String(w("base"))))
        );
    }
}
