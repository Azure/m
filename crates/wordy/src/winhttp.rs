// Copyright (c) Microsoft Corporation.

//! `wordy`'s ordinary WinHTTP client transport (WD-D13).
//!
//! This is the `#[allow(unsafe_code)]` boundary that performs **one** plain
//! WinHTTP request/response transaction over the genuine `winhttp.dll` entry
//! points and returns `(status, body)`. `wordy` uses it to relay its custom-
//! dictionary operations to `merriam`. The calls are **deliberately ordinary**:
//! `wordy` knows nothing about isolation, so when the binary is relinked against
//! the shim's alias object (windows-win32-shim MW17), these `WinHttp*` imports
//! are transparently rerouted into the egress seam — `wordy` is unchanged
//! (SHIM-D19).
//!
//! No `HINTERNET` handle or raw pointer escapes this module; each handle is
//! closed by a RAII guard and every failure is mapped to a `WIN32_ERROR` code.

use core::ffi::c_void;

use windows_sys::Win32::Foundation::GetLastError;
use windows_sys::Win32::Networking::WinHttp::{
    WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_QUERY_STATUS_CODE,
    WinHttpCloseHandle, WinHttpConnect, WinHttpOpen, WinHttpOpenRequest, WinHttpQueryDataAvailable,
    WinHttpQueryHeaders, WinHttpReadData, WinHttpReceiveResponse, WinHttpSendRequest,
};

/// A RAII owner of an `HINTERNET` handle: closes it on drop (null = no-op).
struct Internet(*mut c_void);

impl Drop for Internet {
    fn drop(&mut self) {
        if !self.0.is_null() {
            // SAFETY: a live HINTERNET we own, closed exactly once on drop.
            unsafe {
                WinHttpCloseHandle(self.0);
            }
        }
    }
}

/// A NUL-terminated UTF-16 copy of `s`.
fn wide_nul(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// Perform one plain (non-TLS) WinHTTP request to `host:port`, returning the
/// HTTP status code and the response body as a (lossy-UTF-8) string.
///
/// `headers` are sent as request headers (e.g. `X-Wordy-User`); no request body
/// is sent (none of `wordy`'s relayed operations need one).
///
/// # Errors
/// The `WIN32_ERROR` of the first failing WinHTTP call.
pub fn request(
    host: &str,
    port: u16,
    verb: &str,
    path: &str,
    headers: &[(&str, &str)],
) -> Result<(u16, String), u32> {
    let agent = wide_nul("wordy-relay/1.0");
    let host_w = wide_nul(host);
    let verb_w = wide_nul(verb);
    let path_w = wide_nul(path);

    let mut header_blob = String::new();
    for (name, value) in headers {
        header_blob.push_str(name);
        header_blob.push_str(": ");
        header_blob.push_str(value);
        header_blob.push_str("\r\n");
    }
    let header_w: Vec<u16> = header_blob.encode_utf16().collect();

    // SAFETY: every handle is null-checked before use and closed by `Internet`'s
    // Drop; all pointers reference live, correctly-sized, caller-owned buffers
    // for the duration of each synchronous call.
    unsafe {
        let session = Internet(WinHttpOpen(
            agent.as_ptr(),
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            core::ptr::null(),
            core::ptr::null(),
            0,
        ));
        if session.0.is_null() {
            return Err(GetLastError());
        }

        let connect = Internet(WinHttpConnect(session.0, host_w.as_ptr(), port, 0));
        if connect.0.is_null() {
            return Err(GetLastError());
        }

        let request = Internet(WinHttpOpenRequest(
            connect.0,
            verb_w.as_ptr(),
            path_w.as_ptr(),
            core::ptr::null(),
            core::ptr::null(),
            core::ptr::null(),
            0,
        ));
        if request.0.is_null() {
            return Err(GetLastError());
        }

        let (header_ptr, header_len) = if header_w.is_empty() {
            (core::ptr::null(), 0u32)
        } else {
            (header_w.as_ptr(), header_w.len() as u32)
        };
        if WinHttpSendRequest(request.0, header_ptr, header_len, core::ptr::null(), 0, 0, 0) == 0 {
            return Err(GetLastError());
        }
        if WinHttpReceiveResponse(request.0, core::ptr::null_mut()) == 0 {
            return Err(GetLastError());
        }

        let mut status: u32 = 0;
        let mut status_len: u32 = core::mem::size_of::<u32>() as u32;
        if WinHttpQueryHeaders(
            request.0,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            core::ptr::null(),
            (&mut status as *mut u32).cast::<c_void>(),
            &mut status_len,
            core::ptr::null_mut(),
        ) == 0
        {
            return Err(GetLastError());
        }

        let mut body = Vec::new();
        loop {
            let mut available: u32 = 0;
            if WinHttpQueryDataAvailable(request.0, &mut available) == 0 {
                return Err(GetLastError());
            }
            if available == 0 {
                break;
            }
            let mut chunk = vec![0u8; available as usize];
            let mut read: u32 = 0;
            if WinHttpReadData(request.0, chunk.as_mut_ptr().cast::<c_void>(), available, &mut read)
                == 0
            {
                return Err(GetLastError());
            }
            if read == 0 {
                break;
            }
            chunk.truncate(read as usize);
            body.extend_from_slice(&chunk);
        }

        Ok((status as u16, String::from_utf8_lossy(&body).into_owned()))
    }
}
