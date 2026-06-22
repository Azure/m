// Copyright (c) Microsoft Corporation.

//! Windows implementation of the safe registry FFI wrappers. Every `unsafe` in
//! the registry provider lives in this module (D1 / D13, Option B).
//!
//! [`RegKey`] is a RAII owner of an `HKEY`: predefined roots are non-owning
//! (never closed), opened/created keys are owning (closed on drop). Subkey and
//! value names crossing this boundary are caller-supplied **NUL-terminated**
//! UTF-16 slices; no raw pointer escapes.

use windows::Win32::Foundation::{
    ERROR_FILE_NOT_FOUND, ERROR_PATH_NOT_FOUND, ERROR_SUCCESS, WIN32_ERROR,
};
use windows::Win32::System::Registry::{
    HKEY, HKEY_CLASSES_ROOT, HKEY_CURRENT_CONFIG, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE,
    HKEY_USERS, KEY_READ, KEY_WRITE, REG_OPTION_NON_VOLATILE, RegCloseKey, RegCreateKeyExW,
    RegOpenKeyExW,
};
use windows::core::PCWSTR;

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

    /// The raw handle, for use by the read/write primitives in this module.
    // Consumed by the query/enumerate/set primitives added in M5-2 / M5-3.
    #[allow(dead_code)]
    pub(crate) fn raw(&self) -> HKEY {
        self.handle
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
}
