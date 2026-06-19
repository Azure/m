// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

//
// Win32 filesystem API shim (mwin32 D11). These entry points mirror the shape
// of the genuine <Windows.h> filesystem APIs (CreateDirectoryW, DeleteFileW,
// GetFileAttributesExW, ...) so an unmodified client redirects through the
// generated mwin32_alias object with no source change. Each routes through the
// process-wide PIL session into iplatform::get_filesystem(); the active mode
// (passthrough / buffered / redirecting / logging / fault) is chosen by the
// .pilcfg sidecar.
//
// This header declares the non-handle metadata / namespace family (M-FS-SHIM-2)
// plus the handle-minting CreateFile family (M-FS-SHIM-4). The find family and
// CloseHandle routing (M-FS-SHIM-5 / M-FS-SHIM-6) arrive in the same milestone
// but, like the genuine APIs, consume the generic HANDLE surface declared by
// <Windows.h>.
//

BOOL APIENTRY
mCreateDirectoryW(_In_ LPCWSTR lpPathName, _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes);

BOOL APIENTRY
mCreateDirectoryA(_In_ LPCSTR lpPathName, _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes);

#ifdef UNICODE
#define mCreateDirectory mCreateDirectoryW
#else
#define mCreateDirectory mCreateDirectoryA
#endif // !UNICODE

BOOL APIENTRY
mRemoveDirectoryW(_In_ LPCWSTR lpPathName);

BOOL APIENTRY
mRemoveDirectoryA(_In_ LPCSTR lpPathName);

#ifdef UNICODE
#define mRemoveDirectory mRemoveDirectoryW
#else
#define mRemoveDirectory mRemoveDirectoryA
#endif // !UNICODE

BOOL APIENTRY
mDeleteFileW(_In_ LPCWSTR lpFileName);

BOOL APIENTRY
mDeleteFileA(_In_ LPCSTR lpFileName);

#ifdef UNICODE
#define mDeleteFile mDeleteFileW
#else
#define mDeleteFile mDeleteFileA
#endif // !UNICODE

BOOL APIENTRY
mMoveFileW(_In_ LPCWSTR lpExistingFileName, _In_ LPCWSTR lpNewFileName);

BOOL APIENTRY
mMoveFileA(_In_ LPCSTR lpExistingFileName, _In_ LPCSTR lpNewFileName);

#ifdef UNICODE
#define mMoveFile mMoveFileW
#else
#define mMoveFile mMoveFileA
#endif // !UNICODE

BOOL APIENTRY
mMoveFileExW(_In_ LPCWSTR lpExistingFileName, _In_opt_ LPCWSTR lpNewFileName, _In_ DWORD dwFlags);

BOOL APIENTRY
mMoveFileExA(_In_ LPCSTR lpExistingFileName, _In_opt_ LPCSTR lpNewFileName, _In_ DWORD dwFlags);

#ifdef UNICODE
#define mMoveFileEx mMoveFileExW
#else
#define mMoveFileEx mMoveFileExA
#endif // !UNICODE

DWORD APIENTRY
mGetFileAttributesW(_In_ LPCWSTR lpFileName);

DWORD APIENTRY
mGetFileAttributesA(_In_ LPCSTR lpFileName);

#ifdef UNICODE
#define mGetFileAttributes mGetFileAttributesW
#else
#define mGetFileAttributes mGetFileAttributesA
#endif // !UNICODE

BOOL APIENTRY
mGetFileAttributesExW(_In_ LPCWSTR                lpFileName,
                      _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
                      _Out_writes_bytes_(sizeof(WIN32_FILE_ATTRIBUTE_DATA)) LPVOID lpFileInformation);

BOOL APIENTRY
mGetFileAttributesExA(_In_ LPCSTR                 lpFileName,
                      _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
                      _Out_writes_bytes_(sizeof(WIN32_FILE_ATTRIBUTE_DATA)) LPVOID lpFileInformation);

#ifdef UNICODE
#define mGetFileAttributesEx mGetFileAttributesExW
#else
#define mGetFileAttributesEx mGetFileAttributesExA
#endif // !UNICODE

//
// CreateFile family (M-FS-SHIM-4). Maps dwDesiredAccess / dwCreationDisposition
// onto PIL open_file vs create_file, interns the returned ifile in the handle
// table, and returns the minted HANDLE (INVALID_HANDLE_VALUE on failure, like
// the genuine API).
//
// Content is out of scope this milestone (D14): a minted file handle resolves
// metadata only. ReadFile / WriteFile and every other content API are not
// aliased here, so the handle's only valid consumers are the handle-based
// metadata calls and mCloseHandle; passing it to an un-aliased content API
// reaches the real API and fails with ERROR_INVALID_HANDLE (the D11 handle-
// translation invariant). Content lights up in M-FS-CONTENT.
//
// lpSecurityAttributes, dwShareMode, dwFlagsAndAttributes (other than directory
// intent, which is unsupported here) and hTemplateFile are ignored under PIL
// isolation.
//
HANDLE APIENTRY
mCreateFileW(_In_ LPCWSTR                          lpFileName,
             _In_ DWORD                            dwDesiredAccess,
             _In_ DWORD                            dwShareMode,
             _In_opt_ LPSECURITY_ATTRIBUTES        lpSecurityAttributes,
             _In_ DWORD                            dwCreationDisposition,
             _In_ DWORD                            dwFlagsAndAttributes,
             _In_opt_ HANDLE                       hTemplateFile);

HANDLE APIENTRY
mCreateFileA(_In_ LPCSTR                           lpFileName,
             _In_ DWORD                            dwDesiredAccess,
             _In_ DWORD                            dwShareMode,
             _In_opt_ LPSECURITY_ATTRIBUTES        lpSecurityAttributes,
             _In_ DWORD                            dwCreationDisposition,
             _In_ DWORD                            dwFlagsAndAttributes,
             _In_opt_ HANDLE                       hTemplateFile);

#ifdef UNICODE
#define mCreateFile mCreateFileW
#else
#define mCreateFile mCreateFileA
#endif // !UNICODE

//
// Dusty-deck legacy open / create family (M-FS-LEGACY-1). These 16-bit-era ANSI
// primitives mint an HFILE from the *same* handle_table as mCreateFile (D11):
// the returned value carries one of the reserved pseudo-handle bit patterns, so
// the legacy _l* close / read / write family translates it exactly the way
// mCloseHandle does. The OF_* / access styles map onto the PIL open_file vs
// create_file verbs; byte content is out of scope here (it lights up with the
// legacy content family, M-FS-LEGACY-3 / M-FS-CONTENT).
//
// mOpenFile fills the caller's OFSTRUCT with the public path it was given and
// honors the modifier styles it can model: OF_PARSE (fill only, no open),
// OF_CREATE (create / truncate), OF_DELETE (namespace delete), and OF_EXIST
// (existence probe: open then immediately release the handle). The share-mode
// and verify/prompt styles are ignored under PIL isolation. HFILE_ERROR is
// returned on failure, like the genuine API.
//
HFILE APIENTRY
mOpenFile(_In_ LPCSTR lpFileName, _Out_ LPOFSTRUCT lpReOpenBuff, _In_ UINT uStyle);

HFILE APIENTRY
m_lopen(_In_ LPCSTR lpPathName, _In_ int iReadWrite);

HFILE APIENTRY
m_lcreat(_In_ LPCSTR lpPathName, _In_ int iAttribute);

//
// Dusty-deck legacy content family (M-FS-LEGACY-3). The 16-bit-era _l* / _h*
// byte primitives and the LZ (compress / expand) family traffic in the same
// minted HFILE the legacy open family hands back: each widens the HFILE back to
// its HANDLE and forwards to the content shim (ReadFile / WriteFile /
// SetFilePointer / CloseHandle), so a minted value flows through the PIL ifile
// and a genuine value to the real API. The LZ family is a passthrough: PIL
// models no LZ decompression, so an LZ handle is the plain-file HFILE and no
// expansion is performed (D11 / D16).
//
UINT APIENTRY
m_lread(_In_ HFILE                                   hFile,
        _Out_writes_bytes_to_(uBytes, return) LPVOID lpBuffer,
        _In_ UINT                                    uBytes);

UINT APIENTRY
m_lwrite(_In_ HFILE hFile, _In_reads_bytes_(uBytes) LPCCH lpBuffer, _In_ UINT uBytes);

LONG APIENTRY
m_hread(_In_ HFILE                                   hFile,
        _Out_writes_bytes_to_(lBytes, return) LPVOID lpBuffer,
        _In_ LONG                                    lBytes);

LONG APIENTRY
m_hwrite(_In_ HFILE hFile, _In_reads_bytes_(lBytes) LPCCH lpBuffer, _In_ LONG lBytes);

LONG APIENTRY
m_llseek(_In_ HFILE hFile, _In_ LONG lOffset, _In_ int iOrigin);

HFILE APIENTRY
m_lclose(_In_ HFILE hFile);

INT APIENTRY
mLZOpenFileA(_In_ LPSTR lpFileName, _Inout_ LPOFSTRUCT lpReOpenBuf, _In_ WORD wStyle);

INT APIENTRY
mLZOpenFileW(_In_ LPWSTR lpFileName, _Inout_ LPOFSTRUCT lpReOpenBuf, _In_ WORD wStyle);

#ifdef UNICODE
#define mLZOpenFile mLZOpenFileW
#else
#define mLZOpenFile mLZOpenFileA
#endif // !UNICODE

INT APIENTRY
mLZRead(_In_ INT hFile, _Out_writes_bytes_to_(cbRead, return) CHAR* lpBuffer, _In_ INT cbRead);

LONG APIENTRY
mLZSeek(_In_ INT hFile, _In_ LONG lOffset, _In_ INT iOrigin);

VOID APIENTRY
mLZClose(_In_ INT hFile);

LONG APIENTRY
mLZCopy(_In_ INT hfSource, _In_ INT hfDest);

INT APIENTRY
mLZInit(_In_ INT hfSource);

INT APIENTRY
mGetExpandedNameA(_In_ LPSTR lpszSource, _Out_writes_(MAX_PATH) LPSTR lpszBuffer);

INT APIENTRY
mGetExpandedNameW(_In_ LPWSTR lpszSource, _Out_writes_(MAX_PATH) LPWSTR lpszBuffer);

#ifdef UNICODE
#define mGetExpandedName mGetExpandedNameW
#else
#define mGetExpandedName mGetExpandedNameA
#endif // !UNICODE

//
// Transacted (TxF) filesystem family (M-FS-LEGACY-2). Each declaration mirrors
// the genuine Transactional-NTFS signature so the alias redirect is ABI-exact.
// TxF has no analogue on the PIL surface, so every shim forwards to its
// non-transacted m* sibling and ignores the transaction handle (and any
// TxF-only parameters) under isolation (D11): the operation runs un-transacted
// with the sibling's redirection / buffering / last-error behavior.
//
HANDLE APIENTRY
mCreateFileTransactedW(_In_ LPCWSTR                   lpFileName,
                       _In_ DWORD                     dwDesiredAccess,
                       _In_ DWORD                     dwShareMode,
                       _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                       _In_ DWORD                     dwCreationDisposition,
                       _In_ DWORD                     dwFlagsAndAttributes,
                       _In_opt_ HANDLE                hTemplateFile,
                       _In_ HANDLE                    hTransaction,
                       _In_opt_ PUSHORT               pusMiniVersion,
                       _In_opt_ PVOID                 lpExtendedParameter);

HANDLE APIENTRY
mCreateFileTransactedA(_In_ LPCSTR                    lpFileName,
                       _In_ DWORD                     dwDesiredAccess,
                       _In_ DWORD                     dwShareMode,
                       _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                       _In_ DWORD                     dwCreationDisposition,
                       _In_ DWORD                     dwFlagsAndAttributes,
                       _In_opt_ HANDLE                hTemplateFile,
                       _In_ HANDLE                    hTransaction,
                       _In_opt_ PUSHORT               pusMiniVersion,
                       _In_opt_ PVOID                 lpExtendedParameter);

#ifdef UNICODE
#define mCreateFileTransacted mCreateFileTransactedW
#else
#define mCreateFileTransacted mCreateFileTransactedA
#endif // !UNICODE

BOOL APIENTRY
mCreateDirectoryTransactedW(_In_ LPCWSTR                   lpTemplateDirectory,
                            _In_ LPCWSTR                   lpNewDirectory,
                            _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                            _In_ HANDLE                    hTransaction);

BOOL APIENTRY
mCreateDirectoryTransactedA(_In_ LPCSTR                    lpTemplateDirectory,
                            _In_ LPCSTR                    lpNewDirectory,
                            _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                            _In_ HANDLE                    hTransaction);

#ifdef UNICODE
#define mCreateDirectoryTransacted mCreateDirectoryTransactedW
#else
#define mCreateDirectoryTransacted mCreateDirectoryTransactedA
#endif // !UNICODE

BOOL APIENTRY
mRemoveDirectoryTransactedW(_In_ LPCWSTR lpPathName, _In_ HANDLE hTransaction);

BOOL APIENTRY
mRemoveDirectoryTransactedA(_In_ LPCSTR lpPathName, _In_ HANDLE hTransaction);

#ifdef UNICODE
#define mRemoveDirectoryTransacted mRemoveDirectoryTransactedW
#else
#define mRemoveDirectoryTransacted mRemoveDirectoryTransactedA
#endif // !UNICODE

BOOL APIENTRY
mMoveFileTransactedW(_In_ LPCWSTR                lpExistingFileName,
                     _In_opt_ LPCWSTR            lpNewFileName,
                     _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
                     _In_opt_ LPVOID             lpData,
                     _In_ DWORD                  dwFlags,
                     _In_ HANDLE                 hTransaction);

BOOL APIENTRY
mMoveFileTransactedA(_In_ LPCSTR                 lpExistingFileName,
                     _In_opt_ LPCSTR             lpNewFileName,
                     _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
                     _In_opt_ LPVOID             lpData,
                     _In_ DWORD                  dwFlags,
                     _In_ HANDLE                 hTransaction);

#ifdef UNICODE
#define mMoveFileTransacted mMoveFileTransactedW
#else
#define mMoveFileTransacted mMoveFileTransactedA
#endif // !UNICODE

BOOL APIENTRY
mCopyFileTransactedW(_In_ LPCWSTR                lpExistingFileName,
                     _In_ LPCWSTR                lpNewFileName,
                     _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
                     _In_opt_ LPVOID             lpData,
                     _In_opt_ LPBOOL             pbCancel,
                     _In_ DWORD                  dwCopyFlags,
                     _In_ HANDLE                 hTransaction);

BOOL APIENTRY
mCopyFileTransactedA(_In_ LPCSTR                 lpExistingFileName,
                     _In_ LPCSTR                 lpNewFileName,
                     _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
                     _In_opt_ LPVOID             lpData,
                     _In_opt_ LPBOOL             pbCancel,
                     _In_ DWORD                  dwCopyFlags,
                     _In_ HANDLE                 hTransaction);

#ifdef UNICODE
#define mCopyFileTransacted mCopyFileTransactedW
#else
#define mCopyFileTransacted mCopyFileTransactedA
#endif // !UNICODE

BOOL APIENTRY
mGetFileAttributesTransactedW(_In_ LPCWSTR                lpFileName,
                              _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
                              _Out_ LPVOID                lpFileInformation,
                              _In_ HANDLE                 hTransaction);

BOOL APIENTRY
mGetFileAttributesTransactedA(_In_ LPCSTR                 lpFileName,
                              _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
                              _Out_ LPVOID                lpFileInformation,
                              _In_ HANDLE                 hTransaction);

#ifdef UNICODE
#define mGetFileAttributesTransacted mGetFileAttributesTransactedW
#else
#define mGetFileAttributesTransacted mGetFileAttributesTransactedA
#endif // !UNICODE

BOOL APIENTRY
mSetFileAttributesTransactedW(_In_ LPCWSTR lpFileName,
                              _In_ DWORD   dwFileAttributes,
                              _In_ HANDLE  hTransaction);

BOOL APIENTRY
mSetFileAttributesTransactedA(_In_ LPCSTR lpFileName,
                              _In_ DWORD  dwFileAttributes,
                              _In_ HANDLE hTransaction);

#ifdef UNICODE
#define mSetFileAttributesTransacted mSetFileAttributesTransactedW
#else
#define mSetFileAttributesTransacted mSetFileAttributesTransactedA
#endif // !UNICODE

HANDLE APIENTRY
mFindFirstFileTransactedW(_In_ LPCWSTR            lpFileName,
                          _In_ FINDEX_INFO_LEVELS fInfoLevelId,
                          _Out_ LPVOID            lpFindFileData,
                          _In_ FINDEX_SEARCH_OPS  fSearchOp,
                          _Reserved_ LPVOID       lpSearchFilter,
                          _In_ DWORD              dwAdditionalFlags,
                          _In_ HANDLE             hTransaction);

HANDLE APIENTRY
mFindFirstFileTransactedA(_In_ LPCSTR             lpFileName,
                          _In_ FINDEX_INFO_LEVELS fInfoLevelId,
                          _Out_ LPVOID            lpFindFileData,
                          _In_ FINDEX_SEARCH_OPS  fSearchOp,
                          _Reserved_ LPVOID       lpSearchFilter,
                          _In_ DWORD              dwAdditionalFlags,
                          _In_ HANDLE             hTransaction);

#ifdef UNICODE
#define mFindFirstFileTransacted mFindFirstFileTransactedW
#else
#define mFindFirstFileTransacted mFindFirstFileTransactedA
#endif // !UNICODE

DWORD APIENTRY
mGetLongPathNameTransactedW(_In_ LPCWSTR                       lpszShortPath,
                            _Out_writes_opt_(cchBuffer) LPWSTR lpszLongPath,
                            _In_ DWORD                         cchBuffer,
                            _In_ HANDLE                        hTransaction);

DWORD APIENTRY
mGetLongPathNameTransactedA(_In_ LPCSTR                       lpszShortPath,
                            _Out_writes_opt_(cchBuffer) LPSTR lpszLongPath,
                            _In_ DWORD                        cchBuffer,
                            _In_ HANDLE                       hTransaction);

#ifdef UNICODE
#define mGetLongPathNameTransacted mGetLongPathNameTransactedW
#else
#define mGetLongPathNameTransacted mGetLongPathNameTransactedA
#endif // !UNICODE

//
// Find family (M-FS-SHIM-5). mFindFirstFile enumerates the directory named by
// the pattern's parent via idirectory::enumerate_entries, captures the listing
// behind a handle_table find-enumeration pseudo-handle, and fills
// WIN32_FIND_DATA for the first entry. mFindNextFile advances the cursor
// (ERROR_NO_MORE_FILES at the end); mFindClose releases the state.
//
// Wildcard / literal-name filtering of the pattern leaf is not applied this
// milestone: every child of the parent directory is returned regardless of the
// pattern. The minted find handle is released only by mFindClose (or the
// CloseHandle routing of M-FS-SHIM-6); it is not a content handle.
//
HANDLE APIENTRY
mFindFirstFileW(_In_ LPCWSTR lpFileName, _Out_ LPWIN32_FIND_DATAW lpFindFileData);

HANDLE APIENTRY
mFindFirstFileA(_In_ LPCSTR lpFileName, _Out_ LPWIN32_FIND_DATAA lpFindFileData);

#ifdef UNICODE
#define mFindFirstFile mFindFirstFileW
#else
#define mFindFirstFile mFindFirstFileA
#endif // !UNICODE

BOOL APIENTRY
mFindNextFileW(_In_ HANDLE hFindFile, _Out_ LPWIN32_FIND_DATAW lpFindFileData);

BOOL APIENTRY
mFindNextFileA(_In_ HANDLE hFindFile, _Out_ LPWIN32_FIND_DATAA lpFindFileData);

#ifdef UNICODE
#define mFindNextFile mFindNextFileW
#else
#define mFindNextFile mFindNextFileA
#endif // !UNICODE

BOOL APIENTRY
mFindClose(_In_ HANDLE hFindFile);

//
// CloseHandle routing (M-FS-SHIM-6). Because files (and find handles) share the
// generic CloseHandle entry point, this shim inspects the handle: a value
// matching the handle_table reserved bit pattern is released from the table;
// any other value forwards to the real ::CloseHandle. This shim is therefore
// broader than mRegCloseKey (which only ever sees registry handles) and must
// not break a genuine OS handle passed to it. Aliasing CloseHandle is opt-in
// for exactly this reason (see M-FS-SHIM-7).
//
BOOL APIENTRY
mCloseHandle(_In_ HANDLE hObject);

//
// Handle-based metadata family (M-FS-HANDLE-META). Each consumes a HANDLE the
// shim minted via mCreateFile, so the D11 handle-translation invariant requires
// it be aliased: it resolves the pseudo-handle to its backing PIL ifile and
// serves the answer from ifile::query_information. None touch byte content
// (D14): a reported size is the metadata size, never a content length.
//
// Metadata is read-only on the PIL surface this milestone (there is no
// metadata-write verb). The Set* entry points resolve the handle, validate the
// request, and report success without persisting any change (the same
// accept-and-ignore stance the shim takes for parameters isolation cannot
// model); allocation / EOF / content info classes report ERROR_NOT_SUPPORTED.
//
BOOL APIENTRY
mGetFileInformationByHandle(_In_ HANDLE hFile, _Out_ LPBY_HANDLE_FILE_INFORMATION lpFileInformation);

DWORD APIENTRY
mGetFileSize(_In_ HANDLE hFile, _Out_opt_ LPDWORD lpFileSizeHigh);

BOOL APIENTRY
mGetFileSizeEx(_In_ HANDLE hFile, _Out_ PLARGE_INTEGER lpFileSize);

BOOL APIENTRY
mGetFileInformationByHandleEx(_In_ HANDLE                    hFile,
                              _In_ FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                              _Out_writes_bytes_(dwBufferSize) LPVOID lpFileInformation,
                              _In_ DWORD                              dwBufferSize);

BOOL APIENTRY
mSetFileInformationByHandle(_In_ HANDLE                    hFile,
                            _In_ FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                            _In_reads_bytes_(dwBufferSize) LPVOID lpFileInformation,
                            _In_ DWORD                            dwBufferSize);

BOOL APIENTRY
mGetFileTime(_In_ HANDLE          hFile,
             _Out_opt_ LPFILETIME lpCreationTime,
             _Out_opt_ LPFILETIME lpLastAccessTime,
             _Out_opt_ LPFILETIME lpLastWriteTime);

BOOL APIENTRY
mSetFileTime(_In_ HANDLE              hFile,
             _In_opt_ CONST FILETIME* lpCreationTime,
             _In_opt_ CONST FILETIME* lpLastAccessTime,
             _In_opt_ CONST FILETIME* lpLastWriteTime);

DWORD APIENTRY
mGetFileType(_In_ HANDLE hFile);

DWORD APIENTRY
mGetFinalPathNameByHandleW(_In_ HANDLE                       hFile,
                           _Out_writes_(cchFilePath) LPWSTR  lpszFilePath,
                           _In_ DWORD                        cchFilePath,
                           _In_ DWORD                        dwFlags);

DWORD APIENTRY
mGetFinalPathNameByHandleA(_In_ HANDLE                      hFile,
                           _Out_writes_(cchFilePath) LPSTR  lpszFilePath,
                           _In_ DWORD                       cchFilePath,
                           _In_ DWORD                       dwFlags);

#ifdef UNICODE
#define mGetFinalPathNameByHandle mGetFinalPathNameByHandleW
#else
#define mGetFinalPathNameByHandle mGetFinalPathNameByHandleA
#endif

//
// Byte-content & positioning family (M-FS-CONTENT). These entry points consume a
// minted file HANDLE, translate it to its backing PIL ifile (D11), and serve the
// redirection-backed (D16) whole-file byte stream. A genuine OS handle is
// forwarded untouched to the real API. Content is whole-file (D16): a read
// resolves real backing bytes; a write replaces the file at offset 0; a
// mid-file (non-zero offset) overwrite, vectored scatter / gather, or
// completion-routine (APC) delivery is not modeled and reports
// ERROR_NOT_SUPPORTED.
//
BOOL APIENTRY
mReadFile(_In_ HANDLE                                                              hFile,
          _Out_writes_bytes_to_opt_(nNumberOfBytesToRead, *lpNumberOfBytesRead) LPVOID lpBuffer,
          _In_ DWORD                                                              nNumberOfBytesToRead,
          _Out_opt_ LPDWORD                                                       lpNumberOfBytesRead,
          _Inout_opt_ LPOVERLAPPED                                                lpOverlapped);

BOOL APIENTRY
mWriteFile(_In_ HANDLE                                         hFile,
           _In_reads_bytes_opt_(nNumberOfBytesToWrite) LPCVOID lpBuffer,
           _In_ DWORD                                          nNumberOfBytesToWrite,
           _Out_opt_ LPDWORD                                   lpNumberOfBytesWritten,
           _Inout_opt_ LPOVERLAPPED                            lpOverlapped);

BOOL APIENTRY
mReadFileEx(_In_ HANDLE                                         hFile,
            _Out_writes_bytes_opt_(nNumberOfBytesToRead) LPVOID lpBuffer,
            _In_ DWORD                                          nNumberOfBytesToRead,
            _Inout_ LPOVERLAPPED                                lpOverlapped,
            _In_ LPOVERLAPPED_COMPLETION_ROUTINE                lpCompletionRoutine);

BOOL APIENTRY
mWriteFileEx(_In_ HANDLE                                         hFile,
             _In_reads_bytes_opt_(nNumberOfBytesToWrite) LPCVOID lpBuffer,
             _In_ DWORD                                          nNumberOfBytesToWrite,
             _Inout_ LPOVERLAPPED                                lpOverlapped,
             _In_ LPOVERLAPPED_COMPLETION_ROUTINE                lpCompletionRoutine);

BOOL APIENTRY
mReadFileScatter(_In_ HANDLE               hFile,
                 _In_ FILE_SEGMENT_ELEMENT aSegmentArray[],
                 _In_ DWORD                nNumberOfBytesToRead,
                 _Reserved_ LPDWORD        lpReserved,
                 _Inout_ LPOVERLAPPED      lpOverlapped);

BOOL APIENTRY
mWriteFileGather(_In_ HANDLE               hFile,
                 _In_ FILE_SEGMENT_ELEMENT aSegmentArray[],
                 _In_ DWORD                nNumberOfBytesToWrite,
                 _Reserved_ LPDWORD        lpReserved,
                 _Inout_ LPOVERLAPPED      lpOverlapped);

//
// Positioning + size family (M-FS-CONTENT-2). mSetFilePointer{,Ex} move the
// per-handle sequential cursor; mSetEndOfFile / mSetFileValidData mutate size
// within the whole-file content model (D16): truncate-to-empty or no-op resize
// is honoured, any other partial resize reports ERROR_NOT_SUPPORTED.
//
DWORD APIENTRY
mSetFilePointer(_In_ HANDLE       hFile,
                _In_ LONG         lDistanceToMove,
                _Inout_opt_ PLONG lpDistanceToMoveHigh,
                _In_ DWORD        dwMoveMethod);

BOOL APIENTRY
mSetFilePointerEx(_In_ HANDLE              hFile,
                  _In_ LARGE_INTEGER       liDistanceToMove,
                  _Out_opt_ PLARGE_INTEGER lpNewFilePointer,
                  _In_ DWORD               dwMoveMethod);

BOOL APIENTRY
mSetEndOfFile(_In_ HANDLE hFile);

BOOL APIENTRY
mSetFileValidData(_In_ HANDLE hFile, _In_ LONGLONG ValidDataLength);

//
// Flush / lock / control / duplicate family (M-FS-CONTENT-3). Each translates a
// minted handle and forwards a modeled no-op (durability / byte-range locking),
// reports ERROR_NOT_SUPPORTED (device control), or — for mDuplicateHandle —
// interns a second table entry over the same backing ifile so the duplicate
// shares the original's sequential position.
//
BOOL APIENTRY
mFlushFileBuffers(_In_ HANDLE hFile);

BOOL APIENTRY
mLockFile(_In_ HANDLE hFile,
          _In_ DWORD  dwFileOffsetLow,
          _In_ DWORD  dwFileOffsetHigh,
          _In_ DWORD  nNumberOfBytesToLockLow,
          _In_ DWORD  nNumberOfBytesToLockHigh);

BOOL APIENTRY
mLockFileEx(_In_ HANDLE          hFile,
            _In_ DWORD           dwFlags,
            _Reserved_ DWORD     dwReserved,
            _In_ DWORD           nNumberOfBytesToLockLow,
            _In_ DWORD           nNumberOfBytesToLockHigh,
            _Inout_ LPOVERLAPPED lpOverlapped);

BOOL APIENTRY
mUnlockFile(_In_ HANDLE hFile,
            _In_ DWORD  dwFileOffsetLow,
            _In_ DWORD  dwFileOffsetHigh,
            _In_ DWORD  nNumberOfBytesToUnlockLow,
            _In_ DWORD  nNumberOfBytesToUnlockHigh);

BOOL APIENTRY
mUnlockFileEx(_In_ HANDLE          hFile,
              _Reserved_ DWORD     dwReserved,
              _In_ DWORD           nNumberOfBytesToUnlockLow,
              _In_ DWORD           nNumberOfBytesToUnlockHigh,
              _Inout_ LPOVERLAPPED lpOverlapped);

BOOL APIENTRY
mDeviceIoControl(_In_ HANDLE                                              hDevice,
                 _In_ DWORD                                               dwIoControlCode,
                 _In_reads_bytes_opt_(nInBufferSize) LPVOID               lpInBuffer,
                 _In_ DWORD                                               nInBufferSize,
                 _Out_writes_bytes_to_opt_(nOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
                 _In_ DWORD                                               nOutBufferSize,
                 _Out_opt_ LPDWORD                                        lpBytesReturned,
                 _Inout_opt_ LPOVERLAPPED                                 lpOverlapped);

BOOL APIENTRY
mDuplicateHandle(_In_ HANDLE    hSourceProcessHandle,
                 _In_ HANDLE    hSourceHandle,
                 _In_ HANDLE    hTargetProcessHandle,
                 _Out_ LPHANDLE lpTargetHandle,
                 _In_ DWORD     dwDesiredAccess,
                 _In_ BOOL      bInheritHandle,
                 _In_ DWORD     dwOptions);

//
// Copy / replace / extended namespace & path family (M-FS-COPY). These
// path-based namespace and metadata APIs route through the session ifilesystem
// like the rest of this header. A copy is a namespace copy (D11): the
// destination node is materialized but byte content is not modeled this
// milestone (D14) -- the whole-file content copy lights up with M-FS-CONTENT.
// Progress / cancel callbacks have no long-running byte copy to act on under
// isolation and are ignored.
//
BOOL APIENTRY
mCopyFileW(_In_ LPCWSTR lpExistingFileName, _In_ LPCWSTR lpNewFileName, _In_ BOOL bFailIfExists);

BOOL APIENTRY
mCopyFileA(_In_ LPCSTR lpExistingFileName, _In_ LPCSTR lpNewFileName, _In_ BOOL bFailIfExists);

#ifdef UNICODE
#define mCopyFile mCopyFileW
#else
#define mCopyFile mCopyFileA
#endif // !UNICODE

BOOL APIENTRY
mCopyFileExW(_In_ LPCWSTR                lpExistingFileName,
             _In_ LPCWSTR                lpNewFileName,
             _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
             _In_opt_ LPVOID             lpData,
             _In_opt_ LPBOOL             pbCancel,
             _In_ DWORD                  dwCopyFlags);

BOOL APIENTRY
mCopyFileExA(_In_ LPCSTR                 lpExistingFileName,
             _In_ LPCSTR                 lpNewFileName,
             _In_opt_ LPPROGRESS_ROUTINE lpProgressRoutine,
             _In_opt_ LPVOID             lpData,
             _In_opt_ LPBOOL             pbCancel,
             _In_ DWORD                  dwCopyFlags);

#ifdef UNICODE
#define mCopyFileEx mCopyFileExW
#else
#define mCopyFileEx mCopyFileExA
#endif // !UNICODE

HRESULT APIENTRY
mCopyFile2(_In_ PCWSTR                             pwszExistingFileName,
           _In_ PCWSTR                             pwszNewFileName,
           _In_opt_ COPYFILE2_EXTENDED_PARAMETERS* pExtendedParameters);

//
// mReplaceFile re-keys the namespace (D13): the replacement node takes the
// replaced node's name, optionally preserving the original replaced node under
// the backup name. All three paths must share a root (D11); dwReplaceFlags and
// the reserved parameters are ignored under isolation.
//
BOOL APIENTRY
mReplaceFileW(_In_ LPCWSTR      lpReplacedFileName,
              _In_ LPCWSTR      lpReplacementFileName,
              _In_opt_ LPCWSTR  lpBackupFileName,
              _In_ DWORD        dwReplaceFlags,
              _Reserved_ LPVOID lpExclude,
              _Reserved_ LPVOID lpReserved);

BOOL APIENTRY
mReplaceFileA(_In_ LPCSTR       lpReplacedFileName,
              _In_ LPCSTR       lpReplacementFileName,
              _In_opt_ LPCSTR   lpBackupFileName,
              _In_ DWORD        dwReplaceFlags,
              _Reserved_ LPVOID lpExclude,
              _Reserved_ LPVOID lpReserved);

#ifdef UNICODE
#define mReplaceFile mReplaceFileW
#else
#define mReplaceFile mReplaceFileA
#endif // !UNICODE

//
// mCreateDirectoryEx ignores the template directory under isolation (there is
// no metadata to clone, D14) and otherwise creates the new directory exactly
// like mCreateDirectory.
//
BOOL APIENTRY
mCreateDirectoryExW(_In_ LPCWSTR                   lpTemplateDirectory,
                    _In_ LPCWSTR                   lpNewDirectory,
                    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes);

BOOL APIENTRY
mCreateDirectoryExA(_In_ LPCSTR                    lpTemplateDirectory,
                    _In_ LPCSTR                    lpNewDirectory,
                    _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes);

#ifdef UNICODE
#define mCreateDirectoryEx mCreateDirectoryExW
#else
#define mCreateDirectoryEx mCreateDirectoryExA
#endif // !UNICODE

//
// mGetTempFileName mints a temporary-file name (prefix + 16-bit hex + ".tmp")
// in the named directory. With uUnique == 0 the chosen name's node is created
// empty (deterministically under isolation); otherwise the name is formed
// without creating a node. lpTempFileName must address a MAX_PATH buffer.
//
UINT APIENTRY
mGetTempFileNameW(_In_ LPCWSTR                  lpPathName,
                  _In_ LPCWSTR                  lpPrefixString,
                  _In_ UINT                     uUnique,
                  _Out_writes_(MAX_PATH) LPWSTR lpTempFileName);

UINT APIENTRY
mGetTempFileNameA(_In_ LPCSTR                  lpPathName,
                  _In_ LPCSTR                  lpPrefixString,
                  _In_ UINT                    uUnique,
                  _Out_writes_(MAX_PATH) LPSTR lpTempFileName);

#ifdef UNICODE
#define mGetTempFileName mGetTempFileNameW
#else
#define mGetTempFileName mGetTempFileNameA
#endif // !UNICODE

//
// mSetFileAttributes verifies the target exists, then accepts and discards the
// new attribute mask: PIL exposes no metadata-write verb this milestone, so the
// set is a documented no-op (the shim's accept-and-ignore stance).
//
BOOL APIENTRY
mSetFileAttributesW(_In_ LPCWSTR lpFileName, _In_ DWORD dwFileAttributes);

BOOL APIENTRY
mSetFileAttributesA(_In_ LPCSTR lpFileName, _In_ DWORD dwFileAttributes);

#ifdef UNICODE
#define mSetFileAttributes mSetFileAttributesW
#else
#define mSetFileAttributes mSetFileAttributesA
#endif // !UNICODE

//
// mGetFullPathName canonicalizes a path against the Windows surface and writes
// it using the Win32 path-name length contract; lpFilePart, when supplied, is
// pointed at the final component within lpBuffer (null when the path has no
// distinct file component). Under isolation there is no current directory, so a
// relative input is normalized lexically rather than rooted at a CWD.
//
DWORD APIENTRY
mGetFullPathNameW(_In_ LPCWSTR                           lpFileName,
                  _In_ DWORD                             nBufferLength,
                  _Out_writes_opt_(nBufferLength) LPWSTR lpBuffer,
                  _Outptr_opt_ LPWSTR*                   lpFilePart);

DWORD APIENTRY
mGetFullPathNameA(_In_ LPCSTR                           lpFileName,
                  _In_ DWORD                            nBufferLength,
                  _Out_writes_opt_(nBufferLength) LPSTR lpBuffer,
                  _Outptr_opt_ LPSTR*                   lpFilePart);

#ifdef UNICODE
#define mGetFullPathName mGetFullPathNameW
#else
#define mGetFullPathName mGetFullPathNameA
#endif // !UNICODE

//
// mGetLongPathName verifies the path exists (a missing path fails
// ERROR_FILE_NOT_FOUND as on Windows) and returns its canonical form: there is
// no short/long distinction to expand under isolation.
//
DWORD APIENTRY
mGetLongPathNameW(_In_ LPCWSTR                        lpszShortPath,
                  _Out_writes_opt_(cchBuffer) LPWSTR lpszLongPath,
                  _In_ DWORD                          cchBuffer);

DWORD APIENTRY
mGetLongPathNameA(_In_ LPCSTR                        lpszShortPath,
                  _Out_writes_opt_(cchBuffer) LPSTR lpszLongPath,
                  _In_ DWORD                         cchBuffer);

#ifdef UNICODE
#define mGetLongPathName mGetLongPathNameW
#else
#define mGetLongPathName mGetLongPathNameA
#endif // !UNICODE

//
// mSearchPath looks for lpFileName under the semicolon-separated directories in
// lpPath (a default extension is appended only when the name has none) and
// returns the canonical path of the first match. A NULL lpPath selects the
// real default search order, which has no meaning under isolation and so fails
// ERROR_FILE_NOT_FOUND.
//
DWORD APIENTRY
mSearchPathW(_In_opt_ LPCWSTR                       lpPath,
             _In_ LPCWSTR                           lpFileName,
             _In_opt_ LPCWSTR                       lpExtension,
             _In_ DWORD                             nBufferLength,
             _Out_writes_opt_(nBufferLength) LPWSTR lpBuffer,
             _Outptr_opt_ LPWSTR*                   lpFilePart);

DWORD APIENTRY
mSearchPathA(_In_opt_ LPCSTR                       lpPath,
             _In_ LPCSTR                           lpFileName,
             _In_opt_ LPCSTR                       lpExtension,
             _In_ DWORD                            nBufferLength,
             _Out_writes_opt_(nBufferLength) LPSTR lpBuffer,
             _Outptr_opt_ LPSTR*                   lpFilePart);

#ifdef UNICODE
#define mSearchPath mSearchPathW
#else
#define mSearchPath mSearchPathA
#endif // !UNICODE

//
// Directory change-notification family (M-FS-NOTIFY-1). mReadDirectoryChangesW
// surfaces the Win32 detailed change-notification contract onto the PIL
// filesystem monitor: the directory handle (opened with FILE_FLAG_BACKUP_-
// SEMANTICS) names the watched directory, dwNotifyFilter / bWatchSubtree select
// the change categories, and reported changes are decoded into the caller's
// FILE_NOTIFY_INFORMATION chain. A NULL lpOverlapped blocks until a change
// arrives; an event-bearing OVERLAPPED completes asynchronously and signals its
// event. Whether notifications fire is decided by the active provider (the live
// provider observes real mutations; the buffered provider does not). These APIs
// are wide-only on Windows, so there is no ANSI counterpart.
//
BOOL APIENTRY
mReadDirectoryChangesW(_In_ HANDLE                              hDirectory,
                       _Out_writes_bytes_(nBufferLength) LPVOID lpBuffer,
                       _In_ DWORD                               nBufferLength,
                       _In_ BOOL                                bWatchSubtree,
                       _In_ DWORD                               dwNotifyFilter,
                       _Out_opt_ LPDWORD                        lpBytesReturned,
                       _Inout_opt_ LPOVERLAPPED                 lpOverlapped,
                       _In_opt_ LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);

//
// The Win10 RS3 SDK gates READ_DIRECTORY_NOTIFY_INFORMATION_CLASS and the
// genuine ReadDirectoryChangesExW behind NTDDI_WIN10_RS3; this project pins
// NTDDI_VERSION below that, so the enum is re-declared here when absent. The
// guard makes this inert on any build that targets RS3 or later (where the SDK
// supplies the real definition), so the two never collide.
//
#if (NTDDI_VERSION < NTDDI_WIN10_RS3)
typedef enum _READ_DIRECTORY_NOTIFY_INFORMATION_CLASS
{
    ReadDirectoryNotifyInformation         = 1,
    ReadDirectoryNotifyExtendedInformation = 2,
} READ_DIRECTORY_NOTIFY_INFORMATION_CLASS,
    *PREAD_DIRECTORY_NOTIFY_INFORMATION_CLASS;
#endif // NTDDI_VERSION < NTDDI_WIN10_RS3

BOOL APIENTRY
mReadDirectoryChangesExW(_In_ HANDLE                              hDirectory,
                         _Out_writes_bytes_(nBufferLength) LPVOID lpBuffer,
                         _In_ DWORD                               nBufferLength,
                         _In_ BOOL                                bWatchSubtree,
                         _In_ DWORD                               dwNotifyFilter,
                         _Out_opt_ LPDWORD                        lpBytesReturned,
                         _Inout_opt_ LPOVERLAPPED                 lpOverlapped,
                         _In_opt_ LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
                         _In_ READ_DIRECTORY_NOTIFY_INFORMATION_CLASS
                             ReadDirectoryNotifyInformationClass);

//
// Coarse change-notification family (M-FS-NOTIFY-2). Unlike mReadDirectory-
// ChangesW these deliver no per-change detail: mFindFirstChangeNotification
// registers a watch on the directory and returns a *real* manual-reset Win32
// event (the shim does not intercept WaitForSingleObject, so the returned handle
// must be OS-waitable). The event is signaled when any change matching
// dwNotifyFilter occurs; mFindNextChangeNotification re-arms it (resets the
// signal and continues watching) and mFindCloseChangeNotification unregisters
// the watch and closes the event. Failure returns INVALID_HANDLE_VALUE (the
// genuine sentinel for this family, not NULL).
//
HANDLE APIENTRY
mFindFirstChangeNotificationW(_In_ LPCWSTR lpPathName,
                              _In_ BOOL    bWatchSubtree,
                              _In_ DWORD   dwNotifyFilter);

HANDLE APIENTRY
mFindFirstChangeNotificationA(_In_ LPCSTR lpPathName,
                              _In_ BOOL   bWatchSubtree,
                              _In_ DWORD  dwNotifyFilter);

#ifdef UNICODE
#define mFindFirstChangeNotification mFindFirstChangeNotificationW
#else
#define mFindFirstChangeNotification mFindFirstChangeNotificationA
#endif // !UNICODE

BOOL APIENTRY
mFindNextChangeNotification(_In_ HANDLE hChangeHandle);

BOOL APIENTRY
mFindCloseChangeNotification(_In_ HANDLE hChangeHandle);

//
// Alternate-data-stream enumeration family (M-FS-STREAMS-2). mFindFirstStreamW
// opens an enumeration of the file's named data streams via ifile::enumerate_-
// streams, minting a pseudo-handle that represents the iteration state.
// mFindNextStreamW advances the cursor. ERROR_HANDLE_EOF signals end-of-
// enumeration; mFindClose releases the state (shared with the file-find family).
// Stream names include the leading colon and trailing $DATA type suffix.
//
// WIN32_FIND_STREAM_DATA and STREAM_INFO_LEVELS are defined by the Windows SDK
// in <fileapi.h> (included via <Windows.h>).
//
HANDLE APIENTRY
mFindFirstStreamW(_In_ LPCWSTR            lpFileName,
                  _In_ STREAM_INFO_LEVELS InfoLevel,
                  _Out_ LPVOID            lpFindStreamData,
                  _Reserved_ DWORD        dwFlags);

BOOL APIENTRY
mFindNextStreamW(_In_ HANDLE hFindStream, _Out_ LPVOID lpFindStreamData);
