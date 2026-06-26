// Copyright (c) Microsoft Corporation.

//! The process-wide session (SHIM-D8 / SHIM-D13).
//!
//! A single [`ShimSession`] holds the isolation stack the C ABI routes through.
//! It is created lazily on first use and lives for the rest of the process. On
//! first access the session reads the `<host-executable>.pilcfg` sidecar
//! ([`crate::pilcfg`]) and composes its **registry** and **filesystem** backings
//! accordingly (SHIM-D13): `persisted_state` runs the registry entirely against a
//! loaded snapshot, `buffer_updates` interposes a write-buffering layer over the
//! live registry **and** the live filesystem (overlay-over-live, platform-isolation
//! D30), and the all-default configuration is live passthrough
//! ([`LiveRegistry`] / [`LiveFilesystem`]). Tests construct a session directly
//! from a [`Pilcfg`] via [`ShimSession::from_config`].
//!
//! Hive roots are vended through the held isolation [`Session`], so a path built
//! from this session resolves against the configured registry stack. Predefined
//! `HKEY`s resolve to a [`WellKnownRoot`] via
//! [`predefined_root`](crate::handle_table::predefined_root) and from there to a
//! root path here.

use std::sync::{Arc, Mutex, OnceLock};

use windows_platform_isolation::{
    BlockingEgress, Buffered, BufferedEgress, EgressRequest, EgressResponse, EgressResult,
    EgressSurface, Filesystem, FilesystemResult, FsBuffered, FsRequest, FsResponse, FsSurface,
    Hive, KeyPath, LiveEgress, LiveFilesystem, LiveRegistry, OverlayTree, RedirectRule,
    RedirectingEgress, Registry, ReplayEgress, ReplayMiss, ReplaySet, Request, Response, Session,
    Surface, TreeSurface, WellKnownRoot, Win32OrdinalCasing, load_registry_hive,
    save_registry_hive,
};

use crate::com::ComState;
use crate::egress_engine::EgressEngine;
use crate::handle_table::HandleTable;
use crate::journal::{JournalSink, JournalingEgress};
use crate::loader::LoaderState;
use crate::pilcfg::{EgressMode, Pilcfg, load_pilcfg};
use crate::web::WebState;

/// The live filesystem provider the default session routes through: live
/// passthrough over the real OS filesystem, keyed with the mandated production
/// ordinal casing (`Win32OrdinalCasing`).
type SessionFs = Filesystem<FilesystemBacking>;

/// The egress engine the session routes WinHTTP through (MW17 / SHIM-D22): the
/// reassembly engine over a [`JournalingEgress`] wrapper of the [`EgressBacking`]
/// selected from `.pilcfg`. The wrapper journals API interactions when the
/// `api_journal` block is enabled (AJ-B) and is a zero-overhead passthrough
/// otherwise.
type SessionEgress = EgressEngine<JournalingEgress<EgressBacking>>;

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

/// The filesystem surface the session routes through, selected from the
/// `.pilcfg` configuration (SHIM-D13): live passthrough by default, or an
/// overlay-over-live write buffer when `buffer_updates` is set (which now
/// buffers filesystem mutations as well as registry writes). A single concrete
/// type keeps the handle table and the surface-generic [`fs_ops`](crate::fs_ops)
/// free of dynamic dispatch: each variant is itself an [`FsSurface`], and this
/// enum dispatches across them.
pub enum FilesystemBacking {
    /// Live passthrough over the real OS filesystem (the default).
    Live(LiveFilesystem<Win32OrdinalCasing>),
    /// An overlay-over-live write buffer: namespace mutations are captured in an
    /// in-memory overlay and never written through (`buffer_updates`). Boxed
    /// because the overlay/journal make this variant much larger than `Live`.
    Buffered(Box<FsBuffered<LiveFilesystem<Win32OrdinalCasing>, Win32OrdinalCasing>>),
}

impl FsSurface for FilesystemBacking {
    fn invoke(&mut self, req: &FsRequest) -> FilesystemResult<FsResponse> {
        match self {
            Self::Live(surface) => surface.invoke(req),
            Self::Buffered(surface) => surface.invoke(req),
        }
    }
}

/// The egress (outbound WinHTTP) surface the session routes through, selected
/// from the `.pilcfg` `egress` block (MW17 / SHIM-D22). Each variant is itself an
/// [`EgressSurface`] over the live WinHTTP provider, so the
/// [`EgressEngine`](crate::egress_engine::EgressEngine) stays free of dynamic
/// dispatch.
pub enum EgressBacking {
    /// Live passthrough — reassemble-and-resend via real WinHTTP (the default).
    Passthrough(LiveEgress),
    /// Capture mutating requests in memory; reads fall through to live.
    Buffered(BufferedEgress<LiveEgress>),
    /// Rewrite each request's destination by rule, then send live.
    Redirecting(RedirectingEgress<LiveEgress>),
    /// Serve preloaded fixtures; misses are denied (offline).
    Replay(ReplayEgress<BlockingEgress>),
    /// Deny every request.
    Blocking(BlockingEgress),
}

impl EgressSurface for EgressBacking {
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        match self {
            Self::Passthrough(surface) => surface.send(req),
            Self::Buffered(surface) => surface.send(req),
            Self::Redirecting(surface) => surface.send(req),
            Self::Replay(surface) => surface.send(req),
            Self::Blocking(surface) => surface.send(req),
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
    filesystem: Mutex<SessionFs>,
    handles: HandleTable,
    loader: Mutex<LoaderState>,
    com: Mutex<ComState>,
    web: Mutex<WebState>,
    egress: Mutex<SessionEgress>,
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
    /// `persisted_state` (when it loads) wins and runs the registry entirely
    /// against the snapshot; otherwise `buffer_updates` selects the buffering
    /// layer (for both the registry and the filesystem); failing both, the
    /// backings are live passthrough.
    #[must_use]
    pub fn from_config(cfg: Pilcfg) -> Self {
        let casing = Win32OrdinalCasing;
        // One process-wide journal sink (when the `api_journal` block is enabled);
        // its Arc is shared with the seams that opt in.
        let journal_sink = JournalSink::from_config(&cfg.api_journal);
        Self {
            isolation: Session::new(),
            registry: Mutex::new(Registry::new(build_registry_backing(&cfg, casing))),
            filesystem: Mutex::new(Filesystem::new(build_filesystem_backing(&cfg, casing))),
            handles: HandleTable::new(),
            loader: Mutex::new(LoaderState::new()),
            com: Mutex::new(ComState::new()),
            web: Mutex::new(WebState::new()),
            egress: Mutex::new(EgressEngine::new(JournalingEgress::new(
                build_egress_backing(&cfg),
                journal_sink
                    .as_ref()
                    .filter(|sink| sink.capture_egress())
                    .map(Arc::clone),
            ))),
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
    pub fn with_filesystem<R>(&self, f: impl FnOnce(&mut SessionFs) -> R) -> R {
        let mut guard = self.filesystem.lock().expect("session filesystem poisoned");
        f(&mut guard)
    }

    /// Borrow the egress engine under the session lock (MW17 / SHIM-D22).
    ///
    /// The WinHTTP shims (`mWinHttp*`) route their `HINTERNET` handle table and
    /// per-request transaction reassembly through here.
    pub fn with_egress<R>(&self, f: impl FnOnce(&mut SessionEgress) -> R) -> R {
        let mut guard = self.egress.lock().expect("session egress poisoned");
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

    /// Borrow the web-host policy state under the session lock.
    ///
    /// The web-host shim (`mRegisterModule` and the shim `CHttpModule`
    /// notifications) routes its mode and observation sink through here
    /// (MW11, SHIM-D18).
    pub fn with_web<R>(&self, f: impl FnOnce(&mut WebState) -> R) -> R {
        let mut guard = self.web.lock().expect("session web poisoned");
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

/// Compose the filesystem backing from `cfg` (SHIM-D13): an overlay-over-live
/// write buffer when `buffer_updates` is set (so an unmodified consumer's
/// namespace mutations land in the in-memory overlay and the live filesystem is
/// left untouched until an explicit commit), else live passthrough. `buffer_updates`
/// thus now buffers *both* the registry and the filesystem.
fn build_filesystem_backing(cfg: &Pilcfg, casing: Win32OrdinalCasing) -> FilesystemBacking {
    if cfg.buffer_updates {
        return FilesystemBacking::Buffered(Box::new(FsBuffered::new(
            LiveFilesystem::new(casing),
            casing,
        )));
    }
    FilesystemBacking::Live(LiveFilesystem::new(casing))
}

/// Compose the egress backing from `cfg.egress` (MW17 / SHIM-D22): the outbound
/// network isolation mode, over the live WinHTTP provider. `passthrough`
/// reassembles-and-resends via `LiveEgress`; `buffer` captures mutating requests;
/// `redirect` rewrites destinations by rule; `replay` serves preloaded fixtures
/// (misses denied — offline); `block` denies everything.
fn build_egress_backing(cfg: &Pilcfg) -> EgressBacking {
    match cfg.egress.mode {
        EgressMode::Passthrough => EgressBacking::Passthrough(LiveEgress::new()),
        EgressMode::Buffer => EgressBacking::Buffered(BufferedEgress::new(LiveEgress::new())),
        EgressMode::Redirect => {
            let mut redirect = RedirectingEgress::new(LiveEgress::new());
            for (from, to) in &cfg.egress.redirections {
                if let Some(rule) = parse_redirect_rule(from, to) {
                    redirect.push_rule(rule);
                }
            }
            EgressBacking::Redirecting(redirect)
        }
        EgressMode::Replay => EgressBacking::Replay(ReplayEgress::new(
            BlockingEgress,
            load_replay_fixtures(&cfg.egress.replay_dir),
            ReplayMiss::ReadThrough,
        )),
        EgressMode::Block => EgressBacking::Blocking(BlockingEgress),
    }
}

/// Split a `host` / `host:port` authority into its host and optional port.
fn split_authority(authority: &str) -> (String, Option<u16>) {
    match authority.rsplit_once(':') {
        Some((host, port)) => match port.parse::<u16>() {
            Ok(port) => (host.to_string(), Some(port)),
            Err(_) => (authority.to_string(), None),
        },
        None => (authority.to_string(), None),
    }
}

/// Parse a `.pilcfg` egress redirection `(from, to)` into a [`RedirectRule`].
/// `from` matches `host` (any port) or `host:port`; `to`'s port defaults to the
/// `from` port, else `80`. Empty endpoints yield no rule.
fn parse_redirect_rule(from: &str, to: &str) -> Option<RedirectRule> {
    if from.is_empty() || to.is_empty() {
        return None;
    }
    let (from_host, from_port) = split_authority(from);
    let (to_host, to_port) = split_authority(to);
    let to_port = to_port.or(from_port).unwrap_or(80);
    Some(RedirectRule::new(from_host, from_port, to_host, to_port))
}

/// Load and merge every replay-fixture artifact in `dir` into one [`ReplaySet`].
/// Tolerant: an empty/absent/unreadable directory, and any unreadable or
/// malformed file, are skipped (SHIM-D5).
fn load_replay_fixtures(dir: &str) -> ReplaySet {
    let mut set = ReplaySet::new();
    if dir.is_empty() {
        return set;
    }
    let Ok(entries) = std::fs::read_dir(dir) else {
        return set;
    };
    for entry in entries.flatten() {
        let Ok(text) = std::fs::read_to_string(entry.path()) else {
            continue;
        };
        if let Ok(parsed) = ReplaySet::from_artifact(&text) {
            set.extend(parsed);
        }
    }
    set
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

    /// A unique, not-yet-existing path under the OS temp directory, tagged for
    /// readability and made unique across processes and calls.
    fn unique_temp_dir(tag: &str) -> String {
        use std::sync::atomic::{AtomicU32, Ordering};
        static COUNTER: AtomicU32 = AtomicU32::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let mut dir = std::env::temp_dir();
        dir.push(format!("{tag}-{}-{n}", std::process::id()));
        dir.to_string_lossy().into_owned()
    }

    #[test]
    fn buffer_updates_isolates_filesystem_mutations_from_the_live_fs() {
        use crate::fs_ops;
        use windows_platform_isolation::{FilePath, NodeKind};

        // A unique live path that must never appear on disk.
        let dir = unique_temp_dir("shim-fsbuf");
        let path = FilePath::from_utf8(&dir);

        let session = ShimSession::from_config(Pilcfg {
            buffer_updates: true,
            ..Pilcfg::default()
        });

        // Precondition: the directory does not exist before the buffered create.
        assert!(!std::path::Path::new(&dir).exists());

        // Create it through the buffered session filesystem.
        session
            .with_filesystem(|fs| fs_ops::create_directory(fs, &path))
            .expect("buffered create_directory");

        // Read-your-writes: the buffered session observes the new directory.
        let (_, kind) = session
            .with_filesystem(|fs| fs_ops::stat_path(fs, &path))
            .expect("buffered stat_path");
        assert_eq!(kind, NodeKind::Directory);

        // The live filesystem was never touched: the path is absent on disk.
        assert!(
            !std::path::Path::new(&dir).exists(),
            "a buffered mutation must not reach the live filesystem"
        );
    }

    #[test]
    fn default_config_passes_filesystem_mutations_through_to_the_live_fs() {
        use crate::fs_ops;
        use windows_platform_isolation::FilePath;

        // A unique live path the passthrough session will really create.
        let dir = unique_temp_dir("shim-fspass");
        let path = FilePath::from_utf8(&dir);

        let session = ShimSession::from_config(Pilcfg::default());
        assert!(!std::path::Path::new(&dir).exists());

        session
            .with_filesystem(|fs| fs_ops::create_directory(fs, &path))
            .expect("passthrough create_directory");

        // The default backing is live passthrough: the directory landed on disk.
        let landed = std::path::Path::new(&dir).exists();
        let _ = std::fs::remove_dir(&dir);
        assert!(
            landed,
            "the default (unbuffered) backing must write through to the live filesystem"
        );
    }

    #[test]
    fn split_authority_parses_host_and_port() {
        assert_eq!(split_authority("h"), ("h".to_string(), None));
        assert_eq!(split_authority("h:8019"), ("h".to_string(), Some(8019)));
        assert_eq!(split_authority("h:bad"), ("h:bad".to_string(), None));
    }

    #[test]
    fn parse_redirect_rule_handles_ports_and_empties() {
        assert!(parse_redirect_rule("", "to:1").is_none());
        assert!(parse_redirect_rule("from", "").is_none());
        assert!(parse_redirect_rule("h:8019", "stub").is_some()); // 'to' port defaults to 'from'
        assert!(parse_redirect_rule("h", "stub:9000").is_some());
    }

    #[test]
    fn build_egress_backing_selects_the_configured_mode() {
        use crate::pilcfg::EgressConfig;
        let mk = |mode| Pilcfg {
            egress: EgressConfig { mode, ..EgressConfig::default() },
            ..Pilcfg::default()
        };
        assert!(matches!(
            build_egress_backing(&mk(EgressMode::Passthrough)),
            EgressBacking::Passthrough(_)
        ));
        assert!(matches!(build_egress_backing(&mk(EgressMode::Buffer)), EgressBacking::Buffered(_)));
        assert!(matches!(
            build_egress_backing(&mk(EgressMode::Redirect)),
            EgressBacking::Redirecting(_)
        ));
        assert!(matches!(build_egress_backing(&mk(EgressMode::Replay)), EgressBacking::Replay(_)));
        assert!(matches!(build_egress_backing(&mk(EgressMode::Block)), EgressBacking::Blocking(_)));
    }

    /// Drive one request lifecycle through the session's egress engine, returning
    /// the response status (or the surface error).
    fn drive_egress(
        session: &ShimSession,
        verb: &str,
        host: &str,
        port: u16,
        path: &str,
        body: &[u8],
    ) -> EgressResult<u32> {
        use windows_platform_isolation::Utf16;
        session.with_egress(|engine| {
            let s = engine.open();
            let c = engine.connect(s, Utf16::from_utf8(host), port).expect("connect");
            let r = engine
                .open_request(c, Utf16::from_utf8(verb), Utf16::from_utf8(path), false)
                .expect("open_request");
            engine.send(r, Vec::new(), body.to_vec())?;
            Ok(engine.status(r).expect("status after send"))
        })
    }

    #[test]
    fn egress_block_mode_denies_through_the_session() {
        use crate::pilcfg::EgressConfig;
        let session = ShimSession::from_config(Pilcfg {
            egress: EgressConfig { mode: EgressMode::Block, ..EgressConfig::default() },
            ..Pilcfg::default()
        });
        assert!(drive_egress(&session, "GET", "h", 80, "/", b"").is_err());
    }

    #[test]
    fn egress_buffer_mode_captures_mutations_through_the_session() {
        use crate::pilcfg::EgressConfig;
        let session = ShimSession::from_config(Pilcfg {
            egress: EgressConfig { mode: EgressMode::Buffer, ..EgressConfig::default() },
            ..Pilcfg::default()
        });
        // A POST is buffered (202 ack); no live network is contacted.
        let status = drive_egress(&session, "POST", "h", 80, "/w", b"data").expect("buffered send");
        assert_eq!(status, 202);
    }

    #[test]
    fn egress_replay_mode_serves_fixtures_through_the_session() {
        use crate::pilcfg::EgressConfig;
        let dir = unique_temp_dir("egress-replay");
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(
            std::path::Path::new(&dir).join("fix.xml"),
            r#"<Egress><Fixture verb="GET" path="/custom" status="200"><Body>{"ok":true}</Body></Fixture></Egress>"#,
        )
        .unwrap();

        let session = ShimSession::from_config(Pilcfg {
            egress: EgressConfig {
                mode: EgressMode::Replay,
                replay_dir: dir.clone(),
                ..EgressConfig::default()
            },
            ..Pilcfg::default()
        });
        let status =
            drive_egress(&session, "GET", "merriam", 80, "/custom", b"").expect("replay served");
        assert_eq!(status, 200);
        let _ = std::fs::remove_dir_all(&dir);
    }
}
