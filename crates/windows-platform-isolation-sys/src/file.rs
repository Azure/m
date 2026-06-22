// Copyright (c) Microsoft Corporation.

//! Windows implementation of the safe **filesystem** FFI wrappers. Every
//! `unsafe` in the live filesystem provider lives in this module (D1 / D13,
//! Option B), mirroring the registry leaf [`RegKey`](crate::RegKey).
//!
//! The model is deliberately handle-centric. [`FileHandle`] is a RAII owner of
//! a Win32 file `HANDLE` (opened with `FILE_FLAG_BACKUP_SEMANTICS` so it works
//! uniformly for files and directories) that — unlike [`RegKey`], which keeps
//! its `HKEY` private — **exposes** the raw OS handle via
//! [`AsRawHandle`](std::os::windows::io::AsRawHandle). That is intentional: the
//! whole point of building on Win32 rather than `std::fs` is that a higher
//! layer can hand the handle to an overlapped-I/O reactor (the
//! `windows-threadpool` `CreateThreadpoolIo` IOCP reactor, or another async
//! engine) when stream/content support is added, with no re-open and no
//! rewrite. This leaf takes **no** dependency on any async runtime; it only
//! owns the handle and the `unsafe`.
//!
//! Namespace and metadata operations that do not need a persistent handle are
//! exposed as path-based free functions ([`file_attributes`],
//! [`create_directory`], [`remove_directory`], [`delete_file`],
//! [`set_file_attributes`], [`read_directory`]). Names crossing this boundary
//! are caller-supplied **NUL-terminated** UTF-16 slices; no raw pointer escapes.

use core::ffi::c_void;

use windows::Win32::Foundation::{
    CloseHandle, ERROR_FILE_NOT_FOUND, ERROR_NO_MORE_FILES, ERROR_PATH_NOT_FOUND, FILETIME, HANDLE,
};
use windows::Win32::Storage::FileSystem::{
    BY_HANDLE_FILE_INFORMATION, CREATE_ALWAYS, CreateDirectoryW, CreateFileW, DeleteFileW,
    FILE_ATTRIBUTE_NORMAL, FILE_FLAG_BACKUP_SEMANTICS, FILE_FLAGS_AND_ATTRIBUTES, FILE_GENERIC_READ,
    FILE_GENERIC_WRITE, FILE_SHARE_DELETE, FILE_SHARE_READ, FILE_SHARE_WRITE, FindClose,
    FindFirstFileW, FindNextFileW, GetFileAttributesExW,
    GetFileExInfoStandard, GetFileInformationByHandle, OPEN_EXISTING, RemoveDirectoryW,
    SetFileAttributesW, SetFileTime, WIN32_FILE_ATTRIBUTE_DATA, WIN32_FIND_DATAW,
};
use windows::core::PCWSTR;

/// A failed Win32 filesystem call, carrying the `WIN32_ERROR` status code
/// (mirror of [`RegError`](crate::RegError)).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FsError(pub u32);

impl FsError {
    /// The raw `WIN32_ERROR` status code.
    #[must_use]
    pub fn code(self) -> u32 {
        self.0
    }

    /// Whether this is a "not found" status (`ERROR_FILE_NOT_FOUND` /
    /// `ERROR_PATH_NOT_FOUND`), which the safe layer maps to a missing node
    /// rather than a hard failure.
    #[must_use]
    pub fn is_not_found(self) -> bool {
        self.0 == ERROR_FILE_NOT_FOUND.0 || self.0 == ERROR_PATH_NOT_FOUND.0
    }
}

impl core::fmt::Display for FsError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "Win32 filesystem error {}", self.0)
    }
}

impl std::error::Error for FsError {}

/// Extract the underlying `WIN32_ERROR` code from a [`windows::core::Error`].
///
/// Win32 wrappers report failures as an `HRESULT` produced by
/// `HRESULT_FROM_WIN32`, i.e. `0x8007_0000 | code` for a non-zero Win32 code.
/// Peel that back to the raw code so [`FsError::is_not_found`] and the safe
/// layer's `Os(u32)` mapping see the same numbers the registry leaf does.
fn fs_err(e: windows::core::Error) -> FsError {
    let hr = e.code().0 as u32;
    let code = if (hr & 0xFFFF_0000) == 0x8007_0000 {
        hr & 0x0000_FFFF
    } else {
        hr
    };
    FsError(code)
}

/// Metadata for a filesystem node, decoded from the Win32 representation into
/// the surface's vocabulary: timestamps are the raw `FILETIME` 100-ns ticks
/// packed into `i64` (treated as opaque, round-tripping losslessly), and
/// `attributes` is the `FILE_ATTRIBUTE_*` bitset verbatim.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct FileInfo {
    /// `FILE_ATTRIBUTE_*` flags, verbatim.
    pub attributes: u32,
    /// Byte size (0 for a directory).
    pub size: u64,
    /// Creation time, in `FILETIME` ticks.
    pub creation_time: i64,
    /// Last-access time, in `FILETIME` ticks.
    pub last_access_time: i64,
    /// Last-write time, in `FILETIME` ticks.
    pub last_write_time: i64,
}

/// One entry from [`read_directory`]: a leaf name (UTF-16 code units, no NUL)
/// and its metadata. The `.` and `..` pseudo-entries are filtered out.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FindEntry {
    /// The entry's leaf name (no NUL terminator).
    pub name: Vec<u16>,
    /// The entry's 8.3 short name (`cAlternateFileName`, no NUL terminator), as
    /// populated by the OS. Empty when the volume has no short name for this
    /// entry (e.g. an `8dot3name`-disabled volume, or a name already 8.3-legal).
    pub alternate_name: Vec<u16>,
    /// The entry's metadata.
    pub info: FileInfo,
}

/// The access a [`FileHandle`] is opened with.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FileAccess {
    /// Read-only.
    Read,
    /// Read and write.
    ReadWrite,
}

/// Combine the two 32-bit halves of a Win32 file size into a `u64`.
fn size_of(high: u32, low: u32) -> u64 {
    (u64::from(high) << 32) | u64::from(low)
}

/// Pack a `FILETIME` into the surface's opaque `i64` tick representation.
fn filetime_to_i64(ft: FILETIME) -> i64 {
    (((u64::from(ft.dwHighDateTime)) << 32) | u64::from(ft.dwLowDateTime)) as i64
}

/// Unpack a surface tick `i64` back into a `FILETIME`.
fn i64_to_filetime(v: i64) -> FILETIME {
    let bits = v as u64;
    FILETIME {
        dwLowDateTime: (bits & 0xFFFF_FFFF) as u32,
        dwHighDateTime: (bits >> 32) as u32,
    }
}

/// All three share modes — the live provider never holds an exclusive lock on a
/// node it is only inspecting or stamping.
const SHARE_ALL: windows::Win32::Storage::FileSystem::FILE_SHARE_MODE =
    windows::Win32::Storage::FileSystem::FILE_SHARE_MODE(
        FILE_SHARE_READ.0 | FILE_SHARE_WRITE.0 | FILE_SHARE_DELETE.0,
    );

/// A RAII owner of a Win32 file `HANDLE`.
///
/// Opened with `FILE_FLAG_BACKUP_SEMANTICS`, so a single type covers both files
/// and directories. The handle is closed on drop. The raw handle is exposed
/// (see [`AsRawHandle`](std::os::windows::io::AsRawHandle)) so a higher layer
/// can register it with an overlapped-I/O reactor for async stream work; this
/// leaf itself performs only synchronous metadata operations.
#[derive(Debug)]
pub struct FileHandle {
    handle: HANDLE,
}

// SAFETY: a Win32 `HANDLE` is valid from any thread; `Drop` closes it exactly
// once on the thread that drops it. `FileHandle` is intentionally not `Sync` —
// concurrent use is serialized externally (mirrors `RegKey`, D12).
unsafe impl Send for FileHandle {}

impl FileHandle {
    /// Open an existing file or directory at `path`.
    ///
    /// `path` is a NUL-terminated UTF-16 path. The handle is opened with
    /// `FILE_FLAG_BACKUP_SEMANTICS` so directories open just like files.
    ///
    /// # Errors
    ///
    /// Returns [`FsError`] (with [`FsError::is_not_found`] true when the node is
    /// absent) on any Win32 failure.
    pub fn open(path: &[u16], access: FileAccess) -> Result<Self, FsError> {
        let desired = match access {
            FileAccess::Read => FILE_GENERIC_READ.0,
            FileAccess::ReadWrite => FILE_GENERIC_READ.0 | FILE_GENERIC_WRITE.0,
        };
        let handle = unsafe {
            CreateFileW(
                PCWSTR(path.as_ptr()),
                desired,
                SHARE_ALL,
                None,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                None,
            )
        }
        .map_err(fs_err)?;
        Ok(Self { handle })
    }

    /// Create (or truncate) a file at `path`, returning an open read/write
    /// handle to the new, empty file.
    ///
    /// `path` is a NUL-terminated UTF-16 path. This is `CREATE_ALWAYS`: an
    /// existing file is truncated to zero length. File **content** is out of
    /// scope (metadata-only model); callers stamp attributes/timestamps via
    /// [`set_file_attributes`] and [`FileHandle::set_times`].
    ///
    /// # Errors
    ///
    /// Returns [`FsError`] on any Win32 failure.
    pub fn create(path: &[u16]) -> Result<Self, FsError> {
        let handle = unsafe {
            CreateFileW(
                PCWSTR(path.as_ptr()),
                FILE_GENERIC_READ.0 | FILE_GENERIC_WRITE.0,
                SHARE_ALL,
                None,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                None,
            )
        }
        .map_err(fs_err)?;
        Ok(Self { handle })
    }

    /// Read this node's metadata through the open handle
    /// (`GetFileInformationByHandle`).
    ///
    /// # Errors
    ///
    /// Returns [`FsError`] on any Win32 failure.
    pub fn information(&self) -> Result<FileInfo, FsError> {
        let mut info = BY_HANDLE_FILE_INFORMATION::default();
        unsafe { GetFileInformationByHandle(self.handle, &mut info) }.map_err(fs_err)?;
        Ok(FileInfo {
            attributes: info.dwFileAttributes,
            size: size_of(info.nFileSizeHigh, info.nFileSizeLow),
            creation_time: filetime_to_i64(info.ftCreationTime),
            last_access_time: filetime_to_i64(info.ftLastAccessTime),
            last_write_time: filetime_to_i64(info.ftLastWriteTime),
        })
    }

    /// Stamp this node's timestamps (`SetFileTime`). Each timestamp is in the
    /// surface's opaque `i64` `FILETIME`-tick representation; a value of `0`
    /// leaves that timestamp unchanged.
    ///
    /// # Errors
    ///
    /// Returns [`FsError`] on any Win32 failure.
    pub fn set_times(
        &self,
        creation: i64,
        last_access: i64,
        last_write: i64,
    ) -> Result<(), FsError> {
        let c = i64_to_filetime(creation);
        let a = i64_to_filetime(last_access);
        let w = i64_to_filetime(last_write);
        let cp = if creation != 0 { Some(&c as *const FILETIME) } else { None };
        let ap = if last_access != 0 { Some(&a as *const FILETIME) } else { None };
        let wp = if last_write != 0 { Some(&w as *const FILETIME) } else { None };
        unsafe { SetFileTime(self.handle, cp, ap, wp) }.map_err(fs_err)
    }
}

impl std::os::windows::io::AsRawHandle for FileHandle {
    fn as_raw_handle(&self) -> std::os::windows::io::RawHandle {
        self.handle.0.cast()
    }
}

impl Drop for FileHandle {
    fn drop(&mut self) {
        if !self.handle.0.is_null() {
            // Best-effort close; nothing actionable on failure.
            let _ = unsafe { CloseHandle(self.handle) };
        }
    }
}

/// Read a node's metadata by path (`GetFileAttributesExW`), without opening a
/// handle. Used for existence checks and metadata reads on the live provider.
///
/// # Errors
///
/// Returns [`FsError`] (with [`FsError::is_not_found`] true when the node is
/// absent) on any Win32 failure.
pub fn file_attributes(path: &[u16]) -> Result<FileInfo, FsError> {
    let mut data = WIN32_FILE_ATTRIBUTE_DATA::default();
    unsafe {
        GetFileAttributesExW(
            PCWSTR(path.as_ptr()),
            GetFileExInfoStandard,
            (&mut data as *mut WIN32_FILE_ATTRIBUTE_DATA).cast::<c_void>(),
        )
    }
    .map_err(fs_err)?;
    Ok(FileInfo {
        attributes: data.dwFileAttributes,
        size: size_of(data.nFileSizeHigh, data.nFileSizeLow),
        creation_time: filetime_to_i64(data.ftCreationTime),
        last_access_time: filetime_to_i64(data.ftLastAccessTime),
        last_write_time: filetime_to_i64(data.ftLastWriteTime),
    })
}

/// Set a node's `FILE_ATTRIBUTE_*` flags by path (`SetFileAttributesW`).
///
/// # Errors
///
/// Returns [`FsError`] on any Win32 failure.
pub fn set_file_attributes(path: &[u16], attributes: u32) -> Result<(), FsError> {
    unsafe {
        SetFileAttributesW(
            PCWSTR(path.as_ptr()),
            FILE_FLAGS_AND_ATTRIBUTES(attributes),
        )
    }
    .map_err(fs_err)
}

/// Create a directory at `path` (`CreateDirectoryW`). The parent must exist.
///
/// # Errors
///
/// Returns [`FsError`] on any Win32 failure (e.g. `ERROR_ALREADY_EXISTS`).
pub fn create_directory(path: &[u16]) -> Result<(), FsError> {
    unsafe { CreateDirectoryW(PCWSTR(path.as_ptr()), None) }.map_err(fs_err)
}

/// Remove the empty directory at `path` (`RemoveDirectoryW`).
///
/// # Errors
///
/// Returns [`FsError`] (with [`FsError::is_not_found`] true when absent) on any
/// Win32 failure.
pub fn remove_directory(path: &[u16]) -> Result<(), FsError> {
    unsafe { RemoveDirectoryW(PCWSTR(path.as_ptr())) }.map_err(fs_err)
}

/// Delete the file at `path` (`DeleteFileW`).
///
/// # Errors
///
/// Returns [`FsError`] (with [`FsError::is_not_found`] true when absent) on any
/// Win32 failure.
pub fn delete_file(path: &[u16]) -> Result<(), FsError> {
    unsafe { DeleteFileW(PCWSTR(path.as_ptr())) }.map_err(fs_err)
}

/// Read the leaf name from a fixed `WIN32_FIND_DATAW::cFileName` buffer (up to
/// the first NUL).
fn find_name(buf: &[u16]) -> Vec<u16> {
    let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    buf[..end].to_vec()
}

/// Whether a find name is one of the `.` / `..` pseudo-entries.
fn is_dot_entry(name: &[u16]) -> bool {
    const DOT: u16 = b'.' as u16;
    matches!(name, [DOT] | [DOT, DOT])
}

/// Enumerate the immediate entries of the directory at `dir_path`, returning all
/// real children (the `.` / `..` pseudo-entries are excluded) in the order the
/// OS yields them.
///
/// `dir_path` is a NUL-terminated UTF-16 directory path **without** a trailing
/// separator or wildcard; the `\*` search pattern is appended internally.
///
/// # Errors
///
/// Returns [`FsError`] (with [`FsError::is_not_found`] true when the directory
/// is absent) on any Win32 failure other than the normal end-of-enumeration
/// signal.
pub fn read_directory(dir_path: &[u16]) -> Result<Vec<FindEntry>, FsError> {
    // Build "<dir>\*\0": strip a trailing NUL, append a separator and wildcard.
    let trimmed = match dir_path.last() {
        Some(0) => &dir_path[..dir_path.len() - 1],
        _ => dir_path,
    };
    let mut pattern: Vec<u16> = Vec::with_capacity(trimmed.len() + 3);
    pattern.extend_from_slice(trimmed);
    if !matches!(pattern.last(), Some(&c) if c == u16::from(b'\\') || c == u16::from(b'/')) {
        pattern.push(u16::from(b'\\'));
    }
    pattern.push(u16::from(b'*'));
    pattern.push(0);

    let mut data = WIN32_FIND_DATAW::default();
    let find = unsafe { FindFirstFileW(PCWSTR(pattern.as_ptr()), &mut data) }.map_err(fs_err)?;

    let mut entries = Vec::new();
    loop {
        let name = find_name(&data.cFileName);
        if !is_dot_entry(&name) {
            entries.push(FindEntry {
                name,
                alternate_name: find_name(&data.cAlternateFileName),
                info: FileInfo {
                    attributes: data.dwFileAttributes,
                    size: size_of(data.nFileSizeHigh, data.nFileSizeLow),
                    creation_time: filetime_to_i64(data.ftCreationTime),
                    last_access_time: filetime_to_i64(data.ftLastAccessTime),
                    last_write_time: filetime_to_i64(data.ftLastWriteTime),
                },
            });
        }
        match unsafe { FindNextFileW(find, &mut data) } {
            Ok(()) => {}
            Err(e) => {
                let err = fs_err(e);
                // Close before propagating; end-of-enumeration is success.
                let _ = unsafe { FindClose(find) };
                if err.0 == ERROR_NO_MORE_FILES.0 {
                    return Ok(entries);
                }
                return Err(err);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;

    /// NUL-terminate a UTF-16 conversion of an OS path string.
    fn wz(s: &str) -> Vec<u16> {
        OsStr::new(s)
            .encode_wide()
            .chain(core::iter::once(0))
            .collect()
    }

    /// A unique scratch directory under the OS temp dir, removed on drop.
    struct ScratchDir {
        path: String,
    }

    impl ScratchDir {
        fn new(tag: &str) -> Self {
            let nanos = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let base = std::env::temp_dir();
            let path = base
                .join(format!("wpi-sys-{tag}-{}-{nanos}", std::process::id()))
                .to_string_lossy()
                .into_owned();
            create_directory(&wz(&path)).expect("create scratch dir");
            Self { path }
        }

        fn child(&self, leaf: &str) -> String {
            format!("{}\\{leaf}", self.path)
        }
    }

    impl Drop for ScratchDir {
        fn drop(&mut self) {
            // Best-effort recursive-ish cleanup: remove known children first.
            if let Ok(entries) = read_directory(&wz(&self.path)) {
                for e in entries {
                    let name = String::from_utf16_lossy(&e.name);
                    let child = format!("{}\\{name}", self.path);
                    let _ = delete_file(&wz(&child));
                    let _ = remove_directory(&wz(&child));
                }
            }
            let _ = remove_directory(&wz(&self.path));
        }
    }

    #[test]
    fn missing_path_reports_not_found() {
        let scratch = ScratchDir::new("missing");
        let err = file_attributes(&wz(&scratch.child("nope.txt")))
            .expect_err("absent file should error");
        assert!(err.is_not_found(), "expected not-found, got {err}");
    }

    #[test]
    fn create_file_then_read_attributes() {
        let scratch = ScratchDir::new("create");
        let file = scratch.child("hello.txt");
        let h = FileHandle::create(&wz(&file)).expect("create file");
        drop(h);
        let info = file_attributes(&wz(&file)).expect("read attributes");
        assert_eq!(info.size, 0, "freshly created file is empty");
        const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;
        assert_eq!(
            info.attributes & FILE_ATTRIBUTE_DIRECTORY,
            0,
            "a file is not a directory"
        );
    }

    #[test]
    fn information_through_handle_matches_path() {
        let scratch = ScratchDir::new("info");
        let file = scratch.child("h.txt");
        let h = FileHandle::create(&wz(&file)).expect("create file");
        let via_handle = h.information().expect("info via handle");
        drop(h);
        let via_path = file_attributes(&wz(&file)).expect("info via path");
        assert_eq!(via_handle.size, via_path.size);
        assert_eq!(via_handle.attributes, via_path.attributes);
    }

    #[test]
    fn set_times_round_trips_through_attributes() {
        let scratch = ScratchDir::new("times");
        let file = scratch.child("t.txt");
        let h = FileHandle::create(&wz(&file)).expect("create file");
        // 2010-ish FILETIME ticks; an arbitrary but valid non-zero instant.
        let stamp: i64 = 129_000_000_000_000_000;
        h.set_times(stamp, stamp, stamp).expect("set times");
        let info = h.information().expect("read back");
        assert_eq!(info.last_write_time, stamp);
        assert_eq!(info.creation_time, stamp);
    }

    #[test]
    fn set_file_attributes_applies() {
        let scratch = ScratchDir::new("attrs");
        let file = scratch.child("ro.txt");
        FileHandle::create(&wz(&file)).expect("create file");
        const FILE_ATTRIBUTE_READONLY: u32 = 0x1;
        set_file_attributes(&wz(&file), FILE_ATTRIBUTE_READONLY).expect("set attrs");
        let info = file_attributes(&wz(&file)).expect("read attrs");
        assert_ne!(info.attributes & FILE_ATTRIBUTE_READONLY, 0);
        // Clear read-only so the scratch cleanup can delete it.
        const FILE_ATTRIBUTE_NORMAL_BIT: u32 = 0x80;
        let _ = set_file_attributes(&wz(&file), FILE_ATTRIBUTE_NORMAL_BIT);
    }

    #[test]
    fn create_and_remove_directory() {
        let scratch = ScratchDir::new("dir");
        let sub = scratch.child("sub");
        create_directory(&wz(&sub)).expect("create subdir");
        let info = file_attributes(&wz(&sub)).expect("stat subdir");
        const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;
        assert_ne!(info.attributes & FILE_ATTRIBUTE_DIRECTORY, 0);
        remove_directory(&wz(&sub)).expect("remove subdir");
        assert!(
            file_attributes(&wz(&sub))
                .expect_err("removed dir is gone")
                .is_not_found()
        );
    }

    #[test]
    fn read_directory_lists_children_without_dot_entries() {
        let scratch = ScratchDir::new("enum");
        FileHandle::create(&wz(&scratch.child("a.txt"))).expect("create a");
        FileHandle::create(&wz(&scratch.child("b.txt"))).expect("create b");
        create_directory(&wz(&scratch.child("sub"))).expect("create sub");

        let mut names: Vec<String> = read_directory(&wz(&scratch.path))
            .expect("enumerate")
            .into_iter()
            .map(|e| String::from_utf16_lossy(&e.name))
            .collect();
        names.sort();
        assert_eq!(names, vec!["a.txt", "b.txt", "sub"]);
    }

    #[test]
    fn read_directory_on_missing_dir_is_not_found() {
        let scratch = ScratchDir::new("enum-missing");
        let err = read_directory(&wz(&scratch.child("nope")))
            .expect_err("absent dir should error");
        assert!(err.is_not_found(), "expected not-found, got {err}");
    }

    #[test]
    fn delete_file_removes_it() {
        let scratch = ScratchDir::new("del");
        let file = scratch.child("gone.txt");
        FileHandle::create(&wz(&file)).expect("create");
        delete_file(&wz(&file)).expect("delete");
        assert!(
            file_attributes(&wz(&file))
                .expect_err("deleted file is gone")
                .is_not_found()
        );
    }

    #[test]
    fn open_existing_directory_with_backup_semantics() {
        let scratch = ScratchDir::new("opendir");
        // FILE_FLAG_BACKUP_SEMANTICS lets us open the directory as a handle.
        let h = FileHandle::open(&wz(&scratch.path), FileAccess::Read).expect("open dir");
        let info = h.information().expect("dir info");
        const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;
        assert_ne!(info.attributes & FILE_ATTRIBUTE_DIRECTORY, 0);
    }

    #[test]
    fn raw_handle_is_exposed() {
        use std::os::windows::io::AsRawHandle;
        let scratch = ScratchDir::new("raw");
        let file = scratch.child("r.txt");
        let h = FileHandle::create(&wz(&file)).expect("create");
        assert!(!h.as_raw_handle().is_null(), "raw handle must be usable for async");
    }

    /// Read the 8.3 short name a direct `FindFirstFileW` reports for a single
    /// full child path (no wildcard), for cross-checking enumeration capture.
    fn direct_alternate_name(path_wz: &[u16]) -> Vec<u16> {
        let mut data = WIN32_FIND_DATAW::default();
        let find = unsafe { FindFirstFileW(PCWSTR(path_wz.as_ptr()), &mut data) }
            .expect("direct find on existing child");
        let alt = find_name(&data.cAlternateFileName);
        let _ = unsafe { FindClose(find) };
        alt
    }

    #[test]
    fn read_directory_captures_alternate_short_name() {
        let scratch = ScratchDir::new("shortname");
        // A non-8.3-legal name (an 8dot3-enabled volume mints a short name for
        // it) and an already-8.3-legal name (typically none). The assertion
        // compares against the OS's own answer, so it holds either way.
        FileHandle::create(&wz(&scratch.child("longfilename.txt"))).expect("create long");
        FileHandle::create(&wz(&scratch.child("ab.txt"))).expect("create short");

        let entries = read_directory(&wz(&scratch.path)).expect("enumerate");
        assert!(!entries.is_empty(), "scratch dir has children");
        for e in &entries {
            let name = String::from_utf16_lossy(&e.name);
            let direct = direct_alternate_name(&wz(&scratch.child(&name)));
            assert_eq!(
                e.alternate_name, direct,
                "alternate name for {name:?} must match a direct FindFirstFileW"
            );
        }
    }
}
