// Copyright (c) Microsoft Corporation.

//! The IIS native-module ABI boundary for `wordy` (Windows only).
//!
//! This module is `wordy`'s analogue of windows-win32-shim's `mwinweb`: a
//! hand-rolled, `#[repr(C)]` view of the slice of the IIS native-module ABI that
//! `wordy` actually uses. The IIS `httpserv.h` COM-like interfaces are not part
//! of `windows-sys`, so the vtables are modeled here as the minimal subset
//! `wordy` exercises; their layout is pinned precisely when a genuine host is
//! bound (MW13-5). `wordy` declares its **own** copy of these vtables rather than
//! reusing `mwinweb` so that it remains a peer third-party application with no
//! dependency on — or knowledge of — the shim (SHIM-D19).
//!
//! Flow: IIS loads `wordy.dll` and calls its exported [`RegisterModule`], which
//! installs a module factory for the begin-request and send-response
//! notifications. For each request the factory vends a [`WordyHttpModule`] whose
//! `OnBeginRequest` decodes the method + URL, asks the safe
//! [`crate::routes`] dispatcher for an outcome, and realizes it against the host
//! response.
//!
//! All `unsafe` is confined to this boundary; every block carries a `SAFETY`
//! note. The crate root denies `unsafe_code` and opts this module in explicitly.

use core::ffi::c_void;

use windows_sys::Win32::Foundation::{E_POINTER, S_OK};
use windows_sys::core::{GUID, HRESULT};

use crate::routes::{self, Outcome};

// --- Notification status codes and flags (IIS `httpserv.h`) ----------------
//
// Changing any of these values is a breaking ABI change; they mirror the host's
// `REQUEST_NOTIFICATION_STATUS` enum and `RQ_*` notification flags.

/// `RQ_NOTIFICATION_CONTINUE`: let the host proceed to the next module.
const RQ_NOTIFICATION_CONTINUE: i32 = 0;
/// `RQ_NOTIFICATION_FINISH_REQUEST`: stop the pipeline; the request is complete.
const RQ_NOTIFICATION_FINISH_REQUEST: i32 = 2;
/// `RQ_BEGIN_REQUEST`: the begin-request notification.
const RQ_BEGIN_REQUEST: u32 = 0x0000_0001;
/// `RQ_SEND_RESPONSE`: the send-response notification.
const RQ_SEND_RESPONSE: u32 = 0x0000_0020;

// --- Modeled vtables -------------------------------------------------------

/// Modeled subset of `IHttpModuleRegistrationInfo`'s vtable.
#[repr(C)]
struct IHttpModuleRegistrationInfoVtbl {
    get_id: unsafe extern "system" fn(*mut c_void, *mut GUID) -> HRESULT,
    get_name: unsafe extern "system" fn(*mut c_void) -> *const u16,
    set_request_notifications:
        unsafe extern "system" fn(*mut c_void, *mut c_void, u32, u32) -> HRESULT,
    set_global_notifications:
        unsafe extern "system" fn(*mut c_void, *mut c_void, u32) -> HRESULT,
    set_priority_for_request_notification:
        unsafe extern "system" fn(*mut c_void, u32, *const u16) -> HRESULT,
    set_priority_for_global_notification:
        unsafe extern "system" fn(*mut c_void, u32, *const u16) -> HRESULT,
}

/// Modeled subset of `IHttpModuleFactory`'s vtable.
#[repr(C)]
struct IHttpModuleFactoryVtbl {
    get_http_module:
        unsafe extern "system" fn(*mut c_void, *mut *mut c_void, *mut c_void) -> HRESULT,
    terminate: unsafe extern "system" fn(*mut c_void),
}

/// Modeled subset of `CHttpModule`'s vtable.
#[repr(C)]
struct CHttpModuleVtbl {
    on_begin_request: unsafe extern "system" fn(*mut c_void, *mut c_void, *mut c_void) -> i32,
    on_send_response: unsafe extern "system" fn(*mut c_void, *mut c_void, *mut c_void) -> i32,
    dispose: unsafe extern "system" fn(*mut c_void),
}

/// Modeled subset of `IHttpContext`'s vtable.
#[repr(C)]
struct IHttpContextVtbl {
    get_request: unsafe extern "system" fn(*mut c_void) -> *mut c_void,
    get_response: unsafe extern "system" fn(*mut c_void) -> *mut c_void,
}

/// Modeled subset of `IHttpRequest`'s vtable.
#[repr(C)]
struct IHttpRequestVtbl {
    /// `GetHttpMethod` returns an ASCII `PCSTR`.
    get_http_method: unsafe extern "system" fn(*mut c_void) -> *const u8,
    /// `GetHttpUrl` returns a UTF-16 `PCWSTR`.
    get_http_url: unsafe extern "system" fn(*mut c_void) -> *const u16,
}

/// Modeled subset of `IHttpResponse`'s vtable.
#[repr(C)]
struct IHttpResponseVtbl {
    /// `SetStatus(USHORT, PCSTR reason)` — the status and reason-phrase write.
    /// The genuine method takes additional optional parameters; the modeled
    /// subset carries only the two `wordy` sets.
    set_status: unsafe extern "system" fn(*mut c_void, u16, *const u8) -> HRESULT,
}

// --- `wordy`'s module objects ----------------------------------------------

/// `wordy`'s module factory. Its first field is the vtable pointer so a
/// `*mut WordyModuleFactory` is ABI-identical to the host's `IHttpModuleFactory*`.
#[repr(C)]
struct WordyModuleFactory {
    vtable: *const IHttpModuleFactoryVtbl,
}

/// A `wordy` per-request module. Stateless for the health surface; later
/// milestones attach the dictionary stores here.
#[repr(C)]
struct WordyHttpModule {
    vtable: *const CHttpModuleVtbl,
}

/// The single shared factory vtable every [`WordyModuleFactory`] points at.
static WORDY_MODULE_FACTORY_VTBL: IHttpModuleFactoryVtbl = IHttpModuleFactoryVtbl {
    get_http_module: factory_get_http_module,
    terminate: factory_terminate,
};

/// The single shared module vtable every [`WordyHttpModule`] points at.
static WORDY_HTTP_MODULE_VTBL: CHttpModuleVtbl = CHttpModuleVtbl {
    on_begin_request: module_on_begin_request,
    on_send_response: module_on_send_response,
    dispose: module_dispose,
};

/// Mint a `wordy` module factory, returning a heap pointer the host releases via
/// `Terminate`.
fn mint_module_factory() -> *mut c_void {
    let factory = Box::new(WordyModuleFactory {
        vtable: &WORDY_MODULE_FACTORY_VTBL,
    });
    Box::into_raw(factory).cast::<c_void>()
}

/// Mint a `wordy` `CHttpModule`, returning a heap pointer the host releases via
/// `Dispose`.
fn mint_http_module() -> *mut c_void {
    let module = Box::new(WordyHttpModule {
        vtable: &WORDY_HTTP_MODULE_VTBL,
    });
    Box::into_raw(module).cast::<c_void>()
}

// --- Decode helpers --------------------------------------------------------

/// Convert a possibly-null `PCSTR` to an owned `String` (lossy on invalid UTF-8).
fn pcstr_to_string(p: *const u8) -> String {
    if p.is_null() {
        return String::new();
    }
    let mut len = 0usize;
    // SAFETY: p is non-null; walk to the NUL terminator the host guarantees.
    while unsafe { *p.add(len) } != 0 {
        len += 1;
    }
    // SAFETY: p points at `len` initialized bytes followed by a NUL.
    let bytes = unsafe { core::slice::from_raw_parts(p, len) };
    String::from_utf8_lossy(bytes).into_owned()
}

/// Convert a possibly-null `PCWSTR` to an owned `String` (lossy on unpaired
/// surrogates).
fn pcwstr_to_string(p: *const u16) -> String {
    if p.is_null() {
        return String::new();
    }
    let mut len = 0usize;
    // SAFETY: p is non-null; walk to the NUL terminator the host guarantees.
    while unsafe { *p.add(len) } != 0 {
        len += 1;
    }
    // SAFETY: p points at `len` initialized units followed by a NUL.
    let units = unsafe { core::slice::from_raw_parts(p, len) };
    String::from_utf16_lossy(units)
}

/// Decode the request method and URL from the host context.
///
/// Tolerates a null context or any null interface pointer by yielding empty
/// strings (the dispatcher then declines the request).
///
/// # Safety
/// `context`, when non-null, must point at a live `IHttpContext` whose modeled
/// request vtable is valid for the duration of the call.
unsafe fn decode_request(context: *mut c_void) -> (String, String) {
    if context.is_null() {
        return (String::new(), String::new());
    }
    // SAFETY: context is a live IHttpContext; read its vtable pointer.
    let ctx_vtbl = unsafe { *context.cast::<*const IHttpContextVtbl>() };
    // SAFETY: ctx_vtbl is the host context vtable; fetch the request interface.
    let request = unsafe { ((*ctx_vtbl).get_request)(context) };
    if request.is_null() {
        return (String::new(), String::new());
    }
    // SAFETY: request is a live IHttpRequest; read its vtable pointer.
    let req_vtbl = unsafe { *request.cast::<*const IHttpRequestVtbl>() };
    // SAFETY: req_vtbl is the host request vtable; read method and URL.
    let method = unsafe { pcstr_to_string(((*req_vtbl).get_http_method)(request)) };
    // SAFETY: as above.
    let url = unsafe { pcwstr_to_string(((*req_vtbl).get_http_url)(request)) };
    (method, url)
}

/// Write a status + reason to the host response.
///
/// Tolerates a null context or response by doing nothing.
///
/// # Safety
/// `context`, when non-null, must point at a live `IHttpContext` whose modeled
/// response vtable is valid for the duration of the call.
unsafe fn write_status(context: *mut c_void, status: u16, reason: &str) {
    if context.is_null() {
        return;
    }
    // SAFETY: context is a live IHttpContext; read its vtable pointer.
    let ctx_vtbl = unsafe { *context.cast::<*const IHttpContextVtbl>() };
    // SAFETY: ctx_vtbl is the host context vtable; fetch the response interface.
    let response = unsafe { ((*ctx_vtbl).get_response)(context) };
    if response.is_null() {
        return;
    }
    // SAFETY: response is a live IHttpResponse; read its vtable pointer.
    let resp_vtbl = unsafe { *response.cast::<*const IHttpResponseVtbl>() };
    // The host expects a NUL-terminated reason phrase; build one that outlives
    // the call.
    let mut reason_c: Vec<u8> = reason.as_bytes().to_vec();
    reason_c.push(0);
    // SAFETY: resp_vtbl is the host response vtable; reason_c is a valid
    // NUL-terminated C string alive for the duration of the call.
    unsafe { ((*resp_vtbl).set_status)(response, status, reason_c.as_ptr()) };
}

// --- Module / factory entry points -----------------------------------------

/// `IHttpModuleFactory::GetHttpModule`: vend a `wordy` `CHttpModule`.
unsafe extern "system" fn factory_get_http_module(
    _this: *mut c_void,
    pp_module: *mut *mut c_void,
    _allocator: *mut c_void,
) -> HRESULT {
    if pp_module.is_null() {
        return E_POINTER;
    }
    let module = mint_http_module();
    // SAFETY: pp_module is non-null (checked above); write the out-param.
    unsafe { *pp_module = module };
    S_OK
}

/// `IHttpModuleFactory::Terminate`: reclaim the factory allocation.
unsafe extern "system" fn factory_terminate(this: *mut c_void) {
    if this.is_null() {
        return;
    }
    // SAFETY: this is a live WordyModuleFactory minted by mint_module_factory;
    // the host calls Terminate exactly once.
    drop(unsafe { Box::from_raw(this.cast::<WordyModuleFactory>()) });
}

/// `CHttpModule::OnBeginRequest`: decode the request, dispatch, and realize the
/// outcome against the host response.
unsafe extern "system" fn module_on_begin_request(
    _this: *mut c_void,
    context: *mut c_void,
    _provider: *mut c_void,
) -> i32 {
    // SAFETY: context is the host request context (tolerates null).
    let (method, url) = unsafe { decode_request(context) };
    match routes::dispatch(&method, &url) {
        Outcome::Respond { status, reason } => {
            // SAFETY: context is the host request context (tolerates null).
            unsafe { write_status(context, status, reason) };
            RQ_NOTIFICATION_FINISH_REQUEST
        }
        Outcome::Continue => RQ_NOTIFICATION_CONTINUE,
    }
}

/// `CHttpModule::OnSendResponse`: `wordy` does not post-process responses on the
/// health surface; pass through.
unsafe extern "system" fn module_on_send_response(
    _this: *mut c_void,
    _context: *mut c_void,
    _provider: *mut c_void,
) -> i32 {
    RQ_NOTIFICATION_CONTINUE
}

/// `CHttpModule::Dispose`: reclaim the module allocation.
unsafe extern "system" fn module_dispose(this: *mut c_void) {
    if this.is_null() {
        return;
    }
    // SAFETY: this is a live WordyHttpModule minted by mint_http_module; the host
    // calls Dispose exactly once.
    drop(unsafe { Box::from_raw(this.cast::<WordyHttpModule>()) });
}

/// `RegisterModule`: the IIS native-module entry point IIS calls when it loads
/// `wordy.dll`. It mints a `wordy` module factory and registers it for the
/// begin-request and send-response notifications. On failure the factory is
/// reclaimed and the host's HRESULT propagated.
#[unsafe(no_mangle)]
pub extern "system" fn RegisterModule(
    _dw_server_version: u32,
    p_module_info: *mut c_void,
    _p_global_info: *mut c_void,
) -> HRESULT {
    if p_module_info.is_null() {
        return E_POINTER;
    }
    let factory = mint_module_factory();
    // SAFETY: p_module_info points at a live IHttpModuleRegistrationInfo whose
    // first field is its vtable pointer (the host owns it for the call).
    let vtbl = unsafe { *p_module_info.cast::<*const IHttpModuleRegistrationInfoVtbl>() };
    // SAFETY: vtbl is the host-provided registration-info vtable; install our
    // factory for the two notifications.
    let hr = unsafe {
        ((*vtbl).set_request_notifications)(
            p_module_info,
            factory,
            RQ_BEGIN_REQUEST | RQ_SEND_RESPONSE,
            0,
        )
    };
    if hr < 0 {
        // Registration refused; reclaim our factory and propagate the failure.
        // SAFETY: factory is the Box we just minted and never handed off.
        drop(unsafe { Box::from_raw(factory.cast::<WordyModuleFactory>()) });
        return hr;
    }
    hr
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr::{null, null_mut};

    /// An emulated `IHttpModuleRegistrationInfo` capturing the
    /// `SetRequestNotifications` arguments.
    #[repr(C)]
    struct FakeRegInfo {
        vtable: *const IHttpModuleRegistrationInfoVtbl,
        captured_factory: *mut c_void,
        captured_notifications: u32,
        captured_post: u32,
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
        post: u32,
    ) -> HRESULT {
        let info = this.cast::<FakeRegInfo>();
        // SAFETY: this is the live FakeRegInfo the test passed to RegisterModule.
        unsafe {
            (*info).captured_factory = factory;
            (*info).captured_notifications = notifications;
            (*info).captured_post = post;
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

    /// An emulated `IHttpRequest` carrying a method and URL.
    #[repr(C)]
    struct FakeRequest {
        vtable: *const IHttpRequestVtbl,
        method: *const u8,
        url: *const u16,
    }

    /// An emulated `IHttpResponse` capturing the last `SetStatus` call.
    #[repr(C)]
    struct FakeResponse {
        vtable: *const IHttpResponseVtbl,
        captured_status: u16,
        captured_reason: *const u8,
    }

    /// An emulated `IHttpContext` vending a request and response.
    #[repr(C)]
    struct FakeContext {
        vtable: *const IHttpContextVtbl,
        request: *mut c_void,
        response: *mut c_void,
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
    unsafe extern "system" fn fake_set_status(
        this: *mut c_void,
        status: u16,
        reason: *const u8,
    ) -> HRESULT {
        let resp = this.cast::<FakeResponse>();
        // SAFETY: this is a live FakeResponse the test owns.
        unsafe {
            (*resp).captured_status = status;
            (*resp).captured_reason = reason;
        }
        S_OK
    }

    static FAKE_CONTEXT_VTBL: IHttpContextVtbl = IHttpContextVtbl {
        get_request: fake_get_request,
        get_response: fake_get_response,
    };
    static FAKE_REQUEST_VTBL: IHttpRequestVtbl = IHttpRequestVtbl {
        get_http_method: fake_get_http_method,
        get_http_url: fake_get_http_url,
    };
    static FAKE_RESPONSE_VTBL: IHttpResponseVtbl = IHttpResponseVtbl {
        set_status: fake_set_status,
    };

    /// Build an emulated host context for the given method + URL, returning the
    /// owned request/response/context the caller must keep alive.
    fn make_context(
        method: &'static [u8],
        url: &[u16],
    ) -> (Box<FakeRequest>, Box<FakeResponse>, Box<FakeContext>) {
        let mut request = Box::new(FakeRequest {
            vtable: &FAKE_REQUEST_VTBL,
            method: method.as_ptr(),
            url: url.as_ptr(),
        });
        let mut response = Box::new(FakeResponse {
            vtable: &FAKE_RESPONSE_VTBL,
            captured_status: 0,
            captured_reason: null(),
        });
        let context = Box::new(FakeContext {
            vtable: &FAKE_CONTEXT_VTBL,
            request: (request.as_mut() as *mut FakeRequest).cast(),
            response: (response.as_mut() as *mut FakeResponse).cast(),
        });
        (request, response, context)
    }

    #[test]
    fn register_module_installs_factory_for_begin_and_send_notifications() {
        let mut info = FakeRegInfo {
            vtable: &FAKE_REG_INFO_VTBL,
            captured_factory: null_mut(),
            captured_notifications: 0,
            captured_post: 0,
        };
        let hr = RegisterModule(0, (&mut info as *mut FakeRegInfo).cast(), null_mut());
        assert_eq!(hr, S_OK);
        assert!(!info.captured_factory.is_null());
        assert_eq!(
            info.captured_notifications,
            RQ_BEGIN_REQUEST | RQ_SEND_RESPONSE
        );
        assert_eq!(info.captured_post, 0);

        // Release the captured factory exactly once.
        let factory = info.captured_factory;
        // SAFETY: factory is a live WordyModuleFactory minted by RegisterModule.
        let fvtbl = unsafe { *factory.cast::<*const IHttpModuleFactoryVtbl>() };
        // SAFETY: terminate the factory exactly once.
        unsafe { ((*fvtbl).terminate)(factory) };
    }

    #[test]
    fn register_module_rejects_a_null_registration_info() {
        assert_eq!(RegisterModule(0, null_mut(), null_mut()), E_POINTER);
    }

    #[test]
    fn health_request_finishes_with_status_200() {
        // Drive a full RegisterModule → GetHttpModule → OnBeginRequest cycle for
        // a GET /healthz request and assert the module finishes with status 200.
        let module = mint_http_module();
        // SAFETY: module is a live WordyHttpModule.
        let mvtbl = unsafe { *module.cast::<*const CHttpModuleVtbl>() };

        let url: Vec<u16> = "/healthz\0".encode_utf16().collect();
        let (_req, response, context) = make_context(b"GET\0", &url);
        let ctx_ptr = (context.as_ref() as *const FakeContext as *mut FakeContext).cast();

        // SAFETY: ctx_ptr is a live emulated context exposing the modeled vtables.
        let status = unsafe { ((*mvtbl).on_begin_request)(module, ctx_ptr, null_mut()) };
        assert_eq!(status, RQ_NOTIFICATION_FINISH_REQUEST);
        assert_eq!(response.captured_status, routes::STATUS_OK);

        // SAFETY: release the module exactly once.
        unsafe { ((*mvtbl).dispose)(module) };
    }

    #[test]
    fn unknown_request_continues_without_writing_status() {
        let module = mint_http_module();
        // SAFETY: module is a live WordyHttpModule.
        let mvtbl = unsafe { *module.cast::<*const CHttpModuleVtbl>() };

        let url: Vec<u16> = "/words\0".encode_utf16().collect();
        let (_req, response, context) = make_context(b"GET\0", &url);
        let ctx_ptr = (context.as_ref() as *const FakeContext as *mut FakeContext).cast();

        // SAFETY: ctx_ptr is a live emulated context exposing the modeled vtables.
        let status = unsafe { ((*mvtbl).on_begin_request)(module, ctx_ptr, null_mut()) };
        assert_eq!(status, RQ_NOTIFICATION_CONTINUE);
        assert_eq!(response.captured_status, 0);

        // SAFETY: release the module exactly once.
        unsafe { ((*mvtbl).dispose)(module) };
    }

    #[test]
    fn on_begin_request_tolerates_a_null_context() {
        let module = mint_http_module();
        // SAFETY: module is a live WordyHttpModule.
        let mvtbl = unsafe { *module.cast::<*const CHttpModuleVtbl>() };
        // SAFETY: a null context is explicitly handled; the dispatcher declines.
        let status = unsafe { ((*mvtbl).on_begin_request)(module, null_mut(), null_mut()) };
        assert_eq!(status, RQ_NOTIFICATION_CONTINUE);
        // SAFETY: release the module exactly once.
        unsafe { ((*mvtbl).dispose)(module) };
    }

    #[test]
    fn send_response_passes_through() {
        let module = mint_http_module();
        // SAFETY: module is a live WordyHttpModule.
        let mvtbl = unsafe { *module.cast::<*const CHttpModuleVtbl>() };
        // SAFETY: drive the send-response notification.
        let status = unsafe { ((*mvtbl).on_send_response)(module, null_mut(), null_mut()) };
        assert_eq!(status, RQ_NOTIFICATION_CONTINUE);
        // SAFETY: release the module exactly once.
        unsafe { ((*mvtbl).dispose)(module) };
    }
}
