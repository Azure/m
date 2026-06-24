// Copyright (c) Microsoft Corporation.

//! The in-process web-host activation seam (`windows-win32-shim` SHIM-D18) — the
//! ABI side of platform-isolation's safe web `RequestHandler` surface (M8).
//!
//! The shim is loaded into the web host as a load-time dependency (via the
//! aliasobj relink, MW5) and inserts a request handler at the host's **public
//! activation seam** — the IIS native-module path `RegisterModule` →
//! `IHttpModuleRegistrationInfo::SetRequestNotifications` →
//! `IHttpModuleFactory::GetHttpModule` → `CHttpModule` notifications. Only public
//! Windows SDK names are used.
//!
//! Per SHIM-D2 this is the only place raw caller pointers and the hand-rolled
//! IIS module vtables are touched, so the module opts back into `unsafe_code`
//! (the crate root denies it). The decision to hand-roll the module vtables with
//! raw types — rather than bind a real IIS-hosting crate — keeps the dependency
//! story uniform with the rest of the shim (Design Autonomy, SHIM-D18).
//!
//! ## Residency probe (MW11-1)
//!
//! [`mShimWebProbe`] is a zero-argument diagnostic export that returns a fixed
//! sentinel ([`SHIM_WEB_PROBE_TAG`]). It carries no isolation behavior; it exists
//! so a relinked host (or the link-proof harness) can confirm the shim is
//! resident and its exported entry points are reachable in the host process. It
//! is shim-internal — it mirrors no Win32 name — so it is **not** part of the
//! Win32 alias roster (`windows_win32_shim.def` / `windows_win32_shim_aliases.ndjson`).

#![allow(unsafe_code)]
#![allow(clippy::not_unsafe_ptr_arg_deref, clippy::too_many_arguments)]

use core::ffi::c_void;

use windows_sys::Win32::Foundation::{E_POINTER, S_OK};
use windows_sys::core::{GUID, HRESULT};
use windows_platform_isolation::{Disposition, HttpRequest, HttpResponse, RequestHandler};

use crate::session::session;

/// `RQ_NOTIFICATION_CONTINUE` — the `REQUEST_NOTIFICATION_STATUS` value that
/// tells the host pipeline to proceed to the next module unchanged. Fixed by IIS
/// (`0`); the pass-through cut (MW11) returns it from every notification.
/// Changing it is meaningless, not a breaking change of ours.
const RQ_NOTIFICATION_CONTINUE: i32 = 0;

/// `RQ_NOTIFICATION_FINISH_REQUEST` — the `REQUEST_NOTIFICATION_STATUS` value
/// that tells the host to stop the pipeline and finish the request now. Fixed by
/// IIS (`2`); the bridge returns it when a handler dispositions
/// [`Disposition::FinishRequest`]. Changing it is meaningless, not a breaking
/// change of ours.
const RQ_NOTIFICATION_FINISH_REQUEST: i32 = 2;

/// `RQ_BEGIN_REQUEST` — the request-notification flag for the begin-request
/// stage. Fixed by IIS (`0x0000_0001`); the shim registers for it so its module
/// runs at the start of each request.
const RQ_BEGIN_REQUEST: u32 = 0x0000_0001;

/// `RQ_SEND_RESPONSE` — the request-notification flag for the send-response
/// stage. Fixed by IIS (`0x0000_0020`); the shim registers for it so its module
/// runs on the response path (SHIM-D18).
const RQ_SEND_RESPONSE: u32 = 0x0000_0020;

/// The sentinel [`mShimWebProbe`] returns: the ASCII tag `"MWEB"` packed
/// big-endian (`M`=0x4D, `W`=0x57, `E`=0x45, `B`=0x42). A caller that reads this
/// value back from the exported symbol has proven the shim is resident. The
/// exact value is arbitrary; changing it only invalidates a residency check, it
/// is not a behavioral contract.
pub const SHIM_WEB_PROBE_TAG: u32 = 0x4D57_4542;

/// Residency probe (MW11-1): returns [`SHIM_WEB_PROBE_TAG`] so a host or the
/// link-proof harness can confirm the shim is loaded and its exports resolve.
/// Carries no isolation behavior.
#[unsafe(no_mangle)]
pub extern "system" fn mShimWebProbe() -> u32 {
    SHIM_WEB_PROBE_TAG
}

// --- Hand-rolled IIS module vtables (SHIM-D18, modeled subsets) --------------

/// The `IHttpModuleRegistrationInfo` vtable layout (host-provided). A **modeled
/// subset** of the real interface in real-header order; the shim invokes only
/// `set_request_notifications` (slot index 2), the remaining slots are present
/// so that call lands at the correct vtable offset. The precise layout is pinned
/// when a real host is bound; until then the emulated-host harness builds the
/// same struct, so both sides agree.
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

/// The `IHttpModuleFactory` vtable layout (shim-vended): `GetHttpModule` vends a
/// `CHttpModule`; `Terminate` releases the factory. Not an `IUnknown`-derived
/// interface — IIS module factories are plain abstract classes.
#[repr(C)]
struct IHttpModuleFactoryVtbl {
    get_http_module:
        unsafe extern "system" fn(*mut c_void, *mut *mut c_void, *mut c_void) -> HRESULT,
    terminate: unsafe extern "system" fn(*mut c_void),
}

/// The `CHttpModule` vtable layout (shim-vended, modeled subset): the two
/// per-request notification slots the shim registered for plus `Dispose`. The
/// shim defines and drives this vtable on both ends, so the subset is internally
/// consistent (SHIM-D18).
#[repr(C)]
struct CHttpModuleVtbl {
    on_begin_request: unsafe extern "system" fn(*mut c_void, *mut c_void, *mut c_void) -> i32,
    on_send_response: unsafe extern "system" fn(*mut c_void, *mut c_void, *mut c_void) -> i32,
    dispose: unsafe extern "system" fn(*mut c_void),
}

// --- Host per-request interfaces (modeled subsets, MW12) ---------------------

/// The `IHttpContext` vtable layout (host-provided, modeled subset): the two
/// accessors the bridge uses to reach the request and response for a
/// notification. The real interface is far larger; only the slots the bridge
/// reads are modeled (SHIM-D18), and the precise layout is pinned when a real
/// host is bound.
#[repr(C)]
struct IHttpContextVtbl {
    get_request: unsafe extern "system" fn(*mut c_void) -> *mut c_void,
    get_response: unsafe extern "system" fn(*mut c_void) -> *mut c_void,
}

/// The `IHttpRequest` vtable layout (host-provided, modeled subset). The method
/// is an ANSI `PCSTR` and the URL a wide `PCWSTR`, matching IIS
/// (`GetHttpMethod` / the cooked-URL full URL); the rest of the interface is
/// elided until a scenario needs it.
#[repr(C)]
struct IHttpRequestVtbl {
    get_http_method: unsafe extern "system" fn(*mut c_void) -> *const u8,
    get_http_url: unsafe extern "system" fn(*mut c_void) -> *const u16,
}

/// The `IHttpResponse` vtable layout (host-provided, modeled subset): the status
/// accessor the bridge reads to build the response model. A compact stand-in for
/// `GetStatus` (whose real form returns the code through an out-parameter).
#[repr(C)]
struct IHttpResponseVtbl {
    get_status: unsafe extern "system" fn(*mut c_void) -> u16,
}

/// Read a NUL-terminated ANSI `PCSTR` into an owned `String` (lossy UTF-8). An
/// empty string for a null pointer.
///
/// # Safety
///
/// `ptr` must be null or point at a NUL-terminated byte string the host owns for
/// the duration of the call.
unsafe fn pcstr_to_string(ptr: *const u8) -> String {
    if ptr.is_null() {
        return String::new();
    }
    let mut len = 0usize;
    // SAFETY: ptr is a NUL-terminated C string; scan to the terminator.
    while unsafe { *ptr.add(len) } != 0 {
        len += 1;
    }
    // SAFETY: the first `len` bytes are valid and initialized.
    let bytes = unsafe { core::slice::from_raw_parts(ptr, len) };
    String::from_utf8_lossy(bytes).into_owned()
}

/// Read a NUL-terminated wide `PCWSTR` into an owned `String` (lossy UTF-16). An
/// empty string for a null pointer.
///
/// # Safety
///
/// `ptr` must be null or point at a NUL-terminated UTF-16 string the host owns
/// for the duration of the call.
unsafe fn pcwstr_to_string(ptr: *const u16) -> String {
    if ptr.is_null() {
        return String::new();
    }
    let mut len = 0usize;
    // SAFETY: ptr is a NUL-terminated wide string; scan to the terminator.
    while unsafe { *ptr.add(len) } != 0 {
        len += 1;
    }
    // SAFETY: the first `len` units are valid and initialized.
    let units = unsafe { core::slice::from_raw_parts(ptr, len) };
    String::from_utf16_lossy(units)
}

/// Translate the host's `IHttpContext` into a borrowed-model [`HttpRequest`]
/// (SHIM-D18 / SHIM-D2). A null context (or null request) yields the default
/// empty request, so a host that drives a notification without request data is
/// tolerated. Only the modeled subset (method + URL) is read; headers and body
/// are deferred until a scenario needs them.
///
/// # Safety
///
/// `context` must be null or a live `IHttpContext` whose first field is its
/// vtable pointer, owned by the host for the duration of the call.
unsafe fn decode_request(context: *mut c_void) -> HttpRequest {
    if context.is_null() {
        return HttpRequest::default();
    }
    // SAFETY: context's first field is its vtable pointer.
    let ctx_vtbl = unsafe { *context.cast::<*const IHttpContextVtbl>() };
    // SAFETY: call the host accessor to reach the request.
    let request = unsafe { ((*ctx_vtbl).get_request)(context) };
    if request.is_null() {
        return HttpRequest::default();
    }
    // SAFETY: request's first field is its vtable pointer.
    let req_vtbl = unsafe { *request.cast::<*const IHttpRequestVtbl>() };
    // SAFETY: call the host accessors for the method and URL.
    let method_ptr = unsafe { ((*req_vtbl).get_http_method)(request) };
    let url_ptr = unsafe { ((*req_vtbl).get_http_url)(request) };
    // SAFETY: the accessors return NUL-terminated host strings (or null).
    let method = unsafe { pcstr_to_string(method_ptr) };
    // SAFETY: as above for the wide URL.
    let url = unsafe { pcwstr_to_string(url_ptr) };
    HttpRequest::new(method, url)
}

/// Translate the host's `IHttpContext` into a borrowed-model [`HttpResponse`]
/// (SHIM-D18 / SHIM-D2). A null context (or null response) yields the default
/// empty response. Only the modeled subset (status) is read; headers and body
/// are deferred.
///
/// # Safety
///
/// `context` must be null or a live `IHttpContext` whose first field is its
/// vtable pointer, owned by the host for the duration of the call.
unsafe fn decode_response(context: *mut c_void) -> HttpResponse {
    if context.is_null() {
        return HttpResponse::default();
    }
    // SAFETY: context's first field is its vtable pointer.
    let ctx_vtbl = unsafe { *context.cast::<*const IHttpContextVtbl>() };
    // SAFETY: call the host accessor to reach the response.
    let response = unsafe { ((*ctx_vtbl).get_response)(context) };
    if response.is_null() {
        return HttpResponse::default();
    }
    // SAFETY: response's first field is its vtable pointer.
    let resp_vtbl = unsafe { *response.cast::<*const IHttpResponseVtbl>() };
    // SAFETY: call the host accessor for the status code.
    let status = unsafe { ((*resp_vtbl).get_status)(response) };
    HttpResponse::new(status)
}

/// Map a safe-handler [`Disposition`] back to the host's
/// `REQUEST_NOTIFICATION_STATUS` (MW12-3). The two outcomes round-trip exactly:
/// [`Disposition::Continue`] -> [`RQ_NOTIFICATION_CONTINUE`] and
/// [`Disposition::FinishRequest`] -> [`RQ_NOTIFICATION_FINISH_REQUEST`].
///
/// The M8 handler surface has no *error* disposition, so there is no error code
/// to map here; the HRESULT-returning entry points
/// ([`mRegisterModule`] / [`factory_get_http_module`]) own their own
/// `S_OK` / `E_POINTER` round-trip and are unaffected.
fn disposition_to_status(disposition: Disposition) -> i32 {
    match disposition {
        Disposition::Continue => RQ_NOTIFICATION_CONTINUE,
        Disposition::FinishRequest => RQ_NOTIFICATION_FINISH_REQUEST,
    }
}

// --- Shim-vended objects ----------------------------------------------------

/// The shim's `IHttpModuleFactory`: a heap object whose first field is the
/// vtable pointer (the IIS object layout). `mRegisterModule` hands it to the
/// host; the host reclaims it through `Terminate`.
#[repr(C)]
struct ShimModuleFactory {
    /// Dispatched through by the host via the vtable contract, never read by
    /// Rust.
    #[allow(dead_code)]
    vtable: *const IHttpModuleFactoryVtbl,
}

/// The shim's `CHttpModule`: a heap object the factory vends and the host
/// reclaims through `Dispose`.
#[repr(C)]
struct ShimHttpModule {
    /// Dispatched through by the host via the vtable contract, never read by
    /// Rust. Must stay first so the host's pointer-to-object reads the vtable.
    #[allow(dead_code)]
    vtable: *const CHttpModuleVtbl,
    /// The per-request safe handler stack (MW12) the notifications drive. A
    /// shim-private field the host never reads.
    handler: Box<dyn RequestHandler>,
}

/// The single shared factory vtable every [`ShimModuleFactory`] points at.
static SHIM_MODULE_FACTORY_VTBL: IHttpModuleFactoryVtbl = IHttpModuleFactoryVtbl {
    get_http_module: factory_get_http_module,
    terminate: factory_terminate,
};

/// The single shared module vtable every [`ShimHttpModule`] points at.
static SHIM_HTTP_MODULE_VTBL: CHttpModuleVtbl = CHttpModuleVtbl {
    on_begin_request: module_on_begin_request,
    on_send_response: module_on_send_response,
    dispose: module_dispose,
};

/// Mint a shim module factory, returning a heap pointer the host releases via
/// `Terminate`.
fn mint_shim_module_factory() -> *mut c_void {
    let factory = Box::new(ShimModuleFactory {
        vtable: &SHIM_MODULE_FACTORY_VTBL,
    });
    Box::into_raw(factory).cast::<c_void>()
}

/// Mint a shim `CHttpModule`, returning a heap pointer the host releases via
/// `Dispose`. The module owns the per-request handler stack it drives (MW12).
fn mint_shim_http_module(handler: Box<dyn RequestHandler>) -> *mut c_void {
    let module = Box::new(ShimHttpModule {
        vtable: &SHIM_HTTP_MODULE_VTBL,
        handler,
    });
    Box::into_raw(module).cast::<c_void>()
}

/// `IHttpModuleFactory::GetHttpModule`: vend a shim `CHttpModule` and record the
/// acquisition. The module is minted with the session's current handler stack
/// (MW12) so each request is bridged into the safe `RequestHandler` surface.
unsafe extern "system" fn factory_get_http_module(
    _this: *mut c_void,
    pp_module: *mut *mut c_void,
    _allocator: *mut c_void,
) -> HRESULT {
    if pp_module.is_null() {
        return E_POINTER;
    }
    let handler = session().with_web(|web| web.build_handler());
    let module = mint_shim_http_module(handler);
    // SAFETY: pp_module is non-null (checked above); write the out-param.
    unsafe { *pp_module = module };
    session().with_web(|web| web.on_get_http_module());
    S_OK
}

/// `IHttpModuleFactory::Terminate`: reclaim the factory allocation.
unsafe extern "system" fn factory_terminate(this: *mut c_void) {
    if this.is_null() {
        return;
    }
    // SAFETY: this is a live ShimModuleFactory minted by mint_shim_module_factory;
    // the host calls Terminate exactly once.
    drop(unsafe { Box::from_raw(this.cast::<ShimModuleFactory>()) });
}

/// `CHttpModule::OnBeginRequest`: record the traversal, then bridge the host
/// request into the safe handler stack and return its disposition (MW12). The
/// MW11 seam marker is kept as the coarse on-path signal.
unsafe extern "system" fn module_on_begin_request(
    this: *mut c_void,
    context: *mut c_void,
    _provider: *mut c_void,
) -> i32 {
    session().with_web(|web| web.on_begin_request());
    // SAFETY: this is a live ShimHttpModule minted by mint_shim_http_module.
    let module = this.cast::<ShimHttpModule>();
    // SAFETY: decode the host request (tolerates a null context).
    let request = unsafe { decode_request(context) };
    // SAFETY: drive the per-module handler stack on the request thread.
    let disposition = unsafe { (*module).handler.on_begin_request(&request) };
    disposition_to_status(disposition)
}

/// `CHttpModule::OnSendResponse`: record the traversal, then bridge the host
/// response into the safe handler stack and return its disposition (MW12).
///
/// Pass-through: the (handler-visible) response model is **not** written back to
/// the host. The identity and journaling stacks never mutate it, so the host
/// response is byte-identical to the un-shimmed path; a future redirecting mode
/// will diff the model and flush only real changes.
unsafe extern "system" fn module_on_send_response(
    this: *mut c_void,
    context: *mut c_void,
    _provider: *mut c_void,
) -> i32 {
    session().with_web(|web| web.on_send_response());
    // SAFETY: this is a live ShimHttpModule minted by mint_shim_http_module.
    let module = this.cast::<ShimHttpModule>();
    // SAFETY: decode the host response (tolerates a null context).
    let mut response = unsafe { decode_response(context) };
    // SAFETY: drive the per-module handler stack on the request thread.
    let disposition = unsafe { (*module).handler.on_send_response(&mut response) };
    disposition_to_status(disposition)
}

/// `CHttpModule::Dispose`: reclaim the module allocation.
unsafe extern "system" fn module_dispose(this: *mut c_void) {
    if this.is_null() {
        return;
    }
    // SAFETY: this is a live ShimHttpModule minted by mint_shim_http_module; the
    // host calls Dispose exactly once.
    drop(unsafe { Box::from_raw(this.cast::<ShimHttpModule>()) });
}

/// `RegisterModule` (MW11-3): the IIS native-module entry point. The relinked
/// host's `RegisterModule` is aliased to this export (D24), so the host hands the
/// shim its [`IHttpModuleRegistrationInfo`](IHttpModuleRegistrationInfoVtbl).
///
/// The body mints a shim module factory and registers it for the begin-request
/// and send-response notifications via `SetRequestNotifications`. On success the
/// registration is journaled through the safe web policy ([`crate::web`]) and the
/// host's HRESULT is returned; on failure the factory is reclaimed and the
/// failure propagated. Installation is unconditional — being on the response
/// path is the point; the session mode only decides whether the traversal is
/// observed.
#[unsafe(no_mangle)]
pub extern "system" fn mRegisterModule(
    _dw_server_version: u32,
    p_module_info: *mut c_void,
    _p_global_info: *mut c_void,
) -> HRESULT {
    if p_module_info.is_null() {
        return E_POINTER;
    }
    let factory = mint_shim_module_factory();
    // SAFETY: p_module_info points at a live IHttpModuleRegistrationInfo whose
    // first field is its vtable pointer (the host owns it for the call).
    let vtbl = unsafe { *p_module_info.cast::<*const IHttpModuleRegistrationInfoVtbl>() };
    // SAFETY: vtbl is the host-provided registration-info vtable; call
    // SetRequestNotifications to install our factory for the two stages.
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
        drop(unsafe { Box::from_raw(factory.cast::<ShimModuleFactory>()) });
        return hr;
    }
    session().with_web(|web| web.on_register_module());
    hr
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr::{null, null_mut};

    #[test]
    fn probe_returns_the_residency_tag() {
        assert_eq!(mShimWebProbe(), SHIM_WEB_PROBE_TAG);
        // The tag is the ASCII bytes "MWEB" packed big-endian.
        assert_eq!(SHIM_WEB_PROBE_TAG.to_be_bytes(), *b"MWEB");
    }

    /// An emulated `IHttpModuleRegistrationInfo` that captures the arguments the
    /// shim passes to `SetRequestNotifications`.
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
        // SAFETY: this is the live FakeRegInfo the test passed to mRegisterModule.
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

    #[test]
    fn register_module_installs_factory_for_begin_and_send_notifications() {
        let mut info = FakeRegInfo {
            vtable: &FAKE_REG_INFO_VTBL,
            captured_factory: null_mut(),
            captured_notifications: 0,
            captured_post: 0,
        };
        let hr = mRegisterModule(0, (&mut info as *mut FakeRegInfo).cast(), null_mut());
        assert_eq!(hr, S_OK);
        assert!(!info.captured_factory.is_null());
        assert_eq!(
            info.captured_notifications,
            RQ_BEGIN_REQUEST | RQ_SEND_RESPONSE
        );
        assert_eq!(info.captured_post, 0);

        // Drive the captured factory through the module lifecycle; every
        // notification must report the pass-through disposition.
        let factory = info.captured_factory;
        // SAFETY: factory is a live ShimModuleFactory minted by mRegisterModule.
        let fvtbl = unsafe { *factory.cast::<*const IHttpModuleFactoryVtbl>() };
        let mut module: *mut c_void = null_mut();
        // SAFETY: fvtbl is the shim factory vtable; vend a module.
        let hr = unsafe { ((*fvtbl).get_http_module)(factory, &mut module, null_mut()) };
        assert_eq!(hr, S_OK);
        assert!(!module.is_null());
        // SAFETY: module is a live ShimHttpModule.
        let mvtbl = unsafe { *module.cast::<*const CHttpModuleVtbl>() };
        // SAFETY: drive the two notifications through the module vtable.
        let begin = unsafe { ((*mvtbl).on_begin_request)(module, null_mut(), null_mut()) };
        assert_eq!(begin, RQ_NOTIFICATION_CONTINUE);
        // SAFETY: drive the send-response notification.
        let send = unsafe { ((*mvtbl).on_send_response)(module, null_mut(), null_mut()) };
        assert_eq!(send, RQ_NOTIFICATION_CONTINUE);
        // SAFETY: release the module then the factory exactly once each.
        unsafe { ((*mvtbl).dispose)(module) };
        // SAFETY: terminate the factory exactly once.
        unsafe { ((*fvtbl).terminate)(factory) };
    }

    #[test]
    fn register_module_rejects_a_null_registration_info() {
        assert_eq!(mRegisterModule(0, null_mut(), null_mut()), E_POINTER);
    }

    // --- MW12-1: per-request bridge decode ----------------------------------

    /// An emulated `IHttpRequest` carrying a method and URL.
    #[repr(C)]
    struct FakeRequest {
        vtable: *const IHttpRequestVtbl,
        method: *const u8,
        url: *const u16,
    }

    /// An emulated `IHttpResponse` carrying a status code.
    #[repr(C)]
    struct FakeResponse {
        vtable: *const IHttpResponseVtbl,
        status: u16,
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

    unsafe extern "system" fn fake_get_status(this: *mut c_void) -> u16 {
        // SAFETY: this is a live FakeResponse.
        unsafe { (*this.cast::<FakeResponse>()).status }
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
        get_status: fake_get_status,
    };

    #[test]
    fn decode_translates_method_url_and_status_from_the_host() {
        let method = b"GET\0";
        let url: Vec<u16> = "/index.html\0".encode_utf16().collect();
        let mut request = FakeRequest {
            vtable: &FAKE_REQUEST_VTBL,
            method: method.as_ptr(),
            url: url.as_ptr(),
        };
        let mut response = FakeResponse {
            vtable: &FAKE_RESPONSE_VTBL,
            status: 200,
        };
        let mut context = FakeContext {
            vtable: &FAKE_CONTEXT_VTBL,
            request: (&mut request as *mut FakeRequest).cast(),
            response: (&mut response as *mut FakeResponse).cast(),
        };
        let ctx_ptr = (&mut context as *mut FakeContext).cast();

        // SAFETY: ctx_ptr is a live FakeContext exposing the modeled vtables.
        let decoded_request = unsafe { decode_request(ctx_ptr) };
        assert_eq!(decoded_request.method(), "GET");
        assert_eq!(decoded_request.url(), "/index.html");

        // SAFETY: as above.
        let decoded_response = unsafe { decode_response(ctx_ptr) };
        assert_eq!(decoded_response.status(), 200);
    }

    #[test]
    fn decode_tolerates_a_null_context() {
        // SAFETY: a null context is explicitly handled.
        let request = unsafe { decode_request(null_mut()) };
        assert_eq!(request, HttpRequest::default());
        // SAFETY: as above.
        let response = unsafe { decode_response(null_mut()) };
        assert_eq!(response, HttpResponse::default());
    }

    // --- MW12-3: disposition mapping ----------------------------------------

    #[test]
    fn disposition_round_trips_to_host_status_codes() {
        assert_eq!(
            disposition_to_status(Disposition::Continue),
            RQ_NOTIFICATION_CONTINUE
        );
        assert_eq!(
            disposition_to_status(Disposition::FinishRequest),
            RQ_NOTIFICATION_FINISH_REQUEST
        );
    }
}

