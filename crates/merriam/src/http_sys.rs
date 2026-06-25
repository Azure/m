// Copyright (c) Microsoft Corporation.

//! The HTTP Server API (http.sys) inbound edge for `merriam` (MER-D5).
//!
//! This is `merriam`'s only `unsafe` module (the peer of `wordy`'s `iis`
//! boundary). [`Server`] initializes the HTTP Server API, binds a URL, and runs
//! a receive loop that decodes each inbound `HTTP_REQUEST_V2` into a host-
//! agnostic [`HttpRequest`], asks the [`Service`] to dispatch it, and writes the
//! resulting response with `HttpSendHttpResponse`. No raw pointer or http.sys
//! type escapes this module.
//!
//! Dispatch is **synchronous** within the receive loop (MER-D5): `merriam`'s
//! store already drives native async overlapped file I/O internally (MER-D3), so
//! the headline async requirement is met without an additional thread-pool
//! offload at the inbound edge — which keeps the `unsafe` listener small and
//! verifiable. None of `merriam`'s routes read a request body, so the listener
//! never touches entity chunks.
//!
//! Binding requires a URL reservation (urlacl) or elevation; without one,
//! `HttpAddUrlToUrlGroup` returns `ERROR_ACCESS_DENIED`, which [`Server::bind`]
//! surfaces so callers (the gated integration test) can SKIP.

use core::ffi::c_void;
use std::sync::atomic::{AtomicBool, Ordering};

use windows_sys::Win32::Foundation::{ERROR_MORE_DATA, HANDLE};
use windows_sys::Win32::Networking::HttpServer::{
    HTTP_BINDING_INFO, HTTP_DATA_CHUNK, HTTP_DATA_CHUNK_0_3, HTTP_KNOWN_HEADER, HTTP_PROPERTY_FLAGS,
    HTTP_REQUEST_HEADERS, HTTP_REQUEST_V2, HTTP_RESPONSE_V2, HTTPAPI_VERSION, HTTP_INITIALIZE_SERVER,
    HttpAddUrlToUrlGroup, HttpCloseRequestQueue, HttpCloseServerSession, HttpCloseUrlGroup,
    HttpCreateRequestQueue, HttpCreateServerSession, HttpCreateUrlGroup, HttpDataChunkFromMemory,
    HttpHeaderContentType, HttpInitialize, HttpReceiveHttpRequest, HttpRemoveUrlFromUrlGroup,
    HttpSendHttpResponse, HttpServerBindingProperty, HttpSetUrlGroupProperty, HttpTerminate,
    HttpVerbDELETE, HttpVerbGET, HttpVerbHEAD, HttpVerbPOST, HttpVerbPUT,
};
use windows_sys::core::PCSTR;

use crate::routes::{HttpRequest, HttpResponse, Outcome, Service};

/// `ERROR_ACCESS_DENIED` — `HttpAddUrlToUrlGroup` without a urlacl reservation.
pub const ERROR_ACCESS_DENIED: u32 = 5;

/// The HTTP Server API v2 version stamp.
const HTTP_VERSION_2: HTTPAPI_VERSION = HTTPAPI_VERSION {
    HttpApiMajorVersion: 2,
    HttpApiMinorVersion: 0,
};

/// The `HTTP_PROPERTY_FLAGS` "present" bit.
const PROPERTY_PRESENT: u32 = 1;

/// Bytes for the inbound request buffer (8 KiB), allocated `u64`-aligned so the
/// `HTTP_REQUEST_V2` cast is valid; ample for `merriam`'s small requests.
const REQUEST_BUFFER_U64S: usize = 1024;

/// An http.sys server bound to one URL.
pub struct Server {
    session: u64,
    group: u64,
    queue: HANDLE,
    /// The NUL-terminated wide URL, retained for `HttpRemoveUrlFromUrlGroup`.
    url: Vec<u16>,
}

// The http.sys session/group ids and request-queue handle are process-global
// kernel objects safe to use from any thread; the receive loop runs on one
// thread while the binding is shared by reference. SAFETY: no Rust aliasing of
// the handle's pointee; ownership/teardown is single-threaded via Drop.
unsafe impl Send for Server {}
unsafe impl Sync for Server {}

impl Server {
    /// Initialize the HTTP Server API and bind `url` (e.g.
    /// `"http://127.0.0.1:8080/"`, trailing slash required).
    ///
    /// # Errors
    /// The http.sys `WIN32_ERROR` of the first failing step. In particular
    /// [`ERROR_ACCESS_DENIED`] from `HttpAddUrlToUrlGroup` means the URL is not
    /// reserved for this user (gate / SKIP).
    pub fn bind(url: &str) -> Result<Server, u32> {
        let wide: Vec<u16> = url.encode_utf16().chain(std::iter::once(0)).collect();

        // SAFETY: each call is checked; on failure we tear down only the stages
        // that succeeded, in reverse order, before returning the error.
        unsafe {
            let rc = HttpInitialize(HTTP_VERSION_2, HTTP_INITIALIZE_SERVER, core::ptr::null_mut());
            if rc != 0 {
                return Err(rc);
            }

            let mut session = 0u64;
            let rc = HttpCreateServerSession(HTTP_VERSION_2, &mut session, 0);
            if rc != 0 {
                HttpTerminate(HTTP_INITIALIZE_SERVER, core::ptr::null_mut());
                return Err(rc);
            }

            let mut group = 0u64;
            let rc = HttpCreateUrlGroup(session, &mut group, 0);
            if rc != 0 {
                HttpCloseServerSession(session);
                HttpTerminate(HTTP_INITIALIZE_SERVER, core::ptr::null_mut());
                return Err(rc);
            }

            let mut queue: HANDLE = core::ptr::null_mut();
            let rc = HttpCreateRequestQueue(
                HTTP_VERSION_2,
                core::ptr::null(),
                core::ptr::null(),
                0,
                &mut queue,
            );
            if rc != 0 {
                HttpCloseUrlGroup(group);
                HttpCloseServerSession(session);
                HttpTerminate(HTTP_INITIALIZE_SERVER, core::ptr::null_mut());
                return Err(rc);
            }

            let binding = HTTP_BINDING_INFO {
                Flags: HTTP_PROPERTY_FLAGS { _bitfield: PROPERTY_PRESENT },
                RequestQueueHandle: queue,
            };
            let rc = HttpSetUrlGroupProperty(
                group,
                HttpServerBindingProperty,
                (&binding as *const HTTP_BINDING_INFO).cast::<c_void>(),
                core::mem::size_of::<HTTP_BINDING_INFO>() as u32,
            );
            if rc != 0 {
                HttpCloseRequestQueue(queue);
                HttpCloseUrlGroup(group);
                HttpCloseServerSession(session);
                HttpTerminate(HTTP_INITIALIZE_SERVER, core::ptr::null_mut());
                return Err(rc);
            }

            let rc = HttpAddUrlToUrlGroup(group, wide.as_ptr(), 0, 0);
            if rc != 0 {
                HttpCloseRequestQueue(queue);
                HttpCloseUrlGroup(group);
                HttpCloseServerSession(session);
                HttpTerminate(HTTP_INITIALIZE_SERVER, core::ptr::null_mut());
                return Err(rc);
            }

            Ok(Server { session, group, queue, url: wide })
        }
    }

    /// Run the receive/dispatch/respond loop until `stop` is set.
    ///
    /// The loop blocks in `HttpReceiveHttpRequest`; to stop it, set `stop` and
    /// then send one more request to the bound URL (a "poke") so the pending
    /// receive returns and the loop observes the flag.
    pub fn serve(&self, service: &Service, stop: &AtomicBool) {
        let mut buffer = vec![0u64; REQUEST_BUFFER_U64S];
        loop {
            if stop.load(Ordering::SeqCst) {
                break;
            }
            let mut bytes = 0u32;
            // SAFETY: `buffer` is u64-aligned and `buffer.len()*8` bytes long; the
            // request id 0 receives the next request; no overlapped I/O.
            let rc = unsafe {
                HttpReceiveHttpRequest(
                    self.queue,
                    0,
                    0,
                    buffer.as_mut_ptr().cast::<HTTP_REQUEST_V2>(),
                    (buffer.len() * core::mem::size_of::<u64>()) as u32,
                    &mut bytes,
                    core::ptr::null_mut(),
                )
            };
            if stop.load(Ordering::SeqCst) {
                break;
            }
            if rc == ERROR_MORE_DATA {
                let needed = bytes as usize / core::mem::size_of::<u64>() + 1;
                buffer = vec![0u64; needed.max(REQUEST_BUFFER_U64S)];
                continue;
            }
            if rc != 0 {
                // Queue closed / aborted / transport error: stop serving.
                break;
            }

            // SAFETY: a successful receive filled `buffer` with a valid
            // HTTP_REQUEST_V2 plus its trailing strings (still live until the
            // next receive); we copy out everything we need before looping.
            let request = unsafe { &*buffer.as_ptr().cast::<HTTP_REQUEST_V2>() };
            let request_id = request.Base.RequestId;
            let decoded = decode_request(request);

            let response = match service.dispatch(&decoded) {
                Outcome::Respond(r) => r,
                Outcome::Continue => not_found(),
            };
            // SAFETY: `request_id` came from the received request; the response
            // byte buffers outlive the send call.
            unsafe { self.send_response(request_id, &response) };
        }
    }

    /// Send `response` for `request_id`.
    ///
    /// # Safety
    /// `request_id` must be a request just received on this queue; the response's
    /// byte buffers must outlive the call (they are borrowed for its duration).
    unsafe fn send_response(&self, request_id: u64, response: &HttpResponse) {
        let reason = response.reason.as_bytes();
        let ctype = response.content_type.as_bytes();
        let body = response.body.as_bytes();

        // SAFETY: zeroed is a valid initial HTTP_DATA_CHUNK; we then set the
        // from-memory union arm to point at `body`, which outlives the send.
        let mut chunk: HTTP_DATA_CHUNK = unsafe { core::mem::zeroed() };
        chunk.DataChunkType = HttpDataChunkFromMemory;
        chunk.Anonymous.FromMemory = HTTP_DATA_CHUNK_0_3 {
            pBuffer: body.as_ptr() as *mut c_void,
            BufferLength: body.len() as u32,
        };

        // SAFETY: zeroed is a valid initial HTTP_RESPONSE_V2.
        let mut resp: HTTP_RESPONSE_V2 = unsafe { core::mem::zeroed() };
        resp.Base.StatusCode = response.status;
        resp.Base.pReason = reason.as_ptr();
        resp.Base.ReasonLength = reason.len() as u16;
        resp.Base.Headers.KnownHeaders[HttpHeaderContentType as usize] = HTTP_KNOWN_HEADER {
            RawValueLength: ctype.len() as u16,
            pRawValue: ctype.as_ptr(),
        };
        resp.Base.EntityChunkCount = 1;
        resp.Base.pEntityChunks = &mut chunk;

        let mut sent = 0u32;
        // SAFETY: live queue + request id; `resp` and the buffers it references
        // remain valid for the duration of this synchronous call.
        unsafe {
            HttpSendHttpResponse(
                self.queue,
                request_id,
                0,
                &resp,
                core::ptr::null(),
                &mut sent,
                core::ptr::null(),
                0,
                core::ptr::null_mut(),
                core::ptr::null(),
            );
        }
    }
}

impl Drop for Server {
    fn drop(&mut self) {
        // SAFETY: each id/handle was created by `bind`; teardown is in reverse
        // order and runs once (single-threaded ownership).
        unsafe {
            HttpRemoveUrlFromUrlGroup(self.group, self.url.as_ptr(), 0);
            HttpCloseUrlGroup(self.group);
            HttpCloseRequestQueue(self.queue);
            HttpCloseServerSession(self.session);
            HttpTerminate(HTTP_INITIALIZE_SERVER, core::ptr::null_mut());
        }
    }
}

/// Decode a received `HTTP_REQUEST_V2` into a host-agnostic [`HttpRequest`]
/// (copying out every string before the buffer is reused). `merriam` reads no
/// body, so entity chunks are ignored.
fn decode_request(request: &HTTP_REQUEST_V2) -> HttpRequest {
    let method = verb_to_method(request);
    // SAFETY: pRawUrl/RawUrlLength describe a valid UTF-8 run inside the buffer.
    let url = unsafe { pcstr_to_string(request.Base.pRawUrl, request.Base.RawUrlLength as usize) };
    // SAFETY: the headers block is part of the received request.
    let user = unsafe { find_unknown_header(&request.Base.Headers, crate::routes::USER_HEADER) };
    let locale = unsafe { find_unknown_header(&request.Base.Headers, crate::routes::LOCALE_HEADER) };
    HttpRequest { method, url, body: String::new(), user, locale }
}

/// Map the request verb to an HTTP method string.
fn verb_to_method(request: &HTTP_REQUEST_V2) -> String {
    // NB: the `HttpVerb*` values are bare-identifier consts, so they must be
    // compared by value — a `match` arm would bind, not pattern-match them.
    let verb = request.Base.Verb;
    if verb == HttpVerbGET {
        "GET".to_string()
    } else if verb == HttpVerbPOST {
        "POST".to_string()
    } else if verb == HttpVerbPUT {
        "PUT".to_string()
    } else if verb == HttpVerbDELETE {
        "DELETE".to_string()
    } else if verb == HttpVerbHEAD {
        "HEAD".to_string()
    } else {
        // SAFETY: for an unknown verb http.sys supplies pUnknownVerb/length.
        unsafe {
            pcstr_to_string(request.Base.pUnknownVerb, request.Base.UnknownVerbLength as usize)
        }
    }
}

/// Copy a `(PCSTR, len)` UTF-8 run into an owned `String` (lossy).
///
/// # Safety
/// `ptr` must be null or point to `len` readable bytes.
unsafe fn pcstr_to_string(ptr: PCSTR, len: usize) -> String {
    if ptr.is_null() || len == 0 {
        return String::new();
    }
    // SAFETY: caller guarantees `len` readable bytes at `ptr`.
    let slice = unsafe { core::slice::from_raw_parts(ptr, len) };
    String::from_utf8_lossy(slice).into_owned()
}

/// Find an unknown request header by (case-insensitive) name.
///
/// # Safety
/// `headers` must reference a received request's header block.
unsafe fn find_unknown_header(headers: &HTTP_REQUEST_HEADERS, name: &str) -> Option<String> {
    let count = headers.UnknownHeaderCount as usize;
    if count == 0 || headers.pUnknownHeaders.is_null() {
        return None;
    }
    // SAFETY: `pUnknownHeaders` points to `count` headers in the request buffer.
    let unknown = unsafe { core::slice::from_raw_parts(headers.pUnknownHeaders, count) };
    for header in unknown {
        // SAFETY: each header's name/value spans live in the request buffer.
        let header_name = unsafe { pcstr_to_string(header.pName, header.NameLength as usize) };
        if header_name.eq_ignore_ascii_case(name) {
            // SAFETY: as above.
            return Some(unsafe {
                pcstr_to_string(header.pRawValue, header.RawValueLength as usize)
            });
        }
    }
    None
}

/// The `404` response for an unhandled route.
fn not_found() -> HttpResponse {
    HttpResponse {
        status: 404,
        reason: "Not Found",
        content_type: crate::routes::CONTENT_TYPE_JSON,
        body: r#"{"error":"not found"}"#.to_string(),
    }
}
