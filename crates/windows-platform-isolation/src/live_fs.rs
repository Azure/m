// Copyright (c) Microsoft Corporation.

//! The live ("direct") filesystem provider (D20), the filesystem analogue of
//! [`LiveRegistry`](crate::LiveRegistry).
//!
//! [`LiveFilesystem`] reads and writes the real OS filesystem through the safe
//! primitives in `windows-platform-isolation-sys`. This module is Windows-only
//! and itself contains **no `unsafe`** (D13): every raw call is confined to the
//! leaf crate, which is built on a RAII Win32 file `HANDLE` (not `std::fs`) so a
//! future stream/content layer can drive overlapped I/O through an async reactor
//! without re-opening the file (see the leaf's module docs).
//!
//! Paths are taken from a [`FilePath`]'s native text verbatim, so a path built
//! against the surface resolves directly against the OS. Directory enumeration
//! is re-sorted through the ordinal-casing seam (D6/D8) so a live `read_dir`
//! produces the same ordinal order as the in-memory [`TreeFsSurface`].
//!
//! [`TreeFsSurface`]: crate::TreeFsSurface

use windows_platform_isolation_sys::{
    FileHandle, FileInfo, FsError, create_directory, delete_file, file_attributes, read_directory,
    remove_directory, set_file_attributes,
};

use crate::file_path::{FILE_POSIX_SEPARATOR, FILE_PREFERRED_SEPARATOR, FilePath};
use crate::fs_error::{FilesystemError, FilesystemResult};
use crate::fs_surface::{FsRequest, FsResponse, FsSurface};
use crate::fs_tree::{DirEntry, FileMetadata, NodeKind};
use crate::{OrdinalCasing, Utf16};

/// The `FILE_ATTRIBUTE_DIRECTORY` flag: a node is a directory iff this bit is
/// set in its attribute bitset.
const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;

/// `ERROR_ALREADY_EXISTS`: a directory create that repeats an existing
/// ancestor is benign (idempotent create-on-write, matching the tree surface).
const ERROR_ALREADY_EXISTS: u32 = 183;

/// A provider that operates directly on the live OS filesystem (D20).
///
/// Generic over the ordinal-casing seam `C` (like
/// [`TreeFsSurface`](crate::TreeFsSurface)) so it can both re-sort live
/// directory listings into ordinal order and serve as a drop-in
/// [`FsSurface`](crate::FsSurface).
#[derive(Debug, Default)]
pub struct LiveFilesystem<C: OrdinalCasing> {
    casing: C,
}

impl<C: OrdinalCasing> LiveFilesystem<C> {
    /// Create a live filesystem provider using `casing` for ordinal ordering.
    #[must_use]
    pub fn new(casing: C) -> Self {
        Self { casing }
    }

    /// The ordinal-casing seam this provider sorts and matches with.
    #[must_use]
    pub fn casing(&self) -> &C {
        &self.casing
    }

    /// Whether a directory exists at `path`.
    ///
    /// # Errors
    ///
    /// Returns [`FilesystemError::Os`] on any Win32 failure other than a missing
    /// node (which yields `Ok(false)`).
    pub fn dir_exists(&self, path: &FilePath) -> FilesystemResult<bool> {
        match self.node_info(path) {
            Ok(info) => Ok(is_directory(&info)),
            Err(FilesystemError::NotFound) => Ok(false),
            Err(e) => Err(e),
        }
    }

    /// Whether a file (non-directory) exists at `path`.
    ///
    /// # Errors
    ///
    /// Returns [`FilesystemError::Os`] on any Win32 failure other than a missing
    /// node (which yields `Ok(false)`).
    pub fn file_exists(&self, path: &FilePath) -> FilesystemResult<bool> {
        match self.node_info(path) {
            Ok(info) => Ok(!is_directory(&info)),
            Err(FilesystemError::NotFound) => Ok(false),
            Err(e) => Err(e),
        }
    }

    /// Read the metadata of the file at `path`.
    ///
    /// Mirrors the in-memory [`TreeFsSurface`](crate::TreeFsSurface) contract:
    /// the path must name a file, not a directory.
    ///
    /// # Errors
    ///
    /// [`FilesystemError::NotFound`] if no file exists at `path` (absent, or the
    /// name denotes a directory); [`FilesystemError::Os`] on any other Win32
    /// failure.
    pub fn metadata(&self, path: &FilePath) -> FilesystemResult<FileMetadata> {
        let info = self.node_info(path)?;
        if is_directory(&info) {
            return Err(FilesystemError::NotFound);
        }
        Ok(to_metadata(&info))
    }

    /// Enumerate the immediate entries of the directory at `path`, re-sorted
    /// into ordinal order (D8) so the result matches the in-memory surface.
    ///
    /// # Errors
    ///
    /// [`FilesystemError::NotFound`] if no directory exists at `path`;
    /// [`FilesystemError::Os`] on any other Win32 failure.
    pub fn read_dir(&self, path: &FilePath) -> FilesystemResult<Vec<DirEntry>> {
        let raw = read_directory(&wide_path(path)).map_err(map_fs_err)?;
        let mut keyed: Vec<(Vec<u8>, DirEntry)> = raw
            .into_iter()
            .map(|e| {
                let name = Utf16::from_units(e.name);
                let key = self.casing.sort_key(name.as_units());
                let kind = if is_directory(&e.info) {
                    NodeKind::Directory
                } else {
                    NodeKind::File
                };
                (
                    key,
                    DirEntry {
                        name,
                        kind,
                        metadata: to_metadata(&e.info),
                    },
                )
            })
            .collect();
        keyed.sort_by(|a, b| a.0.cmp(&b.0));
        Ok(keyed.into_iter().map(|(_, entry)| entry).collect())
    }

    /// Create the directory at `path`, creating any missing ancestors (D20),
    /// mirroring the in-memory surface's create-on-write. Idempotent: an
    /// already-existing directory is not an error.
    ///
    /// # Errors
    ///
    /// [`FilesystemError::Os`] on any Win32 failure other than a benign
    /// "already exists".
    pub fn create_dir(&self, path: &FilePath) -> FilesystemResult<()> {
        let units = path.native().as_units();
        let root_len = path.root().text().len();
        let mut i = root_len;
        let n = units.len();
        while i < n {
            while i < n && is_separator(units[i]) {
                i += 1;
            }
            if i >= n {
                break;
            }
            let mut j = i;
            while j < n && !is_separator(units[j]) {
                j += 1;
            }
            let mut wide = units[..j].to_vec();
            wide.push(0);
            match create_directory(&wide) {
                Ok(()) => {}
                Err(e) if e.code() == ERROR_ALREADY_EXISTS => {}
                Err(e) => return Err(map_fs_err(e)),
            }
            i = j;
        }
        Ok(())
    }

    /// Remove the directory at `path` and its entire subtree (D20), mirroring
    /// the in-memory surface. Idempotent: a missing directory is not an error.
    ///
    /// # Errors
    ///
    /// [`FilesystemError::Os`] on any Win32 failure other than a missing node.
    pub fn remove_dir(&self, path: &FilePath) -> FilesystemResult<()> {
        let entries = match read_directory(&wide_path(path)) {
            Ok(entries) => entries,
            Err(e) if e.is_not_found() => return Ok(()),
            Err(e) => return Err(map_fs_err(e)),
        };
        for entry in entries {
            let child = child_path(path, &entry.name);
            if is_directory(&entry.info) {
                self.remove_dir(&child)?;
            } else {
                self.remove_file(&child)?;
            }
        }
        remove_directory(&wide_path(path)).map_err(map_fs_err)
    }

    /// Write an (empty) file at `path`, applying the attributes and timestamps
    /// from `metadata` (D20). Any missing parent directories are created first,
    /// mirroring the in-memory surface's create-on-write.
    ///
    /// The file's *size* is **not** applied: size is a consequence of content,
    /// which this metadata-only provider does not write (see the module docs).
    /// A subsequent [`metadata`](Self::metadata) therefore reports size 0.
    ///
    /// # Errors
    ///
    /// [`FilesystemError::Os`] on any Win32 failure.
    pub fn write_file(&self, path: &FilePath, metadata: FileMetadata) -> FilesystemResult<()> {
        if let Some(parent) = parent_path(path) {
            self.create_dir(&parent)?;
        }
        {
            let handle = FileHandle::create(&wide_path(path)).map_err(map_fs_err)?;
            if metadata.creation_time != 0
                || metadata.last_access_time != 0
                || metadata.last_write_time != 0
            {
                handle
                    .set_times(
                        metadata.creation_time,
                        metadata.last_access_time,
                        metadata.last_write_time,
                    )
                    .map_err(map_fs_err)?;
            }
        }
        // A file can never carry the directory bit; apply the rest only if the
        // caller asked for any attributes (SetFileAttributes rejects 0).
        let attrs = metadata.attributes & !FILE_ATTRIBUTE_DIRECTORY;
        if attrs != 0 {
            set_file_attributes(&wide_path(path), attrs).map_err(map_fs_err)?;
        }
        Ok(())
    }

    /// Remove the file at `path` (D20). Idempotent: a missing file is not an
    /// error, mirroring the in-memory surface.
    ///
    /// # Errors
    ///
    /// [`FilesystemError::Os`] on any Win32 failure other than a missing node.
    pub fn remove_file(&self, path: &FilePath) -> FilesystemResult<()> {
        match delete_file(&wide_path(path)) {
            Ok(()) => Ok(()),
            Err(e) if e.is_not_found() => Ok(()),
            Err(e) => Err(map_fs_err(e)),
        }
    }

    /// Read a node's metadata regardless of kind, mapping a missing node to
    /// [`FilesystemError::NotFound`].
    fn node_info(&self, path: &FilePath) -> FilesystemResult<FileInfo> {
        file_attributes(&wide_path(path)).map_err(map_fs_err)
    }
}

/// Whether a leaf [`FileInfo`] describes a directory.
fn is_directory(info: &FileInfo) -> bool {
    info.attributes & FILE_ATTRIBUTE_DIRECTORY != 0
}

/// Convert a leaf [`FileInfo`] into the surface's [`FileMetadata`]. A directory
/// reports size 0 (the OS already reports 0 for directories).
fn to_metadata(info: &FileInfo) -> FileMetadata {
    FileMetadata {
        size: if is_directory(info) { 0 } else { info.size },
        creation_time: info.creation_time,
        last_write_time: info.last_write_time,
        last_access_time: info.last_access_time,
        attributes: info.attributes,
    }
}

/// The path text a [`FilePath`] resolves to, NUL-terminated for the FFI leaf.
fn wide_path(path: &FilePath) -> Vec<u16> {
    let mut units = path.native().as_units().to_vec();
    units.push(0);
    units
}

/// Map a leaf [`FsError`] into the surface error: "not found" becomes
/// [`FilesystemError::NotFound`], everything else carries the raw code.
fn map_fs_err(e: FsError) -> FilesystemError {
    if e.is_not_found() {
        FilesystemError::NotFound
    } else {
        FilesystemError::Os(e.code())
    }
}

/// Whether `unit` is a path separator (`\` or `/`).
fn is_separator(unit: u16) -> bool {
    unit == FILE_PREFERRED_SEPARATOR || unit == FILE_POSIX_SEPARATOR
}

/// The parent directory of `path`, or `None` when `path` sits directly under
/// its root (whose existence the OS already guarantees). Used to create missing
/// ancestors before a write.
fn parent_path(path: &FilePath) -> Option<FilePath> {
    let units = path.native().as_units();
    let root_len = path.root().text().len();
    let last = units.iter().rposition(|&u| is_separator(u))?;
    if last < root_len {
        return None;
    }
    Some(FilePath::from_units(units[..last].to_vec()))
}

/// `parent` with `name` appended as a child component (separator inserted as
/// needed). Used to descend during recursive removal.
fn child_path(parent: &FilePath, name: &[u16]) -> FilePath {
    let mut units = parent.native().as_units().to_vec();
    if !units.last().is_some_and(|&u| is_separator(u)) {
        units.push(FILE_PREFERRED_SEPARATOR);
    }
    units.extend_from_slice(name);
    FilePath::from_units(units)
}

impl<C: OrdinalCasing> FsSurface for LiveFilesystem<C> {
    fn invoke(&mut self, req: &FsRequest) -> FilesystemResult<FsResponse> {
        match req {
            FsRequest::DirExists { path } => self.dir_exists(path).map(FsResponse::Exists),
            FsRequest::FileExists { path } => self.file_exists(path).map(FsResponse::Exists),
            FsRequest::CreateDir { path } => self.create_dir(path).map(|()| FsResponse::Unit),
            FsRequest::RemoveDir { path } => self.remove_dir(path).map(|()| FsResponse::Unit),
            FsRequest::ReadMetadata { path } => self.metadata(path).map(FsResponse::Metadata),
            FsRequest::WriteFile { path, metadata } => {
                self.write_file(path, *metadata).map(|()| FsResponse::Unit)
            }
            FsRequest::RemoveFile { path } => self.remove_file(path).map(|()| FsResponse::Unit),
            FsRequest::ReadDir { path } => self.read_dir(path).map(FsResponse::Entries),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use windows_text::AsciiOrdinalCasing;

    /// A unique scratch directory tree under the OS temp dir, removed on drop.
    /// Setup uses `std::fs` (safe) so these tests exercise the read path against
    /// real OS state independent of the write path (M9-3).
    struct TempTree {
        root: std::path::PathBuf,
    }

    impl TempTree {
        fn new(tag: &str) -> Self {
            let nanos = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let root = std::env::temp_dir().join(format!(
                "wpi-livefs-{tag}-{}-{nanos}",
                std::process::id()
            ));
            std::fs::create_dir_all(&root).expect("create scratch root");
            Self { root }
        }

        fn path(&self, rel: &str) -> FilePath {
            let full = self.root.join(rel);
            FilePath::from_utf8(&full.to_string_lossy())
        }

        fn root_path(&self) -> FilePath {
            FilePath::from_utf8(&self.root.to_string_lossy())
        }

        fn make_file(&self, rel: &str, bytes: &[u8]) {
            let full = self.root.join(rel);
            if let Some(parent) = full.parent() {
                std::fs::create_dir_all(parent).expect("create parent");
            }
            std::fs::write(&full, bytes).expect("write file");
        }

        fn make_dir(&self, rel: &str) {
            std::fs::create_dir_all(self.root.join(rel)).expect("create dir");
        }
    }

    impl Drop for TempTree {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.root);
        }
    }

    fn live() -> LiveFilesystem<AsciiOrdinalCasing> {
        LiveFilesystem::new(AsciiOrdinalCasing)
    }

    #[test]
    fn dir_exists_reports_directories() {
        let t = TempTree::new("dirx");
        t.make_dir("sub");
        let fs = live();
        assert!(fs.dir_exists(&t.root_path()).unwrap());
        assert!(fs.dir_exists(&t.path("sub")).unwrap());
        assert!(!fs.dir_exists(&t.path("missing")).unwrap());
    }

    #[test]
    fn file_exists_distinguishes_files_from_dirs() {
        let t = TempTree::new("filex");
        t.make_file("a.txt", b"hello");
        t.make_dir("d");
        let fs = live();
        assert!(fs.file_exists(&t.path("a.txt")).unwrap());
        // A directory is not a file.
        assert!(!fs.file_exists(&t.path("d")).unwrap());
        assert!(!fs.file_exists(&t.path("nope.txt")).unwrap());
    }

    #[test]
    fn metadata_reports_size() {
        let t = TempTree::new("meta");
        t.make_file("sized.bin", &[0u8; 1234]);
        let fs = live();
        let md = fs.metadata(&t.path("sized.bin")).unwrap();
        assert_eq!(md.size, 1234);
    }

    #[test]
    fn metadata_on_directory_is_not_found() {
        let t = TempTree::new("metadir");
        t.make_dir("d");
        let fs = live();
        assert_eq!(
            fs.metadata(&t.path("d")),
            Err(FilesystemError::NotFound)
        );
    }

    #[test]
    fn metadata_on_missing_is_not_found() {
        let t = TempTree::new("metamiss");
        let fs = live();
        assert_eq!(
            fs.metadata(&t.path("ghost.txt")),
            Err(FilesystemError::NotFound)
        );
    }

    #[test]
    fn read_dir_is_ordinal_ordered_and_typed() {
        let t = TempTree::new("enum");
        t.make_file("notepad.exe", b"x");
        t.make_dir("System32");
        t.make_file("Apple.txt", b"y");
        let fs = live();
        let entries = fs.read_dir(&t.root_path()).unwrap();
        let names: Vec<String> = entries.iter().map(|e| e.name.to_utf8().unwrap()).collect();
        // AsciiOrdinalCasing folds case: Apple < notepad < System32.
        assert_eq!(
            names,
            vec![
                "Apple.txt".to_string(),
                "notepad.exe".to_string(),
                "System32".to_string()
            ]
        );
        let by_name = |n: &str| entries.iter().find(|e| e.name.to_utf8().unwrap() == n).unwrap();
        assert_eq!(by_name("notepad.exe").kind, NodeKind::File);
        assert_eq!(by_name("System32").kind, NodeKind::Directory);
    }

    #[test]
    fn read_dir_missing_is_not_found() {
        let t = TempTree::new("enummiss");
        let fs = live();
        assert_eq!(
            fs.read_dir(&t.path("nope")).map(|_| ()),
            Err(FilesystemError::NotFound)
        );
    }

    #[test]
    fn read_dir_empty_directory_is_empty() {
        let t = TempTree::new("empty");
        t.make_dir("hollow");
        let fs = live();
        assert!(fs.read_dir(&t.path("hollow")).unwrap().is_empty());
    }

    /// A FILETIME (100ns ticks since 1601) circa 2019, for deterministic
    /// timestamp round-tripping.
    const SAMPLE_TIME: i64 = 132_000_000_000_000_000;
    /// `FILE_ATTRIBUTE_HIDDEN`: a benign attribute that, unlike read-only, does
    /// not block the subsequent deletion in `TempTree::drop`.
    const FILE_ATTRIBUTE_HIDDEN: u32 = 0x2;

    #[test]
    fn create_dir_creates_nested_and_is_idempotent() {
        let t = TempTree::new("mkdir");
        let fs = live();
        let nested = t.path("a/b/c");
        fs.create_dir(&nested).unwrap();
        assert!(fs.dir_exists(&nested).unwrap());
        assert!(fs.dir_exists(&t.path("a")).unwrap());
        assert!(fs.dir_exists(&t.path("a/b")).unwrap());
        // Repeating an existing tree is benign.
        fs.create_dir(&nested).unwrap();
    }

    #[test]
    fn write_file_applies_attributes_and_timestamps() {
        let t = TempTree::new("write");
        let fs = live();
        let path = t.path("note.txt");
        let md = FileMetadata {
            size: 999, // not applied: size is a consequence of content
            creation_time: SAMPLE_TIME,
            last_write_time: SAMPLE_TIME + 111,
            last_access_time: SAMPLE_TIME + 222,
            attributes: FILE_ATTRIBUTE_HIDDEN,
        };
        fs.write_file(&path, md).unwrap();
        assert!(fs.file_exists(&path).unwrap());
        let got = fs.metadata(&path).unwrap();
        assert_eq!(got.size, 0);
        assert_eq!(got.creation_time, md.creation_time);
        assert_eq!(got.last_write_time, md.last_write_time);
        assert!(got.attributes & FILE_ATTRIBUTE_HIDDEN != 0);
    }

    #[test]
    fn write_file_creates_missing_parents() {
        let t = TempTree::new("writeparents");
        let fs = live();
        let path = t.path("deep/nested/leaf.txt");
        fs.write_file(&path, FileMetadata::default()).unwrap();
        assert!(fs.file_exists(&path).unwrap());
        assert!(fs.dir_exists(&t.path("deep/nested")).unwrap());
    }

    #[test]
    fn remove_file_is_idempotent() {
        let t = TempTree::new("rmfile");
        t.make_file("gone.txt", b"x");
        let fs = live();
        let p = t.path("gone.txt");
        fs.remove_file(&p).unwrap();
        assert!(!fs.file_exists(&p).unwrap());
        // Removing an absent file is not an error.
        fs.remove_file(&p).unwrap();
    }

    #[test]
    fn remove_dir_removes_populated_subtree() {
        let t = TempTree::new("rmdir");
        t.make_file("top.txt", b"x");
        t.make_dir("sub");
        t.make_file("sub/inner.txt", b"y");
        t.make_dir("sub/deeper");
        t.make_file("sub/deeper/leaf.txt", b"z");
        let fs = live();
        let sub = t.path("sub");
        fs.remove_dir(&sub).unwrap();
        assert!(!fs.dir_exists(&sub).unwrap());
        // A sibling outside the removed subtree is untouched.
        assert!(fs.file_exists(&t.path("top.txt")).unwrap());
        // Removing an absent directory is not an error.
        fs.remove_dir(&sub).unwrap();
    }

    #[test]
    fn fs_surface_invoke_round_trips_mutations_and_reads() {
        let t = TempTree::new("surface");
        let mut fs = live();
        let dir = t.path("apps");
        let file = t.path("apps/app.cfg");

        assert_eq!(
            fs.invoke(&FsRequest::CreateDir { path: dir.clone() }).unwrap(),
            FsResponse::Unit
        );
        assert_eq!(
            fs.invoke(&FsRequest::DirExists { path: dir.clone() }).unwrap(),
            FsResponse::Exists(true)
        );
        assert_eq!(
            fs.invoke(&FsRequest::WriteFile {
                path: file.clone(),
                metadata: FileMetadata::default(),
            })
            .unwrap(),
            FsResponse::Unit
        );
        assert_eq!(
            fs.invoke(&FsRequest::FileExists { path: file.clone() }).unwrap(),
            FsResponse::Exists(true)
        );
        match fs.invoke(&FsRequest::ReadMetadata { path: file.clone() }).unwrap() {
            FsResponse::Metadata(_) => {}
            other => panic!("expected metadata, got {other:?}"),
        }
        match fs.invoke(&FsRequest::ReadDir { path: dir.clone() }).unwrap() {
            FsResponse::Entries(entries) => {
                assert_eq!(entries.len(), 1);
                assert_eq!(entries[0].name.to_utf8().unwrap(), "app.cfg");
            }
            other => panic!("expected entries, got {other:?}"),
        }
        assert_eq!(
            fs.invoke(&FsRequest::RemoveFile { path: file }).unwrap(),
            FsResponse::Unit
        );
        assert_eq!(
            fs.invoke(&FsRequest::RemoveDir { path: dir.clone() }).unwrap(),
            FsResponse::Unit
        );
        assert_eq!(
            fs.invoke(&FsRequest::DirExists { path: dir }).unwrap(),
            FsResponse::Exists(false)
        );
    }
}
