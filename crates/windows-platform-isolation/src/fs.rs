// Copyright (c) Microsoft Corporation.

//! The typed filesystem facade (D11) and session-vended roots.
//!
//! [`Filesystem`] is the filesystem analogue of the registry
//! [`Registry`](crate::Registry) facade. It borrows the *shape* of `std::fs`
//! (`create_dir` / `remove_dir` / `read_dir` / `metadata`) without taking a
//! dependency on it — `std::path::Path` is `OsStr`-based and cannot express the
//! ordinal casing and UTF-16 storage this crate mandates (D6/D8). Every method
//! lowers to an [`FsRequest`] and speaks to whatever [`FsSurface`] sits
//! underneath (a tree provider, a pass-through, a future buffered decorator —
//! the facade neither knows nor cares).
//!
//! Roots are **vended by a live [`FsSession`]** (D11): there are deliberately no
//! global `C:\` constants. Unlike the registry — whose hive identities form a
//! fixed, closed set ([`WellKnownRoot`](crate::WellKnownRoot)) — the filesystem
//! has an open set of drive roots, so the session vends them by letter rather
//! than from an enum.

use crate::file_path::FilePath;
use crate::fs_error::FilesystemResult;
use crate::fs_surface::{FsRequest, FsResponse, FsSurface};
use crate::fs_tree::{DirEntry, FileMetadata};

/// A live isolation session that vends filesystem roots (D11). Roots come from a
/// session instance, never from global constants, so an isolation scope is
/// always explicit.
#[derive(Clone, Copy, Debug, Default)]
pub struct FsSession;

impl FsSession {
    /// Open a new session.
    #[must_use]
    pub fn new() -> Self {
        Self
    }

    /// The absolute root of a drive, vended by this session (e.g. `'C'` ⇒
    /// `C:\`). The letter is normalized to upper case.
    #[must_use]
    pub fn drive(&self, letter: char) -> FilePath {
        let upper = letter.to_ascii_uppercase();
        FilePath::from_utf8(&format!("{upper}:\\"))
    }

    /// The canonical system drive root (`C:\`). Shorthand for the most common
    /// drive; mirrors the registry's `local_machine` convenience.
    #[must_use]
    pub fn system_drive(&self) -> FilePath {
        self.drive('C')
    }
}

/// A typed filesystem facade over any [`FsSurface`] (D11). Construct one around
/// a surface, then use the `std::fs`-shaped methods; each lowers to an
/// [`FsRequest`].
pub struct Filesystem<S: FsSurface> {
    surface: S,
}

impl<S: FsSurface> Filesystem<S> {
    /// Wrap a surface in the typed facade.
    pub fn new(surface: S) -> Self {
        Self { surface }
    }

    /// Borrow the underlying surface.
    #[must_use]
    pub fn surface(&self) -> &S {
        &self.surface
    }

    /// Mutably borrow the underlying surface.
    pub fn surface_mut(&mut self) -> &mut S {
        &mut self.surface
    }

    /// Recover the underlying surface.
    pub fn into_surface(self) -> S {
        self.surface
    }

    /// Create a directory (and missing ancestors).
    pub fn create_dir(&mut self, path: &FilePath) -> FilesystemResult<()> {
        self.surface
            .invoke(&FsRequest::CreateDir { path: path.clone() })?;
        Ok(())
    }

    /// Remove a directory and its subtree.
    pub fn remove_dir(&mut self, path: &FilePath) -> FilesystemResult<()> {
        self.surface
            .invoke(&FsRequest::RemoveDir { path: path.clone() })?;
        Ok(())
    }

    /// Whether a directory exists.
    pub fn dir_exists(&mut self, path: &FilePath) -> FilesystemResult<bool> {
        match self
            .surface
            .invoke(&FsRequest::DirExists { path: path.clone() })?
        {
            FsResponse::Exists(b) => Ok(b),
            other => contract_violation("Exists", &other),
        }
    }

    /// Whether a file exists.
    pub fn file_exists(&mut self, path: &FilePath) -> FilesystemResult<bool> {
        match self
            .surface
            .invoke(&FsRequest::FileExists { path: path.clone() })?
        {
            FsResponse::Exists(b) => Ok(b),
            other => contract_violation("Exists", &other),
        }
    }

    /// Read a file's metadata.
    pub fn metadata(&mut self, path: &FilePath) -> FilesystemResult<FileMetadata> {
        match self
            .surface
            .invoke(&FsRequest::ReadMetadata { path: path.clone() })?
        {
            FsResponse::Metadata(m) => Ok(m),
            other => contract_violation("Metadata", &other),
        }
    }

    /// Set (or replace) a file's metadata, creating it if absent. The
    /// `std::fs::write` analogue for the metadata-only model (D14): file byte
    /// content is out of scope for the first cut.
    pub fn write_file(&mut self, path: &FilePath, metadata: FileMetadata) -> FilesystemResult<()> {
        self.surface.invoke(&FsRequest::WriteFile {
            path: path.clone(),
            metadata,
        })?;
        Ok(())
    }

    /// Remove a file.
    pub fn remove_file(&mut self, path: &FilePath) -> FilesystemResult<()> {
        self.surface
            .invoke(&FsRequest::RemoveFile { path: path.clone() })?;
        Ok(())
    }

    /// Enumerate a directory's immediate entries (ordinal-ordered by name).
    pub fn read_dir(&mut self, path: &FilePath) -> FilesystemResult<Vec<DirEntry>> {
        match self
            .surface
            .invoke(&FsRequest::ReadDir { path: path.clone() })?
        {
            FsResponse::Entries(v) => Ok(v),
            other => contract_violation("Entries", &other),
        }
    }
}

/// Production stacks vend the mandated Win32 ordinal casing (D6/D8). This is the
/// default casing for non-test builds; unit tests inject the `testing`
/// `AsciiOrdinalCasing` reference instead (M3-2).
#[cfg(windows)]
impl Filesystem<crate::fs_surface::TreeFsSurface<crate::Win32OrdinalCasing>> {
    /// Build an in-memory filesystem facade over `base`, keyed with the mandated
    /// production ordinal casing (`Win32OrdinalCasing`). The base tree must have
    /// been populated with the same casing.
    #[must_use]
    pub fn in_memory(base: crate::fs_tree::FileTree) -> Self {
        let tree = crate::fs_tree::OverlayFileTree::new(crate::Win32OrdinalCasing, base);
        Filesystem::new(crate::fs_surface::TreeFsSurface::new(tree))
    }
}

/// A surface returned a response of the wrong shape for the request it was
/// given. That is an `FsSurface` implementation bug, not an OS condition, so it
/// is a programming error rather than a recoverable error.
#[cold]
fn contract_violation(expected: &str, got: &FsResponse) -> ! {
    panic!("surface contract violation: expected {expected} response, got {got:?}")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs_error::FilesystemError;
    use crate::fs_surface::TreeFsSurface;
    use crate::fs_tree::{FileTree, NodeKind, OverlayFileTree};
    use windows_text::AsciiOrdinalCasing;

    fn p(s: &str) -> FilePath {
        FilePath::from_utf8(s)
    }

    fn md(size: u64) -> FileMetadata {
        FileMetadata {
            size,
            ..FileMetadata::default()
        }
    }

    /// A test double that records every request before delegating.
    struct Recording<S: FsSurface> {
        inner: S,
        log: Vec<FsRequest>,
    }

    impl<S: FsSurface> Recording<S> {
        fn new(inner: S) -> Self {
            Self {
                inner,
                log: Vec::new(),
            }
        }
    }

    impl<S: FsSurface> FsSurface for Recording<S> {
        fn invoke(&mut self, req: &FsRequest) -> FilesystemResult<FsResponse> {
            self.log.push(req.clone());
            self.inner.invoke(req)
        }
    }

    fn empty_fs() -> Filesystem<Recording<TreeFsSurface<AsciiOrdinalCasing>>> {
        let tree = OverlayFileTree::new(AsciiOrdinalCasing, FileTree::new());
        Filesystem::new(Recording::new(TreeFsSurface::new(tree)))
    }

    #[test]
    fn session_vends_drive_roots() {
        let s = FsSession::new();
        assert_eq!(s.system_drive(), p("C:\\"));
        assert_eq!(s.drive('d'), p("D:\\"));
        assert_ne!(s.drive('C'), s.drive('D'));
    }

    #[test]
    fn write_then_read_metadata_round_trips() {
        let mut fs = empty_fs();
        let f = p("C:\\dir\\file.txt");
        fs.create_dir(&p("C:\\dir")).unwrap();
        fs.write_file(&f, md(99)).unwrap();
        assert_eq!(fs.metadata(&f).unwrap(), md(99));
    }

    #[test]
    fn typed_methods_lower_to_expected_request_sequence() {
        let mut fs = empty_fs();
        let f = p("C:\\file.bin");
        fs.write_file(&f, md(5)).unwrap();
        let _ = fs.metadata(&f).unwrap();
        assert_eq!(
            fs.surface().log,
            vec![
                FsRequest::WriteFile {
                    path: f.clone(),
                    metadata: md(5),
                },
                FsRequest::ReadMetadata { path: f },
            ]
        );
    }

    #[test]
    fn create_dir_and_existence() {
        let mut fs = empty_fs();
        let d = p("C:\\Program Files");
        assert!(!fs.dir_exists(&d).unwrap());
        fs.create_dir(&d).unwrap();
        assert!(fs.dir_exists(&d).unwrap());
    }

    #[test]
    fn remove_file_clears_existence() {
        let mut fs = empty_fs();
        let f = p("C:\\a.txt");
        fs.write_file(&f, md(1)).unwrap();
        assert!(fs.file_exists(&f).unwrap());
        fs.remove_file(&f).unwrap();
        assert!(!fs.file_exists(&f).unwrap());
    }

    #[test]
    fn metadata_missing_is_not_found() {
        let mut fs = empty_fs();
        assert_eq!(
            fs.metadata(&p("C:\\nope.dat")),
            Err(FilesystemError::NotFound)
        );
    }

    #[test]
    fn read_dir_returns_ordinal_ordered_entries() {
        let mut fs = empty_fs();
        fs.create_dir(&p("C:\\root")).unwrap();
        fs.create_dir(&p("C:\\root\\Zeta")).unwrap();
        fs.write_file(&p("C:\\root\\alpha.txt"), md(3)).unwrap();
        let entries = fs.read_dir(&p("C:\\root")).unwrap();
        let names: Vec<String> = entries.iter().map(|e| e.name.to_utf8().unwrap()).collect();
        // AsciiOrdinalCasing folds case, so alpha.txt ('A') precedes Zeta ('Z').
        assert_eq!(names, vec!["alpha.txt".to_string(), "Zeta".to_string()]);
        assert_eq!(entries[0].kind, NodeKind::File);
        assert_eq!(entries[1].kind, NodeKind::Directory);
    }

    #[test]
    fn into_surface_recovers_provider() {
        let fs = empty_fs();
        let mut inner = fs.into_surface();
        inner
            .invoke(&FsRequest::CreateDir { path: p("C:\\x") })
            .unwrap();
        assert_eq!(
            inner.invoke(&FsRequest::DirExists { path: p("C:\\x") }),
            Ok(FsResponse::Exists(true))
        );
    }
}
