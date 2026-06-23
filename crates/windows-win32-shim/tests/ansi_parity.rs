// Copyright (c) Microsoft Corporation.

//! MW6 ANSI (`A`) / wide (`W`) parity integration tests.
//!
//! The `*A` entry points differ from their `*W` peers only at the string
//! boundary (`CP_ACP` bytes vs UTF-16) before delegating to the **same**
//! surface-generic `reg_ops` / `fs_ops` core (SHIM-D15). These tests reproduce
//! that boundary sequence with the public [`ansi`] helpers over in-memory
//! `Registry` / `Filesystem` fixtures — the same structural-isolation pattern
//! the `registry.rs` / `filesystem.rs` suites use — and assert the two spellings
//! observe one another:
//!
//! * **registry** — an `A`-writer's stored bytes equal a `W`-writer's, and a
//!   `W`-writer's value reads back through the `A` path as the original `CP_ACP`
//!   bytes (`REG_SZ` and `REG_MULTI_SZ`, the textual cases the C++
//!   `test_mwinreg_value_ops` exercises);
//! * **filesystem** — every entry's reconstructed `WIN32_FIND_DATAA` `cFileName`
//!   (built via [`ansi::fill_ansi_fixed`], exactly as `fill_find_data_ansi` does)
//!   decodes back to the `W` enumeration's name.

#![cfg(windows)]

use windows_platform_isolation::{
    DirEntry, FileMetadata, FilePath, FileTree, Filesystem, Hive, Registry, TreeSurface, Utf16,
    Win32OrdinalCasing,
};
use windows_sys::Win32::System::Registry::{REG_MULTI_SZ, REG_SZ};
use windows_win32_shim::{HandleTable, RawHandle, SearchOp, ansi, fs_ops, reg_ops};

// Predefined `HKEY` reserved value (bare 32-bit form).
const HKCU: RawHandle = 0x8000_0001;

fn w(s: &str) -> Utf16 {
    Utf16::from_utf8(s)
}

fn key(s: &str) -> windows_platform_isolation::KeyPath {
    windows_platform_isolation::KeyPath::parse(s)
}

fn p(s: &str) -> FilePath {
    FilePath::from_utf8(s)
}

fn file_md(size: u64) -> FileMetadata {
    FileMetadata {
        size,
        ..FileMetadata::default()
    }
}

fn name_of(entry: &DirEntry) -> String {
    String::from_utf16_lossy(entry.name.as_units())
}

fn fresh_registry() -> (Registry<TreeSurface<Win32OrdinalCasing>>, HandleTable) {
    (Registry::in_memory(Hive::new()), HandleTable::new())
}

/// Serialize UTF-16 units into the registry stored form (UTF-16 LE bytes).
fn stored_bytes(units: &[u16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(units.len() * 2);
    for &u in units {
        out.extend_from_slice(&u.to_le_bytes());
    }
    out
}

// --- Registry value parity ---------------------------------------------------

/// `A`-writer / `W`-reader: setting a `REG_SZ` value through the `A` boundary
/// (`CP_ACP` bytes → `data_ansi_to_wide` → core) stores exactly what a
/// `W`-writer storing the same logical string would, and the `W` reader sees it.
#[test]
fn reg_sz_a_writer_w_reader_parity() {
    let (mut reg, handles) = fresh_registry();
    let k = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Values")).unwrap();

    // The logical string and its two on-the-wire forms.
    let text = "Hello world";
    let wide_units: Vec<u16> = w(text)
        .as_units()
        .iter()
        .copied()
        .chain(std::iter::once(0)) // REG_SZ stored form is NUL-terminated.
        .collect();
    let w_writer_bytes = stored_bytes(&wide_units);

    // What an `A` caller passes (CP_ACP body + NUL), then the boundary widens it.
    let mut ansi_input = ansi::utf16_to_ansi(w(text).as_units());
    ansi_input.push(0);
    let a_writer_bytes = ansi::data_ansi_to_wide(REG_SZ, &ansi_input);

    assert_eq!(
        a_writer_bytes, w_writer_bytes,
        "A-writer and W-writer must store identical stored-form bytes"
    );

    // Store through the A path, read through the W (core) path.
    reg_ops::set_value(&mut reg, &handles, k, &w("greeting"), REG_SZ, &a_writer_bytes).unwrap();
    let (read_type, read_bytes) = reg_ops::query_value(&mut reg, &handles, k, &w("greeting")).unwrap();
    assert_eq!(read_type, REG_SZ);
    assert_eq!(read_bytes, w_writer_bytes);
}

/// `W`-writer / `A`-reader: a `REG_SZ` value stored by the `W` core reads back
/// through the `A` boundary (`data_wide_to_ansi`) as the original `CP_ACP` bytes.
#[test]
fn reg_sz_w_writer_a_reader_parity() {
    let (mut reg, handles) = fresh_registry();
    let k = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Values")).unwrap();

    let text = "Config value";
    let wide_units: Vec<u16> = w(text).as_units().iter().copied().chain(std::iter::once(0)).collect();
    reg_ops::set_value(&mut reg, &handles, k, &w("v"), REG_SZ, &stored_bytes(&wide_units)).unwrap();

    let (read_type, read_bytes) = reg_ops::query_value(&mut reg, &handles, k, &w("v")).unwrap();
    let a_reader_bytes = ansi::data_wide_to_ansi(read_type, &read_bytes);

    let mut expected = ansi::utf16_to_ansi(w(text).as_units());
    expected.push(0);
    assert_eq!(a_reader_bytes, expected);
}

/// `REG_MULTI_SZ` carries embedded NULs (the string separators); the `A`/`W`
/// boundary must preserve them in both directions (the property `windows-text`
/// is chosen for — whole-slice conversion by length).
#[test]
fn reg_multi_sz_round_trips_through_both_boundaries() {
    let (mut reg, handles) = fresh_registry();
    let k = reg_ops::create_key(&mut reg, &handles, HKCU, &key("Values")).unwrap();

    // "alpha\0beta\0gamma\0\0" as UTF-16 LE (three strings + list terminator).
    let mut units: Vec<u16> = Vec::new();
    for s in ["alpha", "beta", "gamma"] {
        units.extend(w(s).as_units().iter().copied());
        units.push(0);
    }
    units.push(0);
    let w_bytes = stored_bytes(&units);

    // W-writer / A-reader.
    reg_ops::set_value(&mut reg, &handles, k, &w("list"), REG_MULTI_SZ, &w_bytes).unwrap();
    let (read_type, read_bytes) = reg_ops::query_value(&mut reg, &handles, k, &w("list")).unwrap();
    assert_eq!(read_type, REG_MULTI_SZ);
    let a_bytes = ansi::data_wide_to_ansi(read_type, &read_bytes);
    assert_eq!(a_bytes, b"alpha\0beta\0gamma\0\0");

    // A-reader's bytes widen back to the identical stored form (A-writer path).
    let round_tripped = ansi::data_ansi_to_wide(REG_MULTI_SZ, &a_bytes);
    assert_eq!(round_tripped, w_bytes);
}

// --- Filesystem enumeration parity -------------------------------------------

/// A directory enumeration produces, for each entry, a `WIN32_FIND_DATAA`
/// `cFileName` (reconstructed via the same [`ansi::fill_ansi_fixed`] call the
/// `A` find entry points use) that decodes back to the `W` enumeration's name.
#[test]
fn directory_enumeration_cfilename_a_matches_w() {
    let mut tree = FileTree::new();
    tree.insert_dir(&Win32OrdinalCasing, &p("C:\\find"), {
        FileMetadata {
            attributes: windows_sys::Win32::Storage::FileSystem::FILE_ATTRIBUTE_DIRECTORY,
            ..FileMetadata::default()
        }
    });
    for (i, name) in ["alpha.txt", "beta.txt", "gamma.log"].iter().enumerate() {
        tree.insert_file(
            &Win32OrdinalCasing,
            &p(&format!("C:\\find\\{name}")),
            file_md(i as u64),
        );
    }
    let mut fs = Filesystem::in_memory(tree);
    let handles = HandleTable::new();

    // Enumerate with the W core (same engine the A find forms drive).
    let (h, first) =
        fs_ops::find_first(&mut fs, &handles, &p("C:\\find\\*"), SearchOp::NameMatch, false, true)
            .unwrap()
            .expect("at least one entry");

    let mut entries = vec![first];
    while let Some((entry, _)) = fs_ops::find_next(&handles, h).unwrap() {
        entries.push(entry);
    }
    assert!(entries.len() >= 3, "expected the three seeded files");

    // The `WIN32_FIND_DATAA` cFileName buffer is [CHAR; 260]; reconstruct it for
    // each entry and confirm it decodes back to the W name.
    const CFILENAME_CAP: usize = 260;
    for entry in &entries {
        let w_name = name_of(entry);
        let mut cfilename = [0u8; CFILENAME_CAP];
        ansi::fill_ansi_fixed(entry.name.as_units(), &mut cfilename);
        let nul = cfilename.iter().position(|&b| b == 0).unwrap();
        let a_name = ansi::ansi_to_utf16(&cfilename[..nul]);
        assert_eq!(
            String::from_utf16_lossy(a_name.as_units()),
            w_name,
            "A cFileName must decode to the W enumeration name"
        );
    }
}
