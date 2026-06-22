// Copyright (c) Microsoft Corporation.

//! Windows implementation of the safe registry FFI wrappers. Every `unsafe` in
//! the registry provider lives in this module (D1 / D13, Option B).
//!
//! [`RegKey`] is a RAII owner of an `HKEY`: predefined roots are non-owning
//! (never closed), opened/created keys are owning (closed on drop). Subkey and
//! value names crossing this boundary are caller-supplied **NUL-terminated**
//! UTF-16 slices; no raw pointer escapes.

use windows::Win32::Foundation::{
    ERROR_FILE_NOT_FOUND, ERROR_MORE_DATA, ERROR_NO_MORE_ITEMS, ERROR_PATH_NOT_FOUND,
    ERROR_SUCCESS, WIN32_ERROR,
};
use windows::Win32::System::Registry::{
    HKEY, HKEY_CLASSES_ROOT, HKEY_CURRENT_CONFIG, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE,
    HKEY_USERS, KEY_READ, KEY_WRITE, REG_OPTION_NON_VOLATILE, REG_VALUE_TYPE, RegCloseKey,
    RegCreateKeyExW, RegDeleteTreeW, RegDeleteValueW, RegEnumKeyExW, RegEnumValueW, RegOpenKeyExW,
    RegQueryValueExW, RegSetValueExW,
};
use windows::core::{PCWSTR, PWSTR};

/// A failed Win32 registry call, carrying the `WIN32_ERROR` status code.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RegError(pub u32);

impl RegError {
    /// The raw `WIN32_ERROR` status code.
    #[must_use]
    pub fn code(self) -> u32 {
        self.0
    }

    /// Whether this is a "not found" status (`ERROR_FILE_NOT_FOUND` /
    /// `ERROR_PATH_NOT_FOUND`), which the safe layer maps to a missing
    /// key/value rather than a hard failure.
    #[must_use]
    pub fn is_not_found(self) -> bool {
        self.0 == ERROR_FILE_NOT_FOUND.0 || self.0 == ERROR_PATH_NOT_FOUND.0
    }
}

impl core::fmt::Display for RegError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "Win32 registry error {}", self.0)
    }
}

impl std::error::Error for RegError {}

/// A single enumerated registry value: `(name code units, `REG_*` type code,
/// raw value bytes)`.
pub type RawValue = (Vec<u16>, u32, Vec<u8>);

/// Map a `WIN32_ERROR` return into a `Result`.
fn check(rc: WIN32_ERROR) -> Result<(), RegError> {
    if rc == ERROR_SUCCESS {
        Ok(())
    } else {
        Err(RegError(rc.0))
    }
}

/// A RAII registry key handle.
///
/// Predefined roots (`local_machine`, …) are non-owning views of the process
/// pseudo-handles and are never closed; keys returned by [`open_subkey`] /
/// [`create_subkey`] own their `HKEY` and close it on drop.
///
/// [`open_subkey`]: RegKey::open_subkey
/// [`create_subkey`]: RegKey::create_subkey
pub struct RegKey {
    handle: HKEY,
    owned: bool,
}

impl core::fmt::Debug for RegKey {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("RegKey")
            .field("owned", &self.owned)
            .finish_non_exhaustive()
    }
}

// SAFETY: an `HKEY` is an OS handle valid from any thread; `owned` ensures the
// underlying key is closed at most once, on the thread that drops it. `RegKey`
// is deliberately not `Sync` — shared access is serialized externally (D12).
unsafe impl Send for RegKey {}

impl RegKey {
    fn predefined(handle: HKEY) -> Self {
        Self {
            handle,
            owned: false,
        }
    }

    /// `HKEY_LOCAL_MACHINE` (non-owning).
    #[must_use]
    pub fn local_machine() -> Self {
        Self::predefined(HKEY_LOCAL_MACHINE)
    }

    /// `HKEY_CURRENT_USER` (non-owning).
    #[must_use]
    pub fn current_user() -> Self {
        Self::predefined(HKEY_CURRENT_USER)
    }

    /// `HKEY_USERS` (non-owning).
    #[must_use]
    pub fn users() -> Self {
        Self::predefined(HKEY_USERS)
    }

    /// `HKEY_CLASSES_ROOT` (non-owning).
    #[must_use]
    pub fn classes_root() -> Self {
        Self::predefined(HKEY_CLASSES_ROOT)
    }

    /// `HKEY_CURRENT_CONFIG` (non-owning).
    #[must_use]
    pub fn current_config() -> Self {
        Self::predefined(HKEY_CURRENT_CONFIG)
    }

    /// Read a value by name from this key.
    ///
    /// `name` is a NUL-terminated UTF-16 value name (an empty name, i.e. a lone
    /// NUL, selects the key's default value). Returns the raw `REG_*` type code
    /// and the value bytes exactly as stored, using the two-call length probe.
    ///
    /// # Errors
    ///
    /// Returns [`RegError`] (with [`RegError::is_not_found`] true when the value
    /// is absent) on any Win32 failure.
    pub fn query_value(&self, name: &[u16]) -> Result<(u32, Vec<u8>), RegError> {
        let mut ty = REG_VALUE_TYPE(0);
        let mut cb: u32 = 0;
        // Probe: type and byte length (lpData = null).
        let rc = unsafe {
            RegQueryValueExW(
                self.handle,
                PCWSTR(name.as_ptr()),
                None,
                Some(&mut ty),
                None,
                Some(&mut cb),
            )
        };
        check(rc)?;
        if cb == 0 {
            return Ok((ty.0, Vec::new()));
        }
        let mut buf = vec![0u8; cb as usize];
        let rc = unsafe {
            RegQueryValueExW(
                self.handle,
                PCWSTR(name.as_ptr()),
                None,
                Some(&mut ty),
                Some(buf.as_mut_ptr()),
                Some(&mut cb),
            )
        };
        check(rc)?;
        buf.truncate(cb as usize);
        Ok((ty.0, buf))
    }

    /// Enumerate the immediate subkey names of this key (without NUL
    /// terminators), in the registry's native enumeration order.
    ///
    /// # Errors
    ///
    /// Returns [`RegError`] on any Win32 failure other than the normal
    /// end-of-enumeration signal.
    pub fn enum_subkey_names(&self) -> Result<Vec<Vec<u16>>, RegError> {
        let mut names = Vec::new();
        let mut index: u32 = 0;
        // Subkey names are at most 255 chars; start comfortably and grow if the
        // OS ever reports otherwise.
        let mut buf = vec![0u16; 256];
        loop {
            let mut cch = buf.len() as u32;
            let rc = unsafe {
                RegEnumKeyExW(
                    self.handle,
                    index,
                    Some(PWSTR(buf.as_mut_ptr())),
                    &mut cch,
                    None,
                    None,
                    None,
                    None,
                )
            };
            if rc == ERROR_NO_MORE_ITEMS {
                break;
            }
            if rc == ERROR_MORE_DATA {
                let bigger = buf.len().saturating_mul(2);
                buf.resize(bigger, 0);
                continue;
            }
            check(rc)?;
            names.push(buf[..cch as usize].to_vec());
            index += 1;
        }
        Ok(names)
    }

    /// Enumerate this key's values as `(name units, REG_* type code, bytes)`.
    ///
    /// Each value is fetched with a two-call probe: the first call yields the
    /// name, type, and data size; the second reads the bytes.
    ///
    /// # Errors
    ///
    /// Returns [`RegError`] on any Win32 failure other than the normal
    /// end-of-enumeration signal.
    pub fn enum_values(&self) -> Result<Vec<RawValue>, RegError> {
        let mut out = Vec::new();
        let mut index: u32 = 0;
        // Value names are at most 16383 chars; the buffer also carries the NUL.
        let mut name_buf = vec![0u16; 16384];
        loop {
            let mut cch = name_buf.len() as u32;
            let mut ty: u32 = 0;
            let mut cb: u32 = 0;
            // Probe: name, type, and data size (lpData = null).
            let rc = unsafe {
                RegEnumValueW(
                    self.handle,
                    index,
                    Some(PWSTR(name_buf.as_mut_ptr())),
                    &mut cch,
                    None,
                    Some(&mut ty),
                    None,
                    Some(&mut cb),
                )
            };
            if rc == ERROR_NO_MORE_ITEMS {
                break;
            }
            check(rc)?;
            let name = name_buf[..cch as usize].to_vec();
            let data = if cb == 0 {
                Vec::new()
            } else {
                let mut buf = vec![0u8; cb as usize];
                let mut cch2 = name_buf.len() as u32;
                let mut ty2: u32 = 0;
                let mut cb2 = cb;
                let rc = unsafe {
                    RegEnumValueW(
                        self.handle,
                        index,
                        Some(PWSTR(name_buf.as_mut_ptr())),
                        &mut cch2,
                        None,
                        Some(&mut ty2),
                        Some(buf.as_mut_ptr()),
                        Some(&mut cb2),
                    )
                };
                check(rc)?;
                buf.truncate(cb2 as usize);
                buf
            };
            out.push((name, ty, data));
            index += 1;
        }
        Ok(out)
    }

    /// Open an existing subkey relative to this key.
    ///
    /// `subkey` must be a NUL-terminated UTF-16 path (backslash-separated). With
    /// `writable` set the key is opened for read+write, otherwise read-only.
    ///
    /// # Errors
    ///
    /// Returns [`RegError`] (with [`RegError::is_not_found`] true when the key
    /// does not exist) on any Win32 failure.
    pub fn open_subkey(&self, subkey: &[u16], writable: bool) -> Result<RegKey, RegError> {
        let access = if writable {
            KEY_READ | KEY_WRITE
        } else {
            KEY_READ
        };
        let mut out = HKEY(core::ptr::null_mut());
        let rc = unsafe {
            RegOpenKeyExW(self.handle, PCWSTR(subkey.as_ptr()), None, access, &mut out)
        };
        check(rc)?;
        Ok(RegKey {
            handle: out,
            owned: true,
        })
    }

    /// Open (creating if absent) a subkey relative to this key, for read+write.
    ///
    /// `subkey` must be a NUL-terminated UTF-16 path. Intermediate keys are
    /// created as needed (non-volatile).
    ///
    /// # Errors
    ///
    /// Returns [`RegError`] on any Win32 failure.
    pub fn create_subkey(&self, subkey: &[u16]) -> Result<RegKey, RegError> {
        let mut out = HKEY(core::ptr::null_mut());
        let rc = unsafe {
            RegCreateKeyExW(
                self.handle,
                PCWSTR(subkey.as_ptr()),
                None,
                PCWSTR::null(),
                REG_OPTION_NON_VOLATILE,
                KEY_READ | KEY_WRITE,
                None,
                &mut out,
                None,
            )
        };
        check(rc)?;
        Ok(RegKey {
            handle: out,
            owned: true,
        })
    }

    /// Set a value by name on this key.
    ///
    /// `name` is a NUL-terminated UTF-16 value name (a lone NUL selects the
    /// default value). `type_code` is the raw `REG_*` type; `data` is the value
    /// bytes exactly as they should be stored (string types must already carry
    /// their UTF-16 NUL terminator).
    ///
    /// # Errors
    ///
    /// Returns [`RegError`] on any Win32 failure.
    pub fn set_value(&self, name: &[u16], type_code: u32, data: &[u8]) -> Result<(), RegError> {
        let rc = unsafe {
            RegSetValueExW(
                self.handle,
                PCWSTR(name.as_ptr()),
                None,
                REG_VALUE_TYPE(type_code),
                Some(data),
            )
        };
        check(rc)
    }

    /// Delete the value `name` from this key.
    ///
    /// # Errors
    ///
    /// Returns [`RegError`] (with [`RegError::is_not_found`] true when the value
    /// is absent) on any Win32 failure.
    pub fn delete_value(&self, name: &[u16]) -> Result<(), RegError> {
        let rc = unsafe { RegDeleteValueW(self.handle, PCWSTR(name.as_ptr())) };
        check(rc)
    }

    /// Delete a subkey of this key together with all of its descendants.
    ///
    /// `subkey` must be a NUL-terminated UTF-16 path naming the subkey to
    /// remove; the named subkey and everything beneath it are deleted.
    ///
    /// # Errors
    ///
    /// Returns [`RegError`] (with [`RegError::is_not_found`] true when the
    /// subkey is absent) on any Win32 failure.
    pub fn delete_subkey_tree(&self, subkey: &[u16]) -> Result<(), RegError> {
        let rc = unsafe { RegDeleteTreeW(self.handle, PCWSTR(subkey.as_ptr())) };
        check(rc)
    }
}

impl Drop for RegKey {
    fn drop(&mut self) {
        if self.owned && !self.handle.0.is_null() {
            // Best-effort close; nothing actionable on failure.
            let _ = unsafe { RegCloseKey(self.handle) };
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// NUL-terminate a UTF-16 conversion of `s`.
    fn wz(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(core::iter::once(0)).collect()
    }

    #[test]
    fn open_existing_subkey_succeeds() {
        // HKCU\Environment exists on every interactive Windows profile.
        let hkcu = RegKey::current_user();
        let env = hkcu.open_subkey(&wz("Environment"), false);
        assert!(env.is_ok(), "expected to open HKCU\\Environment");
    }

    #[test]
    fn open_missing_subkey_reports_not_found() {
        let hkcu = RegKey::current_user();
        let err = hkcu
            .open_subkey(&wz("Software\\windows-platform-isolation-sys\\does-not-exist"), false)
            .expect_err("missing key should error");
        assert!(err.is_not_found(), "expected not-found, got {err}");
    }

    #[test]
    fn create_set_read_delete_round_trip() {
        // Use a per-test scratch subtree under HKCU (no admin required).
        let hkcu = RegKey::current_user();
        let subtree = wz("Software\\windows-platform-isolation-sys-tests\\round_trip");
        // Best-effort cleanup of any leftovers from a prior aborted run.
        let _ = hkcu.delete_subkey_tree(&subtree);

        let key = hkcu.create_subkey(&subtree).expect("create scratch key");
        // REG_SZ "hi" stored as UTF-16LE with a NUL terminator.
        let name = wz("greeting");
        let data: Vec<u8> = "hi"
            .encode_utf16()
            .chain(core::iter::once(0))
            .flat_map(u16::to_le_bytes)
            .collect();
        const REG_SZ: u32 = 1;
        key.set_value(&name, REG_SZ, &data).expect("set value");

        let (ty, bytes) = key.query_value(&name).expect("read back value");
        assert_eq!(ty, REG_SZ);
        assert_eq!(bytes, data);

        key.delete_value(&name).expect("delete value");
        assert!(
            key.query_value(&name).expect_err("value should be gone").is_not_found(),
            "deleted value should report not-found"
        );

        // Remove the scratch subtree entirely.
        hkcu.delete_subkey_tree(&subtree).expect("delete scratch subtree");
    }
}
