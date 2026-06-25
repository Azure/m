// Copyright (c) Microsoft Corporation.

//! The safe async [`File`] surface over the [`windows_file_io_sys`] leaf and
//! the [`windows_threadpool`] IOCP reactor.

use std::fmt;
use std::os::windows::io::AsRawHandle;
use std::path::Path;

use windows_file_io_sys::{FileHandle, Issue, OpenMode, OverlappedOp, file_size, open, set_end_of_file};
use windows_threadpool::Io;

/// Win32 error codes this crate interprets. Changing any value is a breaking
/// change (they are the OS contract, mirrored here so the safe layer needs no
/// `windows-sys` dependency).
mod win32_error {
    /// `ERROR_FILE_NOT_FOUND`.
    pub const FILE_NOT_FOUND: u32 = 2;
    /// `ERROR_PATH_NOT_FOUND`.
    pub const PATH_NOT_FOUND: u32 = 3;
    /// `ERROR_WRITE_FAULT` — used when a write reports zero progress.
    pub const WRITE_FAULT: u32 = 29;
    /// `ERROR_HANDLE_EOF` — a completed read that reached end-of-file.
    pub const HANDLE_EOF: u32 = 38;
}

/// A failed file operation, carrying the `WIN32_ERROR` status code.
///
/// The code is owned by this crate (Design Autonomy); the `windows-sys` binding
/// stays an implementation detail of the leaf.
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct FileError {
    code: u32,
}

impl FileError {
    /// Construct from a raw `WIN32_ERROR` code.
    #[must_use]
    pub const fn from_code(code: u32) -> Self {
        Self { code }
    }

    /// The underlying `WIN32_ERROR` code.
    #[must_use]
    pub const fn code(self) -> u32 {
        self.code
    }

    /// Whether this is a "not found" status (`ERROR_FILE_NOT_FOUND` /
    /// `ERROR_PATH_NOT_FOUND`).
    #[must_use]
    pub const fn is_not_found(self) -> bool {
        self.code == win32_error::FILE_NOT_FOUND || self.code == win32_error::PATH_NOT_FOUND
    }
}

impl fmt::Display for FileError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "windows file I/O error (os error {})", self.code)
    }
}

impl fmt::Debug for FileError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("FileError").field("code", &self.code).finish()
    }
}

impl std::error::Error for FileError {}

/// The result of a file operation.
pub type FileResult<T> = Result<T, FileError>;

/// Map a thread-pool completion `(io_result, bytes)` to a byte count. A read
/// that completes at end-of-file is reported as zero bytes.
fn completion_to_bytes(io_result: u32, bytes: usize) -> FileResult<usize> {
    match io_result {
        0 => Ok(bytes),
        win32_error::HANDLE_EOF => Ok(0),
        code => Err(FileError::from_code(code)),
    }
}

/// Encode a path as a NUL-terminated UTF-16 buffer for the Win32 boundary.
fn to_wide(path: &Path) -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    path.as_os_str().encode_wide().chain(std::iter::once(0)).collect()
}

/// An open file performing overlapped I/O with thread-pool completion.
///
/// One operation may be in flight at a time (the reactor's single-in-flight
/// model); the `&mut self` read/write methods enforce that at the type level.
pub struct File {
    handle: FileHandle,
    io: Io,
}

impl File {
    /// Open an existing file for reading.
    ///
    /// # Errors
    /// [`FileError`] (with `is_not_found` true when the path is absent).
    pub fn open(path: &Path) -> FileResult<File> {
        Self::with_mode(path, OpenMode::Read)
    }

    /// Create a new file (truncating any existing one) for read/write — the
    /// "store" path.
    ///
    /// # Errors
    /// [`FileError`] if the file cannot be created.
    pub fn create(path: &Path) -> FileResult<File> {
        Self::with_mode(path, OpenMode::CreateWrite)
    }

    /// Open or create a file for read/write without truncating.
    ///
    /// # Errors
    /// [`FileError`] if the file cannot be opened.
    pub fn open_read_write(path: &Path) -> FileResult<File> {
        Self::with_mode(path, OpenMode::ReadWrite)
    }

    fn with_mode(path: &Path, mode: OpenMode) -> FileResult<File> {
        let wide = to_wide(path);
        let handle = open(&wide, mode).map_err(FileError::from_code)?;
        let io = Io::new(handle.as_raw_handle()).map_err(|e| FileError::from_code(e.code()))?;
        Ok(File { handle, io })
    }

    /// The current size of the file in bytes.
    ///
    /// # Errors
    /// [`FileError`] if the size cannot be queried.
    pub fn size(&self) -> FileResult<u64> {
        file_size(&self.handle).map_err(FileError::from_code)
    }

    /// Truncate or extend the file to `size` bytes.
    ///
    /// # Errors
    /// [`FileError`] if the resize fails.
    pub fn set_len(&self, size: u64) -> FileResult<()> {
        set_end_of_file(&self.handle, size).map_err(FileError::from_code)
    }

    /// Read into `buf` starting at byte `offset`, resolving to the number of
    /// bytes read (`0` at end-of-file). At most one operation is in flight.
    ///
    /// # Errors
    /// [`FileError`] if the read fails.
    pub async fn read_at(&mut self, offset: u64, buf: &mut [u8]) -> FileResult<usize> {
        if buf.is_empty() {
            return Ok(0);
        }
        let mut op = OverlappedOp::new(offset);
        self.io.start();
        match op.issue_read(&self.handle, buf) {
            Issue::Pending => {
                let (io_result, bytes) = self.io.completion().await;
                completion_to_bytes(io_result, bytes)
            }
            Issue::Eof => {
                self.io.cancel();
                Ok(0)
            }
            Issue::Failed(code) => {
                self.io.cancel();
                Err(FileError::from_code(code))
            }
        }
    }

    /// Write `buf` starting at byte `offset`, resolving to the number of bytes
    /// written. At most one operation is in flight.
    ///
    /// # Errors
    /// [`FileError`] if the write fails.
    pub async fn write_at(&mut self, offset: u64, buf: &[u8]) -> FileResult<usize> {
        if buf.is_empty() {
            return Ok(0);
        }
        let mut op = OverlappedOp::new(offset);
        self.io.start();
        match op.issue_write(&self.handle, buf) {
            Issue::Pending => {
                let (io_result, bytes) = self.io.completion().await;
                completion_to_bytes(io_result, bytes)
            }
            Issue::Eof => {
                self.io.cancel();
                Ok(0)
            }
            Issue::Failed(code) => {
                self.io.cancel();
                Err(FileError::from_code(code))
            }
        }
    }

    /// Write the whole of `buf` starting at `offset`, looping over short writes.
    ///
    /// # Errors
    /// [`FileError`] if any write fails or reports zero progress.
    pub async fn write_all_at(&mut self, offset: u64, buf: &[u8]) -> FileResult<()> {
        let mut written = 0usize;
        while written < buf.len() {
            let n = self.write_at(offset + written as u64, &buf[written..]).await?;
            if n == 0 {
                return Err(FileError::from_code(win32_error::WRITE_FAULT));
            }
            written += n;
        }
        Ok(())
    }

    /// Read the entire file into a `Vec`, starting at offset 0.
    ///
    /// # Errors
    /// [`FileError`] if the size query or any read fails.
    pub async fn read_to_end(&mut self) -> FileResult<Vec<u8>> {
        let size = usize::try_from(self.size()?).unwrap_or(usize::MAX);
        let mut out = vec![0u8; size];
        let mut filled = 0usize;
        while filled < out.len() {
            let n = self.read_at(filled as u64, &mut out[filled..]).await?;
            if n == 0 {
                out.truncate(filled);
                break;
            }
            filled += n;
        }
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use windows_threadpool_executor::block_on;

    /// A unique scratch path under the temp dir, cleaned up by [`Scratch`].
    struct Scratch {
        path: std::path::PathBuf,
    }

    impl Scratch {
        fn new(tag: &str) -> Self {
            let mut path = std::env::temp_dir();
            let unique = format!(
                "wfio_{tag}_{}_{:?}.bin",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            );
            path.push(unique);
            Self { path }
        }
        fn path(&self) -> &Path {
            &self.path
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = std::fs::remove_file(&self.path);
        }
    }

    fn write_new(path: &Path, bytes: &[u8]) {
        let mut f = File::create(path).expect("create");
        block_on(f.write_all_at(0, bytes)).expect("write");
    }

    #[test]
    fn create_write_then_read_round_trips() {
        let s = Scratch::new("round_trip");
        let payload = b"the quick brown fox jumps over the lazy dog";
        write_new(s.path(), payload);

        let mut f = File::open(s.path()).expect("open");
        let got = block_on(f.read_to_end()).expect("read");
        assert_eq!(got, payload);
    }

    #[test]
    fn len_reflects_written_size() {
        let s = Scratch::new("len");
        write_new(s.path(), &[0u8; 1234]);
        let f = File::open(s.path()).expect("open");
        assert_eq!(f.size().expect("size"), 1234);
    }

    #[test]
    fn read_at_offset_returns_the_right_slice() {
        let s = Scratch::new("offset");
        let payload: Vec<u8> = (0..=255u8).collect();
        write_new(s.path(), &payload);

        let mut f = File::open(s.path()).expect("open");
        let mut buf = [0u8; 16];
        let n = block_on(f.read_at(100, &mut buf)).expect("read_at");
        assert_eq!(n, 16);
        assert_eq!(&buf, &payload[100..116]);
    }

    #[test]
    fn read_past_end_yields_zero() {
        let s = Scratch::new("eof");
        write_new(s.path(), b"short");
        let mut f = File::open(s.path()).expect("open");
        let mut buf = [0u8; 8];
        // Reading exactly at the end is EOF.
        let n = block_on(f.read_at(5, &mut buf)).expect("read_at eof");
        assert_eq!(n, 0);
    }

    #[test]
    fn partial_read_near_end_is_short() {
        let s = Scratch::new("short_read");
        write_new(s.path(), b"abcdef"); // 6 bytes
        let mut f = File::open(s.path()).expect("open");
        let mut buf = [0u8; 10];
        let n = block_on(f.read_at(4, &mut buf)).expect("read_at");
        assert_eq!(n, 2);
        assert_eq!(&buf[..2], b"ef");
    }

    #[test]
    fn empty_buffer_read_is_zero_without_io() {
        let s = Scratch::new("empty_buf");
        write_new(s.path(), b"data");
        let mut f = File::open(s.path()).expect("open");
        let mut buf: [u8; 0] = [];
        let n = block_on(f.read_at(0, &mut buf)).expect("read_at");
        assert_eq!(n, 0);
    }

    #[test]
    fn empty_file_reads_empty() {
        let s = Scratch::new("empty_file");
        write_new(s.path(), b"");
        let mut f = File::open(s.path()).expect("open");
        let got = block_on(f.read_to_end()).expect("read");
        assert!(got.is_empty());
    }

    #[test]
    fn write_at_offset_then_read_back() {
        let s = Scratch::new("write_offset");
        {
            let mut f = File::create(s.path()).expect("create");
            block_on(f.write_all_at(0, &[b'.'; 32])).expect("fill");
            block_on(f.write_all_at(8, b"WXYZ")).expect("overwrite");
        }
        let mut f = File::open(s.path()).expect("open");
        let got = block_on(f.read_to_end()).expect("read");
        assert_eq!(&got[8..12], b"WXYZ");
        assert_eq!(got.len(), 32);
    }

    #[test]
    fn set_len_truncates() {
        let s = Scratch::new("truncate");
        write_new(s.path(), &[7u8; 100]);
        {
            let f = File::open_read_write(s.path()).expect("rw");
            f.set_len(10).expect("truncate");
        }
        let f = File::open(s.path()).expect("open");
        assert_eq!(f.size().expect("size"), 10);
    }

    #[test]
    fn set_len_extends_with_zeros() {
        let s = Scratch::new("extend");
        write_new(s.path(), b"hi");
        {
            let f = File::open_read_write(s.path()).expect("rw");
            f.set_len(8).expect("extend");
        }
        let mut f = File::open(s.path()).expect("open");
        let got = block_on(f.read_to_end()).expect("read");
        assert_eq!(got, b"hi\0\0\0\0\0\0");
    }

    #[test]
    fn open_missing_is_not_found() {
        let mut path = std::env::temp_dir();
        path.push(format!("wfio_missing_{}.bin", std::process::id()));
        let _ = std::fs::remove_file(&path);
        let err = match File::open(&path) {
            Ok(_) => panic!("expected the missing path to fail to open"),
            Err(e) => e,
        };
        assert!(err.is_not_found(), "code {}", err.code());
    }

    #[test]
    fn create_always_truncates_existing() {
        let s = Scratch::new("recreate");
        write_new(s.path(), &[1u8; 50]);
        write_new(s.path(), b"new"); // CreateWrite truncates
        let f = File::open(s.path()).expect("open");
        assert_eq!(f.size().expect("size"), 3);
    }

    #[test]
    fn multibyte_payload_round_trips_across_chunks() {
        let s = Scratch::new("chunks");
        let payload: Vec<u8> = (0..4096u32).map(|i| (i % 251) as u8).collect();
        write_new(s.path(), &payload);

        // Read it back in 7-byte chunks to exercise the offset loop.
        let mut f = File::open(s.path()).expect("open");
        let mut got = Vec::new();
        let mut offset = 0u64;
        loop {
            let mut buf = [0u8; 7];
            let n = block_on(f.read_at(offset, &mut buf)).expect("read_at");
            if n == 0 {
                break;
            }
            got.extend_from_slice(&buf[..n]);
            offset += n as u64;
        }
        assert_eq!(got, payload);
    }
}
