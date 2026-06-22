// Copyright (c) Microsoft Corporation.

//! The live ("direct") registry provider (D20).
//!
//! [`LiveRegistry`] reads and writes the real Windows registry through the safe
//! primitives in `windows-platform-isolation-sys`. This module is Windows-only
//! and itself contains **no `unsafe`** (D13): every raw call is confined to the
//! leaf crate. It maps the crate's path/value vocabulary onto `RegKey` handles
//! and decodes raw OS bytes with the shared [`decode_value`](crate::serial)
//! codec, so live reads and loaded artifacts produce identical [`ValueData`];
//! writes use the inverse [`encode_value`](crate::serial) codec.
//!
//! Paths are rooted at a canonical hive name (e.g. `HKEY_LOCAL_MACHINE`), the
//! same names [`Session`](crate::Session) vends, so a path built from a session
//! resolves directly against the OS.

use windows_platform_isolation_sys::{RegError, RegKey};

use crate::error::{RegistryError, Result};
use crate::path::KeyPath;
use crate::serial::{decode_value, encode_value};
use crate::surface::{Request, Response, Surface};
use crate::tree::{Hive, ValueData};
use crate::{OrdinalCasing, Utf16};

/// A provider that operates directly on the live OS registry (D20).
#[derive(Debug, Default)]
pub struct LiveRegistry {
    _private: (),
}

impl LiveRegistry {
    /// Create a live registry provider.
    #[must_use]
    pub fn new() -> Self {
        Self { _private: () }
    }

    /// Whether the key at `path` exists.
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::Os`] on any Win32 failure other than a missing
    /// key (which yields `Ok(false)`).
    pub fn key_exists(&self, path: &KeyPath) -> Result<bool> {
        match self.open(path, false) {
            Ok(_) => Ok(true),
            Err(RegistryError::KeyNotFound) => Ok(false),
            Err(e) => Err(e),
        }
    }

    /// Read the value `name` under `path`.
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::KeyNotFound`] / [`RegistryError::ValueNotFound`]
    /// when the key or value is absent, or [`RegistryError::Os`] on any other
    /// Win32 failure.
    pub fn read_value(&self, path: &KeyPath, name: &Utf16) -> Result<ValueData> {
        let key = self.open(path, false)?;
        let (type_code, bytes) = key.query_value(&nul_terminated(name)).map_err(map_value_err)?;
        decode_value(type_code, bytes)
    }

    /// Enumerate the immediate subkey names of `path`, in the registry's native
    /// enumeration order.
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::KeyNotFound`] when the key is absent, or
    /// [`RegistryError::Os`] on any other Win32 failure.
    pub fn enum_keys(&self, path: &KeyPath) -> Result<Vec<Utf16>> {
        let key = self.open(path, false)?;
        let names = key.enum_subkey_names().map_err(map_key_err)?;
        Ok(names.into_iter().map(Utf16::from_units).collect())
    }

    /// Enumerate the values of `path` as `(name, data)` pairs.
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::KeyNotFound`] when the key is absent,
    /// [`RegistryError::Os`] on any other Win32 failure, or a decode error if a
    /// value's bytes are malformed for its type.
    pub fn enum_values(&self, path: &KeyPath) -> Result<Vec<(Utf16, ValueData)>> {
        let key = self.open(path, false)?;
        let raw = key.enum_values().map_err(map_value_err)?;
        raw.into_iter()
            .map(|(name, type_code, bytes)| {
                Ok((Utf16::from_units(name), decode_value(type_code, bytes)?))
            })
            .collect()
    }

    /// Open the OS key addressed by `path`. The first component selects a
    /// predefined hive root; the remainder is the subkey path.
    fn open(&self, path: &KeyPath, writable: bool) -> Result<RegKey> {
        let (hive, rest) = path
            .components()
            .split_first()
            .ok_or(RegistryError::KeyNotFound)?;
        let root = resolve_root(hive)?;
        root.open_subkey(&subkey_units(rest), writable)
            .map_err(map_key_err)
    }

    /// Create the key at `path` (and any missing ancestors), returning the open
    /// handle. The first component selects a predefined hive root.
    fn create(&self, path: &KeyPath) -> Result<RegKey> {
        let (hive, rest) = path
            .components()
            .split_first()
            .ok_or(RegistryError::KeyNotFound)?;
        let root = resolve_root(hive)?;
        root.create_subkey(&subkey_units(rest)).map_err(map_key_err)
    }

    /// Create the key at `path`, including any missing ancestors (D20).
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::KeyNotFound`] when `path` has no hive component
    /// or names an unknown hive, or [`RegistryError::Os`] on any Win32 failure.
    pub fn create_key(&self, path: &KeyPath) -> Result<()> {
        self.create(path).map(|_| ())
    }

    /// Delete the key at `path` together with all of its descendants (D20).
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::KeyNotFound`] when `path` names no removable
    /// subkey (e.g. a bare hive) or the key is absent, or
    /// [`RegistryError::Os`] on any other Win32 failure.
    pub fn delete_key(&self, path: &KeyPath) -> Result<()> {
        let (hive, rest) = path
            .components()
            .split_first()
            .ok_or(RegistryError::KeyNotFound)?;
        // A bare hive (no subkey) is not removable.
        let (last, parents) = rest.split_last().ok_or(RegistryError::KeyNotFound)?;
        let root = resolve_root(hive)?;
        let parent = root
            .open_subkey(&subkey_units(parents), true)
            .map_err(map_key_err)?;
        parent
            .delete_subkey_tree(&nul_terminated(last))
            .map_err(map_key_err)
    }

    /// Write `data` to value `name` under `path`, creating the key on demand
    /// (create-on-write, matching the in-memory surface) (D20).
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::KeyNotFound`] when `path` has no hive component
    /// or names an unknown hive, or [`RegistryError::Os`] on any Win32 failure.
    pub fn write_value(&self, path: &KeyPath, name: &Utf16, data: &ValueData) -> Result<()> {
        let key = self.create(path)?;
        let (type_code, bytes) = encode_value(data);
        key.set_value(&nul_terminated(name), type_code, &bytes)
            .map_err(map_value_err)
    }

    /// Delete the value `name` under `path` (D20).
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::KeyNotFound`] / [`RegistryError::ValueNotFound`]
    /// when the key or value is absent, or [`RegistryError::Os`] on any other
    /// Win32 failure.
    pub fn delete_value(&self, path: &KeyPath, name: &Utf16) -> Result<()> {
        let key = self.open(path, true)?;
        key.delete_value(&nul_terminated(name))
            .map_err(map_value_err)
    }

    /// Capture the live subtree rooted at `path` into an in-memory base
    /// [`Hive`] (D20/D21).
    ///
    /// The captured keys and values are placed under `path` itself (its first
    /// component is a canonical hive name), so the result serializes with
    /// [`save_registry_hive`](crate::save_registry_hive) and reloads with
    /// [`load_registry_hive`](crate::load_registry_hive) without remapping. The
    /// root key is recorded even when it is empty.
    ///
    /// # Errors
    ///
    /// Returns [`RegistryError::KeyNotFound`] when `path` is absent, or
    /// [`RegistryError::Os`] on any other Win32 failure.
    pub fn capture<C: OrdinalCasing>(&self, casing: &C, path: &KeyPath) -> Result<Hive> {
        let mut hive = Hive::new();
        hive.insert_key(casing, path);
        self.capture_into(casing, &mut hive, path)?;
        Ok(hive)
    }

    /// Recurse `path`'s values and subkeys into `hive` (depth-first).
    fn capture_into<C: OrdinalCasing>(
        &self,
        casing: &C,
        hive: &mut Hive,
        path: &KeyPath,
    ) -> Result<()> {
        for (name, data) in self.enum_values(path)? {
            hive.insert_value(casing, path, name, data);
        }
        for sub in self.enum_keys(path)? {
            let child = path.child(sub);
            hive.insert_key(casing, &child);
            self.capture_into(casing, hive, &child)?;
        }
        Ok(())
    }
}

/// The live provider implements the full eight-verb [`Surface`] (D20): reads use
/// the inherent reader methods, writes the inherent mutators. This makes
/// `LiveRegistry` a drop-in [`Surface`] for the [`Registry`](crate::Registry)
/// facade, exactly like the in-memory `TreeSurface`.
impl Surface for LiveRegistry {
    fn invoke(&mut self, req: &Request) -> Result<Response> {
        match req {
            Request::KeyExists { path } => self.key_exists(path).map(Response::Exists),
            Request::CreateKey { path } => self.create_key(path).map(|()| Response::Unit),
            Request::DeleteKey { path } => self.delete_key(path).map(|()| Response::Unit),
            Request::ReadValue { path, name } => self.read_value(path, name).map(Response::Value),
            Request::WriteValue { path, name, data } => {
                self.write_value(path, name, data).map(|()| Response::Unit)
            }
            Request::DeleteValue { path, name } => {
                self.delete_value(path, name).map(|()| Response::Unit)
            }
            Request::EnumKeys { path } => self.enum_keys(path).map(Response::Names),
            Request::EnumValues { path } => self.enum_values(path).map(Response::Values),
        }
    }
}

/// Resolve a canonical hive-name component to its predefined (non-owning) root.
fn resolve_root(name: &Utf16) -> Result<RegKey> {
    // Hive names are ASCII; lossy decoding is only used for this comparison.
    let upper = String::from_utf16_lossy(name.as_units()).to_ascii_uppercase();
    let root = match upper.as_str() {
        "HKEY_LOCAL_MACHINE" => RegKey::local_machine(),
        "HKEY_CURRENT_USER" => RegKey::current_user(),
        "HKEY_USERS" => RegKey::users(),
        "HKEY_CLASSES_ROOT" => RegKey::classes_root(),
        "HKEY_CURRENT_CONFIG" => RegKey::current_config(),
        // A hive we cannot open behaves like a missing key.
        _ => return Err(RegistryError::KeyNotFound),
    };
    Ok(root)
}

/// Join subkey components with `\` and append a NUL terminator. An empty slice
/// yields a lone NUL, which opens (a duplicate of) the root itself.
fn subkey_units(components: &[Utf16]) -> Vec<u16> {
    let mut units = Vec::new();
    for (i, component) in components.iter().enumerate() {
        if i > 0 {
            units.push(u16::from(b'\\'));
        }
        units.extend_from_slice(component.as_units());
    }
    units.push(0);
    units
}

/// NUL-terminate a value name for the FFI boundary.
fn nul_terminated(name: &Utf16) -> Vec<u16> {
    let mut units = name.as_units().to_vec();
    units.push(0);
    units
}

/// Map a key-context Win32 error: "not found" becomes [`RegistryError::KeyNotFound`].
fn map_key_err(e: RegError) -> RegistryError {
    if e.is_not_found() {
        RegistryError::KeyNotFound
    } else {
        RegistryError::Os(e.code())
    }
}

/// Map a value-context Win32 error: "not found" becomes [`RegistryError::ValueNotFound`].
fn map_value_err(e: RegError) -> RegistryError {
    if e.is_not_found() {
        RegistryError::ValueNotFound
    } else {
        RegistryError::Os(e.code())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // A key present on every supported Windows install.
    const CURRENT_VERSION: &str = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

    #[test]
    fn existing_key_is_reported_present() {
        let reg = LiveRegistry::new();
        assert!(reg.key_exists(&KeyPath::parse(CURRENT_VERSION)).unwrap());
    }

    #[test]
    fn missing_key_is_reported_absent() {
        let reg = LiveRegistry::new();
        let path = KeyPath::parse(
            "HKEY_CURRENT_USER\\Software\\windows-platform-isolation\\does-not-exist-xyz",
        );
        assert!(!reg.key_exists(&path).unwrap());
    }

    #[test]
    fn unknown_hive_is_absent_not_error() {
        let reg = LiveRegistry::new();
        assert!(!reg.key_exists(&KeyPath::parse("HKEY_BOGUS\\whatever")).unwrap());
    }

    #[test]
    fn enum_values_returns_known_value() {
        let reg = LiveRegistry::new();
        let values = reg.enum_values(&KeyPath::parse(CURRENT_VERSION)).unwrap();
        assert!(!values.is_empty(), "CurrentVersion should have values");
        // ProductName is a stable REG_SZ on every Windows edition.
        let product = values
            .iter()
            .find(|(name, _)| *name == Utf16::from_utf8("ProductName"));
        assert!(
            matches!(product, Some((_, ValueData::String(_)))),
            "expected ProductName as a REG_SZ string"
        );
    }

    #[test]
    fn read_value_decodes_string() {
        let reg = LiveRegistry::new();
        let data = reg
            .read_value(&KeyPath::parse(CURRENT_VERSION), &Utf16::from_utf8("ProductName"))
            .unwrap();
        assert!(matches!(data, ValueData::String(_)));
    }

    #[test]
    fn enum_keys_returns_subkeys() {
        let reg = LiveRegistry::new();
        let keys = reg
            .enum_keys(&KeyPath::parse("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft"))
            .unwrap();
        assert!(!keys.is_empty(), "HKLM\\SOFTWARE\\Microsoft should have subkeys");
    }

    #[test]
    fn read_missing_value_is_value_not_found() {
        let reg = LiveRegistry::new();
        let err = reg
            .read_value(&KeyPath::parse(CURRENT_VERSION), &Utf16::from_utf8("NoSuchValueXyz"))
            .expect_err("missing value should error");
        assert_eq!(err, RegistryError::ValueNotFound);
    }

    // --- Write path (M5-3) -------------------------------------------------
    //
    // Writes run against a deterministic scratch subtree under HKCU (no admin
    // required). Each test uses a distinct leaf for parallel safety, and a RAII
    // guard removes the subtree before and after the test.

    const TEST_ROOT: &str = "HKEY_CURRENT_USER\\Software\\windows-platform-isolation-tests";

    /// A scratch key under HKCU that is created on construction and removed on
    /// drop, leaving the registry clean regardless of test outcome.
    struct ScratchKey {
        reg: LiveRegistry,
        path: KeyPath,
    }

    impl ScratchKey {
        fn new(leaf: &str) -> Self {
            let reg = LiveRegistry::new();
            let path = KeyPath::parse(&format!("{TEST_ROOT}\\{leaf}"));
            // Clean up any leftovers from a prior aborted run, then create fresh.
            let _ = reg.delete_key(&path);
            reg.create_key(&path).expect("create scratch key");
            Self { reg, path }
        }
    }

    impl Drop for ScratchKey {
        fn drop(&mut self) {
            let _ = self.reg.delete_key(&self.path);
        }
    }

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    #[test]
    fn create_key_then_key_exists() {
        let scratch = ScratchKey::new("create_key_then_key_exists");
        assert!(scratch.reg.key_exists(&scratch.path).unwrap());
    }

    #[test]
    fn delete_key_removes_it() {
        let reg = LiveRegistry::new();
        let path = KeyPath::parse(&format!("{TEST_ROOT}\\delete_key_removes_it"));
        let _ = reg.delete_key(&path);
        reg.create_key(&path).unwrap();
        assert!(reg.key_exists(&path).unwrap());
        reg.delete_key(&path).unwrap();
        assert!(!reg.key_exists(&path).unwrap());
    }

    #[test]
    fn string_value_round_trips() {
        let scratch = ScratchKey::new("string_value_round_trips");
        let value = ValueData::String(w("hello world"));
        scratch.reg.write_value(&scratch.path, &w("greeting"), &value).unwrap();
        let read = scratch.reg.read_value(&scratch.path, &w("greeting")).unwrap();
        assert_eq!(read, value);
    }

    #[test]
    fn expand_string_value_round_trips() {
        let scratch = ScratchKey::new("expand_string_value_round_trips");
        let value = ValueData::ExpandString(w("%PATH%"));
        scratch.reg.write_value(&scratch.path, &w("p"), &value).unwrap();
        assert_eq!(scratch.reg.read_value(&scratch.path, &w("p")).unwrap(), value);
    }

    #[test]
    fn multi_string_value_round_trips() {
        let scratch = ScratchKey::new("multi_string_value_round_trips");
        let value = ValueData::MultiString(vec![w("one"), w("two"), w("three")]);
        scratch.reg.write_value(&scratch.path, &w("list"), &value).unwrap();
        assert_eq!(scratch.reg.read_value(&scratch.path, &w("list")).unwrap(), value);
    }

    #[test]
    fn empty_multi_string_value_round_trips() {
        let scratch = ScratchKey::new("empty_multi_string_value_round_trips");
        let value = ValueData::MultiString(Vec::new());
        scratch.reg.write_value(&scratch.path, &w("empty"), &value).unwrap();
        assert_eq!(scratch.reg.read_value(&scratch.path, &w("empty")).unwrap(), value);
    }

    #[test]
    fn dword_value_round_trips() {
        let scratch = ScratchKey::new("dword_value_round_trips");
        let value = ValueData::Dword(0xDEAD_BEEF);
        scratch.reg.write_value(&scratch.path, &w("n"), &value).unwrap();
        assert_eq!(scratch.reg.read_value(&scratch.path, &w("n")).unwrap(), value);
    }

    #[test]
    fn qword_value_round_trips() {
        let scratch = ScratchKey::new("qword_value_round_trips");
        let value = ValueData::Qword(0x0123_4567_89AB_CDEF);
        scratch.reg.write_value(&scratch.path, &w("q"), &value).unwrap();
        assert_eq!(scratch.reg.read_value(&scratch.path, &w("q")).unwrap(), value);
    }

    #[test]
    fn binary_value_round_trips() {
        let scratch = ScratchKey::new("binary_value_round_trips");
        let value = ValueData::Binary(vec![0, 1, 2, 254, 255]);
        scratch.reg.write_value(&scratch.path, &w("b"), &value).unwrap();
        assert_eq!(scratch.reg.read_value(&scratch.path, &w("b")).unwrap(), value);
    }

    #[test]
    fn overwrite_replaces_value() {
        let scratch = ScratchKey::new("overwrite_replaces_value");
        scratch.reg.write_value(&scratch.path, &w("x"), &ValueData::Dword(1)).unwrap();
        scratch.reg.write_value(&scratch.path, &w("x"), &ValueData::Dword(2)).unwrap();
        assert_eq!(
            scratch.reg.read_value(&scratch.path, &w("x")).unwrap(),
            ValueData::Dword(2)
        );
    }

    #[test]
    fn delete_value_removes_it() {
        let scratch = ScratchKey::new("delete_value_removes_it");
        scratch.reg.write_value(&scratch.path, &w("doomed"), &ValueData::Dword(7)).unwrap();
        scratch.reg.delete_value(&scratch.path, &w("doomed")).unwrap();
        let err = scratch
            .reg
            .read_value(&scratch.path, &w("doomed"))
            .expect_err("deleted value should be gone");
        assert_eq!(err, RegistryError::ValueNotFound);
    }

    #[test]
    fn enum_values_lists_written_values() {
        let scratch = ScratchKey::new("enum_values_lists_written_values");
        scratch.reg.write_value(&scratch.path, &w("a"), &ValueData::Dword(1)).unwrap();
        scratch.reg.write_value(&scratch.path, &w("b"), &ValueData::String(w("two"))).unwrap();
        let values = scratch.reg.enum_values(&scratch.path).unwrap();
        assert!(values.iter().any(|(n, d)| *n == w("a") && *d == ValueData::Dword(1)));
        assert!(values.iter().any(|(n, d)| *n == w("b") && *d == ValueData::String(w("two"))));
    }

    #[test]
    fn enum_keys_lists_written_subkeys() {
        let scratch = ScratchKey::new("enum_keys_lists_written_subkeys");
        scratch.reg.create_key(&scratch.path.child(w("sub1"))).unwrap();
        scratch.reg.create_key(&scratch.path.child(w("sub2"))).unwrap();
        let keys = scratch.reg.enum_keys(&scratch.path).unwrap();
        assert!(keys.contains(&w("sub1")));
        assert!(keys.contains(&w("sub2")));
    }

    #[test]
    fn via_registry_surface_round_trips() {
        // Exercise LiveRegistry through the typed Registry facade (Surface impl).
        let scratch = ScratchKey::new("via_registry_surface_round_trips");
        let mut registry = crate::Registry::new(LiveRegistry::new());
        registry
            .set_value(&scratch.path, w("k"), ValueData::Qword(42))
            .unwrap();
        assert_eq!(
            registry.get_value(&scratch.path, &w("k")).unwrap(),
            ValueData::Qword(42)
        );
        assert!(registry.key_exists(&scratch.path).unwrap());
    }
}
