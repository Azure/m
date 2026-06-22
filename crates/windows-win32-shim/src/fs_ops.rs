// Copyright (c) Microsoft Corporation.

//! The safe, surface-generic filesystem core (SHIM-D12).
//!
//! Every filesystem C ABI entry point ([`crate::mwinfile`]) marshals its raw
//! caller pointers into Rust values and then delegates to a function here. These
//! functions are pure safe Rust, generic over the underlying
//! [`FsSurface`](windows_platform_isolation::FsSurface): they operate on a
//! [`Filesystem`] facade plus the process [`HandleTable`], so they can be driven
//! against the live OS filesystem (the default session backing) or an in-memory
//! tree (the integration tests) without change.
//!
//! ## Handle model (SHIM-D12)
//!
//! A minted file `HANDLE` ([`HandlePayload::File`]) carries the **public path**
//! the caller opened plus a sequential byte position; a minted find `HANDLE`
//! ([`HandlePayload::Find`]) carries a captured directory listing plus a cursor.
//! The path-addressed `windows-platform-isolation` facade resolves every
//! operation by absolute path, so the handle table never holds borrowed surface
//! state.
//!
//! ## Owned behavior (SHIM-D12)
//!
//! This crate **specifies** its filesystem behavior (Design Autonomy); the
//! isolation facade is chosen because it satisfies that specification:
//!
//! - Metadata only (the facade models no byte content): content + move/copy
//!   exports report the Win32 not-supported shape ([`crate::mwinfile`]); a file
//!   size is always the metadata size.
//! - Attribute *mutation* is not modeled (the live provider has no
//!   attribute-only write — a metadata write would truncate real content), so
//!   [`set_file_attributes`] validates existence and accepts-and-ignores.
//! - A directory's metadata is recovered from its parent listing (the facade's
//!   `metadata` reads files only); a parentless root synthesizes an empty
//!   directory.

use windows_platform_isolation::{
    DirEntry, FileMetadata, FilePath, Filesystem, FilesystemError, FsSurface, NodeKind,
};
use windows_sys::Win32::Foundation::{
    ERROR_ALREADY_EXISTS, ERROR_FILE_EXISTS, ERROR_FILE_NOT_FOUND, ERROR_INVALID_HANDLE,
    ERROR_INVALID_PARAMETER, ERROR_NEGATIVE_SEEK, WIN32_ERROR,
};
use windows_sys::Win32::Storage::FileSystem::{
    CREATE_ALWAYS, CREATE_NEW, FILE_ATTRIBUTE_DIRECTORY, FILE_ATTRIBUTE_NORMAL, FILE_BEGIN,
    FILE_CURRENT, FILE_END, FILE_FLAG_BACKUP_SEMANTICS, OPEN_ALWAYS, OPEN_EXISTING,
    TRUNCATE_EXISTING,
};

use crate::error_map::filesystem_error_to_win32;
use crate::handle_table::{
    FileHandleState, FindEnumerationState, HandlePayload, HandleTable, RawHandle, SearchOp,
    SearchPredicate,
};
use windows_text::{Win32OrdinalCasing, name_matches_expression};

/// Map a surface error to the `WIN32_ERROR` an entry point reports.
fn fs_err(err: &FilesystemError) -> WIN32_ERROR {
    filesystem_error_to_win32(err)
}

/// Project a node's metadata and kind onto the Win32 attribute bitmask.
///
/// The directory bit is forced to match `kind`; an otherwise-empty mask
/// collapses to `FILE_ATTRIBUTE_NORMAL` (Win32 never reports `0` for an existing
/// node).
#[must_use]
pub fn to_win32_attributes(metadata: &FileMetadata, kind: NodeKind) -> u32 {
    let mut attrs = metadata.attributes;
    match kind {
        NodeKind::Directory => attrs |= FILE_ATTRIBUTE_DIRECTORY,
        NodeKind::File => attrs &= !FILE_ATTRIBUTE_DIRECTORY,
    }
    if attrs == 0 {
        attrs = FILE_ATTRIBUTE_NORMAL;
    }
    attrs
}

/// "Stat" an arbitrary path, recovering its metadata and kind.
///
/// The facade's `metadata` reads files only, so a directory is recovered from
/// its parent listing; a parentless root (e.g. `C:\`) synthesizes an empty
/// directory.
///
/// # Errors
///
/// `ERROR_FILE_NOT_FOUND` when no node exists at `path`; another mapped
/// `WIN32_ERROR` on a surface error.
pub fn stat_path<S: FsSurface>(
    fs: &mut Filesystem<S>,
    path: &FilePath,
) -> Result<(FileMetadata, NodeKind), WIN32_ERROR> {
    match fs.metadata(path) {
        Ok(md) => return Ok((md, NodeKind::File)),
        Err(FilesystemError::NotFound) => {}
        Err(err) => return Err(fs_err(&err)),
    }
    match fs.dir_exists(path) {
        Ok(true) => Ok((directory_metadata(fs, path)?, NodeKind::Directory)),
        Ok(false) => Err(ERROR_FILE_NOT_FOUND),
        Err(err) => Err(fs_err(&err)),
    }
}

/// Recover a directory's metadata from its parent listing, falling back to a
/// synthesized empty-directory metadata for a parentless root or a leaf the
/// parent listing does not surface by exact name.
fn directory_metadata<S: FsSurface>(
    fs: &mut Filesystem<S>,
    path: &FilePath,
) -> Result<FileMetadata, WIN32_ERROR> {
    let (parent, leaf) = path.split_parent_path_and_leaf_name();
    if let Some(parent) = parent {
        let entries = fs.read_dir(&parent).map_err(|e| fs_err(&e))?;
        for entry in entries {
            if entry.kind == NodeKind::Directory
                && entry.name.as_units() == leaf.native().as_units()
            {
                return Ok(entry.metadata);
            }
        }
    }
    Ok(FileMetadata {
        attributes: FILE_ATTRIBUTE_DIRECTORY,
        ..FileMetadata::default()
    })
}

/// Open or create the file (or backup-semantics directory) named by `path`
/// according to `disposition`, minting and returning a fresh file `HANDLE`.
///
/// `FILE_FLAG_BACKUP_SEMANTICS` names an existing directory (the only
/// directory-handle form this milestone serves). Otherwise the creation
/// disposition is interpreted against the file at `path`:
///
/// - `OPEN_EXISTING` / `TRUNCATE_EXISTING`: the file must already exist;
/// - `CREATE_NEW`: the name must be free (file or directory) — else
///   `ERROR_FILE_EXISTS`;
/// - `CREATE_ALWAYS`: create or replace the file (a directory in the way is
///   `ERROR_FILE_EXISTS`);
/// - `OPEN_ALWAYS`: open the file, creating it when absent;
/// - any other value: `ERROR_INVALID_PARAMETER`.
///
/// Content is out of scope (SHIM-D12): `TRUNCATE_EXISTING` does not truncate,
/// and the minted handle resolves metadata only.
///
/// # Errors
///
/// A mapped `WIN32_ERROR` per the disposition rules above or on a surface error.
pub fn create_file<S: FsSurface>(
    fs: &mut Filesystem<S>,
    handles: &HandleTable,
    path: &FilePath,
    disposition: u32,
    flags_and_attributes: u32,
) -> Result<RawHandle, WIN32_ERROR> {
    if flags_and_attributes & FILE_FLAG_BACKUP_SEMANTICS != 0 {
        return match fs.dir_exists(path) {
            Ok(true) => Ok(mint_file(handles, path)),
            Ok(false) => Err(ERROR_FILE_NOT_FOUND),
            Err(err) => Err(fs_err(&err)),
        };
    }

    let file_exists = fs.file_exists(path).map_err(|e| fs_err(&e))?;
    match disposition {
        OPEN_EXISTING | TRUNCATE_EXISTING => {
            if !file_exists {
                return Err(ERROR_FILE_NOT_FOUND);
            }
        }
        CREATE_NEW => {
            if file_exists || fs.dir_exists(path).map_err(|e| fs_err(&e))? {
                return Err(ERROR_FILE_EXISTS);
            }
            fs.write_file(path, FileMetadata::default())
                .map_err(|e| fs_err(&e))?;
        }
        CREATE_ALWAYS => {
            if fs.dir_exists(path).map_err(|e| fs_err(&e))? {
                return Err(ERROR_FILE_EXISTS);
            }
            fs.write_file(path, FileMetadata::default())
                .map_err(|e| fs_err(&e))?;
        }
        OPEN_ALWAYS => {
            if !file_exists {
                if fs.dir_exists(path).map_err(|e| fs_err(&e))? {
                    return Err(ERROR_FILE_EXISTS);
                }
                fs.write_file(path, FileMetadata::default())
                    .map_err(|e| fs_err(&e))?;
            }
        }
        _ => return Err(ERROR_INVALID_PARAMETER),
    }
    Ok(mint_file(handles, path))
}

/// Intern a fresh file handle for `path` at byte position 0.
fn mint_file(handles: &HandleTable, path: &FilePath) -> RawHandle {
    handles.intern(HandlePayload::File(FileHandleState {
        path: path.clone(),
        position: 0,
    }))
}

/// Delete the file named by `path`.
///
/// # Errors
///
/// A mapped `WIN32_ERROR` on a surface error.
pub fn delete_file<S: FsSurface>(
    fs: &mut Filesystem<S>,
    path: &FilePath,
) -> Result<(), WIN32_ERROR> {
    fs.remove_file(path).map_err(|e| fs_err(&e))
}

/// Create the directory named by `path`.
///
/// Matches the Win32 `CreateDirectory` contract that an already-existing
/// directory is `ERROR_ALREADY_EXISTS`.
///
/// # Errors
///
/// `ERROR_ALREADY_EXISTS` when the directory already exists; another mapped
/// `WIN32_ERROR` on a surface error.
pub fn create_directory<S: FsSurface>(
    fs: &mut Filesystem<S>,
    path: &FilePath,
) -> Result<(), WIN32_ERROR> {
    if fs.dir_exists(path).map_err(|e| fs_err(&e))? {
        return Err(ERROR_ALREADY_EXISTS);
    }
    fs.create_dir(path).map_err(|e| fs_err(&e))
}

/// Remove the directory (and its subtree) named by `path`.
///
/// # Errors
///
/// A mapped `WIN32_ERROR` on a surface error.
pub fn remove_directory<S: FsSurface>(
    fs: &mut Filesystem<S>,
    path: &FilePath,
) -> Result<(), WIN32_ERROR> {
    fs.remove_dir(path).map_err(|e| fs_err(&e))
}

/// Set the attributes of the node named by `path`.
///
/// Attribute mutation is not modeled this milestone (SHIM-D12): the live
/// provider has no attribute-only write, and a metadata write would truncate
/// real content. This validates that the node exists and then accepts-and-
/// ignores the request, reporting success.
///
/// # Errors
///
/// `ERROR_FILE_NOT_FOUND` when no node exists at `path`; another mapped
/// `WIN32_ERROR` on a surface error.
pub fn set_file_attributes<S: FsSurface>(
    fs: &mut Filesystem<S>,
    path: &FilePath,
    _attributes: u32,
) -> Result<(), WIN32_ERROR> {
    stat_path(fs, path).map(|_| ())
}

/// The byte size of the file behind a minted file `HANDLE`.
///
/// # Errors
///
/// `ERROR_INVALID_HANDLE` when `handle` is not a live file handle; another
/// mapped `WIN32_ERROR` on a surface error.
pub fn get_file_size<S: FsSurface>(
    fs: &mut Filesystem<S>,
    handles: &HandleTable,
    handle: RawHandle,
) -> Result<u64, WIN32_ERROR> {
    let path = file_handle_path(handles, handle)?;
    let (metadata, _) = stat_path(fs, &path)?;
    Ok(metadata.size)
}

/// Move the byte cursor of a minted file `HANDLE`, returning the new position.
///
/// `method` is `FILE_BEGIN` / `FILE_CURRENT` / `FILE_END`; `distance` is a
/// signed offset from that origin. A resulting position before the start of the
/// file is `ERROR_NEGATIVE_SEEK`.
///
/// # Errors
///
/// `ERROR_INVALID_HANDLE` when `handle` is not a live file handle;
/// `ERROR_INVALID_PARAMETER` for an unknown `method`; `ERROR_NEGATIVE_SEEK` on a
/// negative target; another mapped `WIN32_ERROR` on a surface error.
pub fn set_file_pointer<S: FsSurface>(
    fs: &mut Filesystem<S>,
    handles: &HandleTable,
    handle: RawHandle,
    distance: i64,
    method: u32,
) -> Result<u64, WIN32_ERROR> {
    let base: i128 = match method {
        FILE_BEGIN => 0,
        FILE_CURRENT => i128::from(file_handle_position(handles, handle)?),
        FILE_END => {
            let path = file_handle_path(handles, handle)?;
            i128::from(stat_path(fs, &path)?.0.size)
        }
        _ => return Err(ERROR_INVALID_PARAMETER),
    };
    let target = base + i128::from(distance);
    if target < 0 {
        return Err(ERROR_NEGATIVE_SEEK);
    }
    let new_position = target as u64;
    let updated = handles
        .with_mut(handle, |payload| match payload {
            HandlePayload::File(state) => {
                state.position = new_position;
                true
            }
            _ => false,
        })
        .unwrap_or(false);
    if updated {
        Ok(new_position)
    } else {
        Err(ERROR_INVALID_HANDLE)
    }
}

/// The public path behind a minted file `HANDLE`.
fn file_handle_path(handles: &HandleTable, handle: RawHandle) -> Result<FilePath, WIN32_ERROR> {
    handles
        .with(handle, |payload| match payload {
            HandlePayload::File(state) => Some(state.path.clone()),
            _ => None,
        })
        .flatten()
        .ok_or(ERROR_INVALID_HANDLE)
}

/// The byte position behind a minted file `HANDLE`.
fn file_handle_position(handles: &HandleTable, handle: RawHandle) -> Result<u64, WIN32_ERROR> {
    handles
        .with(handle, |payload| match payload {
            HandlePayload::File(state) => Some(state.position),
            _ => None,
        })
        .flatten()
        .ok_or(ERROR_INVALID_HANDLE)
}

/// Begin a directory enumeration for `pattern`, minting a find `HANDLE` and
/// returning it alongside the first entry that satisfies the search filter.
///
/// The pattern's leaf (wildcard or literal) **is** applied: it is captured into a
/// [`SearchPredicate`] along with `op` and `case_sensitive`, and every entry
/// yielded by this enumeration (here and through [`find_next`]) is matched
/// against it. Name matching uses Win32 DOS-wildcard semantics (delegated to the
/// windows-text matcher, WT-6) under the mandated Win32 ordinal casing (D6/D8);
/// `SearchOp::LimitToDirectories` additionally requires the entry to be a
/// directory. A rootless single component (no parent) is rejected as an invalid
/// parameter, matching the C++ shim. A listing with no matching entry yields
/// `Ok(None)` (the caller reports `ERROR_FILE_NOT_FOUND`).
///
/// # Errors
///
/// `ERROR_INVALID_PARAMETER` for a parentless pattern; another mapped
/// `WIN32_ERROR` on a surface error.
pub fn find_first<S: FsSurface>(
    fs: &mut Filesystem<S>,
    handles: &HandleTable,
    pattern: &FilePath,
    op: SearchOp,
    case_sensitive: bool,
) -> Result<Option<(RawHandle, DirEntry)>, WIN32_ERROR> {
    let (parent, leaf) = pattern.split_parent_path_and_leaf_name();
    let Some(parent) = parent else {
        return Err(ERROR_INVALID_PARAMETER);
    };
    let predicate = SearchPredicate {
        pattern_leaf: leaf.native().clone(),
        op,
        case_sensitive,
    };
    let entries = fs.read_dir(&parent).map_err(|e| fs_err(&e))?;
    let mut cursor = 0;
    while cursor < entries.len() && !predicate_matches(&predicate, &entries[cursor]) {
        cursor += 1;
    }
    if cursor >= entries.len() {
        return Ok(None);
    }
    let first = entries[cursor].clone();
    let handle = handles.intern(HandlePayload::Find(FindEnumerationState {
        entries,
        cursor: cursor + 1,
        predicate,
    }));
    Ok(Some((handle, first)))
}

/// Whether `entry` satisfies the search `predicate`. The leaf is matched against
/// the entry name with Win32 DOS-wildcard semantics (windows-text WT-6) under the
/// mandated Win32 ordinal casing (D6/D8); `LimitToDirectories` additionally
/// requires the entry to be a directory.
fn predicate_matches(predicate: &SearchPredicate, entry: &DirEntry) -> bool {
    let name_ok = name_matches_expression(
        entry.name.as_units(),
        predicate.pattern_leaf.as_units(),
        &Win32OrdinalCasing,
        predicate.case_sensitive,
    );
    match predicate.op {
        SearchOp::NameMatch => name_ok,
        SearchOp::LimitToDirectories => name_ok && entry.kind == NodeKind::Directory,
    }
}

/// Advance a find enumeration, returning the next entry that satisfies the
/// enumeration's captured [`SearchPredicate`], or `Ok(None)` once the listing is
/// exhausted (the caller reports `ERROR_NO_MORE_FILES`).
///
/// # Errors
///
/// `ERROR_INVALID_HANDLE` when `handle` is not a live find handle.
pub fn find_next(
    handles: &HandleTable,
    handle: RawHandle,
) -> Result<Option<DirEntry>, WIN32_ERROR> {
    handles
        .with_mut(handle, |payload| match payload {
            HandlePayload::Find(state) => {
                while state.cursor < state.entries.len() {
                    let entry = state.entries[state.cursor].clone();
                    state.cursor += 1;
                    if predicate_matches(&state.predicate, &entry) {
                        return Ok(Some(entry));
                    }
                }
                Ok(None)
            }
            _ => Err(ERROR_INVALID_HANDLE),
        })
        .unwrap_or(Err(ERROR_INVALID_HANDLE))
}

#[cfg(test)]
mod tests {
    use super::*;
    use windows_platform_isolation::{FileTree, Win32OrdinalCasing};

    fn p(s: &str) -> FilePath {
        FilePath::from_utf8(s)
    }

    /// Build an in-memory filesystem seeded with a small tree, plus a fresh
    /// handle table — the same code path the `m*W` entry points delegate to.
    fn fresh() -> (
        Filesystem<windows_platform_isolation::TreeFsSurface<Win32OrdinalCasing>>,
        HandleTable,
    ) {
        let mut tree = FileTree::new();
        tree.insert_dir(&Win32OrdinalCasing, &p("C:\\dir"), dir_md());
        tree.insert_file(&Win32OrdinalCasing, &p("C:\\dir\\bravo.txt"), file_md(7));
        tree.insert_file(&Win32OrdinalCasing, &p("C:\\dir\\alpha.txt"), file_md(3));
        tree.insert_file(&Win32OrdinalCasing, &p("C:\\dir\\charlie.txt"), file_md(11));
        (Filesystem::in_memory(tree), HandleTable::new())
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

    #[test]
    fn attributes_distinguish_file_and_directory() {
        let (mut fs, _) = fresh();
        let (fmd, fkind) = stat_path(&mut fs, &p("C:\\dir\\alpha.txt")).unwrap();
        assert_eq!(fkind, NodeKind::File);
        assert_eq!(to_win32_attributes(&fmd, fkind) & FILE_ATTRIBUTE_DIRECTORY, 0);

        let (dmd, dkind) = stat_path(&mut fs, &p("C:\\dir")).unwrap();
        assert_eq!(dkind, NodeKind::Directory);
        assert_ne!(
            to_win32_attributes(&dmd, dkind) & FILE_ATTRIBUTE_DIRECTORY,
            0
        );
    }

    #[test]
    fn stat_missing_path_is_not_found() {
        let (mut fs, _) = fresh();
        assert_eq!(
            stat_path(&mut fs, &p("C:\\dir\\nope.txt")),
            Err(ERROR_FILE_NOT_FOUND)
        );
    }

    #[test]
    fn create_file_dispositions_match_win32() {
        let (mut fs, handles) = fresh();
        let existing = p("C:\\dir\\alpha.txt");
        let fresh_path = p("C:\\dir\\new.txt");

        // OPEN_EXISTING on a missing file fails.
        assert_eq!(
            create_file(&mut fs, &handles, &fresh_path, OPEN_EXISTING, 0),
            Err(ERROR_FILE_NOT_FOUND)
        );
        // CREATE_NEW on an existing file fails.
        assert_eq!(
            create_file(&mut fs, &handles, &existing, CREATE_NEW, 0),
            Err(ERROR_FILE_EXISTS)
        );
        // CREATE_NEW on a free name succeeds and the file now exists.
        let h = create_file(&mut fs, &handles, &fresh_path, CREATE_NEW, 0).unwrap();
        assert!(fs.file_exists(&fresh_path).unwrap());
        assert_eq!(get_file_size(&mut fs, &handles, h).unwrap(), 0);
        // OPEN_ALWAYS opens the now-existing file.
        let h2 = create_file(&mut fs, &handles, &fresh_path, OPEN_ALWAYS, 0).unwrap();
        assert_ne!(h, h2, "each open mints a distinct handle");
        // Invalid disposition.
        assert_eq!(
            create_file(&mut fs, &handles, &fresh_path, 999, 0),
            Err(ERROR_INVALID_PARAMETER)
        );
    }

    #[test]
    fn backup_semantics_handle_requires_a_directory() {
        let (mut fs, handles) = fresh();
        assert!(create_file(&mut fs, &handles, &p("C:\\dir"), OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS).is_ok());
        assert_eq!(
            create_file(
                &mut fs,
                &handles,
                &p("C:\\dir\\alpha.txt"),
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS
            ),
            Err(ERROR_FILE_NOT_FOUND)
        );
    }

    #[test]
    fn create_and_remove_directory_match_win32() {
        let (mut fs, _) = fresh();
        // Creating an existing directory is ERROR_ALREADY_EXISTS.
        assert_eq!(
            create_directory(&mut fs, &p("C:\\dir")),
            Err(ERROR_ALREADY_EXISTS)
        );
        // A fresh directory is created, then removed.
        let nd = p("C:\\dir\\sub");
        create_directory(&mut fs, &nd).unwrap();
        assert!(fs.dir_exists(&nd).unwrap());
        remove_directory(&mut fs, &nd).unwrap();
        assert!(!fs.dir_exists(&nd).unwrap());
    }

    #[test]
    fn delete_file_removes_the_node() {
        let (mut fs, _) = fresh();
        let f = p("C:\\dir\\alpha.txt");
        assert!(fs.file_exists(&f).unwrap());
        delete_file(&mut fs, &f).unwrap();
        assert!(!fs.file_exists(&f).unwrap());
    }

    #[test]
    fn set_file_attributes_accepts_and_ignores() {
        let (mut fs, _) = fresh();
        // Existing file/directory succeed without persisting a change.
        set_file_attributes(&mut fs, &p("C:\\dir\\alpha.txt"), 0x1).unwrap();
        set_file_attributes(&mut fs, &p("C:\\dir"), 0x1).unwrap();
        // A missing path is not found.
        assert_eq!(
            set_file_attributes(&mut fs, &p("C:\\dir\\nope.txt"), 0x1),
            Err(ERROR_FILE_NOT_FOUND)
        );
    }

    #[test]
    fn set_file_pointer_origins_and_negative_seek() {
        let (mut fs, handles) = fresh();
        let h = create_file(&mut fs, &handles, &p("C:\\dir\\bravo.txt"), OPEN_EXISTING, 0).unwrap();
        // bravo.txt has metadata size 7.
        assert_eq!(set_file_pointer(&mut fs, &handles, h, 0, FILE_END).unwrap(), 7);
        assert_eq!(set_file_pointer(&mut fs, &handles, h, 2, FILE_BEGIN).unwrap(), 2);
        // FILE_CURRENT advances from the prior position (2 + 3 = 5).
        assert_eq!(set_file_pointer(&mut fs, &handles, h, 3, FILE_CURRENT).unwrap(), 5);
        // A seek before the start fails.
        assert_eq!(
            set_file_pointer(&mut fs, &handles, h, -1, FILE_BEGIN),
            Err(ERROR_NEGATIVE_SEEK)
        );
        // An unknown method is invalid.
        assert_eq!(
            set_file_pointer(&mut fs, &handles, h, 0, 99),
            Err(ERROR_INVALID_PARAMETER)
        );
    }

    #[test]
    fn handle_ops_reject_non_file_handles() {
        let (mut fs, handles) = fresh();
        let bogus = 0x4000_0000; // not interned
        assert_eq!(
            get_file_size(&mut fs, &handles, bogus),
            Err(ERROR_INVALID_HANDLE)
        );
        assert_eq!(
            set_file_pointer(&mut fs, &handles, bogus, 0, FILE_BEGIN),
            Err(ERROR_INVALID_HANDLE)
        );
    }

    #[test]
    fn find_enumeration_is_ordinal_ordered() {
        let (mut fs, handles) = fresh();
        let (h, first) = find_first(&mut fs, &handles, &p("C:\\dir\\*"), SearchOp::NameMatch, false)
            .unwrap()
            .expect("non-empty listing");
        let mut names = vec![String::from_utf16_lossy(first.name.as_units())];
        while let Some(entry) = find_next(&handles, h).unwrap() {
            names.push(String::from_utf16_lossy(entry.name.as_units()));
        }
        assert_eq!(names, vec!["alpha.txt", "bravo.txt", "charlie.txt"]);
        // Past the end, find_next yields None (caller maps to ERROR_NO_MORE_FILES).
        assert_eq!(find_next(&handles, h).unwrap(), None);
        // The handle releases.
        assert!(handles.close(h));
    }

    #[test]
    fn find_first_rejects_parentless_pattern_and_empty_dir() {
        let (mut fs, handles) = fresh();
        // A rootless single component has no parent.
        assert_eq!(
            find_first(&mut fs, &handles, &p("solo"), SearchOp::NameMatch, false),
            Err(ERROR_INVALID_PARAMETER)
        );
        // An empty directory yields Ok(None).
        create_directory(&mut fs, &p("C:\\dir\\empty")).unwrap();
        assert_eq!(
            find_first(&mut fs, &handles, &p("C:\\dir\\empty\\*"), SearchOp::NameMatch, false),
            Ok(None)
        );
    }

    #[test]
    fn find_next_rejects_a_non_find_handle() {
        let (mut fs, handles) = fresh();
        let file = create_file(&mut fs, &handles, &p("C:\\dir\\alpha.txt"), OPEN_EXISTING, 0).unwrap();
        assert_eq!(find_next(&handles, file), Err(ERROR_INVALID_HANDLE));
    }

    /// Drive a full enumeration and collect the matching entry names, in order.
    fn collect(
        fs: &mut Filesystem<windows_platform_isolation::TreeFsSurface<Win32OrdinalCasing>>,
        handles: &HandleTable,
        pattern: &str,
        op: SearchOp,
        case_sensitive: bool,
    ) -> Vec<String> {
        let mut names = Vec::new();
        let Some((h, first)) = find_first(fs, handles, &p(pattern), op, case_sensitive).unwrap()
        else {
            return names;
        };
        names.push(String::from_utf16_lossy(first.name.as_units()));
        while let Some(entry) = find_next(handles, h).unwrap() {
            names.push(String::from_utf16_lossy(entry.name.as_units()));
        }
        assert!(handles.close(h));
        names
    }

    #[test]
    fn find_first_applies_wildcard_leaf() {
        let (mut fs, handles) = fresh();
        // `*.txt` keeps every entry (all three are .txt).
        assert_eq!(
            collect(&mut fs, &handles, "C:\\dir\\*.txt", SearchOp::NameMatch, false),
            vec!["alpha.txt", "bravo.txt", "charlie.txt"]
        );
        // `a*` keeps only the entry beginning with `a`.
        assert_eq!(
            collect(&mut fs, &handles, "C:\\dir\\a*", SearchOp::NameMatch, false),
            vec!["alpha.txt"]
        );
    }

    #[test]
    fn find_first_question_mark_matches_single_char() {
        let (mut fs, handles) = fresh();
        // `?????.txt` (five wildcards) matches only `alpha`/`bravo`, not `charlie`.
        assert_eq!(
            collect(
                &mut fs,
                &handles,
                "C:\\dir\\?????.txt",
                SearchOp::NameMatch,
                false
            ),
            vec!["alpha.txt", "bravo.txt"]
        );
    }

    #[test]
    fn find_first_literal_leaf_matches_exact() {
        let (mut fs, handles) = fresh();
        assert_eq!(
            collect(
                &mut fs,
                &handles,
                "C:\\dir\\alpha.txt",
                SearchOp::NameMatch,
                false
            ),
            vec!["alpha.txt"]
        );
        // A literal that names no entry yields no match (caller: ERROR_FILE_NOT_FOUND).
        assert_eq!(
            find_first(
                &mut fs,
                &handles,
                &p("C:\\dir\\missing.txt"),
                SearchOp::NameMatch,
                false
            ),
            Ok(None)
        );
    }

    #[test]
    fn limit_to_directories_excludes_files() {
        let (mut fs, handles) = fresh();
        create_directory(&mut fs, &p("C:\\dir\\sub")).unwrap();
        // NameMatch over `*` keeps everything (files + the subdirectory).
        assert_eq!(
            collect(&mut fs, &handles, "C:\\dir\\*", SearchOp::NameMatch, false),
            vec!["alpha.txt", "bravo.txt", "charlie.txt", "sub"]
        );
        // LimitToDirectories drops the files.
        assert_eq!(
            collect(
                &mut fs,
                &handles,
                "C:\\dir\\*",
                SearchOp::LimitToDirectories,
                false
            ),
            vec!["sub"]
        );
    }

    #[test]
    fn case_sensitivity_governs_leaf_matching() {
        let (mut fs, handles) = fresh();
        // Default (case-insensitive) matches regardless of leaf case.
        assert_eq!(
            collect(
                &mut fs,
                &handles,
                "C:\\dir\\ALPHA.TXT",
                SearchOp::NameMatch,
                false
            ),
            vec!["alpha.txt"]
        );
        // Case-sensitive matching rejects the mismatched case.
        assert_eq!(
            find_first(
                &mut fs,
                &handles,
                &p("C:\\dir\\ALPHA.TXT"),
                SearchOp::NameMatch,
                true
            ),
            Ok(None)
        );
    }
}
