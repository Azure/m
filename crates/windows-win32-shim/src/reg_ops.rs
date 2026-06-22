// Copyright (c) Microsoft Corporation.

//! The safe, surface-generic registry core (SHIM-D10).
//!
//! Every registry C ABI entry point ([`crate::mwinreg`]) marshals its raw
//! pointers into Rust values and then delegates to a function here. These
//! functions are pure safe Rust, generic over the underlying
//! [`Surface`](windows_platform_isolation::Surface): they operate on a
//! [`Registry`] facade plus the process [`HandleTable`], so they can be driven
//! against the live OS registry (the default session backing) or an in-memory
//! stack (the integration tests) without change.
//!
//! ## Handle model (SHIM-D10)
//!
//! Unlike the C++ shim — which derefs an `HKEY` to a live key *object* and
//! issues operations relative to it — this crate interns an **absolute
//! [`KeyPath`]** behind each minted `HKEY`. A predefined `HKEY` resolves to its
//! well-known root path; a minted `HKEY` resolves to its interned path. A
//! subkey operation [`join`](join)s the resolved base path with the caller's
//! subkey components and addresses the facade by that absolute path. This suits
//! the path-addressed `windows-platform-isolation` facade and keeps the handle
//! table free of borrowed surface state.

use windows_platform_isolation::{KeyPath, Registry, Surface, Utf16, WellKnownRoot};
use windows_sys::Win32::Foundation::{
    ERROR_FILE_NOT_FOUND, ERROR_INVALID_HANDLE, ERROR_MORE_DATA, ERROR_SUCCESS,
};

use crate::error_map::{Lstatus, registry_error_to_lstatus};
use crate::handle_table::{HandlePayload, HandleTable, RawHandle, predefined_root};
use crate::value_codec;

/// Resolve an `HKEY` value to the absolute [`KeyPath`] it names.
///
/// Predefined `HKEY`s resolve to their well-known root path; minted handles
/// resolve to their interned [`KeyPath`]. Returns `ERROR_INVALID_HANDLE` for a
/// value that is neither predefined nor a live interned registry-key handle.
fn resolve_base(handles: &HandleTable, hkey: RawHandle) -> Result<KeyPath, Lstatus> {
    if let Some(root) = predefined_root(hkey) {
        return Ok(root_path(root));
    }
    let found = handles.with(hkey, |payload| match payload {
        HandlePayload::RegistryKey(path) => Some(path.clone()),
        _ => None,
    });
    match found {
        Some(Some(path)) => Ok(path),
        _ => Err(ERROR_INVALID_HANDLE as Lstatus),
    }
}

/// The absolute path of a well-known root (its canonical name as the single
/// leading component).
fn root_path(root: WellKnownRoot) -> KeyPath {
    KeyPath::parse(root.canonical_name())
}

/// Append every component of `sub` to a clone of `base`.
fn join(base: &KeyPath, sub: &KeyPath) -> KeyPath {
    let mut path = base.clone();
    for component in sub.components() {
        path.push(component.clone());
    }
    path
}

/// Open an existing subkey, minting and returning a fresh `HKEY` for it.
///
/// An empty `sub` duplicates the handle onto the same key (Win32 opens a new
/// handle to `hkey` itself). A missing key yields `ERROR_FILE_NOT_FOUND`.
///
/// # Errors
///
/// Returns a mapped [`Lstatus`] on an invalid handle, a missing key, or a
/// surface error.
pub fn open_key<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    sub: &KeyPath,
) -> Result<RawHandle, Lstatus> {
    let base = resolve_base(handles, hkey)?;
    if sub.is_empty() {
        return Ok(handles.intern(HandlePayload::RegistryKey(base)));
    }
    let full = join(&base, sub);
    match reg.key_exists(&full) {
        Ok(true) => Ok(handles.intern(HandlePayload::RegistryKey(full))),
        Ok(false) => Err(ERROR_FILE_NOT_FOUND as Lstatus),
        Err(err) => Err(registry_error_to_lstatus(&err)),
    }
}

/// Create (or open) a subkey and its missing ancestors, minting and returning a
/// fresh `HKEY` for it.
///
/// # Errors
///
/// Returns a mapped [`Lstatus`] on an invalid handle or a surface error.
pub fn create_key<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    sub: &KeyPath,
) -> Result<RawHandle, Lstatus> {
    let base = resolve_base(handles, hkey)?;
    let full = join(&base, sub);
    reg.create_key(&full).map_err(|e| registry_error_to_lstatus(&e))?;
    Ok(handles.intern(HandlePayload::RegistryKey(full)))
}

/// Close an `HKEY`.
///
/// Closing a predefined pseudo-handle is a success no-op; closing a minted
/// handle releases it. An unknown value yields `ERROR_INVALID_HANDLE`.
#[must_use]
pub fn close_key(handles: &HandleTable, hkey: RawHandle) -> Lstatus {
    if handles.close(hkey) {
        ERROR_SUCCESS as Lstatus
    } else {
        ERROR_INVALID_HANDLE as Lstatus
    }
}

/// Set a value on the key named by `hkey`, decoding the Win32 `(type, bytes)`
/// pair into a structured value.
///
/// # Errors
///
/// Returns a mapped [`Lstatus`] on an invalid handle, ill-formed value bytes,
/// or a surface error.
pub fn set_value<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    name: &Utf16,
    win32_type: u32,
    data: &[u8],
) -> Result<(), Lstatus> {
    let path = resolve_base(handles, hkey)?;
    let value = value_codec::decode(win32_type, data)?;
    reg.set_value(&path, name.clone(), value)
        .map_err(|e| registry_error_to_lstatus(&e))
}

/// Read a value from the key named by `hkey`, returning its Win32
/// `(type, bytes)` representation.
///
/// # Errors
///
/// Returns `ERROR_FILE_NOT_FOUND` for a missing value (or key), or another
/// mapped [`Lstatus`] for an invalid handle / surface error.
pub fn query_value<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    name: &Utf16,
) -> Result<(u32, Vec<u8>), Lstatus> {
    get_value_under(reg, handles, hkey, &KeyPath::root(), name)
}

/// Read a value from a subkey of the key named by `hkey` (the `RegGetValueW`
/// shape), returning its Win32 `(type, bytes)` representation.
///
/// # Errors
///
/// Returns `ERROR_FILE_NOT_FOUND` for a missing value (or key), or another
/// mapped [`Lstatus`] for an invalid handle / surface error.
pub fn get_value_under<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    sub: &KeyPath,
    name: &Utf16,
) -> Result<(u32, Vec<u8>), Lstatus> {
    let base = resolve_base(handles, hkey)?;
    let full = join(&base, sub);
    let value = reg
        .get_value(&full, name)
        .map_err(|e| registry_error_to_lstatus(&e))?;
    Ok(value_codec::encode(&value))
}

/// Delete a value from the key named by `hkey`.
///
/// # Errors
///
/// Returns a mapped [`Lstatus`] on an invalid handle, a missing value, or a
/// surface error.
pub fn delete_value<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    name: &Utf16,
) -> Result<(), Lstatus> {
    let path = resolve_base(handles, hkey)?;
    reg.delete_value(&path, name)
        .map_err(|e| registry_error_to_lstatus(&e))
}

/// Delete a subkey (and its subtree) of the key named by `hkey`.
///
/// # Errors
///
/// Returns a mapped [`Lstatus`] on an invalid handle, a missing key, or a
/// surface error.
pub fn delete_key<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    sub: &KeyPath,
) -> Result<(), Lstatus> {
    let base = resolve_base(handles, hkey)?;
    let full = join(&base, sub);
    reg.delete_key(&full)
        .map_err(|e| registry_error_to_lstatus(&e))
}

/// Return the immediate subkey name at `index` in ordinal order, or `None` once
/// the index is past the end (`RegEnumKeyExW`'s `ERROR_NO_MORE_ITEMS`).
///
/// # Errors
///
/// Returns a mapped [`Lstatus`] on an invalid handle or surface error.
pub fn enum_key<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    index: u32,
) -> Result<Option<Utf16>, Lstatus> {
    let path = resolve_base(handles, hkey)?;
    let keys = reg.keys(&path).map_err(|e| registry_error_to_lstatus(&e))?;
    Ok(keys.into_iter().nth(index as usize))
}

/// Return the `(name, type, bytes)` of the value at `index` in ordinal order,
/// or `None` once the index is past the end (`RegEnumValueW`'s
/// `ERROR_NO_MORE_ITEMS`).
///
/// # Errors
///
/// Returns a mapped [`Lstatus`] on an invalid handle or surface error.
pub fn enum_value<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
    index: u32,
) -> Result<Option<(Utf16, u32, Vec<u8>)>, Lstatus> {
    let path = resolve_base(handles, hkey)?;
    let values = reg.values(&path).map_err(|e| registry_error_to_lstatus(&e))?;
    Ok(values.into_iter().nth(index as usize).map(|(name, data)| {
        let (win32_type, bytes) = value_codec::encode(&data);
        (name, win32_type, bytes)
    }))
}

/// Summary counts returned by [`query_info`] (the `RegQueryInfoKeyW` shape).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KeyInfo {
    /// Number of immediate subkeys.
    pub subkeys: u32,
    /// Longest immediate subkey name, in characters, excluding the NUL.
    pub max_subkey_len: u32,
    /// Number of values.
    pub values: u32,
    /// Longest value name, in characters, excluding the NUL.
    pub max_value_name_len: u32,
    /// Largest value payload, in bytes.
    pub max_value_len: u32,
}

/// Compute the subkey/value counts and maxima for the key named by `hkey`.
///
/// # Errors
///
/// Returns a mapped [`Lstatus`] on an invalid handle or surface error.
pub fn query_info<S: Surface>(
    reg: &mut Registry<S>,
    handles: &HandleTable,
    hkey: RawHandle,
) -> Result<KeyInfo, Lstatus> {
    let path = resolve_base(handles, hkey)?;
    let keys = reg.keys(&path).map_err(|e| registry_error_to_lstatus(&e))?;
    let values = reg.values(&path).map_err(|e| registry_error_to_lstatus(&e))?;

    let max_subkey_len = keys.iter().map(Utf16::len).max().unwrap_or(0);
    let max_value_name_len = values.iter().map(|(name, _)| name.len()).max().unwrap_or(0);
    let max_value_len = values
        .iter()
        .map(|(_, data)| value_codec::encode(data).1.len())
        .max()
        .unwrap_or(0);

    Ok(KeyInfo {
        subkeys: keys.len() as u32,
        max_subkey_len: max_subkey_len as u32,
        values: values.len() as u32,
        max_value_name_len: max_value_name_len as u32,
        max_value_len: max_value_len as u32,
    })
}

/// The outcome of applying the Win32 `RegQueryValueEx` buffer contract to a
/// payload of known length.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct QueryBuffer {
    /// `ERROR_SUCCESS` or `ERROR_MORE_DATA`.
    pub status: Lstatus,
    /// The size to report back through `lpcbData` (always the full payload).
    pub required: usize,
    /// Whether the caller's buffer should be filled with the payload.
    pub copy: bool,
}

/// Apply the three-case Win32 query buffer contract (SHIM-D7 parity with the
/// C++ `raw_query_value`):
///
/// - no data buffer (`has_buffer == false`): size/type query — report the
///   required size and succeed (`ERROR_SUCCESS`), no copy;
/// - buffer too small (`capacity < payload_len`): report the required size and
///   return `ERROR_MORE_DATA`, no copy;
/// - otherwise: copy the payload and succeed.
#[must_use]
pub fn apply_query_buffer(payload_len: usize, has_buffer: bool, capacity: usize) -> QueryBuffer {
    if !has_buffer {
        return QueryBuffer {
            status: ERROR_SUCCESS as Lstatus,
            required: payload_len,
            copy: false,
        };
    }
    if capacity < payload_len {
        return QueryBuffer {
            status: ERROR_MORE_DATA as Lstatus,
            required: payload_len,
            copy: false,
        };
    }
    QueryBuffer {
        status: ERROR_SUCCESS as Lstatus,
        required: payload_len,
        copy: true,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use windows_platform_isolation::{Hive, TreeSurface, Win32OrdinalCasing};
    use windows_sys::Win32::Foundation::ERROR_FILE_NOT_FOUND;
    use windows_sys::Win32::System::Registry::{REG_DWORD, REG_SZ};

    // HKEY_CURRENT_USER's reserved value; resolves to its well-known root path.
    const HKCU: RawHandle = 0x8000_0001;
    // A value that is neither predefined nor interned.
    const BOGUS_HANDLE: RawHandle = 0x4000_0000;

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    fn empty_registry() -> Registry<TreeSurface<Win32OrdinalCasing>> {
        Registry::in_memory(Hive::new())
    }

    #[test]
    fn create_then_open_round_trips_a_handle() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        let sub = KeyPath::parse("Software\\App");

        let created = create_key(&mut reg, &handles, HKCU, &sub).unwrap();
        assert!(crate::handle_table::is_minted_value(created));

        let opened = open_key(&mut reg, &handles, HKCU, &sub).unwrap();
        assert!(crate::handle_table::is_minted_value(opened));
        // Both resolve to HKEY_CURRENT_USER\Software\App.
        assert!(
            handles
                .with(opened, |p| matches!(p, HandlePayload::RegistryKey(_)))
                .unwrap()
        );
    }

    #[test]
    fn open_missing_key_is_file_not_found() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        assert_eq!(
            open_key(&mut reg, &handles, HKCU, &KeyPath::parse("Nope\\Missing")),
            Err(ERROR_FILE_NOT_FOUND as Lstatus)
        );
    }

    #[test]
    fn bogus_handle_is_invalid_handle() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        assert_eq!(
            open_key(&mut reg, &handles, BOGUS_HANDLE, &KeyPath::parse("X")),
            Err(ERROR_INVALID_HANDLE as Lstatus)
        );
    }

    #[test]
    fn set_then_query_dword_round_trips_with_type_and_size() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        let key = create_key(&mut reg, &handles, HKCU, &KeyPath::parse("App")).unwrap();

        let dword = 0xDEAD_BEEF_u32;
        set_value(&mut reg, &handles, key, &w("n"), REG_DWORD, &dword.to_le_bytes()).unwrap();

        let (t, bytes) = query_value(&mut reg, &handles, key, &w("n")).unwrap();
        assert_eq!(t, REG_DWORD);
        assert_eq!(bytes, dword.to_le_bytes());
    }

    #[test]
    fn query_missing_value_is_file_not_found() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        let key = create_key(&mut reg, &handles, HKCU, &KeyPath::parse("App")).unwrap();
        assert_eq!(
            query_value(&mut reg, &handles, key, &w("absent")),
            Err(ERROR_FILE_NOT_FOUND as Lstatus)
        );
    }

    #[test]
    fn delete_value_then_query_is_file_not_found() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        let key = create_key(&mut reg, &handles, HKCU, &KeyPath::parse("App")).unwrap();
        set_value(&mut reg, &handles, key, &w("n"), REG_SZ, &units_le(&[0x41, 0x00])).unwrap();
        delete_value(&mut reg, &handles, key, &w("n")).unwrap();
        assert_eq!(
            query_value(&mut reg, &handles, key, &w("n")),
            Err(ERROR_FILE_NOT_FOUND as Lstatus)
        );
    }

    #[test]
    fn enum_keys_and_values_are_ordinal_ordered() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        create_key(&mut reg, &handles, HKCU, &KeyPath::parse("Root\\Beta")).unwrap();
        create_key(&mut reg, &handles, HKCU, &KeyPath::parse("Root\\Alpha")).unwrap();
        let root = create_key(&mut reg, &handles, HKCU, &KeyPath::parse("Root")).unwrap();
        set_value(&mut reg, &handles, root, &w("zed"), REG_DWORD, &1u32.to_le_bytes()).unwrap();

        assert_eq!(enum_key(&mut reg, &handles, root, 0).unwrap(), Some(w("Alpha")));
        assert_eq!(enum_key(&mut reg, &handles, root, 1).unwrap(), Some(w("Beta")));
        assert_eq!(enum_key(&mut reg, &handles, root, 2).unwrap(), None);

        let (name, t, _) = enum_value(&mut reg, &handles, root, 0).unwrap().unwrap();
        assert_eq!(name, w("zed"));
        assert_eq!(t, REG_DWORD);
        assert_eq!(enum_value(&mut reg, &handles, root, 1).unwrap(), None);
    }

    #[test]
    fn query_info_reports_counts_and_maxima() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        create_key(&mut reg, &handles, HKCU, &KeyPath::parse("Root\\Alpha")).unwrap();
        create_key(&mut reg, &handles, HKCU, &KeyPath::parse("Root\\BetaBeta")).unwrap();
        let root = create_key(&mut reg, &handles, HKCU, &KeyPath::parse("Root")).unwrap();
        set_value(&mut reg, &handles, root, &w("name"), REG_DWORD, &1u32.to_le_bytes()).unwrap();

        let info = query_info(&mut reg, &handles, root).unwrap();
        assert_eq!(info.subkeys, 2);
        assert_eq!(info.max_subkey_len, "BetaBeta".len() as u32);
        assert_eq!(info.values, 1);
        assert_eq!(info.max_value_name_len, "name".len() as u32);
        assert_eq!(info.max_value_len, 4); // REG_DWORD payload
    }

    #[test]
    fn delete_key_removes_subtree() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        create_key(&mut reg, &handles, HKCU, &KeyPath::parse("Doomed\\Child")).unwrap();
        delete_key(&mut reg, &handles, HKCU, &KeyPath::parse("Doomed")).unwrap();
        assert_eq!(
            open_key(&mut reg, &handles, HKCU, &KeyPath::parse("Doomed")),
            Err(ERROR_FILE_NOT_FOUND as Lstatus)
        );
    }

    #[test]
    fn close_predefined_is_noop_minted_is_released_unknown_is_invalid() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        assert_eq!(close_key(&handles, HKCU), ERROR_SUCCESS as Lstatus);

        let key = create_key(&mut reg, &handles, HKCU, &KeyPath::parse("K")).unwrap();
        assert_eq!(close_key(&handles, key), ERROR_SUCCESS as Lstatus);
        // Already closed -> no longer interned.
        assert_eq!(close_key(&handles, key), ERROR_INVALID_HANDLE as Lstatus);
        assert_eq!(close_key(&handles, BOGUS_HANDLE), ERROR_INVALID_HANDLE as Lstatus);
    }

    #[test]
    fn apply_query_buffer_three_cases() {
        // Size query: no buffer.
        let q = apply_query_buffer(8, false, 0);
        assert_eq!(q.status, ERROR_SUCCESS as Lstatus);
        assert_eq!(q.required, 8);
        assert!(!q.copy);

        // Too small.
        let q = apply_query_buffer(8, true, 4);
        assert_eq!(q.status, ERROR_MORE_DATA as Lstatus);
        assert_eq!(q.required, 8);
        assert!(!q.copy);

        // Exact / large enough.
        let q = apply_query_buffer(8, true, 8);
        assert_eq!(q.status, ERROR_SUCCESS as Lstatus);
        assert!(q.copy);
    }

    #[test]
    fn empty_subkey_open_duplicates_handle_onto_same_key() {
        let mut reg = empty_registry();
        let handles = HandleTable::new();
        // Opening HKCU with an empty subkey always succeeds (handle duplicate).
        let h = open_key(&mut reg, &handles, HKCU, &KeyPath::root()).unwrap();
        assert!(crate::handle_table::is_minted_value(h));
    }

    fn units_le(units: &[u16]) -> Vec<u8> {
        let mut out = Vec::new();
        for &u in units {
            out.extend_from_slice(&u.to_le_bytes());
        }
        out
    }
}
