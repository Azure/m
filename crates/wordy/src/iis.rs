// Copyright (c) Microsoft Corporation.

//! The IIS native-module ABI boundary for `wordy` (Windows only).
//!
//! This module bridges a genuine IIS / Hostable Web Core host into `wordy`'s
//! safe [`crate::routes`] surface. Unlike `wordy`'s earlier modeled subset, the
//! vtables here are **pinned to the real `httpserv.h` layout** (Windows SDK
//! 10.0.26100): every interface `wordy` touches is laid out at the exact slot
//! offsets the genuine host uses, so a real host's calls land on the right
//! methods and `wordy`'s calls invoke the right host methods (MW16-1).
//!
//! Pinned interfaces and the slots `wordy` uses:
//! - `CHttpModule` — 30 vtable slots; `[0]OnBeginRequest`, `[24]OnSendResponse`,
//!   `[29]Dispose`. `wordy` implements `OnBeginRequest` + `Dispose`; the other 28
//!   slots are safe pass-through stubs (never called — `wordy` registers only
//!   `RQ_BEGIN_REQUEST`).
//! - `IHttpModuleFactory` — `[0]GetHttpModule`, `[1]Terminate` (implemented).
//! - `IHttpModuleRegistrationInfo` — `[2]SetRequestNotifications` (called).
//! - `IHttpContext` — `[3]GetRequest`, `[4]GetResponse` (called).
//! - `IHttpRequest` — `[0]GetRawHttpRequest` (raw URL), `[2]GetHeader`,
//!   `[8]GetHttpMethod`, `[16]ReadEntityBody`, `[18]GetRemainingEntityBytes`.
//! - `IHttpResponse` — `[3]SetStatus`, `[4]SetHeader`, `[10]Clear`,
//!   `[21]WriteEntityChunks`.
//!
//! Two `http.h` structs are also pinned for the data we read/write:
//! `HTTP_REQUEST` (we read `pRawUrl` / `RawUrlLength`) and `HTTP_DATA_CHUNK` (we
//! write one from-memory chunk). Compile-time `offset_of!` assertions guard the
//! layouts.
//!
//! Flow: IIS loads `wordy.dll` and calls its exported [`RegisterModule`], which
//! registers a module factory for the begin-request notification. For each
//! request the factory vends a [`WordyHttpModule`] whose `OnBeginRequest`
//! decodes the method, raw URL, entity body, and `X-Wordy-User` header into a
//! [`crate::routes::HttpRequest`], asks the process-wide
//! [`Service`](crate::routes::Service) to dispatch it, and realizes the response
//! against the host (clear, set status, set `Content-Type`, write the JSON body).
//!
//! All `unsafe` is confined to this boundary; the crate root denies
//! `unsafe_code` and opts this module in explicitly.

use core::ffi::c_void;
use core::ptr::null_mut;
use std::path::PathBuf;
use std::sync::LazyLock;

use windows_sys::Win32::Foundation::{E_POINTER, S_OK};
use windows_sys::core::HRESULT;

use crate::custom::CustomDictionary;
use crate::routes::{self, Outcome, Service};
use crate::words::Locale;

// --- Notification status codes and flags (IIS `httpserv.h`) ----------------
//
// Changing any of these values is a breaking ABI change; they mirror the host's
// `REQUEST_NOTIFICATION_STATUS` enum and `RQ_*` notification flags.

/// `RQ_NOTIFICATION_CONTINUE`: let the host proceed to the next module.
const RQ_NOTIFICATION_CONTINUE: i32 = 0;
/// `RQ_NOTIFICATION_FINISH_REQUEST`: stop the pipeline; the request is complete.
const RQ_NOTIFICATION_FINISH_REQUEST: i32 = 2;
/// `RQ_NOTIFICATION_PENDING`: suspend the request; the host resumes the pipeline
/// when the module later calls `IHttpContext::PostCompletion`. Consumed by the
/// async request path in MW14-2.
#[allow(dead_code)]
const RQ_NOTIFICATION_PENDING: i32 = 1;
/// `RQ_BEGIN_REQUEST`: the begin-request notification flag.
const RQ_BEGIN_REQUEST: u32 = 0x0000_0001;

/// `HttpDataChunkFromMemory`: the from-memory `HTTP_DATA_CHUNK` discriminant.
const HTTP_DATA_CHUNK_FROM_MEMORY: i32 = 0;

/// The environment variable that overrides where the custom-dictionary store is
/// rooted; absent, a per-process directory under the OS temp dir is used.
const CUSTOM_ROOT_ENV: &str = "WORDY_CUSTOM_ROOT";

/// Upper bound on the entity body `wordy` will read from a request.
const MAX_BODY_BYTES: usize = 1 << 20;

/// Append a diagnostic line to the file named by the `WORDY_TRACE` env var, if
/// set. A no-op otherwise. Used to diagnose behavior under a genuine host, where
/// stdout is not visible.
fn trace(message: &str) {
    use std::io::Write;
    if let Some(path) = std::env::var_os("WORDY_TRACE")
        && let Ok(mut file) = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)
    {
        let _ = writeln!(file, "{message}");
    }
}

// --- Pinned `http.h` structs ------------------------------------------------

/// The head of `HTTP_REQUEST` (`http.h`) up to and including `pRawUrl`.
///
/// `wordy` reads the raw request URL from `pRawUrl` / `RawUrlLength`; the fields
/// before them are modeled only to place those two at the genuine offsets.
#[repr(C)]
struct HttpRequestHead {
    flags: u32,                // @0
    _pad0: u32,                // @4
    connection_id: u64,        // @8
    request_id: u64,           // @16
    url_context: u64,          // @24
    version_major: u16,        // @32
    version_minor: u16,        // @34
    verb: i32,                 // @36
    unknown_verb_length: u16,  // @40
    raw_url_length: u16,       // @42
    _pad1: u32,                // @44
    p_unknown_verb: *const u8, // @48
    p_raw_url: *const u8,      // @56
}

/// A from-memory `HTTP_DATA_CHUNK` (`http.h`) for [`IHttpResponse`] body writes.
#[repr(C)]
struct HttpDataChunk {
    chunk_type: i32,       // @0   HttpDataChunkFromMemory
    _pad0: u32,            // @4
    p_buffer: *mut c_void, // @8   FromMemory.pBuffer
    buffer_length: u32,    // @16  FromMemory.BufferLength
    _pad1: u32,            // @20
    _tail: u64,            // @24  fills the union to its 24-byte size (total 32)
}

// Guard the pinned offsets against accidental field reordering.
const _: () = {
    assert!(core::mem::offset_of!(HttpRequestHead, raw_url_length) == 42);
    assert!(core::mem::offset_of!(HttpRequestHead, p_raw_url) == 56);
    assert!(core::mem::offset_of!(HttpDataChunk, p_buffer) == 8);
    assert!(core::mem::offset_of!(HttpDataChunk, buffer_length) == 16);
    assert!(core::mem::size_of::<HttpDataChunk>() == 32);
};

// --- Pinned vtables ---------------------------------------------------------

/// A `CHttpModule` notification slot — `(this, IHttpContext*, IProvider*) ->
/// REQUEST_NOTIFICATION_STATUS`. The two pointer args cover every notification
/// `wordy` registers for; unregistered slots are never called.
type NotifyFn = unsafe extern "system" fn(*mut c_void, *mut c_void, *mut c_void) -> i32;

/// `CHttpModule::Dispose` — `(this) -> VOID`.
type DisposeFn = unsafe extern "system" fn(*mut c_void);

/// An opaque vtable slot `wordy` never calls (host-implemented methods it skips,
/// or its own stub slots). Modeled as a bare function pointer for layout only.
type VtSlot = unsafe extern "system" fn();

/// The full `CHttpModule` vtable: 29 notification slots then `Dispose`.
#[repr(C)]
struct CHttpModuleVtbl {
    /// Slots `[0..29)`; `[0]` = `OnBeginRequest`, `[24]` = `OnSendResponse`.
    notifications: [NotifyFn; 29],
    /// Slot `[29]` = `Dispose`.
    dispose: DisposeFn,
}

/// The `IHttpModuleFactory` vtable.
#[repr(C)]
struct IHttpModuleFactoryVtbl {
    get_http_module:
        unsafe extern "system" fn(*mut c_void, *mut *mut c_void, *mut c_void) -> HRESULT,
    terminate: unsafe extern "system" fn(*mut c_void),
}

/// The `IModuleAllocator` vtable — `wordy` calls `[0]AllocateMemory` to allocate
/// each per-request module from the host's request memory pool.
#[repr(C)]
struct IModuleAllocatorVtbl {
    allocate_memory: unsafe extern "system" fn(*mut c_void, u32) -> *mut c_void,
}

/// The `IHttpModuleRegistrationInfo` vtable (`wordy` calls `[2]`).
#[repr(C)]
struct IHttpModuleRegistrationInfoVtbl {
    get_name: VtSlot,                              // [0]
    get_id: VtSlot,                                // [1]
    set_request_notifications:                     // [2]
        unsafe extern "system" fn(*mut c_void, *mut c_void, u32, u32) -> HRESULT,
    set_global_notifications: VtSlot,              // [3]
    set_priority_for_request_notification: VtSlot, // [4]
    set_priority_for_global_notification: VtSlot,  // [5]
}

/// The `IHttpContext` vtable head (`wordy` calls `[3]`, `[4]`, and `[9]` for
/// asynchronous completion, MW14).
#[repr(C)]
struct IHttpContextVtbl {
    get_site: VtSlot,                                                  // [0]
    get_application: VtSlot,                                           // [1]
    get_connection: VtSlot,                                            // [2]
    get_request: unsafe extern "system" fn(*mut c_void) -> *mut c_void, // [3]
    get_response: unsafe extern "system" fn(*mut c_void) -> *mut c_void, // [4]
    get_response_headers_sent: VtSlot,                                 // [5]
    get_user: VtSlot,                                                  // [6]
    get_module_context_container: VtSlot,                             // [7]
    indicate_completion: VtSlot,                                      // [8]
    post_completion: unsafe extern "system" fn(*mut c_void, u32) -> HRESULT, // [9]
}

// Guard `PostCompletion` at its genuine `httpserv.h` slot (the 10th method).
const _: () = {
    assert!(
        core::mem::offset_of!(IHttpContextVtbl, post_completion)
            == 9 * core::mem::size_of::<*const c_void>()
    );
};

/// Signal the host that asynchronous work for a suspended request has finished
/// via `IHttpContext::PostCompletion(cbBytes)`, so it resumes the pipeline.
/// Consumed by the async request path in MW14-2.
#[allow(dead_code)]
unsafe fn post_completion(context: *mut c_void, bytes: u32) -> HRESULT {
    // SAFETY: the caller guarantees `context` is a live `IHttpContext`.
    let ctx_vtbl = unsafe { *context.cast::<*const IHttpContextVtbl>() };
    // SAFETY: `ctx_vtbl` is the host context vtable; `PostCompletion` is slot 9.
    unsafe { ((*ctx_vtbl).post_completion)(context, bytes) }
}

/// The `IHttpRequest` vtable head (`wordy` calls `[0]`, `[2]`, `[8]`, `[16]`,
/// `[18]`).
#[repr(C)]
struct IHttpRequestVtbl {
    get_raw_http_request: unsafe extern "system" fn(*mut c_void) -> *mut HttpRequestHead, // [0]
    get_raw_http_request_const: VtSlot,                                                   // [1]
    get_header_by_name:                                                                   // [2]
        unsafe extern "system" fn(*mut c_void, *const u8, *mut u16) -> *const u8,
    get_header_by_id: VtSlot,      // [3]
    set_header_by_name: VtSlot,    // [4]
    set_header_by_id: VtSlot,      // [5]
    delete_header_by_name: VtSlot, // [6]
    delete_header_by_id: VtSlot,   // [7]
    get_http_method: unsafe extern "system" fn(*mut c_void) -> *const u8, // [8]
    set_http_method: VtSlot,       // [9]
    set_url_wide: VtSlot,          // [10]
    set_url_ansi: VtSlot,          // [11]
    get_url_changed: VtSlot,       // [12]
    get_forwarded_url: VtSlot,     // [13]
    get_local_address: VtSlot,     // [14]
    get_remote_address: VtSlot,    // [15]
    read_entity_body:              // [16]
        unsafe extern "system" fn(*mut c_void, *mut c_void, u32, i32, *mut u32, *mut i32) -> HRESULT,
    insert_entity_body: VtSlot,    // [17]
    get_remaining_entity_bytes: unsafe extern "system" fn(*mut c_void) -> u32, // [18]
}

/// The `IHttpResponse` vtable head (`wordy` calls `[3]`, `[4]`, `[10]`, `[21]`).
#[repr(C)]
struct IHttpResponseVtbl {
    get_raw_http_response: VtSlot,       // [0]
    get_raw_http_response_const: VtSlot, // [1]
    get_cache_policy: VtSlot,            // [2]
    set_status:                          // [3]
        unsafe extern "system" fn(*mut c_void, u16, *const u8, u16, HRESULT, *mut c_void, i32) -> HRESULT,
    set_header_by_name:                  // [4]
        unsafe extern "system" fn(*mut c_void, *const u8, *const u8, u16, i32) -> HRESULT,
    set_header_by_id: VtSlot,        // [5]
    delete_header_by_name: VtSlot,   // [6]
    delete_header_by_id: VtSlot,     // [7]
    get_header_by_name: VtSlot,      // [8]
    get_header_by_id: VtSlot,        // [9]
    clear: unsafe extern "system" fn(*mut c_void), // [10]
    clear_headers: VtSlot,           // [11]
    set_need_disconnect: VtSlot,     // [12]
    reset_connection: VtSlot,        // [13]
    disable_kernel_cache: VtSlot,    // [14]
    get_kernel_cache_enabled: VtSlot, // [15]
    suppress_headers: VtSlot,        // [16]
    get_headers_suppressed: VtSlot,  // [17]
    flush: VtSlot,                   // [18]
    redirect: VtSlot,                // [19]
    write_entity_chunk_by_reference: VtSlot, // [20]
    write_entity_chunks:             // [21]
        unsafe extern "system" fn(*mut c_void, *mut HttpDataChunk, u32, i32, i32, *mut u32, *mut i32) -> HRESULT,
}

// --- `wordy`'s module objects ----------------------------------------------

/// `wordy`'s module factory. Its first field is the vtable pointer so a
/// `*mut WordyModuleFactory` is ABI-identical to the host's `IHttpModuleFactory*`.
#[repr(C)]
struct WordyModuleFactory {
    vtable: *const IHttpModuleFactoryVtbl,
}

/// A `wordy` per-request module. The dictionary state lives in the process-wide
/// [`SERVICE`]; the module itself is stateless. Its first field is the vtable
/// pointer so a `*mut WordyHttpModule` is ABI-identical to the host's
/// `CHttpModule*`; the `pool_allocated` flag records whether the host's request
/// pool owns the memory (so `Dispose` frees a `Box` only when it does not).
#[repr(C)]
struct WordyHttpModule {
    vtable: *const CHttpModuleVtbl,
    pool_allocated: bool,
}

/// The process-wide service: the shared dictionary plus a per-user custom
/// dictionary rooted under [`CUSTOM_ROOT_ENV`] (default: a per-process directory
/// under the OS temp dir). Built once on first request.
static SERVICE: LazyLock<Service> = LazyLock::new(|| {
    let root = std::env::var_os(CUSTOM_ROOT_ENV)
        .map(PathBuf::from)
        .unwrap_or_else(|| std::env::temp_dir().join("wordy-custom"));
    Service::new(CustomDictionary::new(root), Locale::EnUs)
});

/// The single shared factory vtable every [`WordyModuleFactory`] points at.
static WORDY_MODULE_FACTORY_VTBL: IHttpModuleFactoryVtbl = IHttpModuleFactoryVtbl {
    get_http_module: factory_get_http_module,
    terminate: factory_terminate,
};

/// The single shared module vtable every [`WordyHttpModule`] points at: slot 0
/// is `OnBeginRequest`, slot 29 is `Dispose`, and the other 28 notification
/// slots are per-slot diagnostic trampolines ([`notify_slot`]) that record the
/// exact slot index the host dispatches. `wordy` registers only
/// `RQ_BEGIN_REQUEST`, so in correct operation only slot 0 is ever invoked; the
/// instrumented stubs exist to diagnose a genuine host that dispatches nothing.
static WORDY_HTTP_MODULE_VTBL: CHttpModuleVtbl = CHttpModuleVtbl {
    notifications: [
        module_on_begin_request, // [0] OnBeginRequest (real)
        notify_slot::<1>,
        notify_slot::<2>,
        notify_slot::<3>,
        notify_slot::<4>,
        notify_slot::<5>,
        notify_slot::<6>,
        notify_slot::<7>,
        notify_slot::<8>,
        notify_slot::<9>,
        notify_slot::<10>,
        notify_slot::<11>,
        notify_slot::<12>,
        notify_slot::<13>,
        notify_slot::<14>,
        notify_slot::<15>,
        notify_slot::<16>,
        notify_slot::<17>,
        notify_slot::<18>,
        notify_slot::<19>,
        notify_slot::<20>,
        notify_slot::<21>,
        notify_slot::<22>,
        notify_slot::<23>,
        notify_slot::<24>,
        notify_slot::<25>,
        notify_slot::<26>,
        notify_slot::<27>,
        notify_slot::<28>,
    ],
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
        pool_allocated: false,
    });
    Box::into_raw(module).cast::<c_void>()
}

// --- Decode helpers --------------------------------------------------------

/// Convert a `PCSTR` of `len` bytes to an owned `String` (lossy on invalid UTF-8).
fn pcstr_len_to_string(p: *const u8, len: usize) -> String {
    if p.is_null() || len == 0 {
        return String::new();
    }
    // SAFETY: p points at `len` initialized bytes for the duration of the call.
    let bytes = unsafe { core::slice::from_raw_parts(p, len) };
    String::from_utf8_lossy(bytes).into_owned()
}

/// Convert a NUL-terminated `PCSTR` to an owned `String` (lossy on invalid UTF-8).
fn pcstr_to_string(p: *const u8) -> String {
    if p.is_null() {
        return String::new();
    }
    let mut len = 0usize;
    // SAFETY: p is non-null; walk to the NUL terminator the host guarantees.
    while unsafe { *p.add(len) } != 0 {
        len += 1;
    }
    pcstr_len_to_string(p, len)
}

/// Decode the request method, raw URL, entity body, and `X-Wordy-User` header
/// from the host context into an [`HttpRequest`](routes::HttpRequest).
///
/// Tolerates a null context or any null interface pointer by yielding a default
/// (empty) request, which the dispatcher then declines.
///
/// # Safety
/// `context`, when non-null, must point at a live `IHttpContext` whose pinned
/// request vtable is valid for the duration of the call.
unsafe fn decode_request(context: *mut c_void) -> routes::HttpRequest {
    if context.is_null() {
        return routes::HttpRequest::default();
    }
    // SAFETY: context is a live IHttpContext; read its vtable pointer.
    let ctx_vtbl = unsafe { *context.cast::<*const IHttpContextVtbl>() };
    // SAFETY: ctx_vtbl is the host context vtable; fetch the request interface.
    let request = unsafe { ((*ctx_vtbl).get_request)(context) };
    if request.is_null() {
        return routes::HttpRequest::default();
    }
    // SAFETY: request is a live IHttpRequest; read its vtable pointer.
    let req_vtbl = unsafe { *request.cast::<*const IHttpRequestVtbl>() };
    // SAFETY: req_vtbl is the host request vtable; GetHttpMethod returns a PCSTR.
    let method = unsafe { pcstr_to_string(((*req_vtbl).get_http_method)(request)) };
    // SAFETY: as above.
    let url = unsafe { read_raw_url(req_vtbl, request) };
    // SAFETY: as above.
    let body = unsafe { read_entity_body(req_vtbl, request) };
    // SAFETY: as above.
    let user = unsafe { read_user_header(req_vtbl, request) };
    routes::HttpRequest {
        method,
        url,
        body,
        user,
    }
}

/// Read the raw request URL via `GetRawHttpRequest()->pRawUrl`.
///
/// # Safety
/// `req_vtbl` must be `request`'s live pinned request vtable.
unsafe fn read_raw_url(req_vtbl: *const IHttpRequestVtbl, request: *mut c_void) -> String {
    // SAFETY: GetRawHttpRequest returns the live HTTP_REQUEST for the call.
    let raw = unsafe { ((*req_vtbl).get_raw_http_request)(request) };
    if raw.is_null() {
        return String::new();
    }
    // SAFETY: raw is a live HTTP_REQUEST; pRawUrl points at RawUrlLength bytes.
    let (ptr, len) = unsafe { ((*raw).p_raw_url, (*raw).raw_url_length as usize) };
    pcstr_len_to_string(ptr, len)
}

/// Read the request entity body (up to [`MAX_BODY_BYTES`]) as a UTF-8 (lossy)
/// string via `ReadEntityBody`.
///
/// # Safety
/// `req_vtbl` must be `request`'s live pinned request vtable.
unsafe fn read_entity_body(req_vtbl: *const IHttpRequestVtbl, request: *mut c_void) -> String {
    let mut body: Vec<u8> = Vec::new();
    let mut buffer = [0u8; 4096];
    loop {
        if body.len() >= MAX_BODY_BYTES {
            break;
        }
        let mut read: u32 = 0;
        // SAFETY: request is live; buffer is a valid writable slice; synchronous
        // read (fAsync = FALSE), no completion-pending out-param.
        let hr = unsafe {
            ((*req_vtbl).read_entity_body)(
                request,
                buffer.as_mut_ptr().cast::<c_void>(),
                buffer.len() as u32,
                0,
                &mut read,
                null_mut(),
            )
        };
        // S_OK with bytes means more data; anything else (EOF / error) stops.
        if hr != S_OK || read == 0 {
            break;
        }
        body.extend_from_slice(&buffer[..read as usize]);
    }
    String::from_utf8_lossy(&body).into_owned()
}

/// Read the `X-Wordy-User` header, if present, via `GetHeader`.
///
/// # Safety
/// `req_vtbl` must be `request`'s live pinned request vtable.
unsafe fn read_user_header(
    req_vtbl: *const IHttpRequestVtbl,
    request: *mut c_void,
) -> Option<String> {
    let name = b"X-Wordy-User\0";
    let mut cch: u16 = 0;
    // SAFETY: name is a NUL-terminated PCSTR; GetHeader returns a PCSTR of cch
    // bytes (not NUL-terminated), or null if the header is absent.
    let value = unsafe { ((*req_vtbl).get_header_by_name)(request, name.as_ptr(), &mut cch) };
    if value.is_null() {
        None
    } else {
        Some(pcstr_len_to_string(value, cch as usize))
    }
}

/// Realize an [`HttpResponse`](routes::HttpResponse) against the host response:
/// clear, set status, set `Content-Type`, write the body as one from-memory
/// chunk.
///
/// Tolerates a null context or response by doing nothing.
///
/// # Safety
/// `context`, when non-null, must point at a live `IHttpContext` whose pinned
/// response vtable is valid for the duration of the call.
unsafe fn write_response(context: *mut c_void, response: &routes::HttpResponse) {
    if context.is_null() {
        return;
    }
    // SAFETY: context is a live IHttpContext; read its vtable pointer.
    let ctx_vtbl = unsafe { *context.cast::<*const IHttpContextVtbl>() };
    // SAFETY: ctx_vtbl is the host context vtable; fetch the response interface.
    let resp = unsafe { ((*ctx_vtbl).get_response)(context) };
    if resp.is_null() {
        return;
    }
    // SAFETY: resp is a live IHttpResponse; read its vtable pointer.
    let resp_vtbl = unsafe { *resp.cast::<*const IHttpResponseVtbl>() };

    // SAFETY: clear any buffered response state before writing ours.
    unsafe { ((*resp_vtbl).clear)(resp) };

    // The host expects a NUL-terminated reason phrase; build one for the call.
    let mut reason: Vec<u8> = response.reason.as_bytes().to_vec();
    reason.push(0);
    // SAFETY: resp is live; reason is a valid NUL-terminated C string;
    // sub-status 0, hrErrorToReport S_OK, no exception, fTrySkipCustomErrors 0.
    unsafe {
        ((*resp_vtbl).set_status)(resp, response.status, reason.as_ptr(), 0, S_OK, null_mut(), 0)
    };

    let header_name = b"Content-Type\0";
    let header_value = response.content_type.as_bytes();
    // SAFETY: header_name is a NUL-terminated PCSTR; header_value is a PCSTR of
    // the given length (cchHeaderValue), fReplace = TRUE.
    unsafe {
        ((*resp_vtbl).set_header_by_name)(
            resp,
            header_name.as_ptr(),
            header_value.as_ptr(),
            header_value.len() as u16,
            1,
        )
    };

    let body = response.body.as_bytes();
    if !body.is_empty() {
        let mut chunk = HttpDataChunk {
            chunk_type: HTTP_DATA_CHUNK_FROM_MEMORY,
            _pad0: 0,
            p_buffer: body.as_ptr() as *mut c_void,
            buffer_length: body.len() as u32,
            _pad1: 0,
            _tail: 0,
        };
        let mut sent: u32 = 0;
        // SAFETY: resp is live; chunk references body (alive for the synchronous
        // call); one chunk, fAsync = FALSE, fMoreData = FALSE flushes the body.
        unsafe {
            ((*resp_vtbl).write_entity_chunks)(resp, &mut chunk, 1, 0, 0, &mut sent, null_mut())
        };
    }
}

// --- Module / factory entry points -----------------------------------------

/// `IHttpModuleFactory::GetHttpModule`: vend a `wordy` `CHttpModule`, allocated
/// from the host's request memory pool when an allocator is supplied (the IIS
/// contract) and otherwise from the heap.
unsafe extern "system" fn factory_get_http_module(
    _this: *mut c_void,
    pp_module: *mut *mut c_void,
    allocator: *mut c_void,
) -> HRESULT {
    if pp_module.is_null() {
        return E_POINTER;
    }
    trace("factory: GetHttpModule");
    let module = if allocator.is_null() {
        mint_http_module()
    } else {
        // SAFETY: allocator is a live IModuleAllocator; allocate request-pool
        // memory for the module and place the vtable pointer + ownership flag.
        unsafe {
            let alloc_vtbl = *allocator.cast::<*const IModuleAllocatorVtbl>();
            let mem = ((*alloc_vtbl).allocate_memory)(
                allocator,
                core::mem::size_of::<WordyHttpModule>() as u32,
            );
            if mem.is_null() {
                return E_POINTER;
            }
            let slot = mem.cast::<WordyHttpModule>();
            (*slot).vtable = &WORDY_HTTP_MODULE_VTBL;
            (*slot).pool_allocated = true;
            mem
        }
    };
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
    trace("begin-request");
    // SAFETY: context is the host request context (tolerates null).
    let request = unsafe { decode_request(context) };
    match SERVICE.dispatch(&request) {
        Outcome::Respond(response) => {
            trace(&format!("respond status={}", response.status));
            // SAFETY: context is the host request context (tolerates null).
            unsafe { write_response(context, &response) };
            trace("response written");
            RQ_NOTIFICATION_FINISH_REQUEST
        }
        Outcome::Continue => {
            trace("continue");
            RQ_NOTIFICATION_CONTINUE
        }
    }
}

/// A per-slot diagnostic trampoline for the `CHttpModule` notification slots
/// `wordy` does not register for. `wordy` registers only `RQ_BEGIN_REQUEST`, so
/// the host should never invoke any of these; each records the slot index it was
/// dispatched at (via `WORDY_TRACE`) to diagnose a genuine host that fails to
/// dispatch `OnBeginRequest` (slot 0).
unsafe extern "system" fn notify_slot<const N: usize>(
    _this: *mut c_void,
    _context: *mut c_void,
    _provider: *mut c_void,
) -> i32 {
    trace(&format!("notify-slot {N} dispatched"));
    RQ_NOTIFICATION_CONTINUE
}

/// `CHttpModule::Dispose`: reclaim the module allocation. Heap-minted modules
/// are freed here; request-pool modules are freed by the host, so this is a
/// no-op for them.
unsafe extern "system" fn module_dispose(this: *mut c_void) {
    if this.is_null() {
        return;
    }
    trace("module dispose");
    // SAFETY: this is a live WordyHttpModule; read whether the host pool owns it.
    let pool_allocated = unsafe { (*this.cast::<WordyHttpModule>()).pool_allocated };
    if !pool_allocated {
        // SAFETY: this is a heap module minted by mint_http_module; the host calls
        // Dispose exactly once.
        drop(unsafe { Box::from_raw(this.cast::<WordyHttpModule>()) });
    }
}

/// `RegisterModule`: the IIS native-module entry point IIS calls when it loads
/// `wordy.dll`. It mints a `wordy` module factory and registers it for the
/// begin-request notification. On failure the factory is reclaimed and the
/// host's HRESULT propagated.
#[unsafe(no_mangle)]
pub extern "system" fn RegisterModule(
    _dw_server_version: u32,
    p_module_info: *mut c_void,
    _p_global_info: *mut c_void,
) -> HRESULT {
    if p_module_info.is_null() {
        return E_POINTER;
    }
    trace("RegisterModule: called");
    let factory = mint_module_factory();
    // SAFETY: p_module_info points at a live IHttpModuleRegistrationInfo whose
    // first field is its vtable pointer (the host owns it for the call).
    let vtbl = unsafe { *p_module_info.cast::<*const IHttpModuleRegistrationInfoVtbl>() };
    // SAFETY: vtbl is the host registration-info vtable; register our factory for
    // the begin-request notification (no post-notifications).
    let hr = unsafe {
        ((*vtbl).set_request_notifications)(p_module_info, factory, RQ_BEGIN_REQUEST, 0)
    };
    trace(&format!("RegisterModule: set_request_notifications hr=0x{:08X}", hr as u32));
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
    use core::cell::Cell;
    use core::ptr::{null, null_mut};

    /// A no-op stub for any pinned vtable slot the tests do not exercise.
    unsafe extern "system" fn vt_stub() {}

    // --- Emulated `IHttpModuleRegistrationInfo` ---------------------------

    #[repr(C)]
    struct FakeRegInfo {
        vtable: *const IHttpModuleRegistrationInfoVtbl,
        captured_factory: *mut c_void,
        captured_notifications: u32,
        captured_post: u32,
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

    static FAKE_REG_INFO_VTBL: IHttpModuleRegistrationInfoVtbl = IHttpModuleRegistrationInfoVtbl {
        get_name: vt_stub,
        get_id: vt_stub,
        set_request_notifications: fake_set_request_notifications,
        set_global_notifications: vt_stub,
        set_priority_for_request_notification: vt_stub,
        set_priority_for_global_notification: vt_stub,
    };

    // --- Emulated `IHttpRequest` (+ HTTP_REQUEST) -------------------------

    #[repr(C)]
    struct FakeRequest {
        vtable: *const IHttpRequestVtbl,
        raw: *mut HttpRequestHead,
        method: *const u8,
        user: *const u8,
        user_len: u16,
        body: &'static [u8],
        body_cursor: Cell<usize>,
    }

    unsafe extern "system" fn fake_get_raw_http_request(this: *mut c_void) -> *mut HttpRequestHead {
        // SAFETY: this is a live FakeRequest.
        unsafe { (*this.cast::<FakeRequest>()).raw }
    }
    unsafe extern "system" fn fake_get_http_method(this: *mut c_void) -> *const u8 {
        // SAFETY: this is a live FakeRequest.
        unsafe { (*this.cast::<FakeRequest>()).method }
    }
    unsafe extern "system" fn fake_get_header(
        this: *mut c_void,
        _name: *const u8,
        pcch: *mut u16,
    ) -> *const u8 {
        // The emulated host carries only the one header `wordy` reads.
        // SAFETY: this is a live FakeRequest; pcch is a valid u16 slot.
        unsafe {
            let req = &*this.cast::<FakeRequest>();
            if req.user.is_null() {
                return null();
            }
            *pcch = req.user_len;
            req.user
        }
    }
    unsafe extern "system" fn fake_read_entity_body(
        this: *mut c_void,
        buffer: *mut c_void,
        cb: u32,
        _async: i32,
        pcb_read: *mut u32,
        _pending: *mut i32,
    ) -> HRESULT {
        // SAFETY: this is a live FakeRequest; buffer holds cb writable bytes.
        unsafe {
            let req = &*this.cast::<FakeRequest>();
            let cursor = req.body_cursor.get();
            let remaining = req.body.len() - cursor;
            if remaining == 0 {
                // HRESULT_FROM_WIN32(ERROR_HANDLE_EOF).
                return 0x8007_0026u32 as HRESULT;
            }
            let n = remaining.min(cb as usize);
            core::ptr::copy_nonoverlapping(
                req.body.as_ptr().add(cursor),
                buffer.cast::<u8>(),
                n,
            );
            req.body_cursor.set(cursor + n);
            *pcb_read = n as u32;
        }
        S_OK
    }
    unsafe extern "system" fn fake_get_remaining_entity_bytes(this: *mut c_void) -> u32 {
        // SAFETY: this is a live FakeRequest.
        unsafe {
            let req = &*this.cast::<FakeRequest>();
            (req.body.len() - req.body_cursor.get()) as u32
        }
    }

    static FAKE_REQUEST_VTBL: IHttpRequestVtbl = IHttpRequestVtbl {
        get_raw_http_request: fake_get_raw_http_request,
        get_raw_http_request_const: vt_stub,
        get_header_by_name: fake_get_header,
        get_header_by_id: vt_stub,
        set_header_by_name: vt_stub,
        set_header_by_id: vt_stub,
        delete_header_by_name: vt_stub,
        delete_header_by_id: vt_stub,
        get_http_method: fake_get_http_method,
        set_http_method: vt_stub,
        set_url_wide: vt_stub,
        set_url_ansi: vt_stub,
        get_url_changed: vt_stub,
        get_forwarded_url: vt_stub,
        get_local_address: vt_stub,
        get_remote_address: vt_stub,
        read_entity_body: fake_read_entity_body,
        insert_entity_body: vt_stub,
        get_remaining_entity_bytes: fake_get_remaining_entity_bytes,
    };

    // --- Emulated `IHttpResponse` -----------------------------------------

    #[repr(C)]
    struct FakeResponse {
        vtable: *const IHttpResponseVtbl,
        cleared: bool,
        captured_status: u16,
        captured_content_type: Vec<u8>,
        captured_body: Vec<u8>,
    }

    unsafe extern "system" fn fake_clear(this: *mut c_void) {
        // SAFETY: this is a live FakeResponse the test owns.
        unsafe { (*this.cast::<FakeResponse>()).cleared = true };
    }
    unsafe extern "system" fn fake_set_status(
        this: *mut c_void,
        status: u16,
        _reason: *const u8,
        _sub: u16,
        _hr: HRESULT,
        _exc: *mut c_void,
        _skip: i32,
    ) -> HRESULT {
        // SAFETY: this is a live FakeResponse the test owns.
        unsafe { (*this.cast::<FakeResponse>()).captured_status = status };
        S_OK
    }
    unsafe extern "system" fn fake_set_header(
        this: *mut c_void,
        _name: *const u8,
        value: *const u8,
        cch: u16,
        _replace: i32,
    ) -> HRESULT {
        // SAFETY: this is a live FakeResponse; value holds cch bytes.
        unsafe {
            let bytes = core::slice::from_raw_parts(value, cch as usize);
            (*this.cast::<FakeResponse>()).captured_content_type = bytes.to_vec();
        }
        S_OK
    }
    unsafe extern "system" fn fake_write_entity_chunks(
        this: *mut c_void,
        chunks: *mut HttpDataChunk,
        n: u32,
        _async: i32,
        _more: i32,
        pcb_sent: *mut u32,
        _pending: *mut i32,
    ) -> HRESULT {
        // SAFETY: chunks points at n live HTTP_DATA_CHUNKs; copy each from-memory
        // buffer into the capture.
        unsafe {
            let resp = &mut *this.cast::<FakeResponse>();
            let mut sent = 0u32;
            for i in 0..n as usize {
                let chunk = &*chunks.add(i);
                if chunk.chunk_type == HTTP_DATA_CHUNK_FROM_MEMORY && !chunk.p_buffer.is_null() {
                    let bytes = core::slice::from_raw_parts(
                        chunk.p_buffer.cast::<u8>(),
                        chunk.buffer_length as usize,
                    );
                    resp.captured_body.extend_from_slice(bytes);
                    sent += chunk.buffer_length;
                }
            }
            *pcb_sent = sent;
        }
        S_OK
    }

    static FAKE_RESPONSE_VTBL: IHttpResponseVtbl = IHttpResponseVtbl {
        get_raw_http_response: vt_stub,
        get_raw_http_response_const: vt_stub,
        get_cache_policy: vt_stub,
        set_status: fake_set_status,
        set_header_by_name: fake_set_header,
        set_header_by_id: vt_stub,
        delete_header_by_name: vt_stub,
        delete_header_by_id: vt_stub,
        get_header_by_name: vt_stub,
        get_header_by_id: vt_stub,
        clear: fake_clear,
        clear_headers: vt_stub,
        set_need_disconnect: vt_stub,
        reset_connection: vt_stub,
        disable_kernel_cache: vt_stub,
        get_kernel_cache_enabled: vt_stub,
        suppress_headers: vt_stub,
        get_headers_suppressed: vt_stub,
        flush: vt_stub,
        redirect: vt_stub,
        write_entity_chunk_by_reference: vt_stub,
        write_entity_chunks: fake_write_entity_chunks,
    };

    // --- Emulated `IHttpContext` ------------------------------------------

    #[repr(C)]
    struct FakeContext {
        vtable: *const IHttpContextVtbl,
        request: *mut c_void,
        response: *mut c_void,
        completion_bytes: Cell<Option<u32>>,
    }

    unsafe extern "system" fn fake_get_request(this: *mut c_void) -> *mut c_void {
        // SAFETY: this is a live FakeContext.
        unsafe { (*this.cast::<FakeContext>()).request }
    }
    unsafe extern "system" fn fake_get_response(this: *mut c_void) -> *mut c_void {
        // SAFETY: this is a live FakeContext.
        unsafe { (*this.cast::<FakeContext>()).response }
    }
    unsafe extern "system" fn fake_post_completion(this: *mut c_void, bytes: u32) -> HRESULT {
        // SAFETY: this is a live FakeContext.
        unsafe { (*this.cast::<FakeContext>()).completion_bytes.set(Some(bytes)) };
        S_OK
    }

    static FAKE_CONTEXT_VTBL: IHttpContextVtbl = IHttpContextVtbl {
        get_site: vt_stub,
        get_application: vt_stub,
        get_connection: vt_stub,
        get_request: fake_get_request,
        get_response: fake_get_response,
        get_response_headers_sent: vt_stub,
        get_user: vt_stub,
        get_module_context_container: vt_stub,
        indicate_completion: vt_stub,
        post_completion: fake_post_completion,
    };

    /// Build an emulated host context for the given method, raw URL, body, and
    /// optional user header, returning the owned objects the caller keeps alive.
    fn make_context(
        method: &'static [u8],
        raw_url: &'static [u8],
        body: &'static [u8],
        user: Option<&'static [u8]>,
    ) -> (
        Box<HttpRequestHead>,
        Box<FakeRequest>,
        Box<FakeResponse>,
        Box<FakeContext>,
    ) {
        let mut raw = Box::new(HttpRequestHead {
            flags: 0,
            _pad0: 0,
            connection_id: 0,
            request_id: 0,
            url_context: 0,
            version_major: 1,
            version_minor: 1,
            verb: 0,
            unknown_verb_length: 0,
            raw_url_length: (raw_url.len() - 1) as u16, // exclude trailing NUL
            _pad1: 0,
            p_unknown_verb: null(),
            p_raw_url: raw_url.as_ptr(),
        });
        let mut request = Box::new(FakeRequest {
            vtable: &FAKE_REQUEST_VTBL,
            raw: raw.as_mut() as *mut HttpRequestHead,
            method: method.as_ptr(),
            user: user.map_or(null(), <[u8]>::as_ptr),
            user_len: user.map_or(0, |u| u.len() as u16),
            body,
            body_cursor: Cell::new(0),
        });
        let mut response = Box::new(FakeResponse {
            vtable: &FAKE_RESPONSE_VTBL,
            cleared: false,
            captured_status: 0,
            captured_content_type: Vec::new(),
            captured_body: Vec::new(),
        });
        let context = Box::new(FakeContext {
            vtable: &FAKE_CONTEXT_VTBL,
            request: (request.as_mut() as *mut FakeRequest).cast(),
            response: (response.as_mut() as *mut FakeResponse).cast(),
            completion_bytes: Cell::new(None),
        });
        (raw, request, response, context)
    }

    #[test]
    fn post_completion_reaches_the_host_context() {
        let (_raw, _req, _resp, context) = make_context(b"GET\0", b"/healthz\0", b"", None);
        let ctx_ptr = (context.as_ref() as *const FakeContext as *mut FakeContext).cast();
        // SAFETY: ctx_ptr is the live emulated context for the duration of the call.
        let hr = unsafe { post_completion(ctx_ptr, 4096) };
        assert_eq!(hr, S_OK);
        assert_eq!(context.completion_bytes.get(), Some(4096));
    }

    /// Drive `OnBeginRequest` for the given request, returning the captured
    /// response and the notification status.
    fn drive(
        method: &'static [u8],
        raw_url: &'static [u8],
        body: &'static [u8],
        user: Option<&'static [u8]>,
    ) -> (Box<FakeResponse>, i32) {
        let module = mint_http_module();
        // SAFETY: module is a live WordyHttpModule.
        let mvtbl = unsafe { &*(*module.cast::<*const CHttpModuleVtbl>()) };

        let (_raw, _req, response, context) = make_context(method, raw_url, body, user);
        let ctx_ptr = (context.as_ref() as *const FakeContext as *mut FakeContext).cast();

        // SAFETY: invoke OnBeginRequest (slot 0) on the live emulated context.
        let status = unsafe { (mvtbl.notifications[0])(module, ctx_ptr, null_mut()) };
        // SAFETY: release the module via Dispose (slot 29) exactly once.
        unsafe { (mvtbl.dispose)(module) };
        (response, status)
    }

    #[test]
    fn chttpmodule_vtable_has_thirty_slots() {
        // 29 notification pointers + Dispose == 30 * pointer size.
        assert_eq!(
            core::mem::size_of::<CHttpModuleVtbl>(),
            30 * core::mem::size_of::<usize>()
        );
    }

    #[test]
    fn register_module_registers_for_begin_request() {
        let mut info = FakeRegInfo {
            vtable: &FAKE_REG_INFO_VTBL,
            captured_factory: null_mut(),
            captured_notifications: 0,
            captured_post: 0,
        };
        let hr = RegisterModule(0, (&mut info as *mut FakeRegInfo).cast(), null_mut());
        assert_eq!(hr, S_OK);
        assert!(!info.captured_factory.is_null());
        assert_eq!(info.captured_notifications, RQ_BEGIN_REQUEST);
        assert_eq!(info.captured_post, 0);

        // Release the captured factory via Terminate exactly once.
        let factory = info.captured_factory;
        // SAFETY: factory is a live WordyModuleFactory minted by RegisterModule.
        let fvtbl = unsafe { &*(*factory.cast::<*const IHttpModuleFactoryVtbl>()) };
        // SAFETY: terminate the factory exactly once.
        unsafe { (fvtbl.terminate)(factory) };
    }

    #[test]
    fn register_module_rejects_a_null_registration_info() {
        assert_eq!(RegisterModule(0, null_mut(), null_mut()), E_POINTER);
    }

    #[test]
    fn factory_vends_and_disposes_a_module() {
        let factory = mint_module_factory();
        // SAFETY: factory is a live WordyModuleFactory.
        let fvtbl = unsafe { &*(*factory.cast::<*const IHttpModuleFactoryVtbl>()) };
        let mut module: *mut c_void = null_mut();
        // SAFETY: GetHttpModule writes a live module pointer into `module`.
        let hr = unsafe { (fvtbl.get_http_module)(factory, &mut module, null_mut()) };
        assert_eq!(hr, S_OK);
        assert!(!module.is_null());
        // SAFETY: Dispose then Terminate, each exactly once.
        let mvtbl = unsafe { &*(*module.cast::<*const CHttpModuleVtbl>()) };
        unsafe { (mvtbl.dispose)(module) };
        unsafe { (fvtbl.terminate)(factory) };
    }

    #[test]
    fn health_request_finishes_with_a_json_status_body() {
        let (response, status) = drive(b"GET\0", b"/healthz\0", b"", None);
        assert_eq!(status, RQ_NOTIFICATION_FINISH_REQUEST);
        assert_eq!(response.captured_status, routes::STATUS_OK);
        assert!(response.cleared);
        assert_eq!(
            response.captured_content_type.as_slice(),
            routes::CONTENT_TYPE_JSON.as_bytes()
        );
        let body = String::from_utf8(response.captured_body.clone()).unwrap();
        assert!(body.contains("\"status\""));
        assert!(body.contains("ok"));
    }

    #[test]
    fn spellcheck_request_reads_body_and_writes_json() {
        let body = br#"{"words":["hello","helo"]}"#;
        let (response, status) = drive(b"POST\0", b"/spellcheck\0", body, None);
        assert_eq!(status, RQ_NOTIFICATION_FINISH_REQUEST);
        assert_eq!(response.captured_status, routes::STATUS_OK);
        let text = String::from_utf8(response.captured_body.clone()).unwrap();
        let value: serde_json::Value = serde_json::from_str(&text).unwrap();
        assert_eq!(value["results"][0]["correct"], true);
        assert_eq!(value["results"][1]["correct"], false);
    }

    #[test]
    fn unknown_request_continues_without_writing_status() {
        let (response, status) = drive(b"GET\0", b"/nope\0", b"", None);
        assert_eq!(status, RQ_NOTIFICATION_CONTINUE);
        assert_eq!(response.captured_status, 0);
        assert!(!response.cleared);
    }

    #[test]
    fn on_begin_request_tolerates_a_null_context() {
        let module = mint_http_module();
        // SAFETY: module is a live WordyHttpModule.
        let mvtbl = unsafe { &*(*module.cast::<*const CHttpModuleVtbl>()) };
        // SAFETY: a null context is explicitly handled; the dispatcher declines.
        let status = unsafe { (mvtbl.notifications[0])(module, null_mut(), null_mut()) };
        assert_eq!(status, RQ_NOTIFICATION_CONTINUE);
        // SAFETY: release the module exactly once.
        unsafe { (mvtbl.dispose)(module) };
    }
}
