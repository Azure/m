// Copyright (c) Microsoft Corporation.

//! The WinHTTP transport for `talky` (Windows only).
//!
//! This is `talky`'s reason for being the "native" stress client: it talks to
//! `wordy` over the Microsoft-native **WinHTTP** API rather than a Rust HTTP
//! crate, mirroring `wordy`'s own use of native Win32 surfaces. A single
//! [`WinHttpClient`] owns the WinHTTP session and is shared across worker
//! threads (WinHTTP handles are thread-safe); each [`WinHttpClient::send`] opens
//! a connection + request, sends, reads the status, drains the body, and closes
//! the per-request handles (the session's connection pool is reused).
//!
//! All `unsafe` lives in this module, which the crate root opts into while
//! denying `unsafe_code` elsewhere. Future native authentication (Negotiate /
//! NTLM / Basic, via `WinHttpSetCredentials` / `WinHttpQueryAuthSchemes`) slots
//! in here without touching the workload or driver.

use core::ffi::c_void;
use core::ptr::{null, null_mut};

use windows_sys::Win32::Foundation::GetLastError;
use windows_sys::Win32::Networking::WinHttp::{
    WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_FLAG_SECURE, WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_QUERY_STATUS_CODE, WinHttpCloseHandle, WinHttpConnect, WinHttpOpen, WinHttpOpenRequest,
    WinHttpQueryHeaders, WinHttpReadData, WinHttpReceiveResponse, WinHttpSendRequest,
    WinHttpSetTimeouts,
};

use crate::config::Endpoint;
use crate::workload::LogicalRequest;

/// A WinHTTP-backed HTTP client. Owns the session handle for its lifetime.
pub struct WinHttpClient {
    session: *mut c_void,
}

// SAFETY: WinHTTP session handles are documented thread-safe for concurrent use
// across threads. The handle is created in `new` and only closed in `Drop`,
// after all workers that share the client have joined.
unsafe impl Send for WinHttpClient {}
unsafe impl Sync for WinHttpClient {}

impl WinHttpClient {
    /// Open a WinHTTP session with the given per-operation timeout (ms).
    pub fn new(timeout_ms: u32) -> Result<Self, u32> {
        let agent = wide("talky/1.0");
        // SAFETY: agent is a valid NUL-terminated wide string; proxy params are
        // null (no-proxy access). Returns the session handle or null on failure.
        let session = unsafe {
            WinHttpOpen(
                agent.as_ptr(),
                WINHTTP_ACCESS_TYPE_NO_PROXY,
                null(),
                null(),
                0,
            )
        };
        if session.is_null() {
            return Err(last_error());
        }
        let timeout = timeout_ms as i32;
        // SAFETY: session is the live handle just opened; the timeouts apply to
        // resolve/connect/send/receive.
        unsafe { WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout) };
        Ok(WinHttpClient { session })
    }

    /// Send one request to `endpoint` and return its HTTP status code, or the
    /// Win32 error that prevented a status from being received.
    pub fn send(
        &self,
        endpoint: &Endpoint,
        request: &LogicalRequest,
        user: &str,
    ) -> Result<u16, u32> {
        let host = wide(&endpoint.host);
        // SAFETY: session is live; host is a NUL-terminated wide server name (an
        // IPv4/IPv6 literal or hostname). Returns a connection handle or null.
        let connection = unsafe { WinHttpConnect(self.session, host.as_ptr(), endpoint.port, 0) };
        if connection.is_null() {
            return Err(last_error());
        }
        let _connection = Handle(connection);

        let verb = wide(request.method.as_str());
        let object = wide(&request.path);
        let flags: u32 = if endpoint.is_secure() {
            WINHTTP_FLAG_SECURE
        } else {
            0
        };
        // SAFETY: connection is live; verb and object are NUL-terminated wide
        // strings; the remaining string params are intentionally null.
        let request_handle = unsafe {
            WinHttpOpenRequest(
                connection,
                verb.as_ptr(),
                object.as_ptr(),
                null(),
                null(),
                null(),
                flags,
            )
        };
        if request_handle.is_null() {
            return Err(last_error());
        }
        let _request = Handle(request_handle);

        let mut headers = String::new();
        if request.body.is_some() {
            headers.push_str("Content-Type: application/json\r\n");
        }
        headers.push_str("X-Wordy-User: ");
        headers.push_str(user);
        headers.push_str("\r\n");
        let headers_w = wide(&headers);

        let (body_ptr, body_len) = match &request.body {
            Some(body) => (body.as_ptr().cast::<c_void>(), body.len() as u32),
            None => (null(), 0),
        };

        // SAFETY: request_handle is live; headers_w is NUL-terminated (length
        // computed by WinHTTP via the -1 sentinel); body_ptr points at body_len
        // bytes that outlive the call (borrowed from `request`).
        let sent = unsafe {
            WinHttpSendRequest(
                request_handle,
                headers_w.as_ptr(),
                u32::MAX,
                body_ptr,
                body_len,
                body_len,
                0,
            )
        };
        if sent == 0 {
            return Err(last_error());
        }

        // SAFETY: request_handle is live; lpReserved must be null.
        let received = unsafe { WinHttpReceiveResponse(request_handle, null_mut()) };
        if received == 0 {
            return Err(last_error());
        }

        let mut status: u32 = 0;
        let mut size: u32 = core::mem::size_of::<u32>() as u32;
        // SAFETY: request_handle is live; the NUMBER flag makes WinHTTP write the
        // status code into the u32 pointed to by status, with size bytes.
        let queried = unsafe {
            WinHttpQueryHeaders(
                request_handle,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                null(),
                (&mut status as *mut u32).cast::<c_void>(),
                &mut size,
                null_mut(),
            )
        };
        if queried == 0 {
            return Err(last_error());
        }

        drain_body(request_handle);
        Ok(status as u16)
    }
}

impl Drop for WinHttpClient {
    fn drop(&mut self) {
        // SAFETY: session is the live handle returned by WinHttpOpen.
        unsafe { WinHttpCloseHandle(self.session) };
    }
}

/// An owned WinHTTP handle that closes itself on drop.
struct Handle(*mut c_void);

impl Drop for Handle {
    fn drop(&mut self) {
        // SAFETY: self.0 is a live WinHTTP handle owned by this guard.
        unsafe { WinHttpCloseHandle(self.0) };
    }
}

/// Read and discard the response body so the connection can return to the pool.
fn drain_body(request: *mut c_void) {
    let mut buffer = [0u8; 4096];
    loop {
        let mut read: u32 = 0;
        // SAFETY: request is live; buffer is a valid writable slice of its length.
        let ok = unsafe {
            WinHttpReadData(
                request,
                buffer.as_mut_ptr().cast::<c_void>(),
                buffer.len() as u32,
                &mut read,
            )
        };
        if ok == 0 || read == 0 {
            break;
        }
    }
}

/// The calling thread's last Win32 error code.
fn last_error() -> u32 {
    // SAFETY: GetLastError has no preconditions.
    unsafe { GetLastError() }
}

/// Build a NUL-terminated UTF-16 string.
fn wide(value: &str) -> Vec<u16> {
    value.encode_utf16().chain(core::iter::once(0)).collect()
}
