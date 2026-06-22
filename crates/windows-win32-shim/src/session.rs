// Copyright (c) Microsoft Corporation.

//! The process-wide session (SHIM-D8).
//!
//! A single [`ShimSession`] holds the isolation stack the C ABI routes through.
//! It is created lazily on first use and lives for the rest of the process. The
//! registry surface defaults to **live passthrough** over the real OS registry
//! ([`LiveRegistry`]); `.pilcfg`-driven layering (buffered / persisted-state /
//! redirecting) arrives in MW4. Configuration at this stage is programmatic only
//! ([`ShimSession::new`]).
//!
//! Hive roots are vended through the held isolation [`Session`], so a path built
//! from this session resolves against the configured registry stack. Predefined
//! `HKEY`s resolve to a [`WellKnownRoot`] via
//! [`predefined_root`](crate::handle_table::predefined_root) and from there to a
//! root path here.

use std::sync::{Mutex, OnceLock};

use windows_platform_isolation::{
    KeyPath, LiveRegistry, Registry, Session, WellKnownRoot,
};

use crate::handle_table::HandleTable;

/// The process-wide isolation session and its handle table.
///
/// The registry facade is held behind a [`Mutex`] because
/// [`Surface::invoke`](windows_platform_isolation::Surface) takes `&mut self`;
/// the C ABI is free-threaded, so every registry operation borrows it under the
/// lock via [`ShimSession::with_registry`].
pub struct ShimSession {
    isolation: Session,
    registry: Mutex<Registry<LiveRegistry>>,
    handles: HandleTable,
}

impl ShimSession {
    /// Build a session with the default registry backing: live passthrough over
    /// the real OS registry (SHIM-D8).
    #[must_use]
    pub fn new() -> Self {
        Self {
            isolation: Session::new(),
            registry: Mutex::new(Registry::new(LiveRegistry::new())),
            handles: HandleTable::new(),
        }
    }

    /// Borrow the registry facade under the session lock.
    pub fn with_registry<R>(&self, f: impl FnOnce(&mut Registry<LiveRegistry>) -> R) -> R {
        let mut guard = self.registry.lock().expect("session registry poisoned");
        f(&mut guard)
    }

    /// The session's handle table.
    #[must_use]
    pub fn handles(&self) -> &HandleTable {
        &self.handles
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
