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

use crate::session::session;

/// `RQ_NOTIFICATION_CONTINUE` — the `REQUEST_NOTIFICATION_STATUS` value that
/// tells the host pipeline to proceed to the next module unchanged. Fixed by IIS
/// (`0`); the pass-through cut (MW11) returns it from every notification.
/// Changing it is meaningless, not a breaking change of ours.
const RQ_NOTIFICATION_CONTINUE: i32 = 0;

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
    /// Rust.
    #[allow(dead_code)]
    vtable: *const CHttpModuleVtbl,
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
/// `Dispose`.
fn mint_shim_http_module() -> *mut c_void {
    let module = Box::new(ShimHttpModule {
        vtable: &SHIM_HTTP_MODULE_VTBL,
    });
    Box::into_raw(module).cast::<c_void>()
}

/// `IHttpModuleFactory::GetHttpModule`: vend a shim `CHttpModule` and record the
/// acquisition. Pass-through first cut — the module forwards every notification.
unsafe extern "system" fn factory_get_http_module(
    _this: *mut c_void,
    pp_module: *mut *mut c_void,
    _allocator: *mut c_void,
) -> HRESULT {
    if pp_module.is_null() {
        return E_POINTER;
    }
    let module = mint_shim_http_module();
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

/// `CHttpModule::OnBeginRequest`: record the traversal and continue the pipeline
/// unchanged (the pass-through disposition, MW11).
unsafe extern "system" fn module_on_begin_request(
    _this: *mut c_void,
    _context: *mut c_void,
    _provider: *mut c_void,
) -> i32 {
    session().with_web(|web| web.on_begin_request());
    RQ_NOTIFICATION_CONTINUE
}

/// `CHttpModule::OnSendResponse`: record the traversal and continue the pipeline
/// unchanged (the pass-through disposition, MW11).
unsafe extern "system" fn module_on_send_response(
    _this: *mut c_void,
    _context: *mut c_void,
    _provider: *mut c_void,
) -> i32 {
    session().with_web(|web| web.on_send_response());
    RQ_NOTIFICATION_CONTINUE
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
}

