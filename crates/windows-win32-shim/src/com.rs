// Copyright (c) Microsoft Corporation.

//! COM activation shim state (SHIM-D17 / platform-isolation D24, D29).
//!
//! The COM activation family (`mCoCreateInstance`, `mCoCreateInstanceEx`,
//! `mCoGetClassObject`, and the passthrough lifecycle exports) is how **object
//! activation** comes under the same redirection seam as static imports and the
//! loader family (SHIM-D16). `CoCreateInstance` is an ole32 import — directly
//! aliasable (D24) — but it does **not** flow through `GetProcAddress`, so the
//! loader shims cannot reach it; COM needs its own exports.
//!
//! This module holds the **safe policy state** the exported COM bodies route
//! through; the raw GUID / HRESULT / vtable glue that vends a substitute object
//! is the unsafe boundary's job ([`crate::mwincom`]). It mirrors the loader's
//! safe state ([`crate::loader`]) exactly:
//!
//! * a [`ClassFactoryRegistry`] — a `CLSID → factory` map the session populates,
//!   the peer of the loader's [`EngineSubstitution`](crate::loader::EngineSubstitution)
//!   registry;
//! * a [`ComObservationSink`] (default [`NullComSink`]) every activation is
//!   reported to, keyed so the D29 volume policy can later suppress a
//!   known-safe `(CLSID, IID)` pair; and
//! * a [`ComMode`] driving off / observe / substitute behavior (the peer of
//!   [`LoaderMode`](crate::loader::LoaderMode)).
//!
//! These are the **shim-local** first cut (SHIM-D17): seeded programmatically
//! now and (later) from `.pilcfg`. No C ABI exports and no `unsafe` live here —
//! the GUID/HRESULT/vtable boundary and the exports are in [`crate::mwincom`].
//!
//! Record-mode per-method journaling of a *real* object is out of first-cut
//! scope (SHIM-D17): the first cut does activation-observation plus substitution
//! of a shim-supplied object only.

use std::collections::HashMap;

/// A COM `GUID` (a `CLSID` or an `IID`), represented with the canonical Win32
/// field layout so it is a stable [`HashMap`] key and equality value.
///
/// The fields mirror the Win32 `GUID` struct; the unsafe boundary
/// ([`crate::mwincom`]) converts to and from the raw `windows_sys` `GUID` at the
/// ABI edge, keeping this module free of any OS dependency (so the policy is
/// unit-testable on any platform).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Guid {
    /// The first 32-bit field (`Data1`).
    pub data1: u32,
    /// The first 16-bit field (`Data2`).
    pub data2: u16,
    /// The second 16-bit field (`Data3`).
    pub data3: u16,
    /// The trailing 8 bytes (`Data4`).
    pub data4: [u8; 8],
}

impl Guid {
    /// Construct a `Guid` from its four Win32 fields.
    #[must_use]
    pub const fn new(data1: u32, data2: u16, data3: u16, data4: [u8; 8]) -> Self {
        Self {
            data1,
            data2,
            data3,
            data4,
        }
    }
}

// --- Class-factory substitution registry ------------------------------------

/// A safe shim class factory: the policy half of a substituted COM class.
///
/// The factory declares which interface IIDs it can vend (besides `IUnknown`);
/// the unsafe boundary mints an actual COM object for a requested IID when the
/// factory supports it. The trait is `Send + Sync` because the registry is held
/// behind the session lock and consulted from the free-threaded C ABI.
pub trait ShimClassFactory: Send + Sync {
    /// The interface IIDs this factory can vend (besides the implicit
    /// `IUnknown`). Enumerable so the boundary can snapshot the set when it
    /// hands a class object back across the COM boundary.
    fn supported_iids(&self) -> Vec<Guid>;

    /// Whether this factory can vend an object for `iid`. The default scans
    /// [`supported_iids`](Self::supported_iids); `IUnknown` is always implicitly
    /// supported and is handled by the boundary, not here.
    fn supports(&self, iid: &Guid) -> bool {
        self.supported_iids().iter().any(|g| g == iid)
    }
}

/// The `CLSID → factory` registry (SHIM-D17): the peer of the loader's
/// [`EngineSubstitution`](crate::loader::EngineSubstitution). A CLSID with a
/// registered factory is substituted in [`ComMode::Substitute`]; an unregistered
/// CLSID always forwards to the real activation (transparency invariant).
#[derive(Default)]
pub struct ClassFactoryRegistry {
    factories: HashMap<Guid, Box<dyn ShimClassFactory>>,
}

impl ClassFactoryRegistry {
    /// An empty registry (no CLSID substituted — the first-cut default).
    #[must_use]
    pub fn new() -> Self {
        Self {
            factories: HashMap::new(),
        }
    }

    /// Register `factory` for `clsid`, replacing any previous registration.
    pub fn register(&mut self, clsid: Guid, factory: Box<dyn ShimClassFactory>) {
        self.factories.insert(clsid, factory);
    }

    /// Whether a factory is registered for `clsid`.
    #[must_use]
    pub fn is_registered(&self, clsid: &Guid) -> bool {
        self.factories.contains_key(clsid)
    }

    /// The IIDs the factory registered for `clsid` can vend, if any.
    #[must_use]
    pub fn supported_iids(&self, clsid: &Guid) -> Option<Vec<Guid>> {
        self.factories.get(clsid).map(|f| f.supported_iids())
    }

    /// Whether the factory registered for `clsid` can vend `iid`.
    #[must_use]
    pub fn supports(&self, clsid: &Guid, iid: &Guid) -> bool {
        self.factories
            .get(clsid)
            .is_some_and(|f| f.supports(iid))
    }

    /// The number of registered CLSIDs.
    #[must_use]
    pub fn len(&self) -> usize {
        self.factories.len()
    }

    /// Whether no CLSID is registered (the substitution-off default).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.factories.is_empty()
    }
}

// --- Observation seam (D29) -------------------------------------------------

/// A single observed COM activation or lifecycle call (SHIM-D17 / D29). The
/// variant is the API and its fields are the target, so the session's volume
/// policy can later suppress a known-safe `(CLSID, IID)` pair. Observation never
/// changes a returned value.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ComEvent {
    /// A `CoCreateInstance` / `CoCreateInstanceEx` activation.
    CreateInstance {
        /// The requested class.
        clsid: Guid,
        /// The requested interface.
        iid: Guid,
        /// The requested activation context (`CLSCTX`).
        clsctx: u32,
    },
    /// A `CoGetClassObject` request for a class object.
    GetClassObject {
        /// The requested class.
        clsid: Guid,
        /// The interface requested of the class object (usually
        /// `IID_IClassFactory`).
        iid: Guid,
        /// The requested activation context (`CLSCTX`).
        clsctx: u32,
    },
    /// A `CoInitialize` / `CoInitializeEx` apartment initialization (always
    /// forwarded; recorded so the apartment lifecycle is observable).
    Initialize,
    /// A `CoUninitialize` apartment teardown (always forwarded).
    Uninitialize,
}

/// The seam the session reports every COM activation to (default
/// [`NullComSink`]). Kept minimal — one method — so the storage target is
/// separable from the COM bodies. Implementations must be `Send` because the
/// session is free-threaded and holds the sink behind its lock. Mirrors the
/// loader's [`ObservationSink`](crate::loader::ObservationSink); the two are
/// intentionally separate event channels.
pub trait ComObservationSink: Send {
    /// Record one observed COM call.
    fn observe(&mut self, event: ComEvent);
}

/// The default COM observation sink: records nothing (the off / first-cut
/// posture).
#[derive(Debug, Default)]
pub struct NullComSink;

impl ComObservationSink for NullComSink {
    fn observe(&mut self, _event: ComEvent) {}
}

// --- COM policy state -------------------------------------------------------

/// How the COM shims behave (SHIM-D17, driven by session mode SHIM-D13 / D25).
/// The default is [`ComMode::Off`] — a pure forward that adds nothing. Mirrors
/// [`LoaderMode`](crate::loader::LoaderMode).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ComMode {
    /// Pure passthrough: forward every activation, observe nothing, substitute
    /// nothing (the D24 identity posture).
    #[default]
    Off,
    /// Forward every activation and report it to the observation sink (D29); the
    /// returned interface pointer is unchanged.
    Observe,
    /// Observe and substitute a shim-supplied object for a registered CLSID;
    /// every unregistered CLSID remains transparent.
    Substitute,
}

/// The disposition of an observed `CoCreateInstance` / `CoCreateInstanceEx`
/// activation, decided by the policy and carried out by the ABI body.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ActivationDisposition {
    /// Vend a shim-supplied object claiming this `iid`; do not call ole32.
    Substitute(Guid),
    /// A factory is registered for the CLSID but cannot vend the requested
    /// interface; fail the activation with `E_NOINTERFACE` (do not call ole32).
    NoInterface,
    /// No factory is registered (or the mode does not substitute); forward to the
    /// real ole32 activation.
    Forward,
}

/// The per-interface disposition of an observed `CoCreateInstanceEx`
/// multi-`QueryInterface` activation, decided by the policy and carried out by
/// the ABI body.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ActivationExDisposition {
    /// No factory is registered (or the mode does not substitute); forward the
    /// whole call to the real `CoCreateInstanceEx`.
    Forward,
    /// Substitute shim-supplied objects: one slot per requested `MULTI_QI`
    /// entry, in order. `Some(iid)` vends a shim object claiming `iid`; `None`
    /// fails that slot with `E_NOINTERFACE`.
    Substitute(Vec<Option<Guid>>),
}

/// The disposition of an observed `CoGetClassObject` request.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ClassObjectDisposition {
    /// Vend a shim class factory able to create objects for these IIDs; do not
    /// call ole32.
    Factory(Vec<Guid>),
    /// No factory is registered (or the mode does not substitute); forward to the
    /// real `CoGetClassObject`.
    Forward,
}

/// The session-held COM policy: the mode, the class-factory registry, and the
/// observation sink. Composed like the loader state (SHIM-D16) and held behind
/// the session lock.
pub struct ComState {
    /// The active behavior mode.
    pub mode: ComMode,
    /// The `CLSID → factory` substitution registry.
    pub factories: ClassFactoryRegistry,
    sink: Box<dyn ComObservationSink>,
}

impl ComState {
    /// A default COM state: [`ComMode::Off`], an empty registry, and a
    /// [`NullComSink`] (the fully-transparent first-cut posture).
    #[must_use]
    pub fn new() -> Self {
        Self {
            mode: ComMode::Off,
            factories: ClassFactoryRegistry::new(),
            sink: Box::new(NullComSink),
        }
    }

    /// Replace the observation sink (the session installs the real sink here).
    pub fn set_sink(&mut self, sink: Box<dyn ComObservationSink>) {
        self.sink = sink;
    }

    /// Report `event` to the sink when the mode observes (Observe / Substitute);
    /// a pure no-op in [`ComMode::Off`].
    pub fn observe(&mut self, event: ComEvent) {
        if self.mode != ComMode::Off {
            self.sink.observe(event);
        }
    }

    /// Decide how a `CoCreateInstance(clsid, iid, clsctx)` should behave
    /// (SHIM-D17).
    ///
    /// Off mode is a pure forward (no observation, no substitution). Otherwise
    /// the activation is reported, then in [`ComMode::Substitute`] a registered
    /// factory that supports `iid` yields [`ActivationDisposition::Substitute`];
    /// a registered factory that does **not** support `iid` yields
    /// [`ActivationDisposition::NoInterface`]; everything else forwards (the
    /// transparency-for-unregistered-classes invariant).
    pub fn on_create_instance(
        &mut self,
        clsid: Guid,
        iid: Guid,
        clsctx: u32,
    ) -> ActivationDisposition {
        if self.mode == ComMode::Off {
            return ActivationDisposition::Forward;
        }
        self.observe(ComEvent::CreateInstance { clsid, iid, clsctx });
        if self.mode == ComMode::Substitute && self.factories.is_registered(&clsid) {
            if self.factories.supports(&clsid, &iid) {
                ActivationDisposition::Substitute(iid)
            } else {
                ActivationDisposition::NoInterface
            }
        } else {
            ActivationDisposition::Forward
        }
    }

    /// Decide how a `CoCreateInstanceEx(clsid, iids, clsctx)` should behave
    /// (SHIM-D17) — the multi-interface analogue of [`Self::on_create_instance`].
    ///
    /// Off mode is a pure forward (no observation). Otherwise every requested
    /// interface is reported, then in [`ComMode::Substitute`] a registered CLSID
    /// yields [`ActivationExDisposition::Substitute`] carrying one slot per
    /// requested `iid` — `Some(iid)` where the factory supports it, `None`
    /// where it does not. An unregistered CLSID (or any non-substitute mode)
    /// forwards the whole call (the transparency-for-unregistered-classes
    /// invariant).
    pub fn on_create_instance_ex(
        &mut self,
        clsid: Guid,
        iids: &[Guid],
        clsctx: u32,
    ) -> ActivationExDisposition {
        if self.mode == ComMode::Off {
            return ActivationExDisposition::Forward;
        }
        for iid in iids {
            self.observe(ComEvent::CreateInstance {
                clsid,
                iid: *iid,
                clsctx,
            });
        }
        if self.mode == ComMode::Substitute && self.factories.is_registered(&clsid) {
            let slots = iids
                .iter()
                .map(|iid| {
                    if self.factories.supports(&clsid, iid) {
                        Some(*iid)
                    } else {
                        None
                    }
                })
                .collect();
            ActivationExDisposition::Substitute(slots)
        } else {
            ActivationExDisposition::Forward
        }
    }

    /// Decide how a `CoGetClassObject(clsid, iid, clsctx)` should behave
    /// (SHIM-D17).
    ///
    /// Off mode is a pure forward. Otherwise the request is reported, then in
    /// [`ComMode::Substitute`] a registered CLSID yields
    /// [`ClassObjectDisposition::Factory`] carrying the IIDs the shim factory can
    /// later create; everything else forwards.
    pub fn on_get_class_object(
        &mut self,
        clsid: Guid,
        iid: Guid,
        clsctx: u32,
    ) -> ClassObjectDisposition {
        if self.mode == ComMode::Off {
            return ClassObjectDisposition::Forward;
        }
        self.observe(ComEvent::GetClassObject { clsid, iid, clsctx });
        if self.mode == ComMode::Substitute
            && let Some(iids) = self.factories.supported_iids(&clsid)
        {
            return ClassObjectDisposition::Factory(iids);
        }
        ClassObjectDisposition::Forward
    }
}

impl Default for ComState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    /// A canonical CLSID used across the tests.
    const TEST_CLSID: Guid = Guid::new(0x1111_2222, 0x3333, 0x4444, [1, 2, 3, 4, 5, 6, 7, 8]);
    /// An IID the stub factory supports.
    const SUPPORTED_IID: Guid = Guid::new(0xaaaa_bbbb, 0xcccc, 0xdddd, [8, 7, 6, 5, 4, 3, 2, 1]);
    /// An IID no factory supports.
    const OTHER_IID: Guid = Guid::new(0xdead_beef, 0, 0, [0, 0, 0, 0, 0, 0, 0, 0]);
    /// A `CLSCTX_INPROC_SERVER`-shaped context value used for observation.
    const INPROC: u32 = 1;

    /// A stub factory that vends exactly [`SUPPORTED_IID`].
    struct StubFactory;

    impl ShimClassFactory for StubFactory {
        fn supported_iids(&self) -> Vec<Guid> {
            vec![SUPPORTED_IID]
        }
    }

    /// A recording sink for asserting observation.
    #[derive(Clone, Default)]
    struct RecordingSink {
        events: Arc<Mutex<Vec<ComEvent>>>,
    }

    impl ComObservationSink for RecordingSink {
        fn observe(&mut self, event: ComEvent) {
            self.events.lock().unwrap().push(event);
        }
    }

    #[test]
    fn registry_records_registration_and_support() {
        let mut reg = ClassFactoryRegistry::new();
        assert!(reg.is_empty());
        reg.register(TEST_CLSID, Box::new(StubFactory));
        assert_eq!(reg.len(), 1);
        assert!(reg.is_registered(&TEST_CLSID));
        assert!(reg.supports(&TEST_CLSID, &SUPPORTED_IID));
        assert!(!reg.supports(&TEST_CLSID, &OTHER_IID));
        assert_eq!(reg.supported_iids(&TEST_CLSID), Some(vec![SUPPORTED_IID]));
        assert_eq!(reg.supported_iids(&OTHER_IID), None);
    }

    #[test]
    fn off_mode_create_instance_forwards_without_observing() {
        let sink = RecordingSink::default();
        let mut com = ComState::new();
        com.set_sink(Box::new(sink.clone()));
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
        // Off mode: a registered CLSID is still forwarded, and nothing observed.
        assert_eq!(
            com.on_create_instance(TEST_CLSID, SUPPORTED_IID, INPROC),
            ActivationDisposition::Forward
        );
        assert!(sink.events.lock().unwrap().is_empty());
    }

    #[test]
    fn observe_mode_create_instance_records_but_forwards() {
        let sink = RecordingSink::default();
        let mut com = ComState::new();
        com.mode = ComMode::Observe;
        com.set_sink(Box::new(sink.clone()));
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
        // Observe mode forwards even a registered CLSID, but records it.
        assert_eq!(
            com.on_create_instance(TEST_CLSID, SUPPORTED_IID, INPROC),
            ActivationDisposition::Forward
        );
        assert_eq!(
            sink.events.lock().unwrap().as_slice(),
            &[ComEvent::CreateInstance {
                clsid: TEST_CLSID,
                iid: SUPPORTED_IID,
                clsctx: INPROC,
            }]
        );
    }

    #[test]
    fn substitute_mode_create_instance_substitutes_supported_iid() {
        let mut com = ComState::new();
        com.mode = ComMode::Substitute;
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
        assert_eq!(
            com.on_create_instance(TEST_CLSID, SUPPORTED_IID, INPROC),
            ActivationDisposition::Substitute(SUPPORTED_IID)
        );
    }

    #[test]
    fn substitute_mode_create_instance_no_interface_for_unsupported_iid() {
        let mut com = ComState::new();
        com.mode = ComMode::Substitute;
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
        assert_eq!(
            com.on_create_instance(TEST_CLSID, OTHER_IID, INPROC),
            ActivationDisposition::NoInterface
        );
    }

    #[test]
    fn substitute_mode_create_instance_forwards_unregistered_class() {
        let mut com = ComState::new();
        com.mode = ComMode::Substitute;
        // No factory registered for TEST_CLSID: transparent forward.
        assert_eq!(
            com.on_create_instance(TEST_CLSID, SUPPORTED_IID, INPROC),
            ActivationDisposition::Forward
        );
    }

    #[test]
    fn substitute_mode_get_class_object_returns_factory_iids() {
        let mut com = ComState::new();
        com.mode = ComMode::Substitute;
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
        assert_eq!(
            com.on_get_class_object(TEST_CLSID, OTHER_IID, INPROC),
            ClassObjectDisposition::Factory(vec![SUPPORTED_IID])
        );
    }

    #[test]
    fn get_class_object_forwards_unregistered_and_off_mode() {
        let mut com = ComState::new();
        com.mode = ComMode::Substitute;
        assert_eq!(
            com.on_get_class_object(TEST_CLSID, OTHER_IID, INPROC),
            ClassObjectDisposition::Forward
        );
        // Off mode forwards even a registered CLSID.
        com.mode = ComMode::Off;
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
        assert_eq!(
            com.on_get_class_object(TEST_CLSID, OTHER_IID, INPROC),
            ClassObjectDisposition::Forward
        );
    }

    #[test]
    fn lifecycle_events_observed_only_when_not_off() {
        let sink = RecordingSink::default();
        let mut com = ComState::new();
        com.set_sink(Box::new(sink.clone()));
        // Off mode: nothing observed.
        com.observe(ComEvent::Initialize);
        com.observe(ComEvent::Uninitialize);
        assert!(sink.events.lock().unwrap().is_empty());
        // Observe mode: both recorded in order.
        com.mode = ComMode::Observe;
        com.observe(ComEvent::Initialize);
        com.observe(ComEvent::Uninitialize);
        assert_eq!(
            sink.events.lock().unwrap().as_slice(),
            &[ComEvent::Initialize, ComEvent::Uninitialize]
        );
    }

    #[test]
    fn off_mode_create_instance_ex_forwards_without_observing() {
        let sink = RecordingSink::default();
        let mut com = ComState::new();
        com.set_sink(Box::new(sink.clone()));
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
        assert_eq!(
            com.on_create_instance_ex(TEST_CLSID, &[SUPPORTED_IID, OTHER_IID], INPROC),
            ActivationExDisposition::Forward
        );
        assert!(sink.events.lock().unwrap().is_empty());
    }

    #[test]
    fn substitute_mode_create_instance_ex_resolves_each_slot() {
        let sink = RecordingSink::default();
        let mut com = ComState::new();
        com.mode = ComMode::Substitute;
        com.set_sink(Box::new(sink.clone()));
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
        // Two slots: the supported IID is vended, the unsupported one is None.
        assert_eq!(
            com.on_create_instance_ex(TEST_CLSID, &[SUPPORTED_IID, OTHER_IID], INPROC),
            ActivationExDisposition::Substitute(vec![Some(SUPPORTED_IID), None])
        );
        // Every requested interface was reported, in order.
        assert_eq!(
            sink.events.lock().unwrap().as_slice(),
            &[
                ComEvent::CreateInstance {
                    clsid: TEST_CLSID,
                    iid: SUPPORTED_IID,
                    clsctx: INPROC,
                },
                ComEvent::CreateInstance {
                    clsid: TEST_CLSID,
                    iid: OTHER_IID,
                    clsctx: INPROC,
                },
            ]
        );
    }

    #[test]
    fn substitute_mode_create_instance_ex_forwards_unregistered_class() {
        let mut com = ComState::new();
        com.mode = ComMode::Substitute;
        // No factory registered: the whole multi-QI call forwards transparently.
        assert_eq!(
            com.on_create_instance_ex(TEST_CLSID, &[SUPPORTED_IID], INPROC),
            ActivationExDisposition::Forward
        );
    }
}
