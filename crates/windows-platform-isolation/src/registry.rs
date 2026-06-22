// Copyright (c) Microsoft Corporation.

//! The typed registry facade (D11) and session-vended roots.
//!
//! [`Registry`] borrows the *shape* of `windows-registry` and `std::fs` — typed
//! `get_*` / `set_*` accessors and key/value iterators — without taking a
//! dependency on either. Every method lowers to an M1-4 [`Request`] and speaks
//! to whatever [`Surface`] sits underneath (a tree provider, a buffered
//! decorator, a journaling layer — the facade neither knows nor cares).
//!
//! Roots are **vended by a live [`Session`]** (D11): there are deliberately no
//! global `LOCAL_MACHINE` / `CURRENT_USER` constants. A root only exists in the
//! context of a session that hands it out.

use crate::error::{RegistryError, Result};
use crate::path::KeyPath;
use crate::surface::{Request, Response, Surface};
use crate::tree::{ValueData, ValueType};
use crate::Utf16;

/// Well-known registry hive identities. Obtained from a [`Session`]; never
/// exposed as global constants (D11).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WellKnownRoot {
    /// `HKEY_LOCAL_MACHINE`.
    LocalMachine,
    /// `HKEY_CURRENT_USER`.
    CurrentUser,
    /// `HKEY_USERS`.
    Users,
    /// `HKEY_CLASSES_ROOT`.
    ClassesRoot,
    /// `HKEY_CURRENT_CONFIG`.
    CurrentConfig,
}

impl WellKnownRoot {
    /// The canonical path name this root resolves to.
    #[must_use]
    pub fn canonical_name(self) -> &'static str {
        match self {
            Self::LocalMachine => "HKEY_LOCAL_MACHINE",
            Self::CurrentUser => "HKEY_CURRENT_USER",
            Self::Users => "HKEY_USERS",
            Self::ClassesRoot => "HKEY_CLASSES_ROOT",
            Self::CurrentConfig => "HKEY_CURRENT_CONFIG",
        }
    }
}

/// A live isolation session that vends hive roots (D11). Roots come from a
/// session instance, never from global constants, so an isolation scope is
/// always explicit.
#[derive(Clone, Copy, Debug, Default)]
pub struct Session;

impl Session {
    /// Open a new session.
    #[must_use]
    pub fn new() -> Self {
        Self
    }

    /// The path of a well-known root, vended by this session.
    #[must_use]
    pub fn root(&self, which: WellKnownRoot) -> KeyPath {
        KeyPath::parse(which.canonical_name())
    }

    /// Shorthand for [`WellKnownRoot::LocalMachine`].
    #[must_use]
    pub fn local_machine(&self) -> KeyPath {
        self.root(WellKnownRoot::LocalMachine)
    }

    /// Shorthand for [`WellKnownRoot::CurrentUser`].
    #[must_use]
    pub fn current_user(&self) -> KeyPath {
        self.root(WellKnownRoot::CurrentUser)
    }
}

/// A typed registry facade over any [`Surface`] (D11). Construct one around a
/// surface, then use the typed accessors; each lowers to a [`Request`].
pub struct Registry<S: Surface> {
    surface: S,
}

impl<S: Surface> Registry<S> {
    /// Wrap a surface in the typed facade.
    pub fn new(surface: S) -> Self {
        Self { surface }
    }

    /// Borrow the underlying surface.
    #[must_use]
    pub fn surface(&self) -> &S {
        &self.surface
    }

    /// Mutably borrow the underlying surface.
    pub fn surface_mut(&mut self) -> &mut S {
        &mut self.surface
    }

    /// Recover the underlying surface.
    pub fn into_surface(self) -> S {
        self.surface
    }

    /// Create a key (and missing ancestors).
    pub fn create_key(&mut self, path: &KeyPath) -> Result<()> {
        self.surface
            .invoke(&Request::CreateKey { path: path.clone() })?;
        Ok(())
    }

    /// Delete a key and its subtree.
    pub fn delete_key(&mut self, path: &KeyPath) -> Result<()> {
        self.surface
            .invoke(&Request::DeleteKey { path: path.clone() })?;
        Ok(())
    }

    /// Whether a key exists.
    pub fn key_exists(&mut self, path: &KeyPath) -> Result<bool> {
        match self.surface.invoke(&Request::KeyExists { path: path.clone() })? {
            Response::Exists(b) => Ok(b),
            other => contract_violation("Exists", &other),
        }
    }

    /// Read a value without type discrimination.
    pub fn get_value(&mut self, path: &KeyPath, name: &Utf16) -> Result<ValueData> {
        match self.surface.invoke(&Request::ReadValue {
            path: path.clone(),
            name: name.clone(),
        })? {
            Response::Value(d) => Ok(d),
            other => contract_violation("Value", &other),
        }
    }

    /// Set a value of any type.
    pub fn set_value(&mut self, path: &KeyPath, name: Utf16, data: ValueData) -> Result<()> {
        self.surface.invoke(&Request::WriteValue {
            path: path.clone(),
            name,
            data,
        })?;
        Ok(())
    }

    /// Delete a value.
    pub fn delete_value(&mut self, path: &KeyPath, name: &Utf16) -> Result<()> {
        self.surface.invoke(&Request::DeleteValue {
            path: path.clone(),
            name: name.clone(),
        })?;
        Ok(())
    }

    /// Enumerate immediate subkey names (ordinal-ordered).
    pub fn keys(&mut self, path: &KeyPath) -> Result<Vec<Utf16>> {
        match self.surface.invoke(&Request::EnumKeys { path: path.clone() })? {
            Response::Names(v) => Ok(v),
            other => contract_violation("Names", &other),
        }
    }

    /// Enumerate values (ordinal-ordered by name).
    pub fn values(&mut self, path: &KeyPath) -> Result<Vec<(Utf16, ValueData)>> {
        match self.surface.invoke(&Request::EnumValues { path: path.clone() })? {
            Response::Values(v) => Ok(v),
            other => contract_violation("Values", &other),
        }
    }

    /// Read a `REG_SZ` string.
    pub fn get_string(&mut self, path: &KeyPath, name: &Utf16) -> Result<Utf16> {
        match self.get_value(path, name)? {
            ValueData::String(s) => Ok(s),
            other => Err(type_mismatch(ValueType::String, &other)),
        }
    }

    /// Write a `REG_SZ` string.
    pub fn set_string(&mut self, path: &KeyPath, name: Utf16, value: Utf16) -> Result<()> {
        self.set_value(path, name, ValueData::String(value))
    }

    /// Read a `REG_EXPAND_SZ` string.
    pub fn get_expand_string(&mut self, path: &KeyPath, name: &Utf16) -> Result<Utf16> {
        match self.get_value(path, name)? {
            ValueData::ExpandString(s) => Ok(s),
            other => Err(type_mismatch(ValueType::ExpandString, &other)),
        }
    }

    /// Write a `REG_EXPAND_SZ` string.
    pub fn set_expand_string(&mut self, path: &KeyPath, name: Utf16, value: Utf16) -> Result<()> {
        self.set_value(path, name, ValueData::ExpandString(value))
    }

    /// Read a `REG_MULTI_SZ` string list.
    pub fn get_multi_string(&mut self, path: &KeyPath, name: &Utf16) -> Result<Vec<Utf16>> {
        match self.get_value(path, name)? {
            ValueData::MultiString(v) => Ok(v),
            other => Err(type_mismatch(ValueType::MultiString, &other)),
        }
    }

    /// Write a `REG_MULTI_SZ` string list.
    pub fn set_multi_string(
        &mut self,
        path: &KeyPath,
        name: Utf16,
        value: Vec<Utf16>,
    ) -> Result<()> {
        self.set_value(path, name, ValueData::MultiString(value))
    }

    /// Read a `REG_DWORD`.
    pub fn get_u32(&mut self, path: &KeyPath, name: &Utf16) -> Result<u32> {
        match self.get_value(path, name)? {
            ValueData::Dword(n) => Ok(n),
            other => Err(type_mismatch(ValueType::Dword, &other)),
        }
    }

    /// Write a `REG_DWORD`.
    pub fn set_u32(&mut self, path: &KeyPath, name: Utf16, value: u32) -> Result<()> {
        self.set_value(path, name, ValueData::Dword(value))
    }

    /// Read a `REG_QWORD`.
    pub fn get_u64(&mut self, path: &KeyPath, name: &Utf16) -> Result<u64> {
        match self.get_value(path, name)? {
            ValueData::Qword(n) => Ok(n),
            other => Err(type_mismatch(ValueType::Qword, &other)),
        }
    }

    /// Write a `REG_QWORD`.
    pub fn set_u64(&mut self, path: &KeyPath, name: Utf16, value: u64) -> Result<()> {
        self.set_value(path, name, ValueData::Qword(value))
    }

    /// Read a `REG_BINARY` blob.
    pub fn get_bytes(&mut self, path: &KeyPath, name: &Utf16) -> Result<Vec<u8>> {
        match self.get_value(path, name)? {
            ValueData::Binary(b) => Ok(b),
            other => Err(type_mismatch(ValueType::Binary, &other)),
        }
    }

    /// Write a `REG_BINARY` blob.
    pub fn set_bytes(&mut self, path: &KeyPath, name: Utf16, value: Vec<u8>) -> Result<()> {
        self.set_value(path, name, ValueData::Binary(value))
    }
}

/// Production stacks vend the mandated Win32 ordinal casing (D6/D8). This is the
/// default casing for non-test builds; unit tests inject the `testing`
/// `AsciiOrdinalCasing` reference instead (M3-2).
#[cfg(windows)]
impl Registry<crate::surface::TreeSurface<crate::Win32OrdinalCasing>> {
    /// Build an in-memory registry facade over `base`, keyed with the mandated
    /// production ordinal casing (`Win32OrdinalCasing`). The base hive must have
    /// been populated with the same casing.
    #[must_use]
    pub fn in_memory(base: crate::tree::Hive) -> Self {
        let tree = crate::tree::OverlayTree::new(crate::Win32OrdinalCasing, base);
        Registry::new(crate::surface::TreeSurface::new(tree))
    }
}

fn type_mismatch(expected: ValueType, found: &ValueData) -> RegistryError {
    RegistryError::TypeMismatch {
        expected,
        found: found.value_type(),
    }
}

/// A surface returned a response of the wrong shape for the request it was
/// given. That is a `Surface` implementation bug, not an OS condition, so it is
/// a programming error rather than a recoverable [`RegistryError`].
#[cold]
fn contract_violation(expected: &str, got: &Response) -> ! {
    panic!("surface contract violation: expected {expected} response, got {got:?}")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::surface::TreeSurface;
    use crate::tree::{Hive, OverlayTree};
    use windows_text::AsciiOrdinalCasing;

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    /// A test double that records every request before delegating.
    struct Recording<S: Surface> {
        inner: S,
        log: Vec<Request>,
    }

    impl<S: Surface> Recording<S> {
        fn new(inner: S) -> Self {
            Self {
                inner,
                log: Vec::new(),
            }
        }
    }

    impl<S: Surface> Surface for Recording<S> {
        fn invoke(&mut self, req: &Request) -> Result<Response> {
            self.log.push(req.clone());
            self.inner.invoke(req)
        }
    }

    fn empty_registry() -> Registry<Recording<TreeSurface<AsciiOrdinalCasing>>> {
        let tree = OverlayTree::new(AsciiOrdinalCasing, Hive::new());
        Registry::new(Recording::new(TreeSurface::new(tree)))
    }

    #[test]
    fn session_vends_distinct_roots() {
        let s = Session::new();
        assert_eq!(s.local_machine(), KeyPath::parse("HKEY_LOCAL_MACHINE"));
        assert_eq!(s.current_user(), KeyPath::parse("HKEY_CURRENT_USER"));
        assert_ne!(s.local_machine(), s.current_user());
    }

    #[test]
    fn set_then_get_string_round_trips() {
        let mut reg = empty_registry();
        let app = KeyPath::parse("Software\\App");
        reg.set_string(&app, w("Name"), w("hello")).unwrap();
        assert_eq!(reg.get_string(&app, &w("Name")).unwrap(), w("hello"));
    }

    #[test]
    fn typed_accessors_lower_to_expected_request_sequence() {
        let mut reg = empty_registry();
        let app = KeyPath::parse("Software\\App");
        reg.set_u32(&app, w("Count"), 5).unwrap();
        let _ = reg.get_u32(&app, &w("Count")).unwrap();
        assert_eq!(
            reg.surface().log,
            vec![
                Request::WriteValue {
                    path: app.clone(),
                    name: w("Count"),
                    data: ValueData::Dword(5),
                },
                Request::ReadValue {
                    path: app,
                    name: w("Count"),
                },
            ]
        );
    }

    #[test]
    fn type_mismatch_is_reported() {
        let mut reg = empty_registry();
        let app = KeyPath::parse("Software\\App");
        reg.set_string(&app, w("Name"), w("hello")).unwrap();
        assert_eq!(
            reg.get_u32(&app, &w("Name")),
            Err(RegistryError::TypeMismatch {
                expected: ValueType::Dword,
                found: ValueType::String,
            })
        );
    }

    #[test]
    fn create_key_and_existence() {
        let mut reg = empty_registry();
        let k = KeyPath::parse("Software\\New");
        assert!(!reg.key_exists(&k).unwrap());
        reg.create_key(&k).unwrap();
        assert!(reg.key_exists(&k).unwrap());
    }

    #[test]
    fn keys_and_values_iterators() {
        let mut reg = empty_registry();
        let sw = KeyPath::parse("Software");
        reg.create_key(&KeyPath::parse("Software\\Beta")).unwrap();
        reg.create_key(&KeyPath::parse("Software\\Alpha")).unwrap();
        reg.set_u64(&sw, w("Stamp"), 7).unwrap();

        let keys: Vec<String> = reg
            .keys(&sw)
            .unwrap()
            .iter()
            .map(|n| n.to_utf8().unwrap())
            .collect();
        assert_eq!(keys, vec!["Alpha".to_string(), "Beta".to_string()]);

        let values: Vec<String> = reg
            .values(&sw)
            .unwrap()
            .iter()
            .map(|(n, _)| n.to_utf8().unwrap())
            .collect();
        assert_eq!(values, vec!["Stamp".to_string()]);
    }

    #[test]
    fn all_typed_variants_round_trip() {
        let mut reg = empty_registry();
        let k = KeyPath::parse("K");
        reg.set_expand_string(&k, w("E"), w("%PATH%")).unwrap();
        reg.set_multi_string(&k, w("M"), vec![w("a"), w("b")])
            .unwrap();
        reg.set_u64(&k, w("Q"), 1 << 40).unwrap();
        reg.set_bytes(&k, w("B"), vec![1, 2, 3]).unwrap();

        assert_eq!(reg.get_expand_string(&k, &w("E")).unwrap(), w("%PATH%"));
        assert_eq!(
            reg.get_multi_string(&k, &w("M")).unwrap(),
            vec![w("a"), w("b")]
        );
        assert_eq!(reg.get_u64(&k, &w("Q")).unwrap(), 1 << 40);
        assert_eq!(reg.get_bytes(&k, &w("B")).unwrap(), vec![1, 2, 3]);
    }

    #[test]
    fn delete_value_then_get_is_value_not_found() {
        let mut reg = empty_registry();
        let app = KeyPath::parse("Software\\App");
        reg.set_string(&app, w("Name"), w("hello")).unwrap();
        reg.delete_value(&app, &w("Name")).unwrap();
        assert_eq!(
            reg.get_string(&app, &w("Name")),
            Err(RegistryError::ValueNotFound)
        );
    }
}
