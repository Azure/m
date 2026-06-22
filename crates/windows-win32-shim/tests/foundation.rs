// Copyright (c) Microsoft Corporation.

//! MW1 foundation integration tests: the minted-handle table, predefined
//! `HKEY` → root resolution, and the Win32 error-mapping table, exercised
//! through the crate's public (rlib) API.

#![cfg(windows)]

use windows_platform_isolation::{
    FilesystemError, KeyPath, RegistryError, ValueType, WellKnownRoot,
};
use windows_sys::Win32::Foundation::{
    ERROR_ACCESS_DENIED, ERROR_FILE_NOT_FOUND, ERROR_INVALID_DATA, ERROR_INVALID_NAME,
};
use windows_win32_shim::{
    FileHandleState, FindEnumerationState, HandlePayload, HandleTable, Lstatus, SearchOp,
    SearchPredicate, ShimSession, filesystem_error_to_win32, handle_table::is_minted_value,
    handle_table::predefined_root, registry_error_to_lstatus,
};

#[test]
fn handle_round_trip_and_reserved_bit_invariants() {
    let table = HandleTable::new();
    let mut minted = Vec::new();

    for i in 0..256u64 {
        let key = table.intern(HandlePayload::RegistryKey(KeyPath::parse("HKEY_USERS\\.DEFAULT")));
        let file = table.intern(HandlePayload::File(FileHandleState {
            path: windows_platform_isolation::FilePath::from_utf8("C:\\a\\b"),
            position: i,
        }));
        let find = table.intern(HandlePayload::Find(FindEnumerationState {
            entries: Vec::new(),
            cursor: i as usize,
            predicate: SearchPredicate {
                pattern_leaf: windows_platform_isolation::Utf16::from_units(Vec::new()),
                op: SearchOp::NameMatch,
                case_sensitive: false,
            },
            emit_short_name: true,
        }));

        for h in [key, file, find] {
            assert!(is_minted_value(h), "{h:#x} must be a minted value");
            // Minted values never collide with predefined HKEYs.
            assert_eq!(predefined_root(h), None);
            minted.push(h);
        }
    }

    // All minted values are distinct.
    let mut sorted = minted.clone();
    sorted.sort_unstable();
    sorted.dedup();
    assert_eq!(sorted.len(), minted.len(), "minted values must be unique");

    // Round-trip a file payload's mutable state.
    let h = table.intern(HandlePayload::File(FileHandleState::default()));
    table
        .with_mut(h, |p| {
            if let HandlePayload::File(state) = p {
                state.position = 99;
            }
        })
        .expect("handle should be live");
    table
        .with(h, |p| {
            if let HandlePayload::File(state) = p {
                assert_eq!(state.position, 99);
            } else {
                panic!("expected File payload");
            }
        })
        .expect("handle should be live");

    assert!(table.close(h));
    assert!(table.with(h, |_| ()).is_none());
}

#[test]
fn predefined_hkey_resolves_to_session_root() {
    let session = ShimSession::new();
    let cases = [
        (0x8000_0000usize, WellKnownRoot::ClassesRoot),
        (0x8000_0001, WellKnownRoot::CurrentUser),
        (0x8000_0002, WellKnownRoot::LocalMachine),
        (0x8000_0003, WellKnownRoot::Users),
        (0x8000_0005, WellKnownRoot::CurrentConfig),
    ];

    for (raw, expected_root) in cases {
        let root = predefined_root(raw).expect("predefined HKEY must resolve to a root");
        assert_eq!(root, expected_root);
        assert_eq!(
            session.root_path(root),
            KeyPath::parse(expected_root.canonical_name())
        );
    }

    // A non-predefined value resolves to nothing.
    assert_eq!(predefined_root(0x8000_0099), None);
}

#[test]
fn error_mapping_table() {
    // Registry → LSTATUS.
    assert_eq!(
        registry_error_to_lstatus(&RegistryError::Os(ERROR_ACCESS_DENIED)),
        ERROR_ACCESS_DENIED as Lstatus
    );
    assert_eq!(
        registry_error_to_lstatus(&RegistryError::KeyNotFound),
        ERROR_FILE_NOT_FOUND as Lstatus
    );
    assert_eq!(
        registry_error_to_lstatus(&RegistryError::ValueNotFound),
        ERROR_FILE_NOT_FOUND as Lstatus
    );
    assert_eq!(
        registry_error_to_lstatus(&RegistryError::TypeMismatch {
            expected: ValueType::Dword,
            found: ValueType::String,
        }),
        ERROR_INVALID_DATA as Lstatus
    );

    // Filesystem → WIN32_ERROR.
    assert_eq!(
        filesystem_error_to_win32(&FilesystemError::Os(ERROR_ACCESS_DENIED)),
        ERROR_ACCESS_DENIED
    );
    assert_eq!(
        filesystem_error_to_win32(&FilesystemError::NotFound),
        ERROR_FILE_NOT_FOUND
    );
    assert_eq!(
        filesystem_error_to_win32(&FilesystemError::InvalidPath("..".to_string())),
        ERROR_INVALID_NAME
    );
}
