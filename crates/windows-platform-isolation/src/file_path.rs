// Copyright (c) Microsoft Corporation.

//! Filesystem paths (D22).
//!
//! [`FilePath`] is a faithful Rust port of the C++ `m::pil::file_path`
//! (`src/libraries/pil/include/m/pil/file_path.h`), **not** a wrapper over
//! [`std::path::Path`]. Like that model it stores the path text verbatim
//! (preserving original case *and* separators, so [`FilePath::native`]
//! round-trips the input) plus a parsed [`FileRoot`] descriptor. Separator and
//! dot normalization, parent/leaf splitting, joining, and ordinal comparison
//! are surface-parameterized path *algebra* layered on top (M6-2); the C++
//! `test_file_path.cpp` is the conformance spec.
//!
//! M6-1 establishes the types, the seven-way root parser ([`parse_root`]), and
//! relative/absolute classification — no canonicalization yet.

use crate::Utf16;

/// The preferred Windows path separator (`\`).
pub const FILE_PREFERRED_SEPARATOR: u16 = b'\\' as u16;
/// The POSIX path separator (`/`), also accepted on the Windows surface.
pub const FILE_POSIX_SEPARATOR: u16 = b'/' as u16;

const DRIVE_COLON: u16 = b':' as u16;
const DEVICE_DOT: u16 = b'.' as u16;
const QUERY_MARK: u16 = b'?' as u16;

/// The family of a path's root (mirrors the C++ `file_root_kind`, D22).
///
/// `#[non_exhaustive]` because this is an *open* discriminant: new root
/// families can be added without disturbing existing callers. `None` denotes a
/// rootless (relative) path.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum FileRootKind {
    /// Rootless ⇒ relative path.
    #[default]
    None,
    /// `/` — POSIX absolute root (a bare leading separator).
    Posix,
    /// `C:` — Windows drive root (`C:\` absolute, `C:` / `C:x` drive-relative).
    Drive,
    /// `\\server\share` — Windows UNC share root.
    Unc,
    /// `\\.\…` — Win32 device namespace.
    Device,
    /// `\\?\…` — extended-length; remainder is verbatim.
    Extended,
    /// `\\?\UNC\…` — extended-length UNC; remainder is verbatim.
    ExtendedUnc,
}

/// True for either accepted separator character.
const fn is_separator(c: u16) -> bool {
    c == FILE_PREFERRED_SEPARATOR || c == FILE_POSIX_SEPARATOR
}

/// True only for the literal backslash. The extended-length (`\\?\`) and device
/// (`\\.\`) prefixes are recognized only with literal backslashes; forward
/// slashes do not introduce them.
const fn is_windows_separator(c: u16) -> bool {
    c == FILE_PREFERRED_SEPARATOR
}

const fn is_ascii_letter(c: u16) -> bool {
    (c >= b'A' as u16 && c <= b'Z' as u16) || (c >= b'a' as u16 && c <= b'z' as u16)
}

const fn ascii_upper(c: u16) -> u16 {
    if c >= b'a' as u16 && c <= b'z' as u16 {
        c - (b'a' as u16 - b'A' as u16)
    } else {
        c
    }
}

/// Index of the first separator at or after `from`, or `s.len()` if none.
fn find_separator(s: &[u16], from: usize) -> usize {
    (from..s.len()).find(|&i| is_separator(s[i])).unwrap_or(s.len())
}

/// Given that `s` begins with the 4-unit `\\?\` prefix, does it continue with
/// the case-insensitive `UNC\` token that distinguishes `\\?\UNC\server\share`
/// from a plain `\\?\…` path?
fn has_extended_unc_token(s: &[u16]) -> bool {
    const PREFIX_LEN: usize = 4; // "\\?\"
    const TOKEN_LEN: usize = 3; // "UNC"
    if s.len() < PREFIX_LEN + TOKEN_LEN + 1 {
        return false;
    }
    ascii_upper(s[PREFIX_LEN]) == b'U' as u16
        && ascii_upper(s[PREFIX_LEN + 1]) == b'N' as u16
        && ascii_upper(s[PREFIX_LEN + 2]) == b'C' as u16
        && is_windows_separator(s[PREFIX_LEN + TOKEN_LEN])
}

/// Parse the leading root of `s`, returning the root kind and the number of
/// units that constitute the root text (including any separator that terminates
/// the root). The remainder, `s[len..]`, is the relative portion. No
/// normalization happens here — the input is classified as-is so any input
/// round-trips through [`FilePath::native`] (M6-1); canonical form is M6-2.
#[must_use]
pub fn parse_root(s: &[u16]) -> (FileRootKind, usize) {
    let n = s.len();

    if n == 0 {
        return (FileRootKind::None, 0);
    }

    // Drive root: <letter> ':' [ separator ]
    if n >= 2 && is_ascii_letter(s[0]) && s[1] == DRIVE_COLON {
        let mut len = 2;
        if n >= 3 && is_separator(s[2]) {
            len += 1; // absorb the single terminating separator (drive-absolute)
        }
        return (FileRootKind::Drive, len);
    }

    // All other rooted forms start with two leading separators.
    if n >= 2 && is_separator(s[0]) && is_separator(s[1]) {
        // Extended-length (`\\?\`) and device (`\\.\`) namespaces require literal
        // backslashes; their remainder is opaque (prefix-only root).
        if n >= 4
            && is_windows_separator(s[0])
            && is_windows_separator(s[1])
            && is_windows_separator(s[3])
        {
            if s[2] == QUERY_MARK {
                const EXTENDED_PREFIX_LEN: usize = 4; // "\\?\"
                const EXTENDED_UNC_PREFIX_LEN: usize = 8; // "\\?\UNC\"
                if has_extended_unc_token(s) {
                    return (FileRootKind::ExtendedUnc, EXTENDED_UNC_PREFIX_LEN);
                }
                return (FileRootKind::Extended, EXTENDED_PREFIX_LEN);
            }
            if s[2] == DEVICE_DOT {
                const DEVICE_PREFIX_LEN: usize = 4; // "\\.\"
                return (FileRootKind::Device, DEVICE_PREFIX_LEN);
            }
        }

        // Otherwise UNC `\\server\share`: the root spans the two leading
        // separators, the server, the separator, the share, plus the single
        // separator that terminates the share if present.
        let server_end = find_separator(s, 2);
        if server_end >= n {
            return (FileRootKind::Unc, n); // "\\server" — incomplete UNC
        }
        let share_start = server_end + 1;
        let share_end = find_separator(s, share_start);
        let len = if share_end < n { share_end + 1 } else { share_end };
        return (FileRootKind::Unc, len);
    }

    // Single leading separator: POSIX absolute root (the separator itself).
    if is_separator(s[0]) {
        return (FileRootKind::Posix, 1);
    }

    // No root ⇒ relative path.
    (FileRootKind::None, 0)
}

/// Single source of truth for "is this root fully qualified (absolute)?", shared
/// by [`FileRoot::is_fully_qualified`] and [`FilePath::is_absolute`].
fn root_is_fully_qualified(kind: FileRootKind, text: &[u16]) -> bool {
    match kind {
        FileRootKind::None => false,
        // A drive root is absolute only when terminated by a separator (`C:\`);
        // a bare `C:` or `C:foo` is drive-relative.
        FileRootKind::Drive => text.last().is_some_and(|&c| is_separator(c)),
        FileRootKind::Posix
        | FileRootKind::Unc
        | FileRootKind::Device
        | FileRootKind::Extended
        | FileRootKind::ExtendedUnc => true,
    }
}

/// The root portion of a [`FilePath`]: a kind discriminant plus the exact root
/// text as it appears in the path (including any separator that terminates the
/// root). Stored case is always preserved — case-insensitivity is a comparison
/// concern (D6/D22), never a normalization of the stored characters.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FileRoot {
    kind: FileRootKind,
    text: Utf16,
}

impl FileRoot {
    /// Construct a root from its kind and exact text.
    #[must_use]
    pub fn new(kind: FileRootKind, text: Utf16) -> Self {
        Self { kind, text }
    }

    /// The root family.
    #[must_use]
    pub fn kind(&self) -> FileRootKind {
        self.kind
    }

    /// The exact root text (including any terminating separator).
    #[must_use]
    pub fn text(&self) -> &Utf16 {
        &self.text
    }

    /// True for a rootless (relative) path.
    #[must_use]
    pub fn is_none(&self) -> bool {
        self.kind == FileRootKind::None
    }

    /// True for the extended-length families whose remainder Win32 treats
    /// verbatim (D22): no separator/dot normalization is applied past the root.
    #[must_use]
    pub fn suppresses_normalization(&self) -> bool {
        matches!(self.kind, FileRootKind::Extended | FileRootKind::ExtendedUnc)
    }

    /// True when the root makes the path fully qualified (absolute).
    #[must_use]
    pub fn is_fully_qualified(&self) -> bool {
        root_is_fully_qualified(self.kind, self.text.as_units())
    }
}

/// A filesystem path: the filesystem-surface analogue of [`KeyPath`](crate::KeyPath).
///
/// Stores the full path text (root + remainder) plus the parsed root kind and
/// length; `native()` is exactly the text that was assigned. Equality is exact
/// (case-sensitive over code units) — ordinal case-insensitive comparison is a
/// separate surface-parameterized concern (M6-2, D6/D22).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FilePath {
    /// The entire path text (root + remainder); `native()` == `value`.
    value: Utf16,
    root_kind: FileRootKind,
    /// `value[..root_length]` is the root text; `value[root_length..]` is the
    /// relative remainder.
    root_length: usize,
}

impl Default for FilePath {
    fn default() -> Self {
        Self {
            value: Utf16::from_units(Vec::new()),
            root_kind: FileRootKind::None,
            root_length: 0,
        }
    }
}

impl FilePath {
    /// The empty (rootless, relative) path.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Build a path from UTF-16 code units, parsing the root.
    #[must_use]
    pub fn from_units(units: Vec<u16>) -> Self {
        Self::from_value(Utf16::from_units(units))
    }

    /// Build a path from UTF-8, parsing the root.
    #[must_use]
    pub fn from_utf8(s: &str) -> Self {
        Self::from_value(Utf16::from_utf8(s))
    }

    /// Build a path from a [`Utf16`] value, parsing the root.
    #[must_use]
    pub fn from_value(value: Utf16) -> Self {
        let (root_kind, root_length) = parse_root(value.as_units());
        Self {
            value,
            root_kind,
            root_length,
        }
    }

    /// The entire path text exactly as assigned (round-trips the input).
    #[must_use]
    pub fn native(&self) -> &Utf16 {
        &self.value
    }

    /// The root family.
    #[must_use]
    pub fn root_kind(&self) -> FileRootKind {
        self.root_kind
    }

    /// The root descriptor (kind + text). A rootless path returns a `None` root.
    #[must_use]
    pub fn root(&self) -> FileRoot {
        let text = Utf16::from_units(self.value.as_units()[..self.root_length].to_vec());
        FileRoot::new(self.root_kind, text)
    }

    /// The text following the root. For a rootless path this is the whole value;
    /// otherwise it is everything after the root text.
    #[must_use]
    pub fn relative_path(&self) -> Utf16 {
        Utf16::from_units(self.value.as_units()[self.root_length..].to_vec())
    }

    /// True when the path carries any root (including a drive-relative root).
    #[must_use]
    pub fn has_root(&self) -> bool {
        self.root_kind != FileRootKind::None
    }

    /// True when the path is fully qualified (absolute).
    #[must_use]
    pub fn is_absolute(&self) -> bool {
        root_is_fully_qualified(self.root_kind, &self.value.as_units()[..self.root_length])
    }

    /// True when the path is not fully qualified (rootless, or drive-relative).
    #[must_use]
    pub fn is_relative(&self) -> bool {
        !self.is_absolute()
    }

    /// True when the path text is empty.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.value.is_empty()
    }

    /// Reset to the empty path.
    pub fn clear(&mut self) {
        *self = Self::new();
    }

    /// Swap two paths.
    pub fn swap(&mut self, other: &mut Self) {
        core::mem::swap(self, other);
    }
}

impl From<&str> for FilePath {
    fn from(s: &str) -> Self {
        Self::from_utf8(s)
    }
}

impl From<String> for FilePath {
    fn from(s: String) -> Self {
        Self::from_utf8(&s)
    }
}

impl From<Utf16> for FilePath {
    fn from(value: Utf16) -> Self {
        Self::from_value(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    #[test]
    fn empty_is_rootless_relative() {
        let p = FilePath::from_utf8("");
        assert_eq!(p.root_kind(), FileRootKind::None);
        assert!(!p.has_root());
        assert!(!p.is_absolute());
        assert!(p.is_relative());
        assert_eq!(p.native(), &w(""));
        assert_eq!(p.relative_path(), w(""));
        assert!(p.root().is_none());
    }

    #[test]
    fn relative_path_has_no_root() {
        let p = FilePath::from_utf8("foo\\bar\\baz");
        assert_eq!(p.root_kind(), FileRootKind::None);
        assert!(!p.has_root());
        assert!(p.is_relative());
        assert_eq!(p.native(), &w("foo\\bar\\baz"));
        assert_eq!(p.relative_path(), w("foo\\bar\\baz"));
        assert!(p.root().is_none());
    }

    #[test]
    fn posix_root_is_absolute() {
        let p = FilePath::from_utf8("/usr/local/bin");
        assert_eq!(p.root_kind(), FileRootKind::Posix);
        assert!(p.has_root());
        assert!(p.is_absolute());
        assert!(!p.is_relative());
        assert_eq!(p.native(), &w("/usr/local/bin"));
        assert_eq!(p.root().text(), &w("/"));
        assert_eq!(p.relative_path(), w("usr/local/bin"));
    }

    #[test]
    fn bare_posix_root() {
        let p = FilePath::from_utf8("/");
        assert_eq!(p.root_kind(), FileRootKind::Posix);
        assert!(p.is_absolute());
        assert_eq!(p.native(), &w("/"));
        assert_eq!(p.root().text(), &w("/"));
        assert_eq!(p.relative_path(), w(""));
    }

    #[test]
    fn single_leading_backslash_is_posix_style_root() {
        // A single leading separator (either form) is a POSIX-style root; the
        // original separator character round-trips.
        let p = FilePath::from_utf8("\\foo");
        assert_eq!(p.root_kind(), FileRootKind::Posix);
        assert!(p.is_absolute());
        assert_eq!(p.native(), &w("\\foo"));
        assert_eq!(p.root().text(), &w("\\"));
        assert_eq!(p.relative_path(), w("foo"));
    }

    #[test]
    fn drive_absolute() {
        let p = FilePath::from_utf8("C:\\Windows\\System32");
        assert_eq!(p.root_kind(), FileRootKind::Drive);
        assert!(p.has_root());
        assert!(p.is_absolute());
        assert!(!p.is_relative());
        assert_eq!(p.native(), &w("C:\\Windows\\System32"));
        assert_eq!(p.root().text(), &w("C:\\"));
        assert_eq!(p.relative_path(), w("Windows\\System32"));
    }

    #[test]
    fn bare_drive_is_drive_relative() {
        let p = FilePath::from_utf8("C:");
        assert_eq!(p.root_kind(), FileRootKind::Drive);
        assert!(p.has_root());
        assert!(!p.is_absolute()); // no terminating separator ⇒ drive-relative
        assert!(p.is_relative());
        assert_eq!(p.native(), &w("C:"));
        assert_eq!(p.root().text(), &w("C:"));
        assert_eq!(p.relative_path(), w(""));
    }

    #[test]
    fn drive_relative_with_remainder() {
        let p = FilePath::from_utf8("C:foo\\bar");
        assert_eq!(p.root_kind(), FileRootKind::Drive);
        assert!(!p.is_absolute());
        assert!(p.is_relative());
        // Round-trips without inserting a separator.
        assert_eq!(p.native(), &w("C:foo\\bar"));
        assert_eq!(p.root().text(), &w("C:"));
        assert_eq!(p.relative_path(), w("foo\\bar"));
    }

    #[test]
    fn unc_share() {
        let p = FilePath::from_utf8("\\\\server\\share\\dir\\file");
        assert_eq!(p.root_kind(), FileRootKind::Unc);
        assert!(p.is_absolute());
        assert_eq!(p.native(), &w("\\\\server\\share\\dir\\file"));
        assert_eq!(p.root().text(), &w("\\\\server\\share\\"));
        assert_eq!(p.relative_path(), w("dir\\file"));
    }

    #[test]
    fn bare_unc_share_root() {
        let p = FilePath::from_utf8("\\\\server\\share");
        assert_eq!(p.root_kind(), FileRootKind::Unc);
        assert!(p.is_absolute());
        assert_eq!(p.native(), &w("\\\\server\\share"));
        assert_eq!(p.root().text(), &w("\\\\server\\share"));
        assert_eq!(p.relative_path(), w(""));
    }

    #[test]
    fn forward_slash_unc() {
        let p = FilePath::from_utf8("//server/share/dir");
        assert_eq!(p.root_kind(), FileRootKind::Unc);
        assert!(p.is_absolute());
        assert_eq!(p.native(), &w("//server/share/dir"));
        assert_eq!(p.root().text(), &w("//server/share/"));
        assert_eq!(p.relative_path(), w("dir"));
    }

    #[test]
    fn incomplete_unc_server_only() {
        // "\\server" with no share: the whole text is the (incomplete) root.
        let p = FilePath::from_utf8("\\\\server");
        assert_eq!(p.root_kind(), FileRootKind::Unc);
        assert_eq!(p.root().text(), &w("\\\\server"));
        assert_eq!(p.relative_path(), w(""));
    }

    #[test]
    fn device_namespace() {
        let p = FilePath::from_utf8("\\\\.\\PhysicalDrive0");
        assert_eq!(p.root_kind(), FileRootKind::Device);
        assert!(p.is_absolute());
        assert!(!p.root().suppresses_normalization());
        assert_eq!(p.native(), &w("\\\\.\\PhysicalDrive0"));
        assert_eq!(p.root().text(), &w("\\\\.\\"));
        assert_eq!(p.relative_path(), w("PhysicalDrive0"));
    }

    #[test]
    fn extended_length_remainder_preserved() {
        // The `\\?\` prefix is recognized; M6-1 does no normalization, so the
        // remainder (including `..`) round-trips exactly. The verbatim guarantee
        // is exercised by lexically_normal in M6-2.
        let p = FilePath::from_utf8("\\\\?\\C:\\a\\..\\b");
        assert_eq!(p.root_kind(), FileRootKind::Extended);
        assert!(p.is_absolute());
        assert!(p.root().suppresses_normalization());
        assert_eq!(p.native(), &w("\\\\?\\C:\\a\\..\\b"));
        assert_eq!(p.root().text(), &w("\\\\?\\"));
        assert_eq!(p.relative_path(), w("C:\\a\\..\\b"));
    }

    #[test]
    fn extended_length_unc() {
        let p = FilePath::from_utf8("\\\\?\\UNC\\server\\share\\x");
        assert_eq!(p.root_kind(), FileRootKind::ExtendedUnc);
        assert!(p.is_absolute());
        assert!(p.root().suppresses_normalization());
        assert_eq!(p.native(), &w("\\\\?\\UNC\\server\\share\\x"));
        assert_eq!(p.root().text(), &w("\\\\?\\UNC\\"));
        assert_eq!(p.relative_path(), w("server\\share\\x"));
    }

    #[test]
    fn extended_unc_token_is_case_insensitive() {
        let p = FilePath::from_utf8("\\\\?\\unc\\server\\share");
        assert_eq!(p.root_kind(), FileRootKind::ExtendedUnc);
        // Stored case is preserved.
        assert_eq!(p.native(), &w("\\\\?\\unc\\server\\share"));
        assert_eq!(p.root().text(), &w("\\\\?\\unc\\"));
    }

    #[test]
    fn equality_is_exact() {
        assert_eq!(FilePath::from_utf8("C:\\Foo"), FilePath::from_utf8("C:\\Foo"));
        assert_ne!(FilePath::from_utf8("C:\\Foo"), FilePath::from_utf8("C:\\foo"));
        assert_ne!(FilePath::from_utf8("C:\\Foo"), FilePath::from_utf8("C:/Foo"));
    }

    #[test]
    fn clone_assign_swap_clear() {
        let a = FilePath::from_utf8("\\\\server\\share\\dir");

        let b = a.clone();
        assert_eq!(b, a);
        assert_eq!(b.root_kind(), FileRootKind::Unc);

        let mut d = FilePath::new();
        assert!(d.is_empty());
        d = a.clone();
        assert_eq!(d, a);
        // View assign re-parses.
        let mut e = FilePath::from(Utf16::from_utf8("/etc/hosts"));
        assert_eq!(e.root_kind(), FileRootKind::Posix);
        assert_eq!(e.relative_path(), w("etc/hosts"));

        let mut a2 = a.clone();
        a2.swap(&mut e);
        assert_eq!(a2.root_kind(), FileRootKind::Posix);
        assert_eq!(e.root_kind(), FileRootKind::Unc);

        e.clear();
        assert_eq!(e.root_kind(), FileRootKind::None);
        assert_eq!(e.native(), &w(""));
        assert!(e.is_relative());
        assert!(e.is_empty());
    }

    #[test]
    fn construct_from_str_and_string() {
        let p = FilePath::from("C:\\Temp");
        assert_eq!(p.root_kind(), FileRootKind::Drive);
        assert_eq!(p.native(), &w("C:\\Temp"));

        let q = FilePath::from(String::from("relative\\path"));
        assert_eq!(q.root_kind(), FileRootKind::None);
        assert_eq!(q.native(), &w("relative\\path"));
    }

    #[test]
    fn from_units_round_trips() {
        let units = w("C:\\Windows").as_units().to_vec();
        let p = FilePath::from_units(units);
        assert_eq!(p.root_kind(), FileRootKind::Drive);
        assert_eq!(p.native(), &w("C:\\Windows"));
        assert_eq!(p.relative_path(), w("Windows"));
    }

    #[test]
    fn default_is_empty() {
        let p = FilePath::default();
        assert!(p.is_empty());
        assert_eq!(p.root_kind(), FileRootKind::None);
    }
}
