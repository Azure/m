// Copyright (c) Microsoft Corporation.

//! The WinHTTP egress C ABI (`mWinHttp*` entry points) — MW17 / SHIM-D22.
//!
//! Each export mirrors a `winhttp.dll` prototype so the link-time alias (SHIM-D4)
//! redirects an unmodified client's WinHTTP imports here. A body marshals raw
//! caller pointers into owned Rust values and drives the process-wide session's
//! egress engine ([`EgressEngine`](crate::egress_engine::EgressEngine)); the
//! engine reassembles the request lifecycle and serves the response from the
//! `.pilcfg`-selected backing. No real WinHTTP is called from here — the live
//! network is reached only by the engine's `LiveEgress` bottom.
//!
//! Per SHIM-D2 this is the only place raw caller pointers are touched, so the
//! module opts back into `unsafe_code` and allows the two FFI-boundary lints (as
//! in [`mwinfile`](crate::mwinfile)).

#![allow(unsafe_code)]
#![allow(clippy::not_unsafe_ptr_arg_deref, clippy::too_many_arguments)]

use core::ffi::c_void;

use windows_platform_isolation::{EgressError, Utf16};
use windows_sys::Win32::Foundation::{BOOL, ERROR_INSUFFICIENT_BUFFER, FALSE, TRUE};
use windows_sys::Win32::Networking::WinHttp::{
    WINHTTP_FLAG_SECURE, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_QUERY_CONTENT_TYPE,
    WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_QUERY_RAW_HEADERS_CRLF,
};

use crate::error_map::set_last_error;
use crate::session::session;

/// `ERROR_WINHTTP_CANNOT_CONNECT` — reported when a blocking backing denies a send.
const ERROR_WINHTTP_CANNOT_CONNECT: u32 = 12029;
/// `ERROR_WINHTTP_HEADER_NOT_FOUND` — reported when a queried header is absent.
const ERROR_WINHTTP_HEADER_NOT_FOUND: u32 = 12150;
/// `ERROR_WINHTTP_INTERNAL_ERROR` — a catch-all for an unexpected surface error.
const ERROR_WINHTTP_INTERNAL_ERROR: u32 = 12004;
/// The `dwHeadersLength` sentinel meaning "the string is NUL-terminated" (`-1`).
const WINHTTP_AUTO_LENGTH: u32 = u32::MAX;
/// Mask isolating a `WINHTTP_QUERY_*` header id from its modifier flag bits.
const WINHTTP_QUERY_ID_MASK: u32 = 0x0000_FFFF;
/// Bytes occupied by one UTF-16 code unit.
const WCHAR_BYTES: usize = core::mem::size_of::<u16>();

// --- Pointer marshaling helpers ---------------------------------------------

/// Read a NUL-terminated wide string into owned units (excluding the NUL).
///
/// # Safety
/// `p` must be null or point to a NUL-terminated `u16` sequence.
unsafe fn read_wide_nul(p: *const u16) -> Vec<u16> {
    if p.is_null() {
        return Vec::new();
    }
    // SAFETY: caller guarantees a NUL-terminated buffer.
    unsafe {
        let mut len = 0usize;
        while *p.add(len) != 0 {
            len += 1;
        }
        core::slice::from_raw_parts(p, len).to_vec()
    }
}

/// Read a wide string of `len` units, or NUL-terminated when `len` is the
/// auto-length sentinel.
///
/// # Safety
/// `p` must be null, or point to `len` `u16`s (or a NUL-terminated sequence when
/// `len == WINHTTP_AUTO_LENGTH`).
unsafe fn read_wide_len(p: *const u16, len: u32) -> Vec<u16> {
    if p.is_null() {
        return Vec::new();
    }
    if len == WINHTTP_AUTO_LENGTH {
        // SAFETY: precondition forwards to read_wide_nul.
        return unsafe { read_wide_nul(p) };
    }
    // SAFETY: caller guarantees `len` valid units.
    unsafe { core::slice::from_raw_parts(p, len as usize).to_vec() }
}

/// Read `len` raw bytes (the optional request body).
///
/// # Safety
/// `p` must be null or point to `len` bytes.
unsafe fn read_bytes(p: *const c_void, len: u32) -> Vec<u8> {
    if p.is_null() || len == 0 {
        return Vec::new();
    }
    // SAFETY: caller guarantees `len` valid bytes.
    unsafe { core::slice::from_raw_parts(p.cast::<u8>(), len as usize).to_vec() }
}

/// Parse a `"Name: Value\r\n…"` wide header blob into `(name, value)` pairs.
fn parse_header_blob(units: &[u16]) -> Vec<(Utf16, Utf16)> {
    const LF: u16 = b'\n' as u16;
    const CR: u16 = b'\r' as u16;
    const COLON: u16 = b':' as u16;
    const SP: u16 = b' ' as u16;
    let mut out = Vec::new();
    for line in units.split(|&u| u == LF) {
        let line = match line.last() {
            Some(&CR) => &line[..line.len() - 1],
            _ => line,
        };
        if line.is_empty() {
            continue;
        }
        if let Some(pos) = line.iter().position(|&u| u == COLON) {
            let name = &line[..pos];
            let mut value = &line[pos + 1..];
            if value.first() == Some(&SP) {
                value = &value[1..];
            }
            out.push((Utf16::from_units(name.to_vec()), Utf16::from_units(value.to_vec())));
        }
    }
    out
}

/// Build a `WINHTTP_QUERY_RAW_HEADERS_CRLF` blob (status line + headers + a final
/// blank line) from the captured response.
fn build_raw_headers_blob(status: u32, headers: &[(Utf16, Utf16)]) -> Vec<u16> {
    const COLON_SP: [u16; 2] = [b':' as u16, b' ' as u16];
    const CRLF: [u16; 2] = [b'\r' as u16, b'\n' as u16];
    let mut out: Vec<u16> = format!("HTTP/1.1 {status}").encode_utf16().collect();
    out.extend_from_slice(&CRLF);
    for (name, value) in headers {
        out.extend_from_slice(name.as_units());
        out.extend_from_slice(&COLON_SP);
        out.extend_from_slice(value.as_units());
        out.extend_from_slice(&CRLF);
    }
    out.extend_from_slice(&CRLF);
    out
}

/// Look up a header value (ASCII-case-insensitive name) in the response.
fn lookup_header(headers: &[(Utf16, Utf16)], name: &str) -> Option<Vec<u16>> {
    headers
        .iter()
        .find(|(n, _)| n.to_utf8().is_ok_and(|n| n.eq_ignore_ascii_case(name)))
        .map(|(_, v)| v.as_units().to_vec())
}

/// Apply the Win32 two-call wide-result buffer convention: with no buffer or a
/// too-small one, report the required byte count (including the NUL) and fail
/// with `ERROR_INSUFFICIENT_BUFFER`; otherwise copy the value plus a NUL and set
/// the actual byte count (excluding the NUL).
///
/// # Safety
/// `buffer` (when non-null) must address at least `*buf_len` bytes; `buf_len`
/// must be null or a valid `u32`.
unsafe fn write_wide_result(value: &[u16], buffer: *mut c_void, buf_len: *mut u32) -> BOOL {
    let needed = (value.len() + 1) * WCHAR_BYTES;
    // SAFETY: buf_len is null or valid per the precondition.
    let provided = if buf_len.is_null() { 0 } else { (unsafe { *buf_len }) as usize };
    if buffer.is_null() || provided < needed {
        if !buf_len.is_null() {
            // SAFETY: as above.
            unsafe { *buf_len = needed as u32 };
        }
        set_last_error(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    // SAFETY: buffer addresses >= needed bytes; we write value.len()+1 units.
    unsafe {
        let dst = buffer.cast::<u16>();
        core::ptr::copy_nonoverlapping(value.as_ptr(), dst, value.len());
        *dst.add(value.len()) = 0;
        if !buf_len.is_null() {
            *buf_len = (value.len() * WCHAR_BYTES) as u32;
        }
    }
    TRUE
}

/// Map a surface error to a WinHTTP-shaped `WIN32_ERROR` for `SetLastError`.
fn egress_error_code(e: &EgressError) -> u32 {
    match e {
        EgressError::Os(code) => *code,
        EgressError::Blocked => ERROR_WINHTTP_CANNOT_CONNECT,
        _ => ERROR_WINHTTP_INTERNAL_ERROR,
    }
}

// --- Exports -----------------------------------------------------------------

/// `WinHttpOpen`: mint a session handle (the proxy/agent arguments are accepted
/// and ignored — the egress engine owns the transport).
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpOpen(
    _agent: *const u16,
    _access_type: u32,
    _proxy: *const u16,
    _proxy_bypass: *const u16,
    _flags: u32,
) -> *mut c_void {
    let handle = session().with_egress(|engine| engine.open());
    handle as *mut c_void
}

/// `WinHttpConnect`: mint a connection handle bound to `server_name:port`.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpConnect(
    session_handle: *mut c_void,
    server_name: *const u16,
    server_port: u16,
    _reserved: u32,
) -> *mut c_void {
    // SAFETY: server_name is a caller-supplied wide string per the C ABI.
    let host = Utf16::from_units(unsafe { read_wide_nul(server_name) });
    let session_handle = session_handle as usize;
    match session().with_egress(|engine| engine.connect(session_handle, host, server_port)) {
        Some(connection) => connection as *mut c_void,
        None => core::ptr::null_mut(),
    }
}

/// `WinHttpOpenRequest`: mint a request handle on a connection.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpOpenRequest(
    connection: *mut c_void,
    verb: *const u16,
    object_name: *const u16,
    _version: *const u16,
    _referrer: *const u16,
    _accept_types: *const *const u16,
    flags: u32,
) -> *mut c_void {
    // SAFETY: verb / object_name are caller-supplied wide strings (or null).
    let verb = if verb.is_null() {
        Utf16::from_utf8("GET")
    } else {
        Utf16::from_units(unsafe { read_wide_nul(verb) })
    };
    let path = Utf16::from_units(unsafe { read_wide_nul(object_name) });
    let secure = (flags & WINHTTP_FLAG_SECURE) != 0;
    let connection = connection as usize;
    match session().with_egress(|engine| engine.open_request(connection, verb, path, secure)) {
        Some(request) => request as *mut c_void,
        None => core::ptr::null_mut(),
    }
}

/// `WinHttpAddRequestHeaders`: accumulate headers onto the pending request.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpAddRequestHeaders(
    request: *mut c_void,
    headers: *const u16,
    headers_length: u32,
    _modifiers: u32,
) -> BOOL {
    // SAFETY: headers is a caller-supplied wide string of `headers_length` units.
    let parsed = parse_header_blob(&unsafe { read_wide_len(headers, headers_length) });
    let request = request as usize;
    if session().with_egress(|engine| engine.add_headers(request, parsed)) {
        TRUE
    } else {
        FALSE
    }
}

/// `WinHttpSendRequest`: capture the transaction and run it through the backing.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpSendRequest(
    request: *mut c_void,
    headers: *const u16,
    headers_length: u32,
    optional: *const c_void,
    optional_length: u32,
    _total_length: u32,
    _context: usize,
) -> BOOL {
    // SAFETY: headers / optional are caller-supplied buffers per the C ABI.
    let extra = parse_header_blob(&unsafe { read_wide_len(headers, headers_length) });
    let body = unsafe { read_bytes(optional, optional_length) };
    let request = request as usize;
    match session().with_egress(|engine| engine.send(request, extra, body)) {
        Ok(()) => TRUE,
        Err(e) => {
            set_last_error(egress_error_code(&e));
            FALSE
        }
    }
}

/// `WinHttpReceiveResponse`: confirm the response is ready.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpReceiveResponse(
    request: *mut c_void,
    _reserved: *mut c_void,
) -> BOOL {
    let request = request as usize;
    if session().with_egress(|engine| engine.receive_response(request)).is_ok() {
        TRUE
    } else {
        set_last_error(ERROR_WINHTTP_INTERNAL_ERROR);
        FALSE
    }
}

/// `WinHttpQueryHeaders`: serve the status (numeric), the raw `CRLF` blob, or a
/// named header value from the captured response.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpQueryHeaders(
    request: *mut c_void,
    info_level: u32,
    name: *const u16,
    buffer: *mut c_void,
    buffer_length: *mut u32,
    _index: *mut u32,
) -> BOOL {
    let request = request as usize;

    // Numeric status code.
    if info_level & WINHTTP_QUERY_FLAG_NUMBER != 0 {
        let Some(status) = session().with_egress(|engine| engine.status(request)) else {
            set_last_error(ERROR_WINHTTP_HEADER_NOT_FOUND);
            return FALSE;
        };
        // SAFETY: buffer_length is null or valid.
        let provided =
            if buffer_length.is_null() { 0 } else { (unsafe { *buffer_length }) as usize };
        if buffer.is_null() || provided < core::mem::size_of::<u32>() {
            if !buffer_length.is_null() {
                // SAFETY: as above.
                unsafe { *buffer_length = core::mem::size_of::<u32>() as u32 };
            }
            set_last_error(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        // SAFETY: buffer addresses >= 4 bytes.
        unsafe {
            *buffer.cast::<u32>() = status;
            if !buffer_length.is_null() {
                *buffer_length = core::mem::size_of::<u32>() as u32;
            }
        }
        return TRUE;
    }

    let id = info_level & WINHTTP_QUERY_ID_MASK;

    // Raw header blob.
    if id == (WINHTTP_QUERY_RAW_HEADERS_CRLF & WINHTTP_QUERY_ID_MASK) {
        let Some(blob) = session().with_egress(|engine| {
            let status = engine.status(request)?;
            let headers = engine.response_headers(request)?;
            Some(build_raw_headers_blob(status, headers))
        }) else {
            set_last_error(ERROR_WINHTTP_HEADER_NOT_FOUND);
            return FALSE;
        };
        // SAFETY: buffer / buffer_length follow the two-call convention.
        return unsafe { write_wide_result(&blob, buffer, buffer_length) };
    }

    // Named header (by `name`, or a well-known content id).
    // SAFETY: name is a caller-supplied wide string or null.
    let header_name = if !name.is_null() {
        Utf16::from_units(unsafe { read_wide_nul(name) }).to_utf8().ok()
    } else if id == (WINHTTP_QUERY_CONTENT_TYPE & WINHTTP_QUERY_ID_MASK) {
        Some("Content-Type".to_string())
    } else if id == (WINHTTP_QUERY_CONTENT_LENGTH & WINHTTP_QUERY_ID_MASK) {
        Some("Content-Length".to_string())
    } else {
        None
    };
    let Some(header_name) = header_name else {
        set_last_error(ERROR_WINHTTP_HEADER_NOT_FOUND);
        return FALSE;
    };
    let value = session()
        .with_egress(|engine| engine.response_headers(request).and_then(|h| lookup_header(h, &header_name)));
    match value {
        // SAFETY: two-call convention.
        Some(units) => unsafe { write_wide_result(&units, buffer, buffer_length) },
        None => {
            set_last_error(ERROR_WINHTTP_HEADER_NOT_FOUND);
            FALSE
        }
    }
}

/// `WinHttpQueryDataAvailable`: report the unread body byte count.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpQueryDataAvailable(
    request: *mut c_void,
    bytes_available: *mut u32,
) -> BOOL {
    let request = request as usize;
    match session().with_egress(|engine| engine.data_available(request)) {
        Some(available) => {
            if !bytes_available.is_null() {
                // SAFETY: bytes_available is a valid out-pointer per the C ABI.
                unsafe { *bytes_available = available as u32 };
            }
            TRUE
        }
        None => {
            set_last_error(ERROR_WINHTTP_INTERNAL_ERROR);
            FALSE
        }
    }
}

/// `WinHttpReadData`: copy up to `to_read` body bytes into `buffer`.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpReadData(
    request: *mut c_void,
    buffer: *mut c_void,
    to_read: u32,
    bytes_read: *mut u32,
) -> BOOL {
    let request = request as usize;
    match session().with_egress(|engine| engine.read_data(request, to_read as usize)) {
        Some(bytes) => {
            // SAFETY: buffer addresses >= to_read bytes per the C ABI; we copy
            // at most that many.
            unsafe {
                if !buffer.is_null() && !bytes.is_empty() {
                    core::ptr::copy_nonoverlapping(bytes.as_ptr(), buffer.cast::<u8>(), bytes.len());
                }
                if !bytes_read.is_null() {
                    *bytes_read = bytes.len() as u32;
                }
            }
            TRUE
        }
        None => {
            set_last_error(ERROR_WINHTTP_INTERNAL_ERROR);
            FALSE
        }
    }
}

/// `WinHttpCloseHandle`: free an interned handle.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpCloseHandle(handle: *mut c_void) -> BOOL {
    let handle = handle as usize;
    if session().with_egress(|engine| engine.close(handle)) {
        TRUE
    } else {
        FALSE
    }
}

/// `WinHttpSetTimeouts`: accepted and ignored (the engine has no real transport).
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpSetTimeouts(
    _handle: *mut c_void,
    _resolve: i32,
    _connect: i32,
    _send: i32,
    _receive: i32,
) -> BOOL {
    TRUE
}

/// `WinHttpSetOption`: accepted and ignored.
#[unsafe(no_mangle)]
pub extern "system" fn mWinHttpSetOption(
    _handle: *mut c_void,
    _option: u32,
    _buffer: *const c_void,
    _buffer_length: u32,
) -> BOOL {
    TRUE
}
