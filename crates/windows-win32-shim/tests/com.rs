// Copyright (c) Microsoft Corporation.

//! MW10 COM-activation integration test: drive the exported COM ABI
//! (`mCoCreateInstance` / `mCoCreateInstanceEx` / `mCoGetClassObject` plus the
//! passthrough lifecycle exports) through the process-wide session, proving
//! object substitution, class-factory substitution, observation, real-activation
//! forwarding for an unregistered CLSID, and off-mode transparency end to end.
//!
//! Unlike the unit tests (which exercise the policy on a local `ComState`), this
//! drives the real `#[no_mangle]` exports against the global `session()`, so it
//! covers the ABI marshaling, the session wiring, and the policy together — the
//! same path a client reaches through the redirected COM imports
//! (`/alternatename`, see `windows_win32_shim_aliases.ndjson`).
//!
//! It is a single test function on purpose: the session COM state is a
//! process-wide singleton, so the substitute-mode phase and the off-mode phase
//! must run sequentially rather than race in parallel.

#![cfg(windows)]

use core::ffi::c_void;
use core::ptr::null_mut;
use std::sync::{Arc, Mutex};

use windows_sys::Win32::Foundation::S_OK;
use windows_sys::Win32::System::Com::{CLSCTX_INPROC_SERVER, COINIT_APARTMENTTHREADED};
use windows_sys::core::{GUID, HRESULT};
use windows_win32_shim::mwincom::{
    mCoCreateInstance, mCoGetClassObject, mCoInitializeEx, mCoUninitialize,
};
use windows_win32_shim::session::session;
use windows_win32_shim::{ComEvent, ComMode, Guid, ShimClassFactory};

/// `IID_IClassFactory` — the interface `CoGetClassObject` hands back.
const IID_ICLASSFACTORY: Guid =
    Guid::new(0x0000_0001, 0x0000, 0x0000, [0xc0, 0, 0, 0, 0, 0, 0, 0x46]);
/// The CLSID the test registers a shim factory for.
const TEST_CLSID: Guid = Guid::new(0x1234_5678, 0x9abc, 0xdef0, [1, 2, 3, 4, 5, 6, 7, 8]);
/// The IID the registered factory vends.
const TEST_IID: Guid = Guid::new(0xaaaa_bbbb, 0xcccc, 0xdddd, [8, 7, 6, 5, 4, 3, 2, 1]);
/// A CLSID nothing is registered for (drives the real-activation forward path).
const UNREGISTERED_CLSID: Guid =
    Guid::new(0xdead_beef, 0x0000, 0x0000, [0, 0, 0, 0, 0, 0, 0, 0]);

/// A stub factory that vends exactly [`TEST_IID`].
struct StubFactory;

impl ShimClassFactory for StubFactory {
    fn supported_iids(&self) -> Vec<Guid> {
        vec![TEST_IID]
    }
}

/// An observation sink that records every event for later inspection.
#[derive(Clone, Default)]
struct Recorder {
    events: Arc<Mutex<Vec<ComEvent>>>,
}

impl windows_win32_shim::ComObservationSink for Recorder {
    fn observe(&mut self, event: ComEvent) {
        self.events.lock().expect("recorder poisoned").push(event);
    }
}

/// The raw `IUnknown` vtable prefix of every COM object, used to drive the
/// substitute object the shim hands back.
#[repr(C)]
struct IUnknownVtbl {
    query_interface:
        unsafe extern "system" fn(*mut c_void, *const GUID, *mut *mut c_void) -> HRESULT,
    add_ref: unsafe extern "system" fn(*mut c_void) -> u32,
    release: unsafe extern "system" fn(*mut c_void) -> u32,
}

/// Build a raw `windows-sys` GUID from the policy module's [`Guid`].
fn wguid(g: Guid) -> GUID {
    GUID {
        data1: g.data1,
        data2: g.data2,
        data3: g.data3,
        data4: g.data4,
    }
}

/// Call `IUnknown::QueryInterface` through a COM object's vtable.
///
/// # Safety
///
/// `obj` must be a live COM object whose first field is an `IUnknown` vtable.
unsafe fn query_interface(obj: *mut c_void, iid: Guid) -> (HRESULT, *mut c_void) {
    let raw = wguid(iid);
    let mut out: *mut c_void = null_mut();
    // SAFETY: a COM object begins with its vtable pointer; out is a valid slot.
    let hr = unsafe {
        let vtbl = *obj.cast::<*const IUnknownVtbl>();
        ((*vtbl).query_interface)(obj, &raw, &mut out)
    };
    (hr, out)
}

/// Call `IUnknown::Release` through a COM object's vtable.
///
/// # Safety
///
/// `obj` must be a live COM object whose first field is an `IUnknown` vtable.
unsafe fn release(obj: *mut c_void) -> u32 {
    // SAFETY: a COM object begins with its vtable pointer.
    unsafe {
        let vtbl = *obj.cast::<*const IUnknownVtbl>();
        ((*vtbl).release)(obj)
    }
}

#[test]
fn com_family_end_to_end() {
    // Initialize the apartment through the shim (a pure passthrough). The
    // exported entry points take raw pointers but are safe to call.
    let init = mCoInitializeEx(null_mut(), COINIT_APARTMENTTHREADED as u32);
    assert!(init >= 0, "mCoInitializeEx failed: {init:#x}");

    let recorder = Recorder::default();

    // --- Substitute phase ---------------------------------------------------
    session().with_com(|com| {
        com.mode = ComMode::Substitute;
        com.set_sink(Box::new(recorder.clone()));
        com.factories.register(TEST_CLSID, Box::new(StubFactory));
    });

    // A registered CLSID + supported IID is satisfied by a shim object.
    let clsid = wguid(TEST_CLSID);
    let iid = wguid(TEST_IID);
    let mut obj: *mut c_void = null_mut();
    let hr = mCoCreateInstance(&clsid, null_mut(), CLSCTX_INPROC_SERVER, &iid, &mut obj);
    assert_eq!(hr, S_OK, "registered activation should substitute");
    assert!(!obj.is_null(), "substitute object must be non-null");

    // The shim object answers QueryInterface for its declared IID.
    // SAFETY: obj is the live shim object just minted.
    let (qi_hr, qi_obj) = unsafe { query_interface(obj, TEST_IID) };
    assert_eq!(qi_hr, S_OK, "shim object must support its declared IID");
    assert!(!qi_obj.is_null());
    // Release the QueryInterface reference, then the original.
    // SAFETY: both pointers are live references to the same shim object.
    unsafe {
        release(qi_obj);
        assert_eq!(release(obj), 0, "last release should free the object");
    }

    // An unregistered CLSID forwards to real ole32 activation (which fails for
    // a CLSID that is not registered with COM) — proving the transparent path.
    let bogus = wguid(UNREGISTERED_CLSID);
    let mut forwarded: *mut c_void = null_mut();
    let fwd_hr =
        mCoCreateInstance(&bogus, null_mut(), CLSCTX_INPROC_SERVER, &iid, &mut forwarded);
    assert_ne!(fwd_hr, S_OK, "unregistered CLSID must forward and fail");
    assert!(forwarded.is_null());

    // A registered CLSID yields a shim class factory via mCoGetClassObject.
    let factory_iid = wguid(IID_ICLASSFACTORY);
    let mut factory: *mut c_void = null_mut();
    let cf_hr =
        mCoGetClassObject(&clsid, CLSCTX_INPROC_SERVER, null_mut(), &factory_iid, &mut factory);
    assert_eq!(cf_hr, S_OK, "registered CLSID should vend a shim factory");
    assert!(!factory.is_null());
    // SAFETY: factory is the live shim class object just minted.
    unsafe {
        assert_eq!(release(factory), 0, "last release should free the factory");
    }

    // Observation recorded both activations and the class-object request.
    let events = recorder.events.lock().expect("recorder poisoned").clone();
    assert!(events.contains(&ComEvent::CreateInstance {
        clsid: TEST_CLSID,
        iid: TEST_IID,
        clsctx: CLSCTX_INPROC_SERVER,
    }));
    assert!(events.contains(&ComEvent::CreateInstance {
        clsid: UNREGISTERED_CLSID,
        iid: TEST_IID,
        clsctx: CLSCTX_INPROC_SERVER,
    }));
    assert!(events.contains(&ComEvent::GetClassObject {
        clsid: TEST_CLSID,
        iid: IID_ICLASSFACTORY,
        clsctx: CLSCTX_INPROC_SERVER,
    }));

    // --- Off phase ----------------------------------------------------------
    // Off mode forwards every activation unchanged and observes nothing.
    let before = recorder.events.lock().expect("recorder poisoned").len();
    session().with_com(|com| com.mode = ComMode::Off);
    let mut off_obj: *mut c_void = null_mut();
    let off_hr =
        mCoCreateInstance(&clsid, null_mut(), CLSCTX_INPROC_SERVER, &iid, &mut off_obj);
    // Off mode forwards even a registered CLSID; real activation fails for it.
    assert_ne!(off_hr, S_OK, "off mode must forward, not substitute");
    assert!(off_obj.is_null());
    let after = recorder.events.lock().expect("recorder poisoned").len();
    assert_eq!(before, after, "off mode must observe nothing");

    // Tear the apartment down through the shim (a pure passthrough).
    mCoUninitialize();
}
