// Copyright (c) Microsoft Corporation.

//! The process-wide session (SHIM-D8 / SHIM-D13).
//!
//! A single [`ShimSession`] holds the isolation stack the C ABI routes through.
//! It is created lazily on first use and lives for the rest of the process. On
//! first access the session reads the `<host-executable>.pilcfg` sidecar
//! ([`crate::pilcfg`]) and composes its **registry** backing accordingly
//! (SHIM-D13): `persisted_state` runs entirely against a loaded snapshot,
//! `buffer_updates` interposes a write-buffering layer over the live registry,
//! and the all-default configuration is live passthrough ([`LiveRegistry`]). The
//! filesystem surface is always live passthrough ([`LiveFilesystem`]) for this
//! milestone; `.pilcfg`-driven filesystem layering is a documented gap
//! (SHIM-D13). Tests construct a session directly from a [`Pilcfg`] via
//! [`ShimSession::from_config`].
//!
//! Hive roots are vended through the held isolation [`Session`], so a path built
//! from this session resolves against the configured registry stack. Predefined
//! `HKEY`s resolve to a [`WellKnownRoot`] via
//! [`predefined_root`](crate::handle_table::predefined_root) and from there to a
//! root path here.

use std::sync::{Mutex, OnceLock};

use windows_platform_isolation::{
    Buffered, Filesystem, Hive, KeyPath, LiveFilesystem, LiveRegistry, OverlayTree, Registry,
    Request, Response, Session, Surface, TreeSurface, WellKnownRoot, Win32OrdinalCasing,
    load_registry_hive, save_registry_hive,
};

use crate::com::ComState;
use crate::handle_table::HandleTable;
use crate::loader::LoaderState;
use crate::pilcfg::{Pilcfg, load_pilcfg};

/// The live filesystem provider the default session routes through: live
/// passthrough over the real OS filesystem, keyed with the mandated production
/// ordinal casing (`Win32OrdinalCasing`).
type LiveFs = Filesystem<LiveFilesystem<Win32OrdinalCasing>>;

/// The registry surface the session routes through, selected from the `.pilcfg`
/// configuration (SHIM-D13). A single concrete type keeps the handle table and
/// the surface-generic [`reg_ops`](crate::reg_ops) free of dynamic dispatch:
/// each variant is itself a [`Surface`], and this enum dispatches across them.
pub enum RegistryBacking {
    /// Live passthrough over the real OS registry (the default).
    Live(LiveRegistry),
    /// A write-buffering layer over the live registry: mutations are captured
    /// in memory and never written through (`buffer_updates`).
    Buffered(Buffered<LiveRegistry, Win32OrdinalCasing>),
    /// An in-memory snapshot loaded from a `persisted_state` artifact: the
    /// session runs entirely against it and never touches the live registry.
    Persisted(TreeSurface<Win32OrdinalCasing>),
}

impl Surface for RegistryBacking {
    fn invoke(&mut self, req: &Request) -> windows_platform_isolation::Result<Response> {
        match self {
            Self::Live(surface) => surface.invoke(req),
            Self::Buffered(surface) => surface.invoke(req),
            Self::Persisted(surface) => surface.invoke(req),
        }
    }
}

/// The process-wide isolation session and its handle table.
///
/// The registry and filesystem facades are each held behind a [`Mutex`] because
/// the surface `invoke` takes `&mut self`; the C ABI is free-threaded, so every
/// operation borrows the relevant facade under its lock via
/// [`ShimSession::with_registry`] / [`ShimSession::with_filesystem`].
pub struct ShimSession {
    isolation: Session,
    registry: Mutex<Registry<RegistryBacking>>,
    filesystem: Mutex<LiveFs>,
    handles: HandleTable,
    loader: Mutex<LoaderState>,
    com: Mutex<ComState>,
    casing: Win32OrdinalCasing,
    capture_snapshot: String,
}

impl ShimSession {
    /// Build a session from the `<host-executable>.pilcfg` sidecar (SHIM-D13).
    ///
    /// Equivalent to `from_config(load_pilcfg())`; an absent or malformed
    /// sidecar yields the default passthrough configuration (tolerant load).
    #[must_use]
    pub fn new() -> Self {
        Self::from_config(load_pilcfg())
    }

    /// Build a session whose registry backing is composed from `cfg` (SHIM-D13).
    ///
    /// `persisted_state` (when it loads) wins and runs entirely against the
    /// snapshot; otherwise `buffer_updates` selects the buffering layer; failing
    /// both, the backing is live passthrough. The filesystem surface is always
    /// live passthrough for this milestone.
    #[must_use]
    pub fn from_config(cfg: Pilcfg) -> Self {
        let casing = Win32OrdinalCasing;
        Self {
            isolation: Session::new(),
            registry: Mutex::new(Registry::new(build_registry_backing(&cfg, casing))),
            filesystem: Mutex::new(Filesystem::new(LiveFilesystem::new(casing))),
            handles: HandleTable::new(),
            loader: Mutex::new(LoaderState::new()),
            com: Mutex::new(ComState::new()),
            casing,
            capture_snapshot: cfg.capture_snapshot,
        }
    }

    /// Borrow the registry facade under the session lock.
    pub fn with_registry<R>(&self, f: impl FnOnce(&mut Registry<RegistryBacking>) -> R) -> R {
        let mut guard = self.registry.lock().expect("session registry poisoned");
        f(&mut guard)
    }

    /// Write the configured `capture_snapshot` artifact from the current
    /// registry state, returning whether a snapshot was written (SHIM-D13).
    ///
    /// Best-effort: a no-op (returns `false`) when no `capture_snapshot` path is
    /// configured, when the backing is not a persisted snapshot (live and
    /// buffered backings cannot be serialized — a documented gap), or when the
    /// file write fails. Intended to be invoked at teardown; automatic
    /// process-exit wiring is deferred (SHIM-D13).
    pub fn capture_snapshot(&self) -> bool {
        if self.capture_snapshot.is_empty() {
            return false;
        }
        let casing = self.casing;
        let xml = self.with_registry(|reg| match reg.surface() {
            RegistryBacking::Persisted(surface) => {
                Some(save_registry_hive(casing, &fold_tree_to_hive(surface.tree(), &casing)))
            }
            _ => None,
        });
        match xml {
            Some(xml) => std::fs::write(&self.capture_snapshot, xml).is_ok(),
            None => false,
        }
    }

    /// Borrow the filesystem facade under the session lock.
    pub fn with_filesystem<R>(&self, f: impl FnOnce(&mut LiveFs) -> R) -> R {
        let mut guard = self.filesystem.lock().expect("session filesystem poisoned");
        f(&mut guard)
    }

    /// The session's handle table.
    #[must_use]
    pub fn handles(&self) -> &HandleTable {
        &self.handles
    }

    /// Borrow the loader policy state under the session lock (SHIM-D16).
    ///
    /// The loader shims (`mLoadLibrary*`, `mGetProcAddress`, `mFreeLibrary`,
    /// `mGetModuleHandle*`) route their module table, substitution tables, and
    /// observation sink through here.
    pub fn with_loader<R>(&self, f: impl FnOnce(&mut LoaderState) -> R) -> R {
        let mut guard = self.loader.lock().expect("session loader poisoned");
        f(&mut guard)
    }

    /// Borrow the COM activation policy state under the session lock (SHIM-D17).
    ///
    /// The COM shims (`mCoCreateInstance`, `mCoCreateInstanceEx`,
    /// `mCoGetClassObject`, and the passthrough lifecycle exports) route their
    /// class-factory registry and observation sink through here.
    pub fn with_com<R>(&self, f: impl FnOnce(&mut ComState) -> R) -> R {
        let mut guard = self.com.lock().expect("session com poisoned");
        f(&mut guard)
    }

    /// The path of a well-known root, vended by the held isolation session.
    #[must_use]
    pub fn root_path(&self, which: WellKnownRoot) -> KeyPath {
        self.isolation.root(which)
    }
}

impl Default for ShimSession {
    fn default() -> Self {
        Self::new()
    }
}

/// Compose the registry backing from `cfg` (SHIM-D13).
///
/// `persisted_state` is tried first: when the artifact loads, the session runs
/// entirely against the in-memory snapshot (and `buffer_updates` is ignored, as
/// in the C++ shim). A present-but-unreadable / malformed `persisted_state`
/// falls back to live passthrough (tolerant load, SHIM-D5) rather than failing
/// the host. Absent a snapshot, `buffer_updates` selects the buffering layer;
/// failing that, the backing is live passthrough.
fn build_registry_backing(cfg: &Pilcfg, casing: Win32OrdinalCasing) -> RegistryBacking {
    if !cfg.persisted_state.is_empty() {
        if let Some(backing) = load_persisted_backing(&cfg.persisted_state, casing) {
            return backing;
        }
        return RegistryBacking::Live(LiveRegistry::new());
    }
    if cfg.buffer_updates {
        return RegistryBacking::Buffered(Buffered::new(LiveRegistry::new(), casing));
    }
    RegistryBacking::Live(LiveRegistry::new())
}

/// Load a `persisted_state` artifact into a [`RegistryBacking::Persisted`], or
/// `None` if the file is absent / unreadable / malformed (tolerant).
fn load_persisted_backing(path: &str, casing: Win32OrdinalCasing) -> Option<RegistryBacking> {
    let xml = std::fs::read_to_string(path).ok()?;
    let hive = load_registry_hive(&casing, &xml).ok()?;
    Some(RegistryBacking::Persisted(TreeSurface::new(OverlayTree::new(
        casing, hive,
    ))))
}

/// Collapse an [`OverlayTree`]'s currently-observable state (base plus overlay
/// writes) into a flat [`Hive`] so it can be serialized by
/// [`save_registry_hive`] for `capture_snapshot` (SHIM-D13).
///
/// The facade's `save_registry_hive` accepts a `Hive`, while a live session
/// mutates an `OverlayTree`; this walk re-materializes the tree's enumerated
/// keys and values into a fresh hive, preserving the snapshot the run produced.
fn fold_tree_to_hive(tree: &OverlayTree<Win32OrdinalCasing>, casing: &Win32OrdinalCasing) -> Hive {
    let mut hive = Hive::new();
    let root = KeyPath::root();
    for name in tree.enum_subkeys(&root).unwrap_or_default() {
        copy_subtree(tree, &mut hive, casing, &root.child(name));
    }
    hive
}

/// Recursively copy the key at `path` (and its values and subkeys) from `tree`
/// into `hive`. An empty key is materialized so the name still enumerates.
fn copy_subtree(
    tree: &OverlayTree<Win32OrdinalCasing>,
    hive: &mut Hive,
    casing: &Win32OrdinalCasing,
    path: &KeyPath,
) {
    hive.insert_key(casing, path);
    for (name, data) in tree.enum_values(path).unwrap_or_default() {
        hive.insert_value(casing, path, name, data);
    }
    for sub in tree.enum_subkeys(path).unwrap_or_default() {
        copy_subtree(tree, hive, casing, &path.child(sub));
    }
}

/// The process-wide session storage.
static SESSION: OnceLock<ShimSession> = OnceLock::new();

/// The process-wide [`ShimSession`], created lazily on first access.
pub fn session() -> &'static ShimSession {
    SESSION.get_or_init(ShimSession::new)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn session_is_a_stable_singleton() {
        let a = session() as *const ShimSession;
        let b = session() as *const ShimSession;
        assert_eq!(a, b, "session() must return the same instance");
    }

    #[test]
    fn root_path_matches_canonical_name() {
        let s = ShimSession::new();
        assert_eq!(
            s.root_path(WellKnownRoot::LocalMachine),
            KeyPath::parse(WellKnownRoot::LocalMachine.canonical_name())
        );
        assert_eq!(
            s.root_path(WellKnownRoot::CurrentUser),
            KeyPath::parse("HKEY_CURRENT_USER")
        );
    }

    #[test]
    fn handle_table_is_reachable_from_session() {
        let s = ShimSession::new();
        assert!(s.handles().is_empty());
    }
}
