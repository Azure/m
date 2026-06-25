// Copyright (c) Microsoft Corporation.

//! Windows implementation of the overlapped-file `unsafe` FFI wrappers.
//!
//! Every `unsafe` in `windows-file-io` lives in this module. [`FileHandle`] is
//! a RAII owner of a Win32 file `HANDLE` opened with `FILE_FLAG_OVERLAPPED`;
//! [`OverlappedOp`] owns a boxed `OVERLAPPED` (a stable heap address that
//! outlives an in-flight operation) and issues one overlapped `ReadFile` /
//! `WriteFile`. Completion of an issued operation is delivered by the
//! `windows-threadpool` IOCP reactor the higher layer binds to the exposed raw
//! handle.

use core::mem;
use core::ptr;
use std::os::windows::io::{AsRawHandle, RawHandle};

use windows_sys::Win32::Foundation::{
    CloseHandle, ERROR_HANDLE_EOF, ERROR_IO_PENDING, GENERIC_READ, GENERIC_WRITE, GetLastError,
    HANDLE, INVALID_HANDLE_VALUE,
};
use windows_sys::Win32::Storage::FileSystem::{
    CREATE_ALWAYS, CreateFileW, FILE_BEGIN, FILE_FLAG_OVERLAPPED, FILE_SHARE_READ, FILE_SHARE_WRITE,
    GetFileSizeEx, OPEN_ALWAYS, OPEN_EXISTING, ReadFile, SetEndOfFile, SetFilePointerEx, WriteFile,
};
use windows_sys::Win32::System::IO::OVERLAPPED;

/// How [`open`] should acquire the file handle.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OpenMode {
    /// Open an existing file for reading (shared read/write).
    Read,
    /// Create or truncate a file for read/write (the "store" path).
    CreateWrite,
    /// Open-or-create a file for read/write without truncating.
    ReadWrite,
}

/// The outcome of issuing one overlapped operation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Issue {
    /// The operation is in flight; await the thread-pool completion. A
    /// synchronous success also lands here because the pool-bound handle still
    /// posts a completion packet.
    Pending,
    /// A read reached end-of-file synchronously; no completion will be posted,
    /// so the caller must **not** await (treat as zero bytes).
    Eof,
    /// The operation failed synchronously with this `WIN32_ERROR` code.
    Failed(u32),
}

/// A RAII owner of an overlapped Win32 file `HANDLE`.
///
/// The raw OS handle is **exposed** via [`AsRawHandle`] so the safe layer can
/// bind it to the `windows-threadpool` IOCP reactor; the handle is closed on
/// drop.
pub struct FileHandle {
    handle: HANDLE,
}

// A Win32 handle is process-global and may be moved between threads (the async
// file is opened on one thread and may be driven on another). It is never used
// concurrently from two threads (the single-in-flight reactor model enforces
// that), so `Send` is sound but `Sync` is intentionally not implemented.
// SAFETY: ownership of the handle transfers with the value; no aliasing.
unsafe impl Send for FileHandle {}

impl AsRawHandle for FileHandle {
    fn as_raw_handle(&self) -> RawHandle {
        self.handle as RawHandle
    }
}

impl Drop for FileHandle {
    fn drop(&mut self) {
        // SAFETY: `handle` was returned by a successful `CreateFileW` and is not
        // used after drop.
        unsafe {
            CloseHandle(self.handle);
        }
    }
}

/// Open `wide_path` (a NUL-terminated UTF-16 path) per `mode`, always with
/// `FILE_FLAG_OVERLAPPED`. Returns the captured `WIN32_ERROR` on failure.
///
/// # Errors
/// The Win32 error code from `GetLastError` if `CreateFileW` fails.
pub fn open(wide_path: &[u16], mode: OpenMode) -> Result<FileHandle, u32> {
    let (access, share, disposition) = match mode {
        OpenMode::Read => (GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING),
        OpenMode::CreateWrite => (GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS),
        OpenMode::ReadWrite => (GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, OPEN_ALWAYS),
    };
    // SAFETY: `wide_path` is a caller-supplied NUL-terminated UTF-16 slice; the
    // remaining arguments are valid Win32 flag values; the returned handle is
    // checked against INVALID_HANDLE_VALUE before use.
    let handle = unsafe {
        CreateFileW(
            wide_path.as_ptr(),
            access,
            share,
            ptr::null(),
            disposition,
            FILE_FLAG_OVERLAPPED,
            ptr::null_mut(),
        )
    };
    if ptr::eq(handle, INVALID_HANDLE_VALUE) {
        // SAFETY: no preconditions.
        return Err(unsafe { GetLastError() });
    }
    Ok(FileHandle { handle })
}

/// The current size of the file in bytes.
///
/// # Errors
/// The Win32 error code if `GetFileSizeEx` fails.
pub fn file_size(handle: &FileHandle) -> Result<u64, u32> {
    let mut size: i64 = 0;
    // SAFETY: `handle.handle` is a live file handle; `size` is a valid out-param.
    let ok = unsafe { GetFileSizeEx(handle.handle, &mut size) };
    if ok == 0 {
        // SAFETY: no preconditions.
        return Err(unsafe { GetLastError() });
    }
    Ok(size as u64)
}

/// Truncate or extend the file to `size` bytes (moves the file pointer to
/// `size`, then `SetEndOfFile`).
///
/// # Errors
/// The Win32 error code if `SetFilePointerEx` or `SetEndOfFile` fails.
pub fn set_end_of_file(handle: &FileHandle, size: u64) -> Result<(), u32> {
    let mut new_pos: i64 = 0;
    // SAFETY: live handle; `new_pos` is a valid out-param; FILE_BEGIN is a valid
    // move method.
    let ok = unsafe { SetFilePointerEx(handle.handle, size as i64, &mut new_pos, FILE_BEGIN) };
    if ok == 0 {
        // SAFETY: no preconditions.
        return Err(unsafe { GetLastError() });
    }
    // SAFETY: live handle; the file pointer was positioned above.
    let ok = unsafe { SetEndOfFile(handle.handle) };
    if ok == 0 {
        // SAFETY: no preconditions.
        return Err(unsafe { GetLastError() });
    }
    Ok(())
}

/// A single overlapped operation: a boxed `OVERLAPPED` carrying the byte offset.
///
/// The box gives the `OVERLAPPED` a stable heap address that must outlive the
/// in-flight operation (the kernel writes the completion status into it
/// asynchronously); the safe layer holds the [`OverlappedOp`] across the await.
pub struct OverlappedOp {
    overlapped: Box<OVERLAPPED>,
}

impl OverlappedOp {
    /// Prepare an operation at byte `offset`.
    #[must_use]
    pub fn new(offset: u64) -> Self {
        // SAFETY: a zeroed OVERLAPPED is the documented initial state; the union
        // offset fields are plain integers.
        let mut overlapped: Box<OVERLAPPED> = Box::new(unsafe { mem::zeroed() });
        // Writing the (Copy) union offset fields of a freshly zeroed OVERLAPPED
        // needs no `unsafe` (only reading a union field does).
        overlapped.Anonymous.Anonymous.Offset = (offset & 0xFFFF_FFFF) as u32;
        overlapped.Anonymous.Anonymous.OffsetHigh = (offset >> 32) as u32;
        Self { overlapped }
    }

    /// Issue an overlapped `ReadFile` filling `buf`. `buf` must outlive the
    /// operation (the kernel writes into it until completion).
    pub fn issue_read(&mut self, handle: &FileHandle, buf: &mut [u8]) -> Issue {
        // SAFETY: live overlapped handle; `buf` is valid for `buf.len()` bytes
        // and outlives the operation (the safe layer holds it across the await);
        // the read-count pointer is null as required for overlapped I/O; the
        // OVERLAPPED has a stable boxed address that likewise outlives the op.
        let ok = unsafe {
            ReadFile(
                handle.handle,
                buf.as_mut_ptr(),
                buf.len() as u32,
                ptr::null_mut(),
                &mut *self.overlapped,
            )
        };
        if ok != 0 {
            return Issue::Pending;
        }
        // SAFETY: no preconditions.
        match unsafe { GetLastError() } {
            ERROR_IO_PENDING => Issue::Pending,
            ERROR_HANDLE_EOF => Issue::Eof,
            code => Issue::Failed(code),
        }
    }

    /// Issue an overlapped `WriteFile` from `buf`. `buf` must outlive the
    /// operation (the kernel reads from it until completion).
    pub fn issue_write(&mut self, handle: &FileHandle, buf: &[u8]) -> Issue {
        // SAFETY: live overlapped handle; `buf` is valid for `buf.len()` bytes
        // and outlives the operation; the write-count pointer is null as required
        // for overlapped I/O; the boxed OVERLAPPED outlives the op.
        let ok = unsafe {
            WriteFile(
                handle.handle,
                buf.as_ptr(),
                buf.len() as u32,
                ptr::null_mut(),
                &mut *self.overlapped,
            )
        };
        if ok != 0 {
            return Issue::Pending;
        }
        // SAFETY: no preconditions.
        match unsafe { GetLastError() } {
            ERROR_IO_PENDING => Issue::Pending,
            code => Issue::Failed(code),
        }
    }
}
