// Copyright (c) Microsoft Corporation.

//! Win32 error mapping (SHIM-D7).
//!
//! This module owns the translation from the `windows-platform-isolation`
//! surface errors ([`RegistryError`] / [`FilesystemError`]) into the Win32
//! status vocabulary the C ABI must report. The mapping is **specified here**
//! and not inherited from any dependency (Design Autonomy): the registry path
//! returns an [`Lstatus`] (the `LONG` registry functions return directly), and
//! the filesystem path returns a `WIN32_ERROR` for [`set_last_error`].
//!
//! `Os(code)` carries a raw Win32 code straight through; every structured
//! variant maps to a documented code. Both enums are `#[non_exhaustive]`, so a
//! wildcard arm maps any future variant to the catch-all code documented below;
//! when such a variant is added, give it an explicit arm here.

use windows_platform_isolation::{FilesystemError, RegistryError};
use windows_sys::Win32::Foundation::{
    ERROR_FILE_NOT_FOUND, ERROR_INVALID_DATA, ERROR_INVALID_NAME, SetLastError, WIN32_ERROR,
};

/// The registry status type: `LONG` (`i32`), as the Win32 `Reg*` functions
/// return directly. Mirrors the platform `LSTATUS` typedef.
pub type Lstatus = i32;

/// Map a [`RegistryError`] to the [`Lstatus`] a `Reg*` entry point returns.
///
/// - [`RegistryError::Os`] passes its raw Win32 code straight through.
/// - [`RegistryError::KeyNotFound`] / [`RegistryError::ValueNotFound`] →
///   `ERROR_FILE_NOT_FOUND` (the code Win32 returns for a missing key or value).
/// - [`RegistryError::IllFormedUtf16`], [`RegistryError::TypeMismatch`], and
///   [`RegistryError::MalformedArtifact`] → `ERROR_INVALID_DATA`.
/// - Any future variant → `ERROR_INVALID_DATA` (the documented catch-all).
#[must_use]
pub fn registry_error_to_lstatus(err: &RegistryError) -> Lstatus {
    let code: WIN32_ERROR = match err {
        RegistryError::Os(code) => *code,
        RegistryError::KeyNotFound | RegistryError::ValueNotFound => ERROR_FILE_NOT_FOUND,
        RegistryError::IllFormedUtf16
        | RegistryError::TypeMismatch { .. }
        | RegistryError::MalformedArtifact(_) => ERROR_INVALID_DATA,
        _ => ERROR_INVALID_DATA,
    };
    // WIN32_ERROR is a u32; the LSTATUS contract is the same numeric value
    // reinterpreted as the LONG the Reg* functions return.
    code as Lstatus
}

/// Map a [`FilesystemError`] to the `WIN32_ERROR` a filesystem entry point hands
/// to [`set_last_error`] before returning its failure sentinel.
///
/// - [`FilesystemError::Os`] passes its raw Win32 code straight through.
/// - [`FilesystemError::NotFound`] → `ERROR_FILE_NOT_FOUND`.
/// - [`FilesystemError::IllFormedUtf16`] / [`FilesystemError::InvalidPath`] →
///   `ERROR_INVALID_NAME`.
/// - [`FilesystemError::MalformedArtifact`] → `ERROR_INVALID_DATA`.
/// - Any future variant → `ERROR_INVALID_DATA` (the documented catch-all).
#[must_use]
pub fn filesystem_error_to_win32(err: &FilesystemError) -> WIN32_ERROR {
    match err {
        FilesystemError::Os(code) => *code,
        FilesystemError::NotFound => ERROR_FILE_NOT_FOUND,
        FilesystemError::IllFormedUtf16 | FilesystemError::InvalidPath(_) => ERROR_INVALID_NAME,
        FilesystemError::MalformedArtifact(_) => ERROR_INVALID_DATA,
        _ => ERROR_INVALID_DATA,
    }
}

/// Set the calling thread's Win32 last-error code.
///
/// This is the one FFI call in the module; it wraps `SetLastError`, which only
/// stores a thread-local `DWORD` and is always sound.
#[allow(unsafe_code)]
pub fn set_last_error(code: WIN32_ERROR) {
    // SAFETY: `SetLastError` writes a thread-local `DWORD` and has no
    // precondition on its argument; the call is always sound.
    unsafe { SetLastError(code) };
}

#[cfg(test)]
mod tests {
    use super::*;
    use windows_platform_isolation::ValueType;
    use windows_sys::Win32::Foundation::{ERROR_ACCESS_DENIED, ERROR_SUCCESS};

    #[test]
    fn registry_os_passes_through() {
        assert_eq!(
            registry_error_to_lstatus(&RegistryError::Os(ERROR_ACCESS_DENIED)),
            ERROR_ACCESS_DENIED as Lstatus
        );
        assert_eq!(
            registry_error_to_lstatus(&RegistryError::Os(ERROR_SUCCESS)),
            ERROR_SUCCESS as Lstatus
        );
    }

    #[test]
    fn registry_missing_key_and_value_map_to_file_not_found() {
        assert_eq!(
            registry_error_to_lstatus(&RegistryError::KeyNotFound),
            ERROR_FILE_NOT_FOUND as Lstatus
        );
        assert_eq!(
            registry_error_to_lstatus(&RegistryError::ValueNotFound),
            ERROR_FILE_NOT_FOUND as Lstatus
        );
    }

    #[test]
    fn registry_structured_variants_map_to_invalid_data() {
        assert_eq!(
            registry_error_to_lstatus(&RegistryError::IllFormedUtf16),
            ERROR_INVALID_DATA as Lstatus
        );
        assert_eq!(
            registry_error_to_lstatus(&RegistryError::TypeMismatch {
                expected: ValueType::Dword,
                found: ValueType::String,
            }),
            ERROR_INVALID_DATA as Lstatus
        );
        assert_eq!(
            registry_error_to_lstatus(&RegistryError::MalformedArtifact("bad".to_string())),
            ERROR_INVALID_DATA as Lstatus
        );
    }

    #[test]
    fn filesystem_os_passes_through() {
        assert_eq!(
            filesystem_error_to_win32(&FilesystemError::Os(ERROR_ACCESS_DENIED)),
            ERROR_ACCESS_DENIED
        );
    }

    #[test]
    fn filesystem_not_found_maps_to_file_not_found() {
        assert_eq!(
            filesystem_error_to_win32(&FilesystemError::NotFound),
            ERROR_FILE_NOT_FOUND
        );
    }

    #[test]
    fn filesystem_path_variants_map_to_invalid_name() {
        assert_eq!(
            filesystem_error_to_win32(&FilesystemError::IllFormedUtf16),
            ERROR_INVALID_NAME
        );
        assert_eq!(
            filesystem_error_to_win32(&FilesystemError::InvalidPath("..".to_string())),
            ERROR_INVALID_NAME
        );
    }

    #[test]
    fn filesystem_malformed_artifact_maps_to_invalid_data() {
        assert_eq!(
            filesystem_error_to_win32(&FilesystemError::MalformedArtifact("bad".to_string())),
            ERROR_INVALID_DATA
        );
    }

    #[test]
    fn set_last_error_is_callable() {
        // Exercise the FFI wrapper; the actual stored value is read back via
        // GetLastError in higher-level ABI tests once entry points exist.
        set_last_error(ERROR_INVALID_DATA);
    }
}
