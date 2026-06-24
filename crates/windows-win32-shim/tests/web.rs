// Copyright (c) Microsoft Corporation.

//! MW11 web-host activation integration test: drive the exported activation
//! entry point (`mRegisterModule`) through the process-wide session against an
//! emulated IIS host, proving the interception hands the host a shim-controlled
//! module factory, the host drives the vended `CHttpModule` notifications, every
//! notification reports the pass-through disposition
//! (`RQ_NOTIFICATION_CONTINUE`), and our code is on the call path (observed in
//! `WebMode::Observe`, silent in `WebMode::Off`).
//!
//! Unlike the unit tests (which exercise the policy on a local `WebState` and
//! the ABI on an in-crate fake), this drives the real `#[no_mangle]` export
//! against the global `session()`, so it covers the ABI marshaling, the session
//! wiring, and the policy together — the same path the relinked host reaches
//! through the redirected `RegisterModule` import (`/alternatename`, see
//! `windows_win32_shim_aliases.ndjson`).
//!
//! It is a single test function on purpose: the session web state is a
//! process-wide singleton, so the observe-mode phase and the off-mode phase must
//! run sequentially rather than race in parallel.
//!
//! The three `#[repr(C)]` vtables below mirror the shim's private layouts — they
//! are the **host's** independent view of the same modeled IIS ABI (SHIM-D18),
//! exactly as a real host's `httpserv.h` definitions must agree with ours.

#![cfg(windows)]

use core::ffi::c_void;
use core::ptr::{null, null_mut};
use std::sync::{Arc, Mutex};

use windows_sys::Win32::Foundation::S_OK;
use windows_sys::core::{GUID, HRESULT};
use windows_win32_shim::mwinweb::{SHIM_WEB_PROBE_TAG, mRegisterModule, mShimWebProbe};
use windows_win32_shim::session::session;
use windows_win32_shim::{WebEvent, WebMode, WebObservationSink};

/// `RQ_NOTIFICATION_CONTINUE` — the pass-through disposition the host expects
/// from every notification (fixed by IIS).
const RQ_NOTIFICATION_CONTINUE: i32 = 0;
/// `RQ_BEGIN_REQUEST` — the begin-request notification flag (fixed by IIS).
const RQ_BEGIN_REQUEST: u32 = 0x0000_0001;
/// `RQ_SEND_RESPONSE` — the send-response notification flag (fixed by IIS).
const RQ_SEND_RESPONSE: u32 = 0x0000_0020;

/// The host's view of the `IHttpModuleRegistrationInfo` vtable (mirrors the
/// shim's modeled subset).
#[repr(C)]
struct IHttpModuleRegistrationInfoVtbl {
    get_id: unsafe extern "system" fn(*mut c_void, *mut GUID) -> HRESULT,
    get_name: unsafe extern "system" fn(*mut c_void) -> *const u16,
    set_request_notifications:
        unsafe extern "system" fn(*mut c_void, *mut c_void, u32, u32) -> HRESULT,
    set_global_notifications: unsafe extern "system" fn(*mut c_void, *mut c_void, u32) -> HRESULT,
    set_priority_for_request_notification:
        unsafe extern "system" fn(*mut c_void, u32, *const u16) -> HRESULT,
    set_priority_for_global_notification:
        unsafe extern "system" fn(*mut c_void, u32, *const u16) -> HRESULT,
}

/// The host's view of the `IHttpModuleFactory` vtable.
#[repr(C)]
struct IHttpModuleFactoryVtbl {
    get_http_module:
        unsafe extern "system" fn(*mut c_void, *mut *mut c_void, *mut c_void) -> HRESULT,
    terminate: unsafe extern "system" fn(*mut c_void),
}

/// The host's view of the `CHttpModule` vtable.
#[repr(C)]
struct CHttpModuleVtbl {
    on_begin_request: unsafe extern "system" fn(*mut c_void, *mut c_void, *mut c_void) -> i32,
    on_send_response: unsafe extern "system" fn(*mut c_void, *mut c_void, *mut c_void) -> i32,
    dispose: unsafe extern "system" fn(*mut c_void),
}

/// An emulated `IHttpModuleRegistrationInfo` that captures the factory and
/// notification flags the shim passes to `SetRequestNotifications`.
#[repr(C)]
struct FakeRegInfo {
    vtable: *const IHttpModuleRegistrationInfoVtbl,
    captured_factory: *mut c_void,
    captured_notifications: u32,
}

unsafe extern "system" fn fake_get_id(_this: *mut c_void, _id: *mut GUID) -> HRESULT {
    S_OK
}

unsafe extern "system" fn fake_get_name(_this: *mut c_void) -> *const u16 {
    null()
}

unsafe extern "system" fn fake_set_request_notifications(
    this: *mut c_void,
    factory: *mut c_void,
    notifications: u32,
    _post: u32,
) -> HRESULT {
    let info = this.cast::<FakeRegInfo>();
    // SAFETY: this is the live FakeRegInfo the test passed to mRegisterModule.
    unsafe {
        (*info).captured_factory = factory;
        (*info).captured_notifications = notifications;
    }
    S_OK
}

unsafe extern "system" fn fake_set_global(
    _this: *mut c_void,
    _factory: *mut c_void,
    _notifications: u32,
) -> HRESULT {
    S_OK
}

unsafe extern "system" fn fake_set_pri_req(
    _this: *mut c_void,
    _notifications: u32,
    _priority: *const u16,
) -> HRESULT {
    S_OK
}

unsafe extern "system" fn fake_set_pri_glob(
    _this: *mut c_void,
    _notifications: u32,
    _priority: *const u16,
) -> HRESULT {
    S_OK
}

static FAKE_REG_INFO_VTBL: IHttpModuleRegistrationInfoVtbl = IHttpModuleRegistrationInfoVtbl {
    get_id: fake_get_id,
    get_name: fake_get_name,
    set_request_notifications: fake_set_request_notifications,
    set_global_notifications: fake_set_global,
    set_priority_for_request_notification: fake_set_pri_req,
    set_priority_for_global_notification: fake_set_pri_glob,
};

/// An observation sink that records every web-host notification.
#[derive(Clone, Default)]
struct Recorder {
    events: Arc<Mutex<Vec<WebEvent>>>,
}

impl WebObservationSink for Recorder {
    fn observe(&mut self, event: WebEvent) {
        self.events.lock().expect("recorder poisoned").push(event);
    }
}

/// Register the shim module against a fresh emulated host, then drive the vended
/// factory and module through one request: begin-request and send-response. Each
/// notification's disposition is asserted to be the pass-through continue.
/// Returns nothing; panics on any deviation.
fn register_and_drive_one_request() {
    let mut info = FakeRegInfo {
        vtable: &FAKE_REG_INFO_VTBL,
        captured_factory: null_mut(),
        captured_notifications: 0,
    };
    let hr = mRegisterModule(0, (&mut info as *mut FakeRegInfo).cast(), null_mut());
    assert_eq!(hr, S_OK, "RegisterModule must succeed");
    assert!(
        !info.captured_factory.is_null(),
        "the host must receive a shim factory"
    );
    assert_eq!(
        info.captured_notifications,
        RQ_BEGIN_REQUEST | RQ_SEND_RESPONSE,
        "the factory must be registered for begin-request and send-response"
    );

    let factory = info.captured_factory;
    // SAFETY: factory is a live shim module factory minted by mRegisterModule.
    let fvtbl = unsafe { *factory.cast::<*const IHttpModuleFactoryVtbl>() };
    let mut module: *mut c_void = null_mut();
    // SAFETY: drive the host-facing factory to vend a module.
    let hr = unsafe { ((*fvtbl).get_http_module)(factory, &mut module, null_mut()) };
    assert_eq!(hr, S_OK, "GetHttpModule must succeed");
    assert!(!module.is_null(), "the factory must vend a module");

    // SAFETY: module is a live shim CHttpModule.
    let mvtbl = unsafe { *module.cast::<*const CHttpModuleVtbl>() };
    // SAFETY: drive the begin-request notification.
    let begin = unsafe { ((*mvtbl).on_begin_request)(module, null_mut(), null_mut()) };
    assert_eq!(
        begin, RQ_NOTIFICATION_CONTINUE,
        "OnBeginRequest must pass through (continue)"
    );
    // SAFETY: drive the send-response notification.
    let send = unsafe { ((*mvtbl).on_send_response)(module, null_mut(), null_mut()) };
    assert_eq!(
        send, RQ_NOTIFICATION_CONTINUE,
        "OnSendResponse must pass through (continue)"
    );

    // SAFETY: release the module then the factory exactly once each.
    unsafe { ((*mvtbl).dispose)(module) };
    // SAFETY: terminate the factory exactly once.
    unsafe { ((*fvtbl).terminate)(factory) };
}

#[test]
fn web_host_seam_pass_through_and_observation() {
    // The shim is resident: its probe export resolves to the residency tag.
    assert_eq!(mShimWebProbe(), SHIM_WEB_PROBE_TAG);

    let recorder = Recorder::default();

    // --- Phase A: observing identity ----------------------------------------
    // Install the recorder and switch the web policy to Observe.
    session().with_web(|web| {
        web.mode = WebMode::Observe;
        web.set_sink(Box::new(recorder.clone()));
    });

    register_and_drive_one_request();

    // Our code was on the call path: every seam point was journaled in order.
    let observed = recorder.events.lock().expect("recorder poisoned").clone();
    assert_eq!(
        observed,
        vec![
            WebEvent::RegisterModule,
            WebEvent::GetHttpModule,
            WebEvent::BeginRequest,
            WebEvent::SendResponse,
        ],
        "Observe mode must journal every seam point in order"
    );

    // --- Phase B: silent identity -------------------------------------------
    // Switch to Off; the pass-through must be unchanged but nothing is recorded.
    let baseline = recorder.events.lock().expect("recorder poisoned").len();
    session().with_web(|web| {
        web.mode = WebMode::Off;
    });

    register_and_drive_one_request();

    let after = recorder.events.lock().expect("recorder poisoned").len();
    assert_eq!(
        after, baseline,
        "Off mode must record no observations while still passing through"
    );
}
