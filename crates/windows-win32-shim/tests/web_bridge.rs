// Copyright (c) Microsoft Corporation.

//! MW12 response-path bridge integration test: drive the exported activation
//! entry point (`mRegisterModule`) through the process-wide session against an
//! emulated IIS host that vends a **real** `IHttpContext` (carrying a request
//! method/URL and a response status/body), proving the unsafe vtable bridge
//! (SHIM-D18 / SHIM-D2) translates the host's per-request pointers into the safe
//! `RequestHandler` surface, the identity decorator forwards every notification
//! with the pass-through disposition, the host response is **byte-identical** to
//! the un-shimmed path (the bridge never writes back), and the journaling
//! decorator (`WebMode::Observe`) records each exchange without altering it.
//!
//! Unlike the unit tests (which exercise the policy on a local `WebState` and
//! the bridge on an in-crate fake), this drives the real `#[no_mangle]` export
//! against the global `session()` end-to-end, exactly the path the relinked host
//! reaches through the redirected `RegisterModule` import.
//!
//! It is a single test function on purpose: the session web state is a
//! process-wide singleton, so the off-mode phase and the observe-mode phase must
//! run sequentially rather than race in parallel.
//!
//! The `#[repr(C)]` vtables below mirror the shim's private layouts — they are
//! the **host's** independent view of the same modeled IIS ABI (SHIM-D18),
//! exactly as a real host's `httpserv.h` definitions must agree with ours.

#![cfg(windows)]

use core::ffi::c_void;
use core::ptr::{null, null_mut};

use windows_platform_isolation::ObservedEvent;
use windows_sys::Win32::Foundation::S_OK;
use windows_sys::core::{GUID, HRESULT};
use windows_win32_shim::WebMode;
use windows_win32_shim::mwinweb::mRegisterModule;
use windows_win32_shim::session::session;

/// `RQ_NOTIFICATION_CONTINUE` — the pass-through disposition the host expects
/// from every notification (fixed by IIS).
const RQ_NOTIFICATION_CONTINUE: i32 = 0;
/// `RQ_BEGIN_REQUEST` — the begin-request notification flag (fixed by IIS).
const RQ_BEGIN_REQUEST: u32 = 0x0000_0001;
/// `RQ_SEND_RESPONSE` — the send-response notification flag (fixed by IIS).
const RQ_SEND_RESPONSE: u32 = 0x0000_0020;

// --- Host module-registration / factory / module vtables --------------------

/// The host's view of the `IHttpModuleRegistrationInfo` vtable (modeled subset).
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

// --- Host per-request interfaces (IHttpContext / request / response) ---------

/// The host's view of the `IHttpContext` vtable (modeled subset).
#[repr(C)]
struct IHttpContextVtbl {
    get_request: unsafe extern "system" fn(*mut c_void) -> *mut c_void,
    get_response: unsafe extern "system" fn(*mut c_void) -> *mut c_void,
}

/// The host's view of the `IHttpRequest` vtable (modeled subset).
#[repr(C)]
struct IHttpRequestVtbl {
    get_http_method: unsafe extern "system" fn(*mut c_void) -> *const u8,
    get_http_url: unsafe extern "system" fn(*mut c_void) -> *const u16,
}

/// The host's view of the `IHttpResponse` vtable (modeled subset).
#[repr(C)]
struct IHttpResponseVtbl {
    get_status: unsafe extern "system" fn(*mut c_void) -> u16,
}

// --- Emulated host objects --------------------------------------------------

/// An emulated `IHttpModuleRegistrationInfo` that captures the factory and
/// notification flags the shim passes to `SetRequestNotifications`.
#[repr(C)]
struct FakeRegInfo {
    vtable: *const IHttpModuleRegistrationInfoVtbl,
    captured_factory: *mut c_void,
    captured_notifications: u32,
}

/// An emulated `IHttpRequest` carrying a method and URL.
#[repr(C)]
struct FakeRequest {
    vtable: *const IHttpRequestVtbl,
    method: *const u8,
    url: *const u16,
}

/// An emulated `IHttpResponse` carrying the status and body the host would send.
/// The body models the bytes that leave on the wire; the bridge must never
/// touch it (byte-identical pass-through).
#[repr(C)]
struct FakeResponse {
    vtable: *const IHttpResponseVtbl,
    status: u16,
    body: Vec<u8>,
}

/// An emulated `IHttpContext` vending the request and response.
#[repr(C)]
struct FakeContext {
    vtable: *const IHttpContextVtbl,
    request: *mut c_void,
    response: *mut c_void,
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

unsafe extern "system" fn fake_get_request(this: *mut c_void) -> *mut c_void {
    // SAFETY: this is a live FakeContext.
    unsafe { (*this.cast::<FakeContext>()).request }
}

unsafe extern "system" fn fake_get_response(this: *mut c_void) -> *mut c_void {
    // SAFETY: this is a live FakeContext.
    unsafe { (*this.cast::<FakeContext>()).response }
}

unsafe extern "system" fn fake_get_http_method(this: *mut c_void) -> *const u8 {
    // SAFETY: this is a live FakeRequest.
    unsafe { (*this.cast::<FakeRequest>()).method }
}

unsafe extern "system" fn fake_get_http_url(this: *mut c_void) -> *const u16 {
    // SAFETY: this is a live FakeRequest.
    unsafe { (*this.cast::<FakeRequest>()).url }
}

unsafe extern "system" fn fake_get_status(this: *mut c_void) -> u16 {
    // SAFETY: this is a live FakeResponse.
    unsafe { (*this.cast::<FakeResponse>()).status }
}

static FAKE_REG_INFO_VTBL: IHttpModuleRegistrationInfoVtbl = IHttpModuleRegistrationInfoVtbl {
    get_id: fake_get_id,
    get_name: fake_get_name,
    set_request_notifications: fake_set_request_notifications,
    set_global_notifications: fake_set_global,
    set_priority_for_request_notification: fake_set_pri_req,
    set_priority_for_global_notification: fake_set_pri_glob,
};
static FAKE_CONTEXT_VTBL: IHttpContextVtbl = IHttpContextVtbl {
    get_request: fake_get_request,
    get_response: fake_get_response,
};
static FAKE_REQUEST_VTBL: IHttpRequestVtbl = IHttpRequestVtbl {
    get_http_method: fake_get_http_method,
    get_http_url: fake_get_http_url,
};
static FAKE_RESPONSE_VTBL: IHttpResponseVtbl = IHttpResponseVtbl {
    get_status: fake_get_status,
};

/// Register the shim module against a fresh emulated host, vend a module, then
/// drive one request through it with a real `IHttpContext`. Asserts both
/// notifications return the pass-through continue and the host response is
/// byte-identical (status and body unchanged) after the send-response
/// notification — the un-shimmed-path guarantee.
fn register_and_drive_request_with_context(method: &[u8], url: &[u16], status: u16, body: &[u8]) {
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

    // Build the emulated request/response/context the host hands the bridge.
    let mut request = FakeRequest {
        vtable: &FAKE_REQUEST_VTBL,
        method: method.as_ptr(),
        url: url.as_ptr(),
    };
    let mut response = FakeResponse {
        vtable: &FAKE_RESPONSE_VTBL,
        status,
        body: body.to_vec(),
    };
    let mut context = FakeContext {
        vtable: &FAKE_CONTEXT_VTBL,
        request: (&mut request as *mut FakeRequest).cast(),
        response: (&mut response as *mut FakeResponse).cast(),
    };
    let ctx_ptr = (&mut context as *mut FakeContext).cast();

    // Snapshot the response as it would leave on the un-shimmed path.
    let baseline_status = response.status;
    let baseline_body = response.body.clone();

    // SAFETY: module is a live shim CHttpModule.
    let mvtbl = unsafe { *module.cast::<*const CHttpModuleVtbl>() };
    // SAFETY: drive the begin-request notification with the real context.
    let begin = unsafe { ((*mvtbl).on_begin_request)(module, ctx_ptr, null_mut()) };
    assert_eq!(
        begin, RQ_NOTIFICATION_CONTINUE,
        "OnBeginRequest must pass through (continue)"
    );
    // SAFETY: drive the send-response notification with the real context.
    let send = unsafe { ((*mvtbl).on_send_response)(module, ctx_ptr, null_mut()) };
    assert_eq!(
        send, RQ_NOTIFICATION_CONTINUE,
        "OnSendResponse must pass through (continue)"
    );

    // The bridge never wrote back: the host response is byte-identical.
    assert_eq!(
        response.status, baseline_status,
        "the response status must be byte-identical to the un-shimmed path"
    );
    assert_eq!(
        response.body, baseline_body,
        "the response body must be byte-identical to the un-shimmed path"
    );

    // SAFETY: release the module then the factory exactly once each.
    unsafe { ((*mvtbl).dispose)(module) };
    // SAFETY: terminate the factory exactly once.
    unsafe { ((*fvtbl).terminate)(factory) };
}

#[test]
fn request_bridges_through_identity_and_journaling_byte_identically() {
    let method = b"GET\0";
    let url: Vec<u16> = "/index.html\0".encode_utf16().collect();
    let body = b"<html>hello</html>";

    // The session's shared observation log (fed by the journaling decorator).
    let log = session().with_web(|web| web.observation_log());

    // --- Phase A: silent identity (Off) -------------------------------------
    // The default mode is a true pass-through: byte-identical, journals nothing.
    register_and_drive_request_with_context(method, &url, 200, body);
    assert!(
        log.lock().expect("log poisoned").is_empty(),
        "Off mode must journal nothing"
    );

    // --- Phase B: observing identity (Observe) ------------------------------
    // Record mode journals each exchange but still passes through byte-identical.
    session().with_web(|web| {
        web.mode = WebMode::Observe;
    });

    register_and_drive_request_with_context(method, &url, 200, body);

    let events = log.lock().expect("log poisoned").clone();
    assert_eq!(
        events,
        vec![
            ObservedEvent::BeginRequest {
                method: "GET".to_owned(),
                url: "/index.html".to_owned(),
                header_names: Vec::new(),
            },
            ObservedEvent::SendResponse {
                status: 200,
                header_names: Vec::new(),
            },
        ],
        "Observe mode must journal the request and response of the exchange"
    );
}
