// Copyright (c) Microsoft Corporation.

//! Windows implementation of the safe **egress** (WinHTTP) FFI wrapper. Every
//! `unsafe` in the live egress provider lives in this module (D1 / D13), the
//! egress analogue of the registry [`RegKey`](crate::RegKey) and filesystem
//! [`FileHandle`](crate::FileHandle) leaves.
//!
//! [`http_send`] performs **one** complete WinHTTP request/response transaction
//! from owned inputs (NUL-terminated UTF-16 host / verb / path, a pre-formatted
//! wide header blob, and a body byte slice) and returns owned outputs (status,
//! the raw `CRLF` response-header blob, and the body). No `HINTERNET` handle and
//! no raw pointer crosses this boundary; each handle is closed by a RAII guard
//! and every WinHTTP failure is mapped to a `WIN32_ERROR` code. Parsing the raw
//! header blob into pairs is left to the safe layer.

use core::ffi::c_void;

use windows::Win32::Foundation::{ERROR_INSUFFICIENT_BUFFER, GetLastError};
use windows::Win32::Networking::WinHttp::{
    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_ADDREQ_FLAG_ADD, WINHTTP_ADDREQ_FLAG_REPLACE,
    WINHTTP_FLAG_SECURE, WINHTTP_OPEN_REQUEST_FLAGS, WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_QUERY_STATUS_CODE, WinHttpAddRequestHeaders,
    WinHttpCloseHandle, WinHttpConnect, WinHttpOpen, WinHttpOpenRequest, WinHttpQueryDataAvailable,
    WinHttpQueryHeaders, WinHttpReadData, WinHttpReceiveResponse, WinHttpSendRequest,
};
use windows::core::PCWSTR;

/// A failed WinHTTP call, carrying the `WIN32_ERROR` status code (mirror of
/// [`FsError`](crate::FsError) / [`RegError`](crate::RegError)).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HttpError(pub u32);

impl HttpError {
    /// The raw `WIN32_ERROR` status code.
    #[must_use]
    pub fn code(self) -> u32 {
        self.0
    }
}

impl core::fmt::Display for HttpError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "WinHTTP error {}", self.0)
    }
}

impl std::error::Error for HttpError {}

/// The outcome of one WinHTTP transaction.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HttpReply {
    /// The HTTP status code.
    pub status: u32,
    /// The raw `WINHTTP_QUERY_RAW_HEADERS_CRLF` blob (UTF-16 code units): the
    /// status line and each header separated by `\r\n`, terminated by `\r\n\r\n`.
    /// Empty when no headers were returned. The safe layer parses this.
    pub raw_headers: Vec<u16>,
    /// The full response body.
    pub body: Vec<u8>,
}

/// The user-agent presented to the server (NUL-terminated UTF-16).
const USER_AGENT: &[u16] = &[
    b'w' as u16, b'i' as u16, b'n' as u16, b'd' as u16, b'o' as u16, b'w' as u16, b's' as u16,
    b'-' as u16, b'p' as u16, b'i' as u16, b'l' as u16, 0,
];

/// Peel the current thread's last Win32 error into an [`HttpError`].
fn last_error() -> HttpError {
    // SAFETY: GetLastError reads thread-local state and has no preconditions.
    HttpError(unsafe { GetLastError().0 })
}

/// Map a `windows::core::Error` from a BOOL-returning WinHTTP call to the raw
/// `WIN32_ERROR` code (peeling `HRESULT_FROM_WIN32`).
fn err_code(e: &windows::core::Error) -> u32 {
    let hr = e.code().0 as u32;
    if (hr & 0xFFFF_0000) == 0x8007_0000 { hr & 0x0000_FFFF } else { hr }
}

/// Owned-value form of [`err_code`] for use with `Result::map_err`.
fn core_err(e: windows::core::Error) -> HttpError {
    HttpError(err_code(&e))
}

/// A RAII owner of an `HINTERNET` handle: closes it on drop. A null handle is a
/// no-op (the failed-open sentinel).
struct Internet(*mut c_void);

impl Drop for Internet {
    fn drop(&mut self) {
        if !self.0.is_null() {
            // SAFETY: self.0 is a live HINTERNET we own; closing once on drop.
            unsafe {
                let _ = WinHttpCloseHandle(self.0);
            }
        }
    }
}

/// Perform one WinHTTP request/response transaction.
///
/// `host` / `verb` / `path` are NUL-terminated UTF-16 slices; `header_blob` is a
/// NUL-terminated UTF-16 `"Name: Value\r\nName: Value\r\n"` blob (empty — a lone
/// NUL or zero length — adds no headers); `body` is the request body.
///
/// # Errors
///
/// Returns the `WIN32_ERROR` of the first failing WinHTTP call.
pub fn http_send(
    secure: bool,
    host: &[u16],
    port: u16,
    verb: &[u16],
    path: &[u16],
    header_blob: &[u16],
    body: &[u8],
) -> Result<HttpReply, HttpError> {
    // SAFETY: every handle is null-checked before use and closed by `Internet`'s
    // Drop; all pointers passed to WinHTTP point at live, correctly-sized,
    // caller-owned buffers for the duration of each call.
    unsafe {
        let session = Internet(WinHttpOpen(
            PCWSTR(USER_AGENT.as_ptr()),
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            PCWSTR::null(),
            PCWSTR::null(),
            0,
        ));
        if session.0.is_null() {
            return Err(last_error());
        }

        let connect = Internet(WinHttpConnect(session.0, PCWSTR(host.as_ptr()), port, 0));
        if connect.0.is_null() {
            return Err(last_error());
        }

        let flags = if secure { WINHTTP_FLAG_SECURE } else { WINHTTP_OPEN_REQUEST_FLAGS(0) };
        let request = Internet(WinHttpOpenRequest(
            connect.0,
            PCWSTR(verb.as_ptr()),
            PCWSTR(path.as_ptr()),
            PCWSTR::null(),
            PCWSTR::null(),
            core::ptr::null_mut(),
            flags,
        ));
        if request.0.is_null() {
            return Err(last_error());
        }

        // The wide header blob is NUL-terminated by contract; the slice-based
        // WinHTTP wrapper measures by length, so drop the trailing NUL.
        let headers = header_blob.strip_suffix(&[0u16]).unwrap_or(header_blob);
        if !headers.is_empty() {
            WinHttpAddRequestHeaders(
                request.0,
                headers,
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE,
            )
            .map_err(core_err)?;
        }

        let optional =
            if body.is_empty() { None } else { Some(body.as_ptr().cast::<c_void>()) };
        WinHttpSendRequest(
            request.0,
            None,
            optional,
            body.len() as u32,
            body.len() as u32,
            0,
        )
        .map_err(core_err)?;

        WinHttpReceiveResponse(request.0, core::ptr::null_mut()).map_err(core_err)?;

        let status = query_status(request.0)?;
        let raw_headers = query_raw_headers(request.0)?;
        let body = read_body(request.0)?;

        Ok(HttpReply { status, raw_headers, body })
    }
}

/// Query the numeric status code.
///
/// # Safety
///
/// `request` must be a live `HINTERNET` request handle that has received a
/// response.
unsafe fn query_status(request: *mut c_void) -> Result<u32, HttpError> {
    let mut status: u32 = 0;
    let mut size = core::mem::size_of::<u32>() as u32;
    unsafe {
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            PCWSTR::null(),
            Some((&raw mut status).cast::<c_void>()),
            &mut size,
            core::ptr::null_mut(),
        )
        .map_err(core_err)?;
    }
    Ok(status)
}

/// Query the raw `CRLF` response-header blob (UTF-16). Returns an empty vector
/// when there are no headers.
///
/// # Safety
///
/// `request` must be a live `HINTERNET` request handle that has received a
/// response.
unsafe fn query_raw_headers(request: *mut c_void) -> Result<Vec<u16>, HttpError> {
    // First call (no buffer) reports the required byte length via the expected
    // ERROR_INSUFFICIENT_BUFFER.
    let mut size: u32 = 0;
    let probe = unsafe {
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_RAW_HEADERS_CRLF,
            PCWSTR::null(),
            None,
            &mut size,
            core::ptr::null_mut(),
        )
    };
    match probe {
        Ok(()) => return Ok(Vec::new()), // no headers
        Err(ref e) if err_code(e) == ERROR_INSUFFICIENT_BUFFER.0 => {}
        Err(e) => return Err(core_err(e)),
    }
    if size == 0 {
        return Ok(Vec::new());
    }
    let units = size as usize / core::mem::size_of::<u16>();
    let mut buf = vec![0u16; units + 1];
    unsafe {
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_RAW_HEADERS_CRLF,
            PCWSTR::null(),
            Some(buf.as_mut_ptr().cast::<c_void>()),
            &mut size,
            core::ptr::null_mut(),
        )
        .map_err(core_err)?;
    }
    buf.truncate(size as usize / core::mem::size_of::<u16>());
    Ok(buf)
}

/// Drain the response body via the `QueryDataAvailable` / `ReadData` loop.
///
/// # Safety
///
/// `request` must be a live `HINTERNET` request handle that has received a
/// response.
unsafe fn read_body(request: *mut c_void) -> Result<Vec<u8>, HttpError> {
    let mut body = Vec::new();
    loop {
        let mut available: u32 = 0;
        unsafe {
            WinHttpQueryDataAvailable(request, &mut available).map_err(core_err)?;
        }
        if available == 0 {
            break;
        }
        let mut chunk = vec![0u8; available as usize];
        let mut read: u32 = 0;
        unsafe {
            WinHttpReadData(
                request,
                chunk.as_mut_ptr().cast::<c_void>(),
                available,
                &mut read,
            )
            .map_err(core_err)?;
        }
        if read == 0 {
            break;
        }
        chunk.truncate(read as usize);
        body.extend_from_slice(&chunk);
    }
    Ok(body)
}
