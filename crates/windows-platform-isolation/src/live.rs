// Copyright (c) Microsoft Corporation.

//! The live ("direct") registry provider (D20).
//!
//! [`LiveRegistry`] reads (and, from M5-3, writes) the real Windows registry
//! through the safe primitives in `windows-platform-isolation-sys`. This module
//! is Windows-only and itself contains **no `unsafe`** (D13): every raw call is
//! confined to the leaf crate. It maps the crate's path/value vocabulary onto
//! `RegKey` handles and decodes raw OS bytes with the shared
//! [`decode_value`](crate::serial) codec, so live reads and loaded artifacts
//! produce identical [`ValueData`].
//!
//! Paths are rooted at a canonical hive name (e.g. `HKEY_LOCAL_MACHINE`), the
//! same names [`Session`](crate::Session) vends, so a path built from a session
//! resolves directly against the OS.

use windows_platform_isolation_sys::{RegError, RegKey};

use crate::error::{RegistryError, Result};
use crate::path::KeyPath;
use crate::serial::decode_value;
use crate::tree::ValueData;
use crate::Utf16;

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
}
