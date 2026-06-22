// Copyright (c) Microsoft Corporation.

//! MW3 filesystem integration tests.
//!
//! These drive the safe, surface-generic filesystem core ([`fs_ops`]) against an
//! in-memory [`Filesystem`] (the mandated production `Win32OrdinalCasing`) plus a
//! fresh handle table — the same code path the exported `m*W` entry points
//! delegate to, minus the raw-pointer marshaling. They mirror the C++
//! `test_mwinfile_handle_meta` / `test_mwinfile_legacy` scenarios (create/open
//! dispositions, attribute + size metadata, directory lifecycle, file-pointer
//! arithmetic, and find-first/find-next enumeration) and add larger-scale
//! enumeration coverage.

#![cfg(windows)]

use windows_platform_isolation::{
    DirEntry, FileMetadata, FilePath, FileTree, Filesystem, NodeKind, TreeFsSurface,
    Win32OrdinalCasing,
};
use windows_sys::Win32::Foundation::{
    ERROR_ALREADY_EXISTS, ERROR_FILE_EXISTS, ERROR_FILE_NOT_FOUND, ERROR_INVALID_HANDLE,
    ERROR_INVALID_PARAMETER, ERROR_NEGATIVE_SEEK,
};
use windows_sys::Win32::Storage::FileSystem::{
    CREATE_ALWAYS, CREATE_NEW, FILE_ATTRIBUTE_DIRECTORY, FILE_BEGIN, FILE_CURRENT, FILE_END,
    FILE_FLAG_BACKUP_SEMANTICS, OPEN_ALWAYS, OPEN_EXISTING, TRUNCATE_EXISTING,
};
use windows_win32_shim::fs_ops;
use windows_win32_shim::{HandleTable, RawHandle, SearchOp};

const BOGUS: RawHandle = 0x4000_0000;

fn p(s: &str) -> FilePath {
    FilePath::from_utf8(s)
}

fn file_md(size: u64) -> FileMetadata {
    FileMetadata {
        size,
        ..FileMetadata::default()
    }
}

fn dir_md() -> FileMetadata {
    FileMetadata {
        attributes: FILE_ATTRIBUTE_DIRECTORY,
        ..FileMetadata::default()
    }
}

/// A small seeded tree: `C:\data` holding three files of distinct sizes, plus a
/// nested `C:\data\sub` directory.
fn fresh() -> (Filesystem<TreeFsSurface<Win32OrdinalCasing>>, HandleTable) {
    let mut tree = FileTree::new();
    tree.insert_dir(&Win32OrdinalCasing, &p("C:\\data"), dir_md());
    tree.insert_dir(&Win32OrdinalCasing, &p("C:\\data\\sub"), dir_md());
    tree.insert_file(&Win32OrdinalCasing, &p("C:\\data\\one.txt"), file_md(10));
    tree.insert_file(&Win32OrdinalCasing, &p("C:\\data\\two.txt"), file_md(20));
    tree.insert_file(&Win32OrdinalCasing, &p("C:\\data\\three.txt"), file_md(30));
    (Filesystem::in_memory(tree), HandleTable::new())
}

fn name_of(entry: &DirEntry) -> String {
    String::from_utf16_lossy(entry.name.as_units())
}

#[test]
fn create_dispositions_match_win32_contracts() {
    let (mut fs, handles) = fresh();
    let existing = p("C:\\data\\one.txt");
    let missing = p("C:\\data\\absent.txt");

    // OPEN_EXISTING / TRUNCATE_EXISTING require the file to exist.
    assert_eq!(
        fs_ops::create_file(&mut fs, &handles, &missing, OPEN_EXISTING, 0),
        Err(ERROR_FILE_NOT_FOUND)
    );
    assert_eq!(
        fs_ops::create_file(&mut fs, &handles, &missing, TRUNCATE_EXISTING, 0),
        Err(ERROR_FILE_NOT_FOUND)
    );
    assert!(fs_ops::create_file(&mut fs, &handles, &existing, OPEN_EXISTING, 0).is_ok());

    // CREATE_NEW fails on an existing name, succeeds on a free one.
    assert_eq!(
        fs_ops::create_file(&mut fs, &handles, &existing, CREATE_NEW, 0),
        Err(ERROR_FILE_EXISTS)
    );
    assert!(fs_ops::create_file(&mut fs, &handles, &missing, CREATE_NEW, 0).is_ok());
    assert!(fs.file_exists(&missing).unwrap());

    // CREATE_ALWAYS / OPEN_ALWAYS both succeed (one replaces, one opens).
    assert!(fs_ops::create_file(&mut fs, &handles, &existing, CREATE_ALWAYS, 0).is_ok());
    assert!(fs_ops::create_file(&mut fs, &handles, &existing, OPEN_ALWAYS, 0).is_ok());

    // An unknown disposition is rejected.
    assert_eq!(
        fs_ops::create_file(&mut fs, &handles, &existing, 0, 0),
        Err(ERROR_INVALID_PARAMETER)
    );
}

#[test]
fn backup_semantics_opens_directories_only() {
    let (mut fs, handles) = fresh();
    assert!(
        fs_ops::create_file(
            &mut fs,
            &handles,
            &p("C:\\data"),
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS
        )
        .is_ok()
    );
    // A file path cannot be opened as a backup-semantics directory handle.
    assert_eq!(
        fs_ops::create_file(
            &mut fs,
            &handles,
            &p("C:\\data\\one.txt"),
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS
        ),
        Err(ERROR_FILE_NOT_FOUND)
    );
}

#[test]
fn attributes_and_size_report_metadata() {
    let (mut fs, _) = fresh();

    let (fmd, fkind) = fs_ops::stat_path(&mut fs, &p("C:\\data\\two.txt")).unwrap();
    assert_eq!(fkind, NodeKind::File);
    assert_eq!(fmd.size, 20);
    assert_eq!(
        fs_ops::to_win32_attributes(&fmd, fkind) & FILE_ATTRIBUTE_DIRECTORY,
        0
    );

    let (dmd, dkind) = fs_ops::stat_path(&mut fs, &p("C:\\data\\sub")).unwrap();
    assert_eq!(dkind, NodeKind::Directory);
    assert_ne!(
        fs_ops::to_win32_attributes(&dmd, dkind) & FILE_ATTRIBUTE_DIRECTORY,
        0
    );

    // A missing path is reported as not found.
    assert_eq!(
        fs_ops::stat_path(&mut fs, &p("C:\\data\\absent.txt")),
        Err(ERROR_FILE_NOT_FOUND)
    );
}

#[test]
fn get_file_size_reads_through_a_handle() {
    let (mut fs, handles) = fresh();
    let h = fs_ops::create_file(&mut fs, &handles, &p("C:\\data\\three.txt"), OPEN_EXISTING, 0)
        .unwrap();
    assert_eq!(fs_ops::get_file_size(&mut fs, &handles, h).unwrap(), 30);
    // A handle that was never minted is invalid.
    assert_eq!(
        fs_ops::get_file_size(&mut fs, &handles, BOGUS),
        Err(ERROR_INVALID_HANDLE)
    );
}

#[test]
fn set_file_pointer_walks_all_origins() {
    let (mut fs, handles) = fresh();
    let h = fs_ops::create_file(&mut fs, &handles, &p("C:\\data\\two.txt"), OPEN_EXISTING, 0)
        .unwrap();
    // two.txt has size 20.
    assert_eq!(
        fs_ops::set_file_pointer(&mut fs, &handles, h, 0, FILE_END).unwrap(),
        20
    );
    assert_eq!(
        fs_ops::set_file_pointer(&mut fs, &handles, h, 5, FILE_BEGIN).unwrap(),
        5
    );
    assert_eq!(
        fs_ops::set_file_pointer(&mut fs, &handles, h, 4, FILE_CURRENT).unwrap(),
        9
    );
    assert_eq!(
        fs_ops::set_file_pointer(&mut fs, &handles, h, -100, FILE_END),
        Err(ERROR_NEGATIVE_SEEK)
    );
}

#[test]
fn directory_lifecycle_matches_win32() {
    let (mut fs, _) = fresh();
    // Creating an existing directory is ERROR_ALREADY_EXISTS.
    assert_eq!(
        fs_ops::create_directory(&mut fs, &p("C:\\data\\sub")),
        Err(ERROR_ALREADY_EXISTS)
    );
    // A fresh directory creates and removes cleanly.
    let nd = p("C:\\data\\fresh");
    fs_ops::create_directory(&mut fs, &nd).unwrap();
    assert!(fs.dir_exists(&nd).unwrap());
    fs_ops::remove_directory(&mut fs, &nd).unwrap();
    assert!(!fs.dir_exists(&nd).unwrap());
}

#[test]
fn delete_and_set_attributes_behave() {
    let (mut fs, _) = fresh();
    let f = p("C:\\data\\one.txt");
    // SetFileAttributes accepts-and-ignores on an existing node.
    fs_ops::set_file_attributes(&mut fs, &f, 0x1).unwrap();
    // Delete removes the node; SetFileAttributes then reports not found.
    fs_ops::delete_file(&mut fs, &f).unwrap();
    assert!(!fs.file_exists(&f).unwrap());
    assert_eq!(
        fs_ops::set_file_attributes(&mut fs, &f, 0x1),
        Err(ERROR_FILE_NOT_FOUND)
    );
}

#[test]
fn find_first_then_next_enumerates_in_ordinal_order() {
    let (mut fs, handles) = fresh();
    let (h, first) = fs_ops::find_first(&mut fs, &handles, &p("C:\\data\\*"), SearchOp::NameMatch, false, true)
        .unwrap()
        .expect("non-empty listing");
    let mut names = vec![name_of(&first)];
    while let Some((entry, _)) = fs_ops::find_next(&handles, h).unwrap() {
        names.push(name_of(&entry));
    }
    // Ordinal order: 'one.txt', 'sub', 'three.txt', 'two.txt' sort by UTF-16 unit.
    assert_eq!(names, vec!["one.txt", "sub", "three.txt", "two.txt"]);
    // Past the end yields None (caller maps to ERROR_NO_MORE_FILES).
    assert_eq!(fs_ops::find_next(&handles, h).unwrap(), None);
    assert!(handles.close(h));
}

#[test]
fn find_first_rejects_parentless_and_reports_empty() {
    let (mut fs, handles) = fresh();
    // A rootless single component has no parent directory.
    assert_eq!(
        fs_ops::find_first(&mut fs, &handles, &p("loose"), SearchOp::NameMatch, false, true),
        Err(ERROR_INVALID_PARAMETER)
    );
    // An empty directory yields Ok(None).
    assert_eq!(
        fs_ops::find_first(&mut fs, &handles, &p("C:\\data\\sub\\*"), SearchOp::NameMatch, false, true),
        Ok(None)
    );
}

#[test]
fn find_next_rejects_non_find_handles() {
    let (mut fs, handles) = fresh();
    let file =
        fs_ops::create_file(&mut fs, &handles, &p("C:\\data\\one.txt"), OPEN_EXISTING, 0).unwrap();
    assert_eq!(fs_ops::find_next(&handles, file), Err(ERROR_INVALID_HANDLE));
    assert_eq!(fs_ops::find_next(&handles, BOGUS), Err(ERROR_INVALID_HANDLE));
}

#[test]
fn large_scale_enumeration_is_complete_and_ordered() {
    let mut tree = FileTree::new();
    tree.insert_dir(&Win32OrdinalCasing, &p("C:\\bulk"), dir_md());
    // Seed many files with zero-padded names so ordinal order is well-defined.
    const COUNT: usize = 500;
    for i in 0..COUNT {
        let name = format!("C:\\bulk\\f{i:04}.dat");
        tree.insert_file(&Win32OrdinalCasing, &p(&name), file_md(i as u64));
    }
    let mut fs = Filesystem::in_memory(tree);
    let handles = HandleTable::new();

    let (h, first) = fs_ops::find_first(&mut fs, &handles, &p("C:\\bulk\\*"), SearchOp::NameMatch, false, true)
        .unwrap()
        .expect("non-empty listing");
    let mut names = vec![name_of(&first)];
    while let Some((entry, _)) = fs_ops::find_next(&handles, h).unwrap() {
        names.push(name_of(&entry));
    }
    assert_eq!(names.len(), COUNT);
    // Already ordinal-ordered by the surface; confirm and check endpoints.
    let mut sorted = names.clone();
    sorted.sort();
    assert_eq!(names, sorted);
    assert_eq!(names.first().unwrap(), "f0000.dat");
    assert_eq!(names.last().unwrap(), "f0499.dat");
}
