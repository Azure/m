// Copyright (c) Microsoft Corporation.

//! Dynamic-loader shim state (SHIM-D16 / platform-isolation D26, D29).
//!
//! The loader family (`mLoadLibrary*`, `mGetProcAddress`, `mFreeLibrary`,
//! `mGetModuleHandle*`) is how **runtime-bound** symbols and **substitutable
//! engines** come under the same redirection as static imports: a
//! dynamically-resolved API is called through a function pointer the host
//! obtained at runtime, so the aliasobj link-time seam (SHIM-D4 / D24) cannot
//! reach it; intercepting the host's *loader calls* is the only seam that does.
//!
//! This module holds the **safe policy state** the exported loader bodies route
//! through (the raw `HMODULE` / `FARPROC` pointer manipulation is the unsafe
//! boundary's job, added in MW9-2/3). It is composed exactly as the
//! registry/filesystem backings are (SHIM-D13):
//!
//! * a [`ModuleTable`] — the peer of the [`HandleTable`](crate::handle_table)
//!   that interns each load as either a **real** `HMODULE` (passthrough) or a
//!   **minted sentinel** standing for a shim-substituted engine module, using the
//!   same reserved bit pattern (SHIM-D3) so a sentinel is recognizable as ours;
//! * an [`ObservationSink`] (default [`NullSink`]) the session reports every
//!   loader call to, keyed so the D29 volume policy can later suppress a
//!   known-safe `(api, target)` pair;
//! * an [`EngineSubstitution`] registry naming the engine DLLs whose load mints a
//!   sentinel and the shim procs that sentinel resolves to; and
//! * a [`ShimProcTable`] mapping an exported API name to the shim body
//!   `mGetProcAddress` returns when a *dynamically*-resolved call should land in
//!   the same body as a *statically*-aliased one.
//!
//! These are the **shim-local** first cut (SHIM-D16): seeded programmatically
//! now and (later) from `.pilcfg`; promoting them into a shared
//! `windows-platform-isolation` loader surface is deferred until a second
//! consumer needs it. No C ABI exports live here — they arrive in MW9-2..MW9-4.

use std::collections::{HashMap, HashSet};

use crate::handle_table::{mint_value, RawHandle};

/// The integer width an `HMODULE` is interned and compared as. Like a Win32
/// `HANDLE` (see [`RawHandle`]), an `HMODULE` is pointer-sized; a `usize` holds
/// every value losslessly and keeps the sentinel minting inside the reserved
/// 31-bit pattern.
pub type RawModule = RawHandle;

/// A shim-supplied procedure address — a `FARPROC` represented as its integer
/// address. The first-cut policy tables store proc addresses as plain integers
/// so this module stays free of raw-pointer `unsafe`; the conversion to/from an
/// actual `FARPROC` happens in the loader's `#[allow(unsafe_code)]` boundary
/// (MW9-3).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct ShimProc(pub usize);

/// Normalize a module name for case-insensitive lookup.
///
/// Windows module names are case-insensitive; this first cut folds ASCII case
/// only (the names the loader sees — `kernel32`, an engine DLL — are ASCII).
/// Extension-equivalence (`foo` vs `foo.dll`) and non-ASCII folding are
/// deferred (SHIM-D16 first cut).
fn normalize_module_name(name: &str) -> String {
    name.to_ascii_lowercase()
}

// --- Module handle table ----------------------------------------------------

/// The entry interned behind a module-table value: either a real OS module or a
/// shim-substituted engine sentinel.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModuleEntry {
    /// A real OS `HMODULE` returned by the live loader (passthrough). The value
    /// is the real module base; recording it gives observability and complete
    /// accounting.
    Real(RawModule),
    /// A minted sentinel standing for a shim-substituted engine module, keyed by
    /// the (normalized) engine name so `mGetModuleHandle*` can find it again.
    Sentinel {
        /// The engine DLL name the sentinel was minted for (as the caller gave
        /// it; lookup is case-insensitive).
        name: String,
    },
}

/// The module handle table (SHIM-D16) — a peer of the [`HandleTable`].
///
/// `record_real` interns a real load; `mint_sentinel` mints (or re-returns) a
/// sentinel `HMODULE` for a substituted engine name; `entry` / `is_sentinel`
/// classify a raw value; `release_sentinel` frees a minted sentinel. The table
/// is accessed under the session lock, so unlike the [`HandleTable`] it carries
/// no interior `Mutex`.
///
/// [`HandleTable`]: crate::handle_table::HandleTable
#[derive(Debug, Default)]
pub struct ModuleTable {
    next_sequence: RawModule,
    entries: HashMap<RawModule, ModuleEntry>,
    sentinel_by_name: HashMap<String, RawModule>,
    pinned: HashSet<RawModule>,
}

impl ModuleTable {
    /// Create an empty module table.
    #[must_use]
    pub fn new() -> Self {
        Self {
            next_sequence: 1,
            entries: HashMap::new(),
            sentinel_by_name: HashMap::new(),
            pinned: HashSet::new(),
        }
    }

    /// Record a real OS module load so it is interned for accounting. Idempotent
    /// for a repeated base value. Real values are never minted here, so a real
    /// base that happens to satisfy the reserved pattern is still stored — it is
    /// classified by table contents, not by the bit test alone.
    pub fn record_real(&mut self, module: RawModule) {
        self.entries.entry(module).or_insert(ModuleEntry::Real(module));
    }

    /// Mint (or re-return) a sentinel `HMODULE` for the engine `name`.
    ///
    /// Re-minting the same name returns the existing sentinel so a substituted
    /// engine is found again by `mGetModuleHandle*`. The returned value always
    /// satisfies [`is_minted_value`] and never collides with a live entry.
    pub fn mint_sentinel(&mut self, name: &str) -> RawModule {
        let key = normalize_module_name(name);
        if let Some(&existing) = self.sentinel_by_name.get(&key) {
            return existing;
        }
        let value = loop {
            let sequence = self.next_sequence;
            self.next_sequence = self.next_sequence.wrapping_add(1);
            let candidate = mint_value(sequence);
            if !self.entries.contains_key(&candidate) {
                break candidate;
            }
            // Extremely unlikely wrap-around collision; keep minting.
        };
        self.entries.insert(
            value,
            ModuleEntry::Sentinel {
                name: name.to_owned(),
            },
        );
        self.sentinel_by_name.insert(key, value);
        value
    }

    /// The sentinel previously minted for `name`, if any (case-insensitive).
    #[must_use]
    pub fn sentinel_for_name(&self, name: &str) -> Option<RawModule> {
        self.sentinel_by_name
            .get(&normalize_module_name(name))
            .copied()
    }

    /// The entry interned behind `value`, if any.
    #[must_use]
    pub fn entry(&self, value: RawModule) -> Option<&ModuleEntry> {
        self.entries.get(&value)
    }

    /// Whether `value` is one of our minted engine sentinels (as opposed to a
    /// real module or an unknown value). This is the transparency query: any
    /// value that is not a sentinel must be forwarded untouched.
    #[must_use]
    pub fn is_sentinel(&self, value: RawModule) -> bool {
        matches!(self.entries.get(&value), Some(ModuleEntry::Sentinel { .. }))
    }

    /// Pin a minted sentinel so a later `release_sentinel` leaves it in place.
    /// Models the `GET_MODULE_HANDLE_EX_FLAG_PIN` flag of `GetModuleHandleEx`
    /// (SHIM-D16): a pinned engine sentinel survives `mFreeLibrary` just as a
    /// pinned OS module survives `FreeLibrary`. A no-op for non-sentinels.
    pub fn pin(&mut self, value: RawModule) {
        if self.is_sentinel(value) {
            self.pinned.insert(value);
        }
    }

    /// Whether `value` is a pinned sentinel.
    #[must_use]
    pub fn is_pinned(&self, value: RawModule) -> bool {
        self.pinned.contains(&value)
    }

    /// Release a minted sentinel, returning `true` if `value` named a live
    /// sentinel (so the caller does not forward to the real `FreeLibrary`). A
    /// **pinned** sentinel reports `true` but is kept (pin semantics); an
    /// unpinned sentinel is removed. A real or unknown value is left untouched
    /// and returns `false` (the caller forwards it).
    pub fn release_sentinel(&mut self, value: RawModule) -> bool {
        if let Some(ModuleEntry::Sentinel { name }) = self.entries.get(&value) {
            if self.pinned.contains(&value) {
                return true;
            }
            let key = normalize_module_name(name);
            self.sentinel_by_name.remove(&key);
            self.entries.remove(&value);
            return true;
        }
        false
    }

    /// The number of interned entries (real + sentinel), for diagnostics.
    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the table has no interned entries.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

// --- Engine substitution registry -------------------------------------------

/// The engine-substitution registry (SHIM-D16): the engine DLL names whose load
/// returns a minted sentinel, and the shim procs each sentinel resolves to.
///
/// Engine names are matched case-insensitively (like module names); proc names
/// are matched exactly, because `GetProcAddress` name resolution is
/// case-sensitive.
#[derive(Debug, Default)]
pub struct EngineSubstitution {
    engines: HashMap<String, HashMap<String, ShimProc>>,
}

impl EngineSubstitution {
    /// An empty registry (no engines substituted — the first-cut default).
    #[must_use]
    pub fn new() -> Self {
        Self {
            engines: HashMap::new(),
        }
    }

    /// Register `name` as a substituted engine with no procs yet (idempotent).
    pub fn register_engine(&mut self, name: &str) {
        self.engines
            .entry(normalize_module_name(name))
            .or_default();
    }

    /// Register the shim `proc` (by exact name) the engine `engine` resolves to,
    /// registering the engine if needed.
    pub fn add_proc(&mut self, engine: &str, proc: &str, address: ShimProc) {
        self.engines
            .entry(normalize_module_name(engine))
            .or_default()
            .insert(proc.to_owned(), address);
    }

    /// Whether `name` names a substituted engine (case-insensitive).
    #[must_use]
    pub fn is_engine(&self, name: &str) -> bool {
        self.engines.contains_key(&normalize_module_name(name))
    }

    /// The shim proc the engine `engine` resolves `proc` to, if registered.
    #[must_use]
    pub fn proc(&self, engine: &str, proc: &str) -> Option<ShimProc> {
        self.engines
            .get(&normalize_module_name(engine))?
            .get(proc)
            .copied()
    }

    /// The number of registered engines.
    #[must_use]
    pub fn len(&self) -> usize {
        self.engines.len()
    }

    /// Whether no engines are registered (the substitution-off default).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.engines.is_empty()
    }
}

// --- name -> shim-proc table ------------------------------------------------

/// The name→shim-proc table (SHIM-D16): maps an exported API name to the shim
/// body `mGetProcAddress` returns so a dynamically-resolved call lands in the
/// same `m*` body a static alias would. Seeded from the current export roster
/// and grows as new surfaces land. Proc names are matched exactly
/// (`GetProcAddress` name resolution is case-sensitive).
#[derive(Debug, Default)]
pub struct ShimProcTable {
    procs: HashMap<String, ShimProc>,
}

impl ShimProcTable {
    /// An empty table (no APIs shimmed for dynamic resolution yet).
    #[must_use]
    pub fn new() -> Self {
        Self {
            procs: HashMap::new(),
        }
    }

    /// Seed `name` to resolve to the shim body at `address` (overwrites any
    /// existing mapping for the exact name).
    pub fn seed(&mut self, name: &str, address: ShimProc) {
        self.procs.insert(name.to_owned(), address);
    }

    /// The shim body `name` resolves to, if shimmed (exact-name match).
    #[must_use]
    pub fn lookup(&self, name: &str) -> Option<ShimProc> {
        self.procs.get(name).copied()
    }

    /// Whether `name` is shimmed for dynamic resolution.
    #[must_use]
    pub fn contains(&self, name: &str) -> bool {
        self.procs.contains_key(name)
    }

    /// The number of shimmed names.
    #[must_use]
    pub fn len(&self) -> usize {
        self.procs.len()
    }

    /// Whether no names are shimmed.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.procs.is_empty()
    }
}

// --- Observation seam (D29) -------------------------------------------------

/// The procedure a `GetProcAddress` call resolved — by name or by ordinal. A
/// proc can be requested either way; the substitution tables key on names, but
/// observation records whichever form the caller used.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ProcQuery {
    /// Resolution by exported name.
    Named(String),
    /// Resolution by ordinal (the low word of the `lpProcName` integer).
    Ordinal(u16),
}

/// A single observed loader call (SHIM-D16 / D29). The variant is the API and
/// its fields are the target, so the session's volume policy can later suppress
/// a known-safe `(api, target)` pair. Observation never changes a returned value.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LoaderEvent {
    /// A `LoadLibrary*` of the named library.
    LoadLibrary {
        /// The library name the caller passed.
        name: String,
    },
    /// A `GetProcAddress` resolving `proc` in `module`.
    GetProcAddress {
        /// The module the proc was resolved in (best-effort name, else the raw
        /// value rendered for diagnostics).
        module: String,
        /// The procedure requested.
        proc: ProcQuery,
    },
    /// A `FreeLibrary` of the named/identified module.
    FreeLibrary {
        /// The module being freed (name if known, else a rendered value).
        module: String,
    },
    /// A `GetModuleHandle*` lookup of the named module.
    GetModuleHandle {
        /// The module name looked up.
        name: String,
    },
}

/// The seam the session reports every loader call to (default [`NullSink`]).
/// Kept minimal — one method — so the storage target is separable from the
/// loader bodies. Implementations must be `Send` because the session is
/// free-threaded and holds the sink behind its lock.
pub trait ObservationSink: Send {
    /// Record one observed loader call.
    fn observe(&mut self, event: LoaderEvent);
}

/// The default observation sink: records nothing (the off / first-cut posture).
#[derive(Debug, Default)]
pub struct NullSink;

impl ObservationSink for NullSink {
    fn observe(&mut self, _event: LoaderEvent) {}
}

// --- Loader policy state ----------------------------------------------------

/// How the loader shims behave (SHIM-D16, driven by session mode SHIM-D13 /
/// D25). The default is [`LoaderMode::Off`] — a pure forward that adds nothing.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum LoaderMode {
    /// Pure passthrough: forward every call, observe nothing, substitute
    /// nothing (the D24 identity posture).
    #[default]
    Off,
    /// Forward every call and report it to the observation sink (D29); the
    /// returned value is unchanged.
    Observe,
    /// Observe and apply engine / proc substitution for registered targets;
    /// untargeted calls remain transparent.
    Substitute,
}

/// The disposition of an observed `LoadLibrary*` call, decided by the policy and
/// carried out by the ABI body.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LoadDisposition {
    /// Return this minted sentinel `HMODULE`; do not call the OS loader (the
    /// engine was substituted).
    Substitute(RawModule),
    /// Call the real OS loader. `record` says whether to intern the real result
    /// in the module table afterward (suppressed in [`LoaderMode::Off`] so a
    /// pure passthrough adds no state).
    Forward {
        /// Whether to intern the OS result via [`LoaderState::record_loaded`].
        record: bool,
    },
}

/// The disposition of an observed `FreeLibrary` call.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FreeDisposition {
    /// The value named a minted sentinel that was released; do not call the OS
    /// loader.
    Released,
    /// The value is a real / unknown module; forward to the OS `FreeLibrary`.
    Forward,
}

/// The disposition of an observed `GetProcAddress` call.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProcDisposition {
    /// Return this shim proc address; do not call the OS `GetProcAddress`. A
    /// [`ShimProc`] of `0` is the "not found" result for a sentinel module whose
    /// engine does not supply the requested proc (still never forwarded — a
    /// sentinel is not a real module).
    Shim(ShimProc),
    /// Forward to the real `GetProcAddress` against a genuine module.
    Forward,
}

/// The disposition of an observed `GetModuleHandle` / `GetModuleHandleEx` call.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ModuleHandleDisposition {
    /// The name resolved to a previously-minted sentinel; return it without
    /// touching the OS loader.
    Sentinel(RawModule),
    /// No sentinel matched; forward to the real `GetModuleHandle*`.
    Forward,
}

/// The session-held loader policy: the mode, the substitution tables, the module
/// handle table, and the observation sink. Composed like the registry/filesystem
/// backings (SHIM-D13) and held behind the session lock.
pub struct LoaderState {
    /// The active behavior mode.
    pub mode: LoaderMode,
    /// The engine-substitution registry.
    pub engines: EngineSubstitution,
    /// The name→shim-proc table for dynamic resolution.
    pub procs: ShimProcTable,
    /// The module handle table.
    pub modules: ModuleTable,
    sink: Box<dyn ObservationSink>,
}

impl LoaderState {
    /// A default loader state: [`LoaderMode::Off`], empty tables, and a
    /// [`NullSink`] (the fully-transparent first-cut posture).
    #[must_use]
    pub fn new() -> Self {
        Self {
            mode: LoaderMode::Off,
            engines: EngineSubstitution::new(),
            procs: ShimProcTable::new(),
            modules: ModuleTable::new(),
            sink: Box::new(NullSink),
        }
    }

    /// Replace the observation sink (the session installs the real sink here).
    pub fn set_sink(&mut self, sink: Box<dyn ObservationSink>) {
        self.sink = sink;
    }

    /// Report `event` to the sink when the mode observes (Observe / Substitute);
    /// a pure no-op in [`LoaderMode::Off`].
    pub fn observe(&mut self, event: LoaderEvent) {
        if self.mode != LoaderMode::Off {
            self.sink.observe(event);
        }
    }

    /// Decide how a `LoadLibrary*(name)` should behave (SHIM-D16).
    ///
    /// In [`LoaderMode::Off`] this is a pure forward that records nothing. When
    /// observing it reports the load; in [`LoaderMode::Substitute`] a load of a
    /// registered engine mints (or re-returns) a sentinel `HMODULE` and skips the
    /// OS loader entirely. Every other load forwards and is interned by
    /// [`record_loaded`](Self::record_loaded).
    pub fn on_load_library(&mut self, name: &str) -> LoadDisposition {
        if self.mode == LoaderMode::Off {
            return LoadDisposition::Forward { record: false };
        }
        self.observe(LoaderEvent::LoadLibrary {
            name: name.to_owned(),
        });
        if self.mode == LoaderMode::Substitute && self.engines.is_engine(name) {
            LoadDisposition::Substitute(self.modules.mint_sentinel(name))
        } else {
            LoadDisposition::Forward { record: true }
        }
    }

    /// Intern a real `HMODULE` the OS loader returned (the [`LoadDisposition::Forward`]
    /// `record` follow-up).
    pub fn record_loaded(&mut self, module: RawModule) {
        self.modules.record_real(module);
    }

    /// Decide how a `FreeLibrary(module)` should behave (SHIM-D16).
    ///
    /// A minted sentinel is released here and never reaches the OS loader
    /// (the transparency-for-minted-values half of the invariant); any real or
    /// unknown value forwards. Observation (when the mode observes) names the
    /// sentinel being freed, else renders the raw value for diagnostics.
    pub fn on_free_library(&mut self, module: RawModule) -> FreeDisposition {
        if self.mode != LoaderMode::Off {
            let rendered = match self.modules.entry(module) {
                Some(ModuleEntry::Sentinel { name }) => name.clone(),
                _ => format!("{module:#x}"),
            };
            self.observe(LoaderEvent::FreeLibrary { module: rendered });
        }
        if self.modules.release_sentinel(module) {
            FreeDisposition::Released
        } else {
            FreeDisposition::Forward
        }
    }

    /// Decide how to resolve a `GetProcAddress` against `module` for `query`.
    ///
    /// Off mode is a pure forward (no observation, no redirection). Otherwise the
    /// resolution is reported to the sink, then:
    ///
    /// * a minted sentinel module never reaches the OS loader (transparency for
    ///   minted values): a named proc supplied by the substituted engine yields
    ///   [`ProcDisposition::Shim`]; anything else yields `Shim(ShimProc(0))`
    ///   (a null "not found"), still without forwarding;
    /// * in [`LoaderMode::Substitute`], a real / unknown module whose requested
    ///   proc name is in the shim-proc table is redirected to the shim body;
    /// * every other case forwards to the genuine `GetProcAddress`.
    ///
    /// Engine substitution and proc redirection key on the proc *name*; ordinal
    /// queries against a sentinel resolve to the null "not found" result.
    pub fn on_get_proc_address(&mut self, module: RawModule, query: &ProcQuery) -> ProcDisposition {
        if self.mode == LoaderMode::Off {
            return ProcDisposition::Forward;
        }
        let module_name = match self.modules.entry(module) {
            Some(ModuleEntry::Sentinel { name }) => name.clone(),
            _ => format!("{module:#x}"),
        };
        self.observe(LoaderEvent::GetProcAddress {
            module: module_name,
            proc: query.clone(),
        });
        if let Some(ModuleEntry::Sentinel { name }) = self.modules.entry(module) {
            if let ProcQuery::Named(proc) = query
                && let Some(shim) = self.engines.proc(name, proc)
            {
                return ProcDisposition::Shim(shim);
            }
            return ProcDisposition::Shim(ShimProc(0));
        }
        if self.mode == LoaderMode::Substitute
            && let ProcQuery::Named(proc) = query
            && let Some(shim) = self.procs.lookup(proc)
        {
            return ProcDisposition::Shim(shim);
        }
        ProcDisposition::Forward
    }

    /// Decide how to resolve a `GetModuleHandleW`/`A` for `name`.
    ///
    /// Off mode is a pure forward. Otherwise the lookup is reported to the sink,
    /// then a previously-minted engine sentinel for `name` yields
    /// [`ModuleHandleDisposition::Sentinel`]; anything else forwards to the real
    /// `GetModuleHandle*` (a real module is never interned by a handle query — it
    /// is interned only by an actual load).
    pub fn on_get_module_handle(&mut self, name: &str) -> ModuleHandleDisposition {
        if self.mode == LoaderMode::Off {
            return ModuleHandleDisposition::Forward;
        }
        self.observe(LoaderEvent::GetModuleHandle {
            name: name.to_owned(),
        });
        match self.modules.sentinel_for_name(name) {
            Some(raw) => ModuleHandleDisposition::Sentinel(raw),
            None => ModuleHandleDisposition::Forward,
        }
    }

    /// The `GetModuleHandleEx` form of [`on_get_module_handle`]: resolves `name`
    /// the same way and, when `pin` is set and a sentinel matched, pins the
    /// sentinel so a later `mFreeLibrary` leaves it in place (SHIM-D16 minimal
    /// `GET_MODULE_HANDLE_EX_FLAG_PIN` modeling). The unchanged-refcount flag is
    /// inherently a no-op for sentinels, which carry no OS reference count.
    ///
    /// [`on_get_module_handle`]: Self::on_get_module_handle
    pub fn on_get_module_handle_ex(&mut self, name: &str, pin: bool) -> ModuleHandleDisposition {
        let disposition = self.on_get_module_handle(name);
        if pin && let ModuleHandleDisposition::Sentinel(raw) = disposition {
            self.modules.pin(raw);
        }
        disposition
    }
}

impl Default for LoaderState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::handle_table::is_minted_value;

    #[test]
    fn minted_sentinels_satisfy_reserved_pattern() {
        let mut table = ModuleTable::new();
        for i in 0..500 {
            let name = format!("engine{i}.dll");
            let value = table.mint_sentinel(&name);
            assert!(
                is_minted_value(value),
                "sentinel {value:#x} failed the reserved pattern"
            );
            assert!(table.is_sentinel(value));
        }
        assert_eq!(table.len(), 500);
    }

    #[test]
    fn mint_sentinel_is_idempotent_per_name() {
        let mut table = ModuleTable::new();
        let first = table.mint_sentinel("WebCore.dll");
        let again = table.mint_sentinel("WebCore.dll");
        assert_eq!(first, again, "re-minting a name returns the same sentinel");
        assert_eq!(table.len(), 1);
    }

    #[test]
    fn sentinel_lookup_is_case_insensitive() {
        let mut table = ModuleTable::new();
        let value = table.mint_sentinel("WebCore.dll");
        assert_eq!(table.sentinel_for_name("webcore.dll"), Some(value));
        assert_eq!(table.sentinel_for_name("WEBCORE.DLL"), Some(value));
        assert_eq!(table.sentinel_for_name("other.dll"), None);
    }

    #[test]
    fn real_modules_are_recorded_but_not_sentinels() {
        let mut table = ModuleTable::new();
        let real: RawModule = 0x7fff_0000;
        table.record_real(real);
        assert_eq!(table.entry(real), Some(&ModuleEntry::Real(real)));
        assert!(!table.is_sentinel(real));
        // An unknown value is neither recorded nor a sentinel.
        assert!(!table.is_sentinel(0x1234));
        assert_eq!(table.entry(0x1234), None);
    }

    #[test]
    fn record_real_is_idempotent() {
        let mut table = ModuleTable::new();
        let real: RawModule = 0x6000_0000;
        table.record_real(real);
        table.record_real(real);
        assert_eq!(table.len(), 1);
    }

    #[test]
    fn release_sentinel_frees_only_sentinels() {
        let mut table = ModuleTable::new();
        let sentinel = table.mint_sentinel("engine.dll");
        let real: RawModule = 0x5000_0000;
        table.record_real(real);

        // A real or unknown value is not released here (the caller forwards it).
        assert!(!table.release_sentinel(real));
        assert!(!table.release_sentinel(0xABCD));

        // The sentinel is released and its name lookup forgotten.
        assert!(table.release_sentinel(sentinel));
        assert!(!table.is_sentinel(sentinel));
        assert_eq!(table.sentinel_for_name("engine.dll"), None);
        // A double free reports nothing was released.
        assert!(!table.release_sentinel(sentinel));
    }

    #[test]
    fn engine_substitution_register_and_resolve() {
        let mut engines = EngineSubstitution::new();
        assert!(engines.is_empty());
        engines.add_proc("WebCore.dll", "WebCoreActivate", ShimProc(0x1000));

        assert!(engines.is_engine("webcore.dll"));
        assert!(!engines.is_engine("kernel32.dll"));
        assert_eq!(
            engines.proc("WEBCORE.DLL", "WebCoreActivate"),
            Some(ShimProc(0x1000))
        );
        // Proc names are case-sensitive: a different case does not match.
        assert_eq!(engines.proc("WebCore.dll", "webcoreactivate"), None);
        assert_eq!(engines.len(), 1);
    }

    #[test]
    fn register_engine_without_procs_is_substitutable() {
        let mut engines = EngineSubstitution::new();
        engines.register_engine("Empty.dll");
        assert!(engines.is_engine("empty.dll"));
        assert_eq!(engines.proc("Empty.dll", "Anything"), None);
    }

    #[test]
    fn shim_proc_table_seed_and_lookup_is_case_sensitive() {
        let mut procs = ShimProcTable::new();
        assert!(procs.is_empty());
        procs.seed("LoadLibraryW", ShimProc(0x2000));

        assert!(procs.contains("LoadLibraryW"));
        assert_eq!(procs.lookup("LoadLibraryW"), Some(ShimProc(0x2000)));
        // Exact-name match only.
        assert_eq!(procs.lookup("loadlibraryw"), None);
        assert!(!procs.contains("GetProcAddress"));
        assert_eq!(procs.len(), 1);
    }

    /// A test sink that records every event it observes.
    #[derive(Default)]
    struct RecordingSink {
        events: std::sync::Arc<std::sync::Mutex<Vec<LoaderEvent>>>,
    }

    impl ObservationSink for RecordingSink {
        fn observe(&mut self, event: LoaderEvent) {
            self.events.lock().expect("sink poisoned").push(event);
        }
    }

    #[test]
    fn loader_state_defaults_to_off_and_empty() {
        let state = LoaderState::new();
        assert_eq!(state.mode, LoaderMode::Off);
        assert!(state.engines.is_empty());
        assert!(state.procs.is_empty());
        assert!(state.modules.is_empty());
    }

    #[test]
    fn off_mode_observes_nothing() {
        let events = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
        let mut state = LoaderState::new();
        state.set_sink(Box::new(RecordingSink {
            events: events.clone(),
        }));
        // Mode is Off by default: observe is a no-op.
        state.observe(LoaderEvent::LoadLibrary {
            name: "kernel32.dll".to_owned(),
        });
        assert!(events.lock().unwrap().is_empty());
    }

    #[test]
    fn observe_mode_reports_events_to_the_sink() {
        let events = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
        let mut state = LoaderState::new();
        state.set_sink(Box::new(RecordingSink {
            events: events.clone(),
        }));
        state.mode = LoaderMode::Observe;

        state.observe(LoaderEvent::LoadLibrary {
            name: "WebCore.dll".to_owned(),
        });
        state.observe(LoaderEvent::GetProcAddress {
            module: "WebCore.dll".to_owned(),
            proc: ProcQuery::Named("WebCoreActivate".to_owned()),
        });
        state.observe(LoaderEvent::GetProcAddress {
            module: "kernel32.dll".to_owned(),
            proc: ProcQuery::Ordinal(7),
        });

        let recorded = events.lock().unwrap();
        assert_eq!(recorded.len(), 3);
        assert_eq!(
            recorded[0],
            LoaderEvent::LoadLibrary {
                name: "WebCore.dll".to_owned()
            }
        );
        assert_eq!(
            recorded[2],
            LoaderEvent::GetProcAddress {
                module: "kernel32.dll".to_owned(),
                proc: ProcQuery::Ordinal(7),
            }
        );
    }

    #[test]
    fn off_mode_load_forwards_without_recording() {
        let mut state = LoaderState::new();
        let disposition = state.on_load_library("kernel32.dll");
        assert_eq!(disposition, LoadDisposition::Forward { record: false });
        // Off mode interns nothing.
        assert!(state.modules.is_empty());
    }

    #[test]
    fn observe_mode_load_forwards_and_records() {
        let events = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
        let mut state = LoaderState::new();
        state.set_sink(Box::new(RecordingSink {
            events: events.clone(),
        }));
        state.mode = LoaderMode::Observe;

        let disposition = state.on_load_library("kernel32.dll");
        assert_eq!(disposition, LoadDisposition::Forward { record: true });
        // An unregistered engine is never substituted, even when observing.
        assert_eq!(
            *events.lock().unwrap(),
            vec![LoaderEvent::LoadLibrary {
                name: "kernel32.dll".to_owned()
            }]
        );

        // The recorded real module is interned and not classified as a sentinel.
        let real: RawModule = 0x7ffe_0000;
        state.record_loaded(real);
        assert!(!state.modules.is_sentinel(real));
        assert_eq!(state.modules.len(), 1);
    }

    #[test]
    fn substitute_mode_mints_sentinel_for_registered_engine() {
        let mut state = LoaderState::new();
        state.mode = LoaderMode::Substitute;
        state.engines.register_engine("WebCore.dll");

        let disposition = state.on_load_library("webcore.dll");
        let sentinel = match disposition {
            LoadDisposition::Substitute(value) => value,
            other => panic!("expected substitution, got {other:?}"),
        };
        assert!(is_minted_value(sentinel));
        assert!(state.modules.is_sentinel(sentinel));

        // A non-engine load in the same mode still forwards.
        assert_eq!(
            state.on_load_library("kernel32.dll"),
            LoadDisposition::Forward { record: true }
        );
    }

    #[test]
    fn free_releases_sentinel_and_forwards_real() {
        let mut state = LoaderState::new();
        state.mode = LoaderMode::Substitute;
        state.engines.register_engine("WebCore.dll");
        let sentinel = match state.on_load_library("WebCore.dll") {
            LoadDisposition::Substitute(value) => value,
            other => panic!("expected substitution, got {other:?}"),
        };

        // A real module forwards; the sentinel is released.
        let real: RawModule = 0x7ffe_0000;
        state.record_loaded(real);
        assert_eq!(state.on_free_library(real), FreeDisposition::Forward);
        assert_eq!(state.on_free_library(sentinel), FreeDisposition::Released);
        // A second free of the released sentinel forwards (it is gone).
        assert_eq!(state.on_free_library(sentinel), FreeDisposition::Forward);
    }

    #[test]
    fn off_mode_free_forwards_unknown_value() {
        let mut state = LoaderState::new();
        assert_eq!(state.on_free_library(0x1234), FreeDisposition::Forward);
    }

    #[test]
    fn off_mode_get_proc_address_forwards_without_observing() {
        let events = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
        let mut state = LoaderState::new();
        state.set_sink(Box::new(RecordingSink {
            events: events.clone(),
        }));
        let real: RawModule = 0x7ffe_0000;
        let query = ProcQuery::Named("RegOpenKeyExW".to_owned());
        assert_eq!(
            state.on_get_proc_address(real, &query),
            ProcDisposition::Forward
        );
        assert!(events.lock().unwrap().is_empty());
    }

    #[test]
    fn observe_mode_get_proc_address_records_but_forwards() {
        let events = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
        let mut state = LoaderState::new();
        state.set_sink(Box::new(RecordingSink {
            events: events.clone(),
        }));
        state.mode = LoaderMode::Observe;
        // A shimmed name is in the table, but observe mode never redirects.
        state
            .procs
            .seed("RegOpenKeyExW", ShimProc(0x4000_1000));
        let real: RawModule = 0x7ffe_0000;
        let query = ProcQuery::Named("RegOpenKeyExW".to_owned());
        assert_eq!(
            state.on_get_proc_address(real, &query),
            ProcDisposition::Forward
        );
        assert_eq!(
            *events.lock().unwrap(),
            vec![LoaderEvent::GetProcAddress {
                module: format!("{real:#x}"),
                proc: ProcQuery::Named("RegOpenKeyExW".to_owned()),
            }]
        );
    }

    #[test]
    fn substitute_mode_redirects_shimmed_proc_on_real_module() {
        let mut state = LoaderState::new();
        state.mode = LoaderMode::Substitute;
        state.procs.seed("RegOpenKeyExW", ShimProc(0x4000_1000));
        let real: RawModule = 0x7ffe_0000;

        assert_eq!(
            state.on_get_proc_address(real, &ProcQuery::Named("RegOpenKeyExW".to_owned())),
            ProcDisposition::Shim(ShimProc(0x4000_1000))
        );
        // An unshimmed name on a real module forwards.
        assert_eq!(
            state.on_get_proc_address(real, &ProcQuery::Named("CreateFileW".to_owned())),
            ProcDisposition::Forward
        );
    }

    #[test]
    fn substitute_mode_resolves_sentinel_proc_via_engine() {
        let mut state = LoaderState::new();
        state.mode = LoaderMode::Substitute;
        state.engines.register_engine("WebCore.dll");
        state
            .engines
            .add_proc("WebCore.dll", "WebCoreActivate", ShimProc(0x4200_2000));
        let sentinel = match state.on_load_library("WebCore.dll") {
            LoadDisposition::Substitute(value) => value,
            other => panic!("expected substitution, got {other:?}"),
        };

        // A proc the engine supplies resolves to the shim body.
        assert_eq!(
            state.on_get_proc_address(sentinel, &ProcQuery::Named("WebCoreActivate".to_owned())),
            ProcDisposition::Shim(ShimProc(0x4200_2000))
        );
        // A proc the engine does not supply is the null "not found" result, and
        // the sentinel is never forwarded to the OS loader.
        assert_eq!(
            state.on_get_proc_address(sentinel, &ProcQuery::Named("Missing".to_owned())),
            ProcDisposition::Shim(ShimProc(0))
        );
        // An ordinal query against a sentinel is likewise the null result.
        assert_eq!(
            state.on_get_proc_address(sentinel, &ProcQuery::Ordinal(7)),
            ProcDisposition::Shim(ShimProc(0))
        );
    }

    #[test]
    fn off_mode_get_module_handle_forwards_without_observing() {
        let events = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
        let mut state = LoaderState::new();
        state.set_sink(Box::new(RecordingSink {
            events: events.clone(),
        }));
        assert_eq!(
            state.on_get_module_handle("kernel32.dll"),
            ModuleHandleDisposition::Forward
        );
        assert!(events.lock().unwrap().is_empty());
    }

    #[test]
    fn get_module_handle_resolves_minted_sentinel_by_name() {
        let mut state = LoaderState::new();
        state.mode = LoaderMode::Substitute;
        state.engines.register_engine("WebCore.dll");
        let sentinel = match state.on_load_library("WebCore.dll") {
            LoadDisposition::Substitute(value) => value,
            other => panic!("expected substitution, got {other:?}"),
        };

        // Case-insensitive name resolution returns the same sentinel.
        assert_eq!(
            state.on_get_module_handle("webcore.dll"),
            ModuleHandleDisposition::Sentinel(sentinel)
        );
        // An unknown name forwards.
        assert_eq!(
            state.on_get_module_handle("kernel32.dll"),
            ModuleHandleDisposition::Forward
        );
    }

    #[test]
    fn pinned_sentinel_survives_free() {
        let mut state = LoaderState::new();
        state.mode = LoaderMode::Substitute;
        state.engines.register_engine("WebCore.dll");
        let sentinel = match state.on_load_library("WebCore.dll") {
            LoadDisposition::Substitute(value) => value,
            other => panic!("expected substitution, got {other:?}"),
        };

        // Pin via the Ex path, then free: the sentinel reports released (ABI
        // TRUE) but remains resolvable.
        assert_eq!(
            state.on_get_module_handle_ex("WebCore.dll", true),
            ModuleHandleDisposition::Sentinel(sentinel)
        );
        assert!(state.modules.is_pinned(sentinel));
        assert_eq!(state.on_free_library(sentinel), FreeDisposition::Released);
        assert_eq!(
            state.on_get_module_handle("WebCore.dll"),
            ModuleHandleDisposition::Sentinel(sentinel)
        );

        // An unpinned engine is removed on free.
        state.engines.register_engine("Other.dll");
        let other = match state.on_load_library("Other.dll") {
            LoadDisposition::Substitute(value) => value,
            other => panic!("expected substitution, got {other:?}"),
        };
        assert_eq!(state.on_free_library(other), FreeDisposition::Released);
        assert_eq!(
            state.on_get_module_handle("Other.dll"),
            ModuleHandleDisposition::Forward
        );
    }
}
