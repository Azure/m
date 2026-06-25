// Copyright (c) Microsoft Corporation.

//! The reified filesystem operation model and the [`FsSurface`] seam (D10).
//!
//! This is the filesystem analogue of the registry [`Surface`](crate::Surface)
//! seam: every filesystem operation is a value — an [`FsRequest`] — and every
//! result is an [`FsResponse`]. Providers and decorators all speak this single
//! vocabulary through one object-safe method, [`FsSurface::invoke`].
//!
//! Per D10, pass-through and buffered "carry surface-specific semantics," so the
//! filesystem ships its own [`FsPassThrough`] rather than literally reusing the
//! registry decorator (the C++ PIL likewise ships separate filesystem facets).
//! The genuinely surface-agnostic layers (logging, journaling, fault injection)
//! are written once over the seam and are future work for both surfaces.
//!
//! [`TreeFsSurface`] is the leaf provider (D15): it lowers each `FsRequest`
//! onto an in-memory [`OverlayFileTree`].

use crate::file_path::FilePath;
use crate::fs_error::{FilesystemError, FilesystemResult};
use crate::fs_tree::{DirEntry, FileMetadata, FileTree, OverlayFileTree, OverlayPresence};
use crate::OrdinalCasing;

/// A filesystem operation, modeled as data (D10). This is also the journaling
/// verb stream (D4) for any future buffered/journaling filesystem decorator.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FsRequest {
    /// Test whether a directory exists.
    DirExists { path: FilePath },
    /// Test whether a file exists.
    FileExists { path: FilePath },
    /// Create a directory (and any missing ancestors).
    CreateDir { path: FilePath },
    /// Remove a directory and its subtree.
    RemoveDir { path: FilePath },
    /// Read a file's metadata.
    ReadMetadata { path: FilePath },
    /// Set (or replace) a file's metadata, creating it if absent.
    WriteFile {
        path: FilePath,
        metadata: FileMetadata,
    },
    /// Remove a file.
    RemoveFile { path: FilePath },
    /// Enumerate a directory's immediate entries (ordinal-ordered).
    ReadDir { path: FilePath },
}

/// The result of invoking an [`FsRequest`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum FsResponse {
    /// No payload (a successful mutation).
    Unit,
    /// An existence answer.
    Exists(bool),
    /// A single file's metadata.
    Metadata(FileMetadata),
    /// A list of directory entries (ordinal-ordered by name).
    Entries(Vec<DirEntry>),
}

/// The filesystem decorator/provider seam (D10): one object-safe method through
/// which all filesystem operations flow. Cross-cutting layers are implemented
/// once as `FsSurface` wrappers, independent of the underlying provider.
pub trait FsSurface {
    /// Execute a request and produce a response.
    fn invoke(&mut self, req: &FsRequest) -> FilesystemResult<FsResponse>;
}

/// The leaf provider: lowers requests onto an in-memory [`OverlayFileTree`] (D15).
pub struct TreeFsSurface<C: OrdinalCasing> {
    tree: OverlayFileTree<C>,
}

impl<C: OrdinalCasing> TreeFsSurface<C> {
    /// Wrap an overlay filesystem tree as a surface.
    pub fn new(tree: OverlayFileTree<C>) -> Self {
        Self { tree }
    }

    /// The underlying tree (for inspection / proving copy-on-write isolation).
    #[must_use]
    pub fn tree(&self) -> &OverlayFileTree<C> {
        &self.tree
    }
}

impl<C: OrdinalCasing> FsSurface for TreeFsSurface<C> {
    fn invoke(&mut self, req: &FsRequest) -> FilesystemResult<FsResponse> {
        match req {
            FsRequest::DirExists { path } => Ok(FsResponse::Exists(self.tree.dir_exists(path))),
            FsRequest::FileExists { path } => Ok(FsResponse::Exists(self.tree.file_exists(path))),
            FsRequest::CreateDir { path } => {
                self.tree.create_dir(path);
                Ok(FsResponse::Unit)
            }
            FsRequest::RemoveDir { path } => {
                self.tree.remove_dir(path);
                Ok(FsResponse::Unit)
            }
            FsRequest::ReadMetadata { path } => {
                self.tree.file_metadata(path).map(FsResponse::Metadata)
            }
            FsRequest::WriteFile { path, metadata } => {
                self.tree.set_file(path, *metadata);
                Ok(FsResponse::Unit)
            }
            FsRequest::RemoveFile { path } => {
                self.tree.remove_file(path);
                Ok(FsResponse::Unit)
            }
            FsRequest::ReadDir { path } => self.tree.read_dir(path).map(FsResponse::Entries),
        }
    }
}

/// A transparent filesystem decorator: forwards every request to the inner
/// surface unchanged. Useful as a seam for inserting behavior without altering
/// call sites, and as the trivial baseline that proves the decorator pattern.
///
/// Per D10 this is surface-specific (it speaks [`FsRequest`]/[`FsResponse`]),
/// mirroring the registry's `PassThrough` rather than reusing it.
pub struct FsPassThrough<S: FsSurface> {
    inner: S,
}

impl<S: FsSurface> FsPassThrough<S> {
    /// Wrap an inner surface.
    pub fn new(inner: S) -> Self {
        Self { inner }
    }

    /// Recover the inner surface.
    pub fn into_inner(self) -> S {
        self.inner
    }
}

impl<S: FsSurface> FsSurface for FsPassThrough<S> {
    fn invoke(&mut self, req: &FsRequest) -> FilesystemResult<FsResponse> {
        self.inner.invoke(req)
    }
}

/// A write-buffering filesystem decorator — the filesystem analogue of the
/// registry [`Buffered`](crate::Buffered) (D4 / D10). Mutations land in an
/// in-memory overlay (with tombstones that shadow the inner surface) and are
/// **never** applied to the inner surface; reads see the overlay layered over
/// the inner surface (read-your-writes), so a live inner surface is left
/// untouched until [`commit`](FsBuffered::commit) replays the journal.
///
/// The journal is the [`FsRequest`] verb stream: each buffered mutation is the
/// originating request, and `commit` replays them in order onto the inner
/// surface. This is the isolation seam the `.pilcfg` `buffer_updates` flag
/// selects for the filesystem (windows-win32-shim SHIM-D13).
pub struct FsBuffered<S: FsSurface, C: OrdinalCasing + Clone> {
    inner: S,
    overlay: OverlayFileTree<C>,
    casing: C,
    journal: Vec<FsRequest>,
}

impl<S: FsSurface, C: OrdinalCasing + Clone> FsBuffered<S, C> {
    /// Wrap an inner surface with an empty write overlay keyed by `casing`.
    pub fn new(inner: S, casing: C) -> Self {
        Self {
            inner,
            overlay: OverlayFileTree::new(casing.clone(), FileTree::new()),
            casing,
            journal: Vec::new(),
        }
    }

    /// The pending verb stream (the buffered mutations, in order).
    #[must_use]
    pub fn journal(&self) -> &[FsRequest] {
        &self.journal
    }

    /// Whether any mutations are buffered.
    #[must_use]
    pub fn is_dirty(&self) -> bool {
        !self.journal.is_empty()
    }

    /// The write overlay (for inspection / proving the inner surface is
    /// untouched).
    #[must_use]
    pub fn overlay(&self) -> &OverlayFileTree<C> {
        &self.overlay
    }

    /// Replay the buffered mutations onto the inner surface in order, then clear
    /// the buffer. After this the inner surface reflects all buffered writes.
    pub fn commit(&mut self) -> FilesystemResult<()> {
        for req in std::mem::take(&mut self.journal) {
            self.inner.invoke(&req)?;
        }
        Ok(())
    }

    /// Discard all buffered mutations without touching the inner surface.
    pub fn rollback(&mut self) {
        self.journal.clear();
        self.overlay = OverlayFileTree::new(self.casing.clone(), FileTree::new());
    }

    /// Recover the inner surface, discarding any uncommitted buffer.
    pub fn into_inner(self) -> S {
        self.inner
    }

    fn dir_exists(&mut self, path: &FilePath) -> FilesystemResult<bool> {
        match self.overlay.dir_presence(path) {
            OverlayPresence::Present => Ok(true),
            OverlayPresence::Deleted => Ok(false),
            OverlayPresence::Absent => match self
                .inner
                .invoke(&FsRequest::DirExists { path: path.clone() })?
            {
                FsResponse::Exists(found) => Ok(found),
                _ => Ok(false),
            },
        }
    }

    fn file_exists(&mut self, path: &FilePath) -> FilesystemResult<bool> {
        match self.overlay.file_presence(path) {
            OverlayPresence::Present => Ok(true),
            OverlayPresence::Deleted => Ok(false),
            OverlayPresence::Absent => match self
                .inner
                .invoke(&FsRequest::FileExists { path: path.clone() })?
            {
                FsResponse::Exists(found) => Ok(found),
                _ => Ok(false),
            },
        }
    }

    fn read_metadata(&mut self, path: &FilePath) -> FilesystemResult<FileMetadata> {
        match self.overlay.file_presence(path) {
            OverlayPresence::Present => self.overlay.file_metadata(path),
            OverlayPresence::Deleted => Err(FilesystemError::NotFound),
            OverlayPresence::Absent => match self
                .inner
                .invoke(&FsRequest::ReadMetadata { path: path.clone() })?
            {
                FsResponse::Metadata(metadata) => Ok(metadata),
                _ => Err(FilesystemError::NotFound),
            },
        }
    }

    fn read_dir(&mut self, path: &FilePath) -> FilesystemResult<Vec<DirEntry>> {
        match self.overlay.dir_presence(path) {
            OverlayPresence::Deleted => Err(FilesystemError::NotFound),
            OverlayPresence::Absent => match self
                .inner
                .invoke(&FsRequest::ReadDir { path: path.clone() })?
            {
                FsResponse::Entries(entries) => Ok(entries),
                _ => Err(FilesystemError::NotFound),
            },
            OverlayPresence::Present => {
                // Start with the overlay's materialized children, then merge any
                // inner entries the overlay has neither materialized nor
                // tombstoned (read-your-writes layered over the inner surface).
                let mut entries = self.overlay.read_dir(path)?;
                if let Ok(FsResponse::Entries(inner)) =
                    self.inner.invoke(&FsRequest::ReadDir { path: path.clone() })
                {
                    for entry in inner {
                        let child = path.join(&FilePath::from_value(entry.name.clone()));
                        let shadowed = self.overlay.dir_presence(&child) != OverlayPresence::Absent
                            || self.overlay.file_presence(&child) != OverlayPresence::Absent;
                        if !shadowed {
                            entries.push(entry);
                        }
                    }
                    entries.sort_by(|a, b| {
                        self.casing
                            .compare_ignore_case(a.name.as_units(), b.name.as_units())
                    });
                }
                Ok(entries)
            }
        }
    }
}

impl<S: FsSurface, C: OrdinalCasing + Clone> FsSurface for FsBuffered<S, C> {
    fn invoke(&mut self, req: &FsRequest) -> FilesystemResult<FsResponse> {
        match req {
            FsRequest::DirExists { path } => Ok(FsResponse::Exists(self.dir_exists(path)?)),
            FsRequest::FileExists { path } => Ok(FsResponse::Exists(self.file_exists(path)?)),
            FsRequest::ReadMetadata { path } => self.read_metadata(path).map(FsResponse::Metadata),
            FsRequest::ReadDir { path } => self.read_dir(path).map(FsResponse::Entries),
            FsRequest::CreateDir { path } => {
                self.overlay.create_dir(path);
                self.journal.push(req.clone());
                Ok(FsResponse::Unit)
            }
            FsRequest::RemoveDir { path } => {
                self.overlay.remove_dir(path);
                self.journal.push(req.clone());
                Ok(FsResponse::Unit)
            }
            FsRequest::WriteFile { path, metadata } => {
                self.overlay.set_file(path, *metadata);
                self.journal.push(req.clone());
                Ok(FsResponse::Unit)
            }
            FsRequest::RemoveFile { path } => {
                self.overlay.remove_file(path);
                self.journal.push(req.clone());
                Ok(FsResponse::Unit)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs_error::FilesystemError;
    use crate::fs_tree::{FileTree, NodeKind};
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

    fn surface() -> TreeFsSurface<AsciiOrdinalCasing> {
        let c = AsciiOrdinalCasing;
        let mut base = FileTree::new();
        base.insert_dir(&c, &p("C:\\Windows"), md(0));
        base.insert_file(&c, &p("C:\\Windows\\notepad.exe"), md(100));
        base.insert_dir(&c, &p("C:\\Windows\\System32"), md(0));
        TreeFsSurface::new(OverlayFileTree::new(c, base))
    }

    #[test]
    fn dir_and_file_exists_requests() {
        let mut s = surface();
        assert_eq!(
            s.invoke(&FsRequest::DirExists {
                path: p("C:\\Windows"),
            }),
            Ok(FsResponse::Exists(true))
        );
        assert_eq!(
            s.invoke(&FsRequest::FileExists {
                path: p("C:\\Windows\\notepad.exe"),
            }),
            Ok(FsResponse::Exists(true))
        );
        assert_eq!(
            s.invoke(&FsRequest::DirExists {
                path: p("C:\\Nope"),
            }),
            Ok(FsResponse::Exists(false))
        );
    }

    #[test]
    fn read_metadata_request() {
        let mut s = surface();
        assert_eq!(
            s.invoke(&FsRequest::ReadMetadata {
                path: p("C:\\Windows\\notepad.exe"),
            }),
            Ok(FsResponse::Metadata(md(100)))
        );
    }

    #[test]
    fn write_then_read_metadata_request() {
        let mut s = surface();
        let f = p("C:\\Windows\\new.txt");
        assert_eq!(
            s.invoke(&FsRequest::WriteFile {
                path: f.clone(),
                metadata: md(42),
            }),
            Ok(FsResponse::Unit)
        );
        assert_eq!(
            s.invoke(&FsRequest::ReadMetadata { path: f }),
            Ok(FsResponse::Metadata(md(42)))
        );
    }

    #[test]
    fn create_and_remove_dir_requests() {
        let mut s = surface();
        let d = p("C:\\Windows\\New");
        assert_eq!(
            s.invoke(&FsRequest::CreateDir { path: d.clone() }),
            Ok(FsResponse::Unit)
        );
        assert_eq!(
            s.invoke(&FsRequest::DirExists { path: d.clone() }),
            Ok(FsResponse::Exists(true))
        );
        assert_eq!(
            s.invoke(&FsRequest::RemoveDir { path: d.clone() }),
            Ok(FsResponse::Unit)
        );
        assert_eq!(
            s.invoke(&FsRequest::DirExists { path: d }),
            Ok(FsResponse::Exists(false))
        );
    }

    #[test]
    fn remove_file_request() {
        let mut s = surface();
        let f = p("C:\\Windows\\notepad.exe");
        assert_eq!(
            s.invoke(&FsRequest::RemoveFile { path: f.clone() }),
            Ok(FsResponse::Unit)
        );
        assert_eq!(
            s.invoke(&FsRequest::FileExists { path: f }),
            Ok(FsResponse::Exists(false))
        );
    }

    #[test]
    fn read_metadata_missing_propagates_error() {
        let mut s = surface();
        assert_eq!(
            s.invoke(&FsRequest::ReadMetadata {
                path: p("C:\\Windows\\ghost.dll"),
            }),
            Err(FilesystemError::NotFound)
        );
    }

    #[test]
    fn read_dir_request_is_ordinal_ordered() {
        let mut s = surface();
        match s
            .invoke(&FsRequest::ReadDir {
                path: p("C:\\Windows"),
            })
            .unwrap()
        {
            FsResponse::Entries(entries) => {
                let names: Vec<String> =
                    entries.iter().map(|e| e.name.to_utf8().unwrap()).collect();
                // AsciiOrdinalCasing folds case, so notepad.exe ('N') precedes
                // System32 ('S').
                assert_eq!(names, vec!["notepad.exe".to_string(), "System32".to_string()]);
                assert_eq!(entries[0].kind, NodeKind::File);
                assert_eq!(entries[1].kind, NodeKind::Directory);
            }
            other => panic!("expected Entries, got {other:?}"),
        }
    }

    #[test]
    fn pass_through_forwards_unchanged() {
        let mut s = FsPassThrough::new(surface());
        assert_eq!(
            s.invoke(&FsRequest::FileExists {
                path: p("C:\\Windows\\notepad.exe"),
            }),
            Ok(FsResponse::Exists(true))
        );
        s.invoke(&FsRequest::WriteFile {
            path: p("C:\\Windows\\added.bin"),
            metadata: md(7),
        })
        .unwrap();
        assert_eq!(
            s.invoke(&FsRequest::ReadMetadata {
                path: p("C:\\Windows\\added.bin"),
            }),
            Ok(FsResponse::Metadata(md(7)))
        );
    }

    #[test]
    fn pass_through_into_inner_recovers_provider() {
        let s = FsPassThrough::new(surface());
        let mut inner = s.into_inner();
        assert_eq!(
            inner.invoke(&FsRequest::DirExists {
                path: p("C:\\Windows\\System32"),
            }),
            Ok(FsResponse::Exists(true))
        );
    }

    // --- FsBuffered (overlay-over-live write buffering) -------------------

    #[test]
    fn fs_buffered_writes_are_isolated_from_inner() {
        let mut buf = FsBuffered::new(surface(), AsciiOrdinalCasing);
        let f = p("C:\\Windows\\new.txt");
        assert_eq!(
            buf.invoke(&FsRequest::WriteFile {
                path: f.clone(),
                metadata: md(7),
            }),
            Ok(FsResponse::Unit)
        );
        // Read-your-writes through the decorator.
        assert_eq!(
            buf.invoke(&FsRequest::FileExists { path: f.clone() }),
            Ok(FsResponse::Exists(true))
        );
        assert_eq!(
            buf.invoke(&FsRequest::ReadMetadata { path: f.clone() }),
            Ok(FsResponse::Metadata(md(7)))
        );
        assert!(buf.is_dirty());
        assert_eq!(buf.journal().len(), 1);
        // The inner surface never saw the write.
        let mut inner = buf.into_inner();
        assert_eq!(
            inner.invoke(&FsRequest::FileExists { path: f }),
            Ok(FsResponse::Exists(false))
        );
    }

    #[test]
    fn fs_buffered_reads_fall_through_to_inner() {
        let mut buf = FsBuffered::new(surface(), AsciiOrdinalCasing);
        assert_eq!(
            buf.invoke(&FsRequest::FileExists {
                path: p("C:\\Windows\\notepad.exe"),
            }),
            Ok(FsResponse::Exists(true))
        );
        assert_eq!(
            buf.invoke(&FsRequest::DirExists {
                path: p("C:\\Windows\\System32"),
            }),
            Ok(FsResponse::Exists(true))
        );
        assert_eq!(
            buf.invoke(&FsRequest::ReadMetadata {
                path: p("C:\\Windows\\notepad.exe"),
            }),
            Ok(FsResponse::Metadata(md(100)))
        );
    }

    #[test]
    fn fs_buffered_remove_shadows_inner_without_touching_it() {
        let mut buf = FsBuffered::new(surface(), AsciiOrdinalCasing);
        let f = p("C:\\Windows\\notepad.exe");
        assert_eq!(
            buf.invoke(&FsRequest::RemoveFile { path: f.clone() }),
            Ok(FsResponse::Unit)
        );
        // Shadowed through the decorator (read-your-deletes).
        assert_eq!(
            buf.invoke(&FsRequest::FileExists { path: f.clone() }),
            Ok(FsResponse::Exists(false))
        );
        // The inner surface still has it.
        let mut inner = buf.into_inner();
        assert_eq!(
            inner.invoke(&FsRequest::FileExists { path: f }),
            Ok(FsResponse::Exists(true))
        );
    }

    #[test]
    fn fs_buffered_read_dir_merges_overlay_over_inner() {
        let mut buf = FsBuffered::new(surface(), AsciiOrdinalCasing);
        // Overlay-create a new file in an inner-existing directory.
        buf.invoke(&FsRequest::WriteFile {
            path: p("C:\\Windows\\added.bin"),
            metadata: md(3),
        })
        .unwrap();
        match buf
            .invoke(&FsRequest::ReadDir {
                path: p("C:\\Windows"),
            })
            .unwrap()
        {
            FsResponse::Entries(entries) => {
                let names: Vec<String> =
                    entries.iter().map(|e| e.name.to_utf8().unwrap()).collect();
                // added.bin (overlay) + notepad.exe + System32 (inner), ordinal.
                assert_eq!(
                    names,
                    vec![
                        "added.bin".to_string(),
                        "notepad.exe".to_string(),
                        "System32".to_string(),
                    ]
                );
            }
            other => panic!("expected Entries, got {other:?}"),
        }
    }

    #[test]
    fn fs_buffered_commit_replays_journal_onto_inner() {
        let mut buf = FsBuffered::new(surface(), AsciiOrdinalCasing);
        let f = p("C:\\Windows\\committed.txt");
        buf.invoke(&FsRequest::WriteFile {
            path: f.clone(),
            metadata: md(9),
        })
        .unwrap();
        buf.invoke(&FsRequest::RemoveFile {
            path: p("C:\\Windows\\notepad.exe"),
        })
        .unwrap();
        assert!(buf.is_dirty());
        buf.commit().unwrap();
        assert!(!buf.is_dirty());
        // The inner now reflects the replayed writes.
        let mut inner = buf.into_inner();
        assert_eq!(
            inner.invoke(&FsRequest::FileExists { path: f }),
            Ok(FsResponse::Exists(true))
        );
        assert_eq!(
            inner.invoke(&FsRequest::FileExists {
                path: p("C:\\Windows\\notepad.exe"),
            }),
            Ok(FsResponse::Exists(false))
        );
    }
}
