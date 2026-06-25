// Copyright (c) Microsoft Corporation.

//! The filesystem overlay / copy-on-write tree (D11 shape; pure safe-half
//! logic, D13) — the second isolation surface, modeled on the registry tree.
//!
//! An [`OverlayFileTree`] layers a mutable *overlay* over an immutable *base*
//! [`FileTree`]. Reads see the overlay union-mounted over the base; writes land
//! only in the overlay (copy-on-write) so the base is never mutated; deletions
//! are recorded as tombstones that shadow the base. All name ordering and
//! case-insensitive matching go through the ordinal-casing seam (D6/D8): every
//! map is keyed by the binary sort key, so iteration is ordinal-ordered and
//! lookups are ordinal case-insensitive (NTFS semantics).
//!
//! Two facts shape this tree relative to the registry one:
//!
//! * The namespace is **unified** (D13): a child of a directory is exactly one
//!   node — a subdirectory or a file — never both under one name. The directory
//!   map mirrors the registry's subkeys and the file map mirrors its values;
//!   the unified-namespace invariant is enforced at the mutation boundary by
//!   evicting any same-named entry of the other kind.
//! * A file carries **metadata only** (D14): byte content is out of scope for
//!   this milestone (the deferred streams work). A file node is therefore a
//!   leaf with a [`FileMetadata`] payload, exactly where the registry tree
//!   stores a value's data.

use std::collections::BTreeMap;

use crate::file_path::FilePath;
use crate::fs_error::{FilesystemError, FilesystemResult};
use crate::{OrdinalCasing, Utf16};

/// The kind of a filesystem node. In the unified namespace (D13) every child of
/// a directory is exactly one of these. The set is intrinsically binary, so —
/// unlike the registry's `ValueType` — it is a closed enum.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum NodeKind {
    /// A directory: an interior node that may contain further entries.
    Directory,
    /// A file: a leaf node carrying [`FileMetadata`].
    File,
}

/// Metadata captured for a filesystem node (mirror of the C++ `file_metadata`).
///
/// Timestamps are raw clock-tick counts as stored in the shared artifact (D5);
/// the Rust surface treats them as opaque `i64`s that round-trip losslessly.
/// `attributes` carries the Win32 `FILE_ATTRIBUTE_*` bitset verbatim. Content
/// is intentionally absent (D14).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FileMetadata {
    /// Byte size; always 0 for a directory.
    pub size: u64,
    /// Creation time, in surface-clock ticks.
    pub creation_time: i64,
    /// Last-write time, in surface-clock ticks.
    pub last_write_time: i64,
    /// Last-access time, in surface-clock ticks.
    pub last_access_time: i64,
    /// Win32 `FILE_ATTRIBUTE_*` flags, verbatim.
    pub attributes: u32,
}

/// One entry yielded by [`OverlayFileTree::read_dir`]: a leaf name, its kind,
/// its metadata, and its optional 8.3 short name. The list a directory yields
/// is ordinal-ordered (D8).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DirEntry {
    /// The entry's leaf name, in its original casing.
    pub name: Utf16,
    /// Whether the entry is a subdirectory or a file.
    pub kind: NodeKind,
    /// The entry's captured metadata.
    pub metadata: FileMetadata,
    /// The entry's 8.3 short name (alternate name), or `None` when the source
    /// supplies none. On the live filesystem this is sourced from the OS's
    /// `cAlternateFileName`; on synthetic trees it is whatever short name the
    /// base node was stamped with (default `None`). See D23.
    pub short_name: Option<Utf16>,
}

// --- Base tree: immutable captured content ----------------------------------

/// An immutable directory tree — the base layer an [`OverlayFileTree`] shadows.
/// Built from synthetic data or, in M6-6, from a C++ filesystem artifact (D15).
#[derive(Clone, Debug, Default)]
pub struct FileTree {
    root: BaseNode,
}

/// A node in the immutable base. The root is a directory; `dirs` mirrors the
/// registry hive's subkeys and `files` mirrors its values (here, metadata).
#[derive(Clone, Debug, Default)]
struct BaseNode {
    metadata: FileMetadata,
    /// This directory's 8.3 short name as seen from its parent, or `None`
    /// (default). The root's value is unused. See D23.
    short_name: Option<Utf16>,
    /// sort_key(name) -> (original name, child directory)
    dirs: BTreeMap<Vec<u8>, (Utf16, BaseNode)>,
    /// sort_key(name) -> file entry (original name, metadata, optional short name)
    files: BTreeMap<Vec<u8>, BaseFileEntry>,
}

/// A file stored in the immutable [`FileTree`] base: its original-cased leaf
/// name, its metadata, and its optional 8.3 short name (default `None`,
/// see D23).
#[derive(Clone, Debug)]
struct BaseFileEntry {
    name: Utf16,
    metadata: FileMetadata,
    short_name: Option<Utf16>,
}

impl FileTree {
    /// An empty tree (a single empty root directory).
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Ensure a directory exists at `path` (creating intermediate directories),
    /// stamping the leaf with `metadata`.
    pub fn insert_dir<C: OrdinalCasing>(&mut self, casing: &C, path: &FilePath, metadata: FileMetadata) {
        let comps = path.components();
        let node = self.root.ensure_path(casing, &comps);
        node.metadata = metadata;
    }

    /// Like [`insert_dir`](Self::insert_dir), but also stamps the leaf
    /// directory with an 8.3 `short_name` surfaced by [`OverlayFileTree::read_dir`]
    /// (D23). Used to build synthetic trees that exercise short-name fidelity
    /// deterministically off-Windows.
    pub fn insert_dir_with_short_name<C: OrdinalCasing>(
        &mut self,
        casing: &C,
        path: &FilePath,
        metadata: FileMetadata,
        short_name: Option<Utf16>,
    ) {
        let comps = path.components();
        let node = self.root.ensure_path(casing, &comps);
        node.metadata = metadata;
        node.short_name = short_name;
    }

    /// Insert (or replace) a file at `path` with `metadata`, creating any
    /// intermediate directories. A `path` with no components is ignored (a file
    /// must have a leaf name).
    pub fn insert_file<C: OrdinalCasing>(&mut self, casing: &C, path: &FilePath, metadata: FileMetadata) {
        self.insert_file_with_short_name(casing, path, metadata, None);
    }

    /// Like [`insert_file`](Self::insert_file), but also stamps the file with an
    /// 8.3 `short_name` surfaced by [`OverlayFileTree::read_dir`] (D23).
    pub fn insert_file_with_short_name<C: OrdinalCasing>(
        &mut self,
        casing: &C,
        path: &FilePath,
        metadata: FileMetadata,
        short_name: Option<Utf16>,
    ) {
        let comps = path.components();
        let Some((parent, name)) = split_leaf(&comps) else {
            return;
        };
        let node = self.root.ensure_path(casing, parent);
        let key = casing.sort_key(name.as_units());
        node.files.insert(
            key,
            BaseFileEntry {
                name: name.clone(),
                metadata,
                short_name,
            },
        );
    }

    fn node_at<C: OrdinalCasing>(&self, casing: &C, comps: &[Utf16]) -> Option<&BaseNode> {
        let mut node = &self.root;
        for c in comps {
            let key = casing.sort_key(c.as_units());
            node = &node.dirs.get(&key)?.1;
        }
        Some(node)
    }
}

impl BaseNode {
    fn ensure_path<C: OrdinalCasing>(&mut self, casing: &C, comps: &[Utf16]) -> &mut BaseNode {
        let mut node = self;
        for c in comps {
            let key = casing.sort_key(c.as_units());
            node = &mut node
                .dirs
                .entry(key)
                .or_insert_with(|| (c.clone(), BaseNode::default()))
                .1;
        }
        node
    }
}

// --- Overlay: modifications layered over the base ---------------------------

#[derive(Debug)]
enum FileState {
    Set(Utf16, FileMetadata),
    Deleted,
}

/// A node in the overlay. A present (non-deleted) node asserts the directory
/// exists in the overlay; `deleted` is a tombstone that shadows the base
/// subtree.
#[derive(Debug, Default)]
struct OverlayNode {
    deleted: bool,
    metadata: Option<FileMetadata>,
    files: BTreeMap<Vec<u8>, FileState>,
    dirs: BTreeMap<Vec<u8>, (Utf16, OverlayNode)>,
}

enum Resolve<'a> {
    /// A tombstone (this directory or an ancestor) shadows the base: absent.
    Deleted,
    /// The overlay has a live node for this exact path.
    Overlay(&'a OverlayNode),
    /// The overlay says nothing about this path; defer to the base.
    NotInOverlay,
}

/// The overlay's opinion of a path, used to layer the overlay over a *live*
/// inner surface ([`FsBuffered`](crate::FsBuffered)). The public existence /
/// metadata queries collapse "tombstoned" and "absent" against the (often empty)
/// base, but a live-backed decorator must distinguish them: a tombstone shadows
/// the live path, whereas an absent overlay entry must defer to it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OverlayPresence {
    /// The overlay materialized this node (a created directory or written file).
    Present,
    /// The overlay tombstoned this node (or a containing directory): it must be
    /// hidden even if the inner surface still has it.
    Deleted,
    /// The overlay says nothing about this path; defer to the inner surface.
    Absent,
}

/// An overlay / copy-on-write view over an immutable [`FileTree`] base.
pub struct OverlayFileTree<C: OrdinalCasing> {
    casing: C,
    base: FileTree,
    overlay: OverlayNode,
}

impl<C: OrdinalCasing> OverlayFileTree<C> {
    /// Create an overlay over `base` using `casing` for ordinal matching.
    pub fn new(casing: C, base: FileTree) -> Self {
        Self {
            casing,
            base,
            overlay: OverlayNode::default(),
        }
    }

    /// The immutable base layer (never mutated by writes — exposed for tests and
    /// callers that need to prove copy-on-write isolation).
    #[must_use]
    pub fn base(&self) -> &FileTree {
        &self.base
    }

    fn resolve(&self, comps: &[Utf16]) -> Resolve<'_> {
        let mut node = &self.overlay;
        for c in comps {
            let key = self.casing.sort_key(c.as_units());
            match node.dirs.get(&key) {
                Some((_, child)) => {
                    if child.deleted {
                        return Resolve::Deleted;
                    }
                    node = child;
                }
                None => return Resolve::NotInOverlay,
            }
        }
        Resolve::Overlay(node)
    }

    fn overlay_path(&mut self, comps: &[Utf16]) -> &mut OverlayNode {
        let casing = &self.casing;
        let mut node = &mut self.overlay;
        for c in comps {
            let key = casing.sort_key(c.as_units());
            let entry = node
                .dirs
                .entry(key)
                .or_insert_with(|| (c.clone(), OverlayNode::default()));
            // Navigating through a node asserts it exists (clears any tombstone).
            entry.1.deleted = false;
            node = &mut entry.1;
        }
        node
    }

    /// Whether a directory exists at `path` (the root always exists).
    #[must_use]
    pub fn dir_exists(&self, path: &FilePath) -> bool {
        let comps = path.components();
        match self.resolve(&comps) {
            Resolve::Deleted => false,
            Resolve::Overlay(_) => true,
            Resolve::NotInOverlay => self.base.node_at(&self.casing, &comps).is_some(),
        }
    }

    /// Whether a file exists at `path`. A path with no leaf name is never a file.
    #[must_use]
    pub fn file_exists(&self, path: &FilePath) -> bool {
        let comps = path.components();
        let Some((parent, name)) = split_leaf(&comps) else {
            return false;
        };
        let fkey = self.casing.sort_key(name.as_units());
        match self.resolve(parent) {
            Resolve::Deleted => false,
            Resolve::NotInOverlay => self.base_file(parent, &fkey).is_some(),
            Resolve::Overlay(node) => match node.files.get(&fkey) {
                Some(FileState::Set(_, _)) => true,
                Some(FileState::Deleted) => false,
                None => self.base_file(parent, &fkey).is_some(),
            },
        }
    }

    /// Create a directory at `path` (and any missing ancestors). Evicts any
    /// same-named file shadowing the leaf, preserving the unified-namespace
    /// invariant (D13).
    pub fn create_dir(&mut self, path: &FilePath) {
        let comps = path.components();
        if comps.is_empty() {
            return;
        }
        self.evict_file(&comps);
        self.overlay_path(&comps);
    }

    /// Delete the directory at `path` and its subtree (tombstone shadowing the
    /// base).
    pub fn remove_dir(&mut self, path: &FilePath) {
        let comps = path.components();
        if comps.is_empty() {
            return;
        }
        let node = self.overlay_path(&comps);
        node.deleted = true;
        node.metadata = None;
        node.files.clear();
        node.dirs.clear();
    }

    /// Set (or replace) the file at `path` with `metadata`. Copy-on-write: only
    /// the overlay is touched; the base is unchanged. Evicts any same-named
    /// directory shadowing the leaf (D13).
    pub fn set_file(&mut self, path: &FilePath, metadata: FileMetadata) {
        let comps = path.components();
        let Some((parent, name)) = split_leaf(&comps) else {
            return;
        };
        let fkey = self.casing.sort_key(name.as_units());
        let name = name.clone();
        // Evict a same-named directory living in the overlay at the parent.
        let parent_vec = parent.to_vec();
        let pnode = self.overlay_path(&parent_vec);
        pnode.dirs.remove(&fkey);
        pnode.files.insert(fkey, FileState::Set(name, metadata));
    }

    /// Delete the file at `path` (tombstone shadowing the base file).
    pub fn remove_file(&mut self, path: &FilePath) {
        let comps = path.components();
        let Some((parent, name)) = split_leaf(&comps) else {
            return;
        };
        let fkey = self.casing.sort_key(name.as_units());
        let parent_vec = parent.to_vec();
        let pnode = self.overlay_path(&parent_vec);
        pnode.files.insert(fkey, FileState::Deleted);
    }

    fn base_file(&self, parent: &[Utf16], fkey: &[u8]) -> Option<FileMetadata> {
        let node = self.base.node_at(&self.casing, parent)?;
        node.files.get(fkey).map(|f| f.metadata)
    }

    /// Read the metadata of the file at `path`.
    ///
    /// # Errors
    ///
    /// [`FilesystemError::NotFound`] if no file exists at `path` (absent,
    /// tombstoned, or the name denotes a directory).
    pub fn file_metadata(&self, path: &FilePath) -> FilesystemResult<FileMetadata> {
        let comps = path.components();
        let Some((parent, name)) = split_leaf(&comps) else {
            return Err(FilesystemError::NotFound);
        };
        let fkey = self.casing.sort_key(name.as_units());
        match self.resolve(parent) {
            Resolve::Deleted => Err(FilesystemError::NotFound),
            Resolve::NotInOverlay => self.base_file(parent, &fkey).ok_or(FilesystemError::NotFound),
            Resolve::Overlay(node) => match node.files.get(&fkey) {
                Some(FileState::Set(_, md)) => Ok(*md),
                Some(FileState::Deleted) => Err(FilesystemError::NotFound),
                None => self.base_file(parent, &fkey).ok_or(FilesystemError::NotFound),
            },
        }
    }

    /// The overlay's opinion of the *directory* at `path`, ignoring the base
    /// ([`OverlayPresence`]). A live-backed decorator uses this to decide whether
    /// to answer from the overlay, hide the inner path, or defer to the inner.
    #[must_use]
    pub fn dir_presence(&self, path: &FilePath) -> OverlayPresence {
        let comps = path.components();
        match self.resolve(&comps) {
            Resolve::Deleted => OverlayPresence::Deleted,
            Resolve::Overlay(_) => OverlayPresence::Present,
            Resolve::NotInOverlay => OverlayPresence::Absent,
        }
    }

    /// The overlay's opinion of the *file* at `path`, ignoring the base
    /// ([`OverlayPresence`]).
    #[must_use]
    pub fn file_presence(&self, path: &FilePath) -> OverlayPresence {
        let comps = path.components();
        let Some((parent, name)) = split_leaf(&comps) else {
            return OverlayPresence::Absent;
        };
        let fkey = self.casing.sort_key(name.as_units());
        match self.resolve(parent) {
            Resolve::Deleted => OverlayPresence::Deleted,
            Resolve::NotInOverlay => OverlayPresence::Absent,
            Resolve::Overlay(node) => match node.files.get(&fkey) {
                Some(FileState::Set(_, _)) => OverlayPresence::Present,
                Some(FileState::Deleted) => OverlayPresence::Deleted,
                None => OverlayPresence::Absent,
            },
        }
    }

    /// Enumerate the immediate entries of the directory at `path`,
    /// ordinal-ordered (D8), with the overlay merged over the base (created
    /// entries added, deleted entries removed).
    ///
    /// # Errors
    ///
    /// [`FilesystemError::NotFound`] if no directory exists at `path`.
    pub fn read_dir(&self, path: &FilePath) -> FilesystemResult<Vec<DirEntry>> {
        let comps = path.components();
        if !self.dir_exists(path) {
            return Err(FilesystemError::NotFound);
        }

        // Merge base then overlay into one ordinal-keyed map so directories and
        // files interleave in a single ordinal order.
        let mut merged: BTreeMap<Vec<u8>, DirEntry> = BTreeMap::new();
        if let Some(node) = self.base.node_at(&self.casing, &comps) {
            for (skey, (name, child)) in &node.dirs {
                merged.insert(
                    skey.clone(),
                    DirEntry {
                        name: name.clone(),
                        kind: NodeKind::Directory,
                        metadata: child.metadata,
                        short_name: child.short_name.clone(),
                    },
                );
            }
            for (fkey, f) in &node.files {
                merged.insert(
                    fkey.clone(),
                    DirEntry {
                        name: f.name.clone(),
                        kind: NodeKind::File,
                        metadata: f.metadata,
                        short_name: f.short_name.clone(),
                    },
                );
            }
        }
        if let Resolve::Overlay(node) = self.resolve(&comps) {
            for (skey, (name, child)) in &node.dirs {
                if child.deleted {
                    merged.remove(skey);
                } else {
                    // Preserve a base entry's short name when the overlay merely
                    // re-asserts an existing directory; purely overlay-created
                    // directories have no synthetic short name (D23).
                    let short_name = merged.get(skey).and_then(|e| e.short_name.clone());
                    merged.insert(
                        skey.clone(),
                        DirEntry {
                            name: name.clone(),
                            kind: NodeKind::Directory,
                            metadata: child.metadata.unwrap_or_default(),
                            short_name,
                        },
                    );
                }
            }
            for (fkey, state) in &node.files {
                match state {
                    FileState::Set(name, md) => {
                        merged.insert(
                            fkey.clone(),
                            DirEntry {
                                name: name.clone(),
                                kind: NodeKind::File,
                                metadata: *md,
                                // Overlay-written files carry no synthetic short
                                // name (D23).
                                short_name: None,
                            },
                        );
                    }
                    FileState::Deleted => {
                        merged.remove(fkey);
                    }
                }
            }
        }
        Ok(merged.into_values().collect())
    }

    /// Evict a same-named file at the leaf of `comps` from the overlay so a
    /// directory may take its place (unified namespace, D13).
    fn evict_file(&mut self, comps: &[Utf16]) {
        let Some((parent, name)) = split_leaf(comps) else {
            return;
        };
        let fkey = self.casing.sort_key(name.as_units());
        let parent_vec = parent.to_vec();
        let pnode = self.overlay_path(&parent_vec);
        pnode.files.remove(&fkey);
    }
}

/// Split a component list into `(parent_components, leaf_name)`, or `None` when
/// the list is empty (there is no leaf).
fn split_leaf(comps: &[Utf16]) -> Option<(&[Utf16], &Utf16)> {
    comps.split_last().map(|(leaf, parent)| (parent, leaf))
}

#[cfg(test)]
mod tests {
    use super::*;
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

    /// Build a small base tree:
    ///   C:\Windows\notepad.exe   (file, size 100)
    ///   C:\Windows\System32\     (directory)
    ///   C:\Users\                (directory)
    fn base_tree() -> FileTree {
        let c = AsciiOrdinalCasing;
        let mut t = FileTree::new();
        t.insert_file(&c, &p("C:\\Windows\\notepad.exe"), md(100));
        t.insert_dir(&c, &p("C:\\Windows\\System32"), md(0));
        t.insert_dir(&c, &p("C:\\Users"), md(0));
        t
    }

    fn tree() -> OverlayFileTree<AsciiOrdinalCasing> {
        OverlayFileTree::new(AsciiOrdinalCasing, base_tree())
    }

    fn names(entries: &[DirEntry]) -> Vec<String> {
        entries.iter().map(|e| e.name.to_utf8().unwrap()).collect()
    }

    #[test]
    fn root_directory_always_exists() {
        let t = tree();
        assert!(t.dir_exists(&p("")));
    }

    #[test]
    fn reads_through_to_base_directories() {
        let t = tree();
        assert!(t.dir_exists(&p("C:\\Windows")));
        assert!(t.dir_exists(&p("C:\\Windows\\System32")));
        assert!(t.dir_exists(&p("C:\\Users")));
        assert!(!t.dir_exists(&p("C:\\Nope")));
    }

    #[test]
    fn reads_through_to_base_files() {
        let t = tree();
        assert!(t.file_exists(&p("C:\\Windows\\notepad.exe")));
        assert_eq!(
            t.file_metadata(&p("C:\\Windows\\notepad.exe")),
            Ok(md(100))
        );
    }

    #[test]
    fn missing_file_is_not_found() {
        let t = tree();
        assert_eq!(
            t.file_metadata(&p("C:\\Windows\\absent.txt")),
            Err(FilesystemError::NotFound)
        );
        assert!(!t.file_exists(&p("C:\\Windows\\absent.txt")));
    }

    #[test]
    fn lookup_is_case_insensitive() {
        let t = tree();
        assert!(t.dir_exists(&p("c:\\windows\\system32")));
        assert!(t.file_exists(&p("C:\\WINDOWS\\NOTEPAD.EXE")));
        assert_eq!(t.file_metadata(&p("c:\\windows\\NotePad.exe")), Ok(md(100)));
    }

    #[test]
    fn create_dir_lands_only_in_overlay() {
        let mut t = tree();
        assert!(!t.dir_exists(&p("C:\\Windows\\Temp")));
        t.create_dir(&p("C:\\Windows\\Temp"));
        assert!(t.dir_exists(&p("C:\\Windows\\Temp")));
        // Base is untouched.
        let c = AsciiOrdinalCasing;
        assert!(t.base().node_at(&c, &p("C:\\Windows\\Temp").components()).is_none());
    }

    #[test]
    fn set_file_is_copy_on_write() {
        let mut t = tree();
        t.set_file(&p("C:\\Users\\readme.txt"), md(42));
        assert!(t.file_exists(&p("C:\\Users\\readme.txt")));
        assert_eq!(t.file_metadata(&p("C:\\Users\\readme.txt")), Ok(md(42)));
        // Base unchanged.
        let c = AsciiOrdinalCasing;
        let node = t.base().node_at(&c, &p("C:\\Users").components()).unwrap();
        assert!(node.files.is_empty());
    }

    #[test]
    fn overlay_file_overrides_base_metadata() {
        let mut t = tree();
        t.set_file(&p("C:\\Windows\\notepad.exe"), md(999));
        assert_eq!(t.file_metadata(&p("C:\\Windows\\notepad.exe")), Ok(md(999)));
    }

    #[test]
    fn remove_file_tombstones_base() {
        let mut t = tree();
        t.remove_file(&p("C:\\Windows\\notepad.exe"));
        assert!(!t.file_exists(&p("C:\\Windows\\notepad.exe")));
        assert_eq!(
            t.file_metadata(&p("C:\\Windows\\notepad.exe")),
            Err(FilesystemError::NotFound)
        );
    }

    #[test]
    fn remove_dir_tombstones_subtree() {
        let mut t = tree();
        t.remove_dir(&p("C:\\Windows"));
        assert!(!t.dir_exists(&p("C:\\Windows")));
        assert!(!t.dir_exists(&p("C:\\Windows\\System32")));
        assert!(!t.file_exists(&p("C:\\Windows\\notepad.exe")));
        // A sibling is unaffected.
        assert!(t.dir_exists(&p("C:\\Users")));
    }

    #[test]
    fn recreate_after_remove_dir() {
        let mut t = tree();
        t.remove_dir(&p("C:\\Windows"));
        assert!(!t.dir_exists(&p("C:\\Windows")));
        t.create_dir(&p("C:\\Windows"));
        assert!(t.dir_exists(&p("C:\\Windows")));
        // Recreation clears the tombstone (overlay_path un-marks `deleted`), so
        // the shadowed base content is re-exposed — the same semantic as the
        // registry overlay's create-after-delete (D11 mirror).
        assert!(t.file_exists(&p("C:\\Windows\\notepad.exe")));
    }

    #[test]
    fn read_dir_merges_and_orders_ordinally() {
        let t = tree();
        let entries = t.read_dir(&p("C:\\Windows")).unwrap();
        // Ordinal case-insensitive: notepad.exe folds to 'N'(0x4e) which sorts
        // before System32's 'S'(0x53).
        assert_eq!(names(&entries), vec!["notepad.exe", "System32"]);
        assert_eq!(entries[0].kind, NodeKind::File);
        assert_eq!(entries[1].kind, NodeKind::Directory);
    }

    #[test]
    fn read_dir_reflects_overlay_additions_and_deletions() {
        let mut t = tree();
        t.set_file(&p("C:\\Windows\\Aaa.txt"), md(1));
        t.create_dir(&p("C:\\Windows\\zzz"));
        t.remove_file(&p("C:\\Windows\\notepad.exe"));
        let entries = t.read_dir(&p("C:\\Windows")).unwrap();
        // Ordinal: 'A'(0x41) < 'S'(0x53) < 'z'(0x7a); notepad.exe removed.
        assert_eq!(names(&entries), vec!["Aaa.txt", "System32", "zzz"]);
    }

    #[test]
    fn read_dir_surfaces_synthetic_short_names() {
        let c = AsciiOrdinalCasing;
        let mut base = FileTree::new();
        base.insert_file_with_short_name(
            &c,
            &p("C:\\Windows\\longfilename.txt"),
            md(10),
            Some(Utf16::from_utf8("LONGFI~1.TXT")),
        );
        base.insert_dir_with_short_name(
            &c,
            &p("C:\\Windows\\LongDirName"),
            md(0),
            Some(Utf16::from_utf8("LONGDI~1")),
        );
        // A file with no short name surfaces `None`.
        base.insert_file(&c, &p("C:\\Windows\\ab.txt"), md(5));
        let t = OverlayFileTree::new(c, base);

        let entries = t.read_dir(&p("C:\\Windows")).unwrap();
        let by = |n: &str| entries.iter().find(|e| e.name.to_utf8().unwrap() == n).unwrap();
        assert_eq!(
            by("longfilename.txt").short_name.as_ref().map(|s| s.to_utf8().unwrap()),
            Some("LONGFI~1.TXT".to_string())
        );
        assert_eq!(
            by("LongDirName").short_name.as_ref().map(|s| s.to_utf8().unwrap()),
            Some("LONGDI~1".to_string())
        );
        assert_eq!(by("ab.txt").short_name, None);
    }

    #[test]
    fn read_dir_overlay_created_entries_have_no_short_name() {
        let mut t = tree();
        t.set_file(&p("C:\\Windows\\new.txt"), md(1));
        t.create_dir(&p("C:\\Windows\\newdir"));
        let entries = t.read_dir(&p("C:\\Windows")).unwrap();
        let by = |n: &str| entries.iter().find(|e| e.name.to_utf8().unwrap() == n).unwrap();
        assert_eq!(by("new.txt").short_name, None);
        assert_eq!(by("newdir").short_name, None);
    }

    #[test]
    fn read_dir_overlay_preserves_base_dir_short_name() {
        let c = AsciiOrdinalCasing;
        let mut base = FileTree::new();
        base.insert_dir_with_short_name(
            &c,
            &p("C:\\Windows\\LongDirName"),
            md(0),
            Some(Utf16::from_utf8("LONGDI~1")),
        );
        let mut t = OverlayFileTree::new(c, base);
        // Navigating into the base directory through the overlay makes the
        // overlay branch re-assert it at the parent's read_dir; the base short
        // name must survive (D23).
        t.create_dir(&p("C:\\Windows\\LongDirName\\child"));
        let entries = t.read_dir(&p("C:\\Windows")).unwrap();
        let dir = entries
            .iter()
            .find(|e| e.name.to_utf8().unwrap() == "LongDirName")
            .unwrap();
        assert_eq!(
            dir.short_name.as_ref().map(|s| s.to_utf8().unwrap()),
            Some("LONGDI~1".to_string())
        );
    }

    #[test]
    fn read_dir_on_missing_directory_is_not_found() {
        let t = tree();
        assert_eq!(
            t.read_dir(&p("C:\\Nope")),
            Err(FilesystemError::NotFound)
        );
    }

    #[test]
    fn read_dir_on_a_file_path_is_not_found() {
        let t = tree();
        assert_eq!(
            t.read_dir(&p("C:\\Windows\\notepad.exe")),
            Err(FilesystemError::NotFound)
        );
    }

    #[test]
    fn create_dir_evicts_same_named_overlay_file() {
        let mut t = tree();
        t.set_file(&p("C:\\Users\\thing"), md(7));
        assert!(t.file_exists(&p("C:\\Users\\thing")));
        t.create_dir(&p("C:\\Users\\thing"));
        assert!(t.dir_exists(&p("C:\\Users\\thing")));
        assert!(!t.file_exists(&p("C:\\Users\\thing")));
    }

    #[test]
    fn set_file_evicts_same_named_overlay_dir() {
        let mut t = tree();
        t.create_dir(&p("C:\\Users\\thing"));
        assert!(t.dir_exists(&p("C:\\Users\\thing")));
        t.set_file(&p("C:\\Users\\thing"), md(7));
        assert!(t.file_exists(&p("C:\\Users\\thing")));
        assert!(!t.dir_exists(&p("C:\\Users\\thing")));
    }

    #[test]
    fn forward_slashes_split_like_backslashes() {
        let t = tree();
        assert!(t.dir_exists(&p("C:/Windows/System32")));
        assert!(t.file_exists(&p("C:/Windows/notepad.exe")));
    }

    #[test]
    fn deep_creation_makes_intermediate_directories() {
        let mut t = tree();
        t.set_file(&p("C:\\a\\b\\c\\d.txt"), md(5));
        assert!(t.dir_exists(&p("C:\\a")));
        assert!(t.dir_exists(&p("C:\\a\\b")));
        assert!(t.dir_exists(&p("C:\\a\\b\\c")));
        assert!(t.file_exists(&p("C:\\a\\b\\c\\d.txt")));
    }

    #[test]
    fn read_dir_of_root_lists_drive_component() {
        let t = tree();
        let entries = t.read_dir(&p("")).unwrap();
        assert_eq!(names(&entries), vec!["C:"]);
        assert_eq!(entries[0].kind, NodeKind::Directory);
    }
}
