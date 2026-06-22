// Copyright (c) Microsoft Corporation.

//! MW2 registry integration tests.
//!
//! These drive the safe, surface-generic registry core ([`reg_ops`]) against an
//! in-memory [`Registry`] (the mandated production `Win32OrdinalCasing`) plus a
//! fresh handle table — the same code path the exported `mReg*W` entry points
//! delegate to, minus the raw-pointer marshaling. They mirror the C++
//! `test_mwinreg_*` scenarios (predefined roots, open/close lifecycle, value
//! operations and the three-case query buffer contract) and add larger-scale
//! enumeration coverage.

#![cfg(windows)]

use windows_platform_isolation::{Hive, Registry, TreeSurface, Utf16, ValueData, Win32OrdinalCasing};
use windows_sys::Win32::Foundation::{
    ERROR_FILE_NOT_FOUND, ERROR_INVALID_HANDLE, ERROR_MORE_DATA, ERROR_SUCCESS,
};
use windows_sys::Win32::System::Registry::{
    REG_BINARY, REG_DWORD, REG_EXPAND_SZ, REG_MULTI_SZ, REG_QWORD, REG_SZ,
};
use windows_win32_shim::handle_table::is_minted_value;
use windows_win32_shim::{HandlePayload, HandleTable, Lstatus, RawHandle, reg_ops, value_codec};

// Predefined `HKEY` reserved values (bare 32-bit form; `predefined_root`
// resolves both this and the 64-bit sign-extended form).
const HKCU: RawHandle = 0x8000_0001;
const HKLM: RawHandle = 0x8000_0002;
const BOGUS: RawHandle = 0x4000_0000;

fn w(s: &str) -> Utf16 {
    Utf16::from_utf8(s)
}

fn key(s: &str) -> windows_platform_isolation::KeyPath {
    windows_platform_isolation::KeyPath::parse(s)
}

fn fresh() -> (Registry<TreeSurface<Win32OrdinalCasing>>, HandleTable) {
    (Registry::in_memory(Hive::new()), HandleTable::new())
}

#[test]
fn predefined_roots_resolve_and_accept_subkey_creation() {
    let (mut reg, handles) = fresh();
    for root in [HKCU, HKLM] {
        let h = reg_ops::create_key(&mut reg, &handles, root, &key("Software\\Vendor\\App"))
            .expect("create under predefined root");
        assert!(is_minted_value(h));
        let opened = reg_ops::open_key(&mut reg, &handles, root, &key("Software\\Vendor\\App"))
            .expect("open the just-created key");
        assert!(is_minted_value(opened));
    }
}

#[test]
fn open_close_lifecycle_matches_win32_contracts() {
    let (mut reg, handles) = fresh();

    // Opening a missing key fails with ERROR_FILE_NOT_FOUND.
    assert_eq!(
        reg_ops::open_key(&mut reg, &handles, HKCU, &key("Missing")),
        Err(ERROR_FILE_NOT_FOUND as Lstatus)
    );

    // A bogus handle is ERROR_INVALID_HANDLE.
    assert_eq!(
        reg_ops::open_key(&mut reg, &handles, BOGUS, &key("X")),
        Err(ERROR_INVALID_HANDLE as Lstatus)
    );

    // Create, then open, then close the minted handle.
    let created = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Live")).unwrap();
    let opened = reg_ops::open_key(&mut reg, &handles, HKCU, &key("Live")).unwrap();
    assert_ne!(created, opened, "each open mints a distinct handle");

    // Closing a predefined pseudo-handle succeeds as a no-op.
    assert_eq!(reg_ops::close_key(&handles, HKCU), ERROR_SUCCESS as Lstatus);

    // Closing a minted handle releases it; a second close is invalid.
    assert_eq!(reg_ops::close_key(&handles, created), ERROR_SUCCESS as Lstatus);
    assert_eq!(
        reg_ops::close_key(&handles, created),
        ERROR_INVALID_HANDLE as Lstatus
    );

    // The other handle remains live.
    assert!(
        handles
            .with(opened, |p| matches!(p, HandlePayload::RegistryKey(_)))
            .unwrap()
    );
}

#[test]
fn all_six_value_types_round_trip_through_the_codec() {
    let (mut reg, handles) = fresh();
    let k = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Values")).unwrap();

    let cases: Vec<(u32, Vec<u8>)> = vec![
        value_codec::encode(&ValueData::String(w("hello world"))),
        value_codec::encode(&ValueData::ExpandString(w("%TEMP%\\x"))),
        value_codec::encode(&ValueData::MultiString(vec![w("a"), w("bb"), w("ccc")])),
        value_codec::encode(&ValueData::Dword(0xDEAD_BEEF)),
        value_codec::encode(&ValueData::Qword(0x0123_4567_89AB_CDEF)),
        value_codec::encode(&ValueData::Binary(vec![0, 1, 2, 254, 255])),
    ];
    let expected_types = [
        REG_SZ,
        REG_EXPAND_SZ,
        REG_MULTI_SZ,
        REG_DWORD,
        REG_QWORD,
        REG_BINARY,
    ];

    for (i, (vtype, bytes)) in cases.iter().enumerate() {
        let name = w(&format!("v{i}"));
        reg_ops::set_value(&mut reg, &handles, k, &name, *vtype, bytes).unwrap();
        let (read_type, read_bytes) = reg_ops::query_value(&mut reg, &handles, k, &name).unwrap();
        assert_eq!(read_type, expected_types[i], "type tag for v{i}");
        assert_eq!(&read_bytes, bytes, "byte-faithful round-trip for v{i}");
    }
}

#[test]
fn query_value_three_case_buffer_contract() {
    let (mut reg, handles) = fresh();
    let k = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Buf")).unwrap();
    let (vtype, bytes) = value_codec::encode(&ValueData::String(w("data")));
    reg_ops::set_value(&mut reg, &handles, k, &w("n"), vtype, &bytes).unwrap();

    // The query core returns the full payload; the buffer contract is applied
    // independently (the exported ABI uses exactly this helper).
    let (_, payload) = reg_ops::query_value(&mut reg, &handles, k, &w("n")).unwrap();

    // Case 1 — size query (no buffer): success, required = payload length.
    let q = reg_ops::apply_query_buffer(payload.len(), false, 0);
    assert_eq!(q.status, ERROR_SUCCESS as Lstatus);
    assert_eq!(q.required, payload.len());
    assert!(!q.copy);

    // Case 2 — buffer too small: ERROR_MORE_DATA, no copy, required reported.
    let q = reg_ops::apply_query_buffer(payload.len(), true, payload.len() - 1);
    assert_eq!(q.status, ERROR_MORE_DATA as Lstatus);
    assert_eq!(q.required, payload.len());
    assert!(!q.copy);

    // Case 3 — buffer large enough: success, copy.
    let q = reg_ops::apply_query_buffer(payload.len(), true, payload.len());
    assert_eq!(q.status, ERROR_SUCCESS as Lstatus);
    assert!(q.copy);
}

#[test]
fn set_query_delete_value_cycle() {
    let (mut reg, handles) = fresh();
    let k = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Cycle")).unwrap();
    let (vtype, bytes) = value_codec::encode(&ValueData::Dword(7));
    reg_ops::set_value(&mut reg, &handles, k, &w("n"), vtype, &bytes).unwrap();

    assert!(reg_ops::query_value(&mut reg, &handles, k, &w("n")).is_ok());
    reg_ops::delete_value(&mut reg, &handles, k, &w("n")).unwrap();
    assert_eq!(
        reg_ops::query_value(&mut reg, &handles, k, &w("n")),
        Err(ERROR_FILE_NOT_FOUND as Lstatus)
    );
}

#[test]
fn get_value_under_reads_through_a_subkey() {
    let (mut reg, handles) = fresh();
    let k = reg_ops::create_key(&mut reg, &handles, HKCU, &key("A\\B")).unwrap();
    let (vtype, bytes) = value_codec::encode(&ValueData::Qword(42));
    reg_ops::set_value(&mut reg, &handles, k, &w("q"), vtype, &bytes).unwrap();

    // Read it relative to HKCU\A using the subkey "B" (the RegGetValueW shape).
    let base = reg_ops::open_key(&mut reg, &handles, HKCU, &key("A")).unwrap();
    let (read_type, read_bytes) =
        reg_ops::get_value_under(&mut reg, &handles, base, &key("B"), &w("q")).unwrap();
    assert_eq!(read_type, REG_QWORD);
    assert_eq!(read_bytes, bytes);
}

#[test]
fn enumeration_is_ordinal_and_terminates() {
    let (mut reg, handles) = fresh();
    let root = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Enum")).unwrap();

    // Larger-scale data: 300 subkeys and 300 values inserted out of order.
    const COUNT: usize = 300;
    for i in 0..COUNT {
        let n = (i * 7) % COUNT; // pseudo-shuffled insertion order
        reg_ops::create_key(&mut reg, &handles, root, &key(&format!("k{n:04}"))).unwrap();
        let (vtype, bytes) = value_codec::encode(&ValueData::Dword(n as u32));
        reg_ops::set_value(&mut reg, &handles, root, &w(&format!("v{n:04}")), vtype, &bytes)
            .unwrap();
    }

    // Subkeys enumerate in ordinal (ascending) order.
    let mut prev: Option<Utf16> = None;
    for idx in 0..COUNT {
        let name = reg_ops::enum_key(&mut reg, &handles, root, idx as u32)
            .unwrap()
            .expect("subkey present");
        if let Some(p) = &prev {
            assert!(p.as_units() < name.as_units(), "ordinal order at {idx}");
        }
        prev = Some(name);
    }
    // One past the end yields None (ERROR_NO_MORE_ITEMS at the ABI).
    assert_eq!(
        reg_ops::enum_key(&mut reg, &handles, root, COUNT as u32).unwrap(),
        None
    );

    // Values enumerate in ordinal order too, with their types preserved.
    let first = reg_ops::enum_value(&mut reg, &handles, root, 0)
        .unwrap()
        .expect("value present");
    assert_eq!(first.0, w("v0000"));
    assert_eq!(first.1, REG_DWORD);
    assert_eq!(
        reg_ops::enum_value(&mut reg, &handles, root, COUNT as u32).unwrap(),
        None
    );
}

#[test]
fn query_info_reports_counts_and_maxima() {
    let (mut reg, handles) = fresh();
    let root = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Info")).unwrap();
    reg_ops::create_key(&mut reg, &handles, root, &key("short")).unwrap();
    reg_ops::create_key(&mut reg, &handles, root, &key("a_longer_subkey")).unwrap();
    let (vtype, bytes) = value_codec::encode(&ValueData::Binary(vec![0u8; 16]));
    reg_ops::set_value(&mut reg, &handles, root, &w("value_name"), vtype, &bytes).unwrap();

    let info = reg_ops::query_info(&mut reg, &handles, root).unwrap();
    assert_eq!(info.subkeys, 2);
    assert_eq!(info.max_subkey_len, "a_longer_subkey".len() as u32);
    assert_eq!(info.values, 1);
    assert_eq!(info.max_value_name_len, "value_name".len() as u32);
    assert_eq!(info.max_value_len, 16);
}

#[test]
fn delete_key_removes_subtree() {
    let (mut reg, handles) = fresh();
    reg_ops::create_key(&mut reg, &handles, HKCU, &key("Doomed\\Child\\Grandchild")).unwrap();
    reg_ops::delete_key(&mut reg, &handles, HKCU, &key("Doomed")).unwrap();
    assert_eq!(
        reg_ops::open_key(&mut reg, &handles, HKCU, &key("Doomed")),
        Err(ERROR_FILE_NOT_FOUND as Lstatus)
    );
}

#[test]
fn value_operations_against_a_bogus_handle_are_invalid() {
    let (mut reg, handles) = fresh();
    let (vtype, bytes) = value_codec::encode(&ValueData::Dword(1));
    assert_eq!(
        reg_ops::set_value(&mut reg, &handles, BOGUS, &w("n"), vtype, &bytes),
        Err(ERROR_INVALID_HANDLE as Lstatus)
    );
    assert_eq!(
        reg_ops::query_value(&mut reg, &handles, BOGUS, &w("n")),
        Err(ERROR_INVALID_HANDLE as Lstatus)
    );
}
