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
//! relative/absolute classification — no canonicalization yet. M6-2 adds the
//! surface-parameterized algebra: [`FilePath::lexically_normal`], parent/leaf
//! splitting, joining, and ordinal comparison.

use core::cmp::Ordering;

use crate::OrdinalCasing;
use crate::Utf16;
use crate::fs_error::{FilesystemError, FilesystemResult};

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

/// The platform surface whose path rules govern canonicalization and name
/// comparison (D22). The PIL models a *chosen* platform that need not be the
/// host, so the surface is an explicit value rather than a compile-time
/// decision: [`Windows`](PathSurface::Windows) accepts both separators and the
/// drive/UNC/device/extended root families; [`Posix`](PathSurface::Posix) uses
/// only `/` (a backslash is an ordinary filename character) and the single `/`
/// root. This is a closed discriminant — there are exactly two surfaces.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PathSurface {
    /// Windows path rules.
    Windows,
    /// POSIX path rules.
    Posix,
}

const fn is_dot(seg: &[u16]) -> bool {
    seg.len() == 1 && seg[0] == DEVICE_DOT
}

const fn is_dotdot(seg: &[u16]) -> bool {
    seg.len() == 2 && seg[0] == DEVICE_DOT && seg[1] == DEVICE_DOT
}

/// Split `rem` into non-empty components. `/` is always a separator; `\` is a
/// separator only on the Windows surface (on POSIX it is an ordinary filename
/// character). Empty components — the product of repeated separators — are
/// dropped, which is how separator collapsing happens.
fn split_segments(rem: &[u16], backslash_is_separator: bool) -> Vec<&[u16]> {
    let mut out = Vec::new();
    let n = rem.len();
    let mut start = 0usize;
    for i in 0..=n {
        let at_end = i == n;
        let sep = !at_end
            && (rem[i] == FILE_POSIX_SEPARATOR
                || (backslash_is_separator && rem[i] == FILE_PREFERRED_SEPARATOR));
        if at_end || sep {
            if i > start {
                out.push(&rem[start..i]);
            }
            start = i + 1;
        }
    }
    out
}

/// Resolve `.` and `..` lexically. A `..` that would pop past the start of an
/// absolute (fully qualified) path underflows the root and is rejected (D22). In
/// a relative path leading `..` segments are preserved, since they are
/// meaningful against an unknown base.
fn resolve_dot_segments<'a>(
    segments: &[&'a [u16]],
    absolute: bool,
) -> FilesystemResult<Vec<&'a [u16]>> {
    let mut out: Vec<&[u16]> = Vec::new();
    for &seg in segments {
        if is_dot(seg) {
            continue;
        }
        if is_dotdot(seg) {
            if out.last().is_some_and(|last| !is_dotdot(last)) {
                out.pop();
            } else if absolute {
                return Err(FilesystemError::InvalidPath(
                    "'..' underflows the root".to_string(),
                ));
            } else {
                out.push(seg);
            }
        } else {
            out.push(seg);
        }
    }
    Ok(out)
}

/// Join an already-parsed root with resolved segments. The root text already
/// carries the boundary separator when one is needed (e.g. `C:\`,
/// `\\server\share\`); the only non-separator-terminated root that takes
/// segments is the drive-relative `C:` form, which intentionally abuts its first
/// segment with no separator (`C:foo`).
fn join_root_and_segments(root_text: &[u16], segments: &[&[u16]], separator: u16) -> Vec<u16> {
    let mut result = root_text.to_vec();
    for (i, seg) in segments.iter().enumerate() {
        if i > 0 {
            result.push(separator);
        }
        result.extend_from_slice(seg);
    }
    result
}

fn canonicalize_windows(v: &[u16]) -> FilesystemResult<Vec<u16>> {
    // Extended-length paths are verbatim (D22): the prefix is recognized but
    // nothing past it is touched.
    let (kind0, _) = parse_root(v);
    if matches!(kind0, FileRootKind::Extended | FileRootKind::ExtendedUnc) {
        return Ok(v.to_vec());
    }

    // Normalize every separator to the preferred backslash, then re-parse the
    // root on the normalized text.
    let mut buffer = v.to_vec();
    for c in buffer.iter_mut() {
        if *c == FILE_POSIX_SEPARATOR {
            *c = FILE_PREFERRED_SEPARATOR;
        }
    }

    let (kind, root_len) = parse_root(&buffer);
    let root_text = &buffer[..root_len];
    let remainder = &buffer[root_len..];

    let segments = resolve_dot_segments(
        &split_segments(remainder, /* backslash_is_separator */ true),
        root_is_fully_qualified(kind, root_text),
    )?;

    Ok(join_root_and_segments(
        root_text,
        &segments,
        FILE_PREFERRED_SEPARATOR,
    ))
}

fn canonicalize_posix(v: &[u16]) -> FilesystemResult<Vec<u16>> {
    // POSIX recognizes only the single `/` root; collapse any run of leading
    // slashes to one. A backslash is an ordinary character here.
    let absolute = v.first() == Some(&FILE_POSIX_SEPARATOR);
    let (root_text, remainder): (&[u16], &[u16]) = if absolute {
        let mut i = 0;
        while i < v.len() && v[i] == FILE_POSIX_SEPARATOR {
            i += 1;
        }
        (&v[..1], &v[i..])
    } else {
        (&v[..0], v)
    };

    let segments = resolve_dot_segments(
        &split_segments(remainder, /* backslash_is_separator */ false),
        absolute,
    )?;

    Ok(join_root_and_segments(
        root_text,
        &segments,
        FILE_POSIX_SEPARATOR,
    ))
}

/// The separator that joins a child onto a path: POSIX paths join with `/`,
/// everything else with the preferred backslash.
const fn join_separator_for(kind: FileRootKind) -> u16 {
    match kind {
        FileRootKind::Posix => FILE_POSIX_SEPARATOR,
        _ => FILE_PREFERRED_SEPARATOR,
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

    /// The path's name components, root-first, for hierarchical (tree)
    /// navigation. The whole `native()` text is split on either separator
    /// (`\` or `/`) with empty components dropped, so a drive/UNC/POSIX root
    /// contributes its own leading component(s) (e.g. `C:\Windows\System32`
    /// yields `["C:", "Windows", "System32"]`).
    ///
    /// This is a surface-agnostic decomposition — the same rule the registry's
    /// `KeyPath` uses — chosen so the in-memory filesystem tree is a plain
    /// ordinal-keyed namespace. A POSIX filename that legitimately contains a
    /// backslash would be over-split; that limitation is acceptable for the
    /// isolated model and matches the registry path convention.
    #[must_use]
    pub fn components(&self) -> Vec<Utf16> {
        self.value
            .as_units()
            .split(|&u| u == FILE_PREFERRED_SEPARATOR || u == FILE_POSIX_SEPARATOR)
            .filter(|seg| !seg.is_empty())
            .map(|seg| Utf16::from_units(seg.to_vec()))
            .collect()
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

    /// The lexically canonical form of this path for the given surface (D22):
    /// separators normalized to the surface's preferred form, repeated
    /// separators collapsed, a trailing separator stripped (except a bare root),
    /// and `.`/`..` resolved lexically. A `..` that underflows a fully qualified
    /// root yields [`FilesystemError::InvalidPath`] (it is rejected, never
    /// clamped). Inside an extended-length (`\\?\` / `\\?\UNC\`) path nothing is
    /// normalized — the remainder is preserved verbatim, because Win32 treats
    /// such a path as a literally distinct object.
    ///
    /// # Errors
    /// Returns [`FilesystemError::InvalidPath`] when a `..` underflows an
    /// absolute root.
    pub fn lexically_normal(&self, surface: PathSurface) -> FilesystemResult<Self> {
        let normalized = match surface {
            PathSurface::Windows => canonicalize_windows(self.value.as_units())?,
            PathSurface::Posix => canonicalize_posix(self.value.as_units())?,
        };
        Ok(Self::from_units(normalized))
    }

    /// Split into `(parent, leaf)`. The leaf is the final component (the file or
    /// directory name); the parent is everything before it. A path with no
    /// parent (rootless single component, or a bare root) yields a `None`
    /// parent. Lexical: a single trailing separator past the root is ignored.
    #[must_use]
    pub fn split_parent_path_and_leaf_name(&self) -> (Option<Self>, Self) {
        let full = self.value.as_units();
        let root_text = &full[..self.root_length];
        let mut relative = &full[self.root_length..];

        let backslash_is_separator = self.root_kind != FileRootKind::Posix;
        let is_sep = |c: u16| {
            c == FILE_POSIX_SEPARATOR || (backslash_is_separator && c == FILE_PREFERRED_SEPARATOR)
        };

        // A trailing separator past the root names no leaf; ignore it.
        while relative.last().is_some_and(|&c| is_sep(c)) {
            relative = &relative[..relative.len() - 1];
        }

        if relative.is_empty() {
            return (None, Self::new());
        }

        // Index of the final separator within the relative remainder, if any.
        let last_sep = relative.iter().rposition(|&c| is_sep(c));

        let leaf_view = match last_sep {
            Some(i) => &relative[i + 1..],
            None => relative,
        };
        let leaf = Self::from_units(leaf_view.to_vec());

        let mut parent_rel = match last_sep {
            Some(i) => &relative[..i],
            None => &relative[..0],
        };
        while parent_rel.last().is_some_and(|&c| is_sep(c)) {
            parent_rel = &parent_rel[..parent_rel.len() - 1];
        }

        if parent_rel.is_empty() {
            if self.root_length == 0 {
                return (None, leaf);
            }
            return (Some(Self::from_units(root_text.to_vec())), leaf);
        }

        let mut parent_text = root_text.to_vec();
        parent_text.extend_from_slice(parent_rel);
        (Some(Self::from_units(parent_text)), leaf)
    }

    /// The path with its final component removed. A path that has no parent
    /// (rootless single component, or a bare root) returns an empty path; use
    /// [`split_parent_path_and_leaf_name`](Self::split_parent_path_and_leaf_name)
    /// to distinguish the cases.
    #[must_use]
    pub fn parent_path(&self) -> Self {
        self.split_parent_path_and_leaf_name()
            .0
            .unwrap_or_default()
    }

    /// True when the path has a parent (its final component is not the whole
    /// path).
    #[must_use]
    pub fn has_parent_path(&self) -> bool {
        self.split_parent_path_and_leaf_name().0.is_some()
    }

    /// Append `rhs` as a child component (the mutating join, `operator/=`).
    /// Appending a fully qualified path replaces this path entirely
    /// (`std::filesystem` semantics). The joining separator follows this path's
    /// convention (`/` for a POSIX root, otherwise `\`).
    pub fn push(&mut self, rhs: &Self) {
        // Appending a fully qualified path replaces this path entirely.
        if rhs.is_absolute() {
            *self = rhs.clone();
            return;
        }

        let rhs_value = rhs.value.as_units();
        if rhs_value.is_empty() {
            return;
        }

        let lhs_value = self.value.as_units();
        if lhs_value.is_empty() {
            *self = rhs.clone();
            return;
        }

        let need_sep = !is_separator(lhs_value[lhs_value.len() - 1]) && !is_separator(rhs_value[0]);
        let mut combined = lhs_value.to_vec();
        if need_sep {
            combined.push(join_separator_for(self.root_kind));
        }
        combined.extend_from_slice(rhs_value);
        *self = Self::from_units(combined);
    }

    /// The non-mutating join (`operator/`): `self` with `rhs` appended as a
    /// child component. See [`push`](Self::push) for the joining rules.
    #[must_use]
    pub fn join(&self, rhs: &Self) -> Self {
        let mut result = self.clone();
        result.push(rhs);
        result
    }

    /// Name comparison under a surface's case rules (D6/D22). The Windows
    /// surface compares ordinal case-insensitively (via the [`OrdinalCasing`]
    /// seam); the POSIX surface compares ordinal case-sensitively (code-unit
    /// order). The stored case is never altered — [`native`](Self::native)
    /// always returns the original casing; only the comparison folds case.
    /// Comparison operates on the path text exactly as stored; canonicalize both
    /// operands first if path (rather than byte) equivalence is wanted.
    #[must_use]
    pub fn equivalent<C: OrdinalCasing>(
        &self,
        other: &Self,
        surface: PathSurface,
        casing: &C,
    ) -> bool {
        let lhs = self.value.as_units();
        let rhs = other.value.as_units();
        match surface {
            PathSurface::Windows => casing.compare_ignore_case(lhs, rhs) == Ordering::Equal,
            PathSurface::Posix => lhs == rhs,
        }
    }

    /// Strict-weak ordering consistent with [`equivalent`](Self::equivalent):
    /// two paths are unordered (neither precedes the other) iff they are
    /// equivalent under the same surface.
    #[must_use]
    pub fn precedes<C: OrdinalCasing>(
        &self,
        other: &Self,
        surface: PathSurface,
        casing: &C,
    ) -> bool {
        let lhs = self.value.as_units();
        let rhs = other.value.as_units();
        match surface {
            PathSurface::Windows => casing.compare_ignore_case(lhs, rhs) == Ordering::Less,
            PathSurface::Posix => lhs < rhs,
        }
    }
}

impl core::ops::Div<&FilePath> for &FilePath {
    type Output = FilePath;

    fn div(self, rhs: &FilePath) -> FilePath {
        self.join(rhs)
    }
}

impl core::ops::Div<FilePath> for FilePath {
    type Output = FilePath;

    fn div(mut self, rhs: FilePath) -> FilePath {
        self.push(&rhs);
        self
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

    // ------------------------------------------------------------------
    // M-FS-PATH-2: lexically_normal, parent/leaf split, join.
    // ------------------------------------------------------------------

    fn normal(s: &str, surface: PathSurface) -> Utf16 {
        FilePath::from_utf8(s)
            .lexically_normal(surface)
            .expect("normalization should succeed")
            .native()
            .clone()
    }

    #[test]
    fn normalize_forward_slashes_to_backslashes_windows() {
        assert_eq!(
            normal("C:/Windows/System32", PathSurface::Windows),
            w("C:\\Windows\\System32")
        );
    }

    #[test]
    fn collapse_repeated_separators_windows() {
        assert_eq!(
            normal("C:\\\\Windows\\\\\\System32", PathSurface::Windows),
            w("C:\\Windows\\System32")
        );
    }

    #[test]
    fn strip_trailing_separator_windows() {
        assert_eq!(normal("C:\\Windows\\", PathSurface::Windows), w("C:\\Windows"));
    }

    #[test]
    fn bare_root_keeps_trailing_separator() {
        assert_eq!(normal("C:\\", PathSurface::Windows), w("C:\\"));
    }

    #[test]
    fn resolve_dot_and_dotdot_windows() {
        assert_eq!(
            normal("C:\\a\\.\\b\\..\\c", PathSurface::Windows),
            w("C:\\a\\c")
        );
    }

    #[test]
    fn relative_leading_dotdot_preserved() {
        assert_eq!(
            normal("..\\..\\a\\b", PathSurface::Windows),
            w("..\\..\\a\\b")
        );
    }

    #[test]
    fn dotdot_underflow_absolute_windows_is_rejected() {
        let err = FilePath::from_utf8("C:\\..")
            .lexically_normal(PathSurface::Windows)
            .unwrap_err();
        assert!(matches!(err, FilesystemError::InvalidPath(_)));
    }

    #[test]
    fn dotdot_underflow_posix_is_rejected() {
        let err = FilePath::from_utf8("/..")
            .lexically_normal(PathSurface::Posix)
            .unwrap_err();
        assert!(matches!(err, FilesystemError::InvalidPath(_)));
    }

    #[test]
    fn extended_length_not_normalized() {
        // Win32 treats "\\?\C:\a\..\b" as a literally distinct object: nothing
        // past the prefix is normalized.
        let verbatim = "\\\\?\\C:\\a\\..\\b";
        assert_eq!(normal(verbatim, PathSurface::Windows), w(verbatim));
    }

    #[test]
    fn posix_surface_treats_backslash_as_name() {
        assert_eq!(
            normal("/usr//local/../bin", PathSurface::Posix),
            w("/usr/bin")
        );
    }

    #[test]
    fn posix_relative_dot_resolution() {
        assert_eq!(normal("a/./b/../c", PathSurface::Posix), w("a/c"));
    }

    #[test]
    fn split_drive_absolute() {
        let (parent, leaf) =
            FilePath::from_utf8("C:\\Windows\\System32").split_parent_path_and_leaf_name();
        assert_eq!(parent.unwrap().native(), &w("C:\\Windows"));
        assert_eq!(leaf.native(), &w("System32"));
    }

    #[test]
    fn split_single_component_under_root() {
        let (parent, leaf) = FilePath::from_utf8("C:\\Windows").split_parent_path_and_leaf_name();
        assert_eq!(parent.unwrap().native(), &w("C:\\"));
        assert_eq!(leaf.native(), &w("Windows"));
    }

    #[test]
    fn split_bare_root_has_no_parent_or_leaf() {
        let (parent, leaf) = FilePath::from_utf8("C:\\").split_parent_path_and_leaf_name();
        assert!(parent.is_none());
        assert!(leaf.native().is_empty());
    }

    #[test]
    fn split_rootless_single_component() {
        let (parent, leaf) = FilePath::from_utf8("foo").split_parent_path_and_leaf_name();
        assert!(parent.is_none());
        assert_eq!(leaf.native(), &w("foo"));
    }

    #[test]
    fn split_posix_path() {
        let (parent, leaf) = FilePath::from_utf8("/usr/bin").split_parent_path_and_leaf_name();
        assert_eq!(parent.unwrap().native(), &w("/usr"));
        assert_eq!(leaf.native(), &w("bin"));
    }

    #[test]
    fn split_trailing_separator_ignored() {
        let (parent, leaf) = FilePath::from_utf8("C:\\Windows\\").split_parent_path_and_leaf_name();
        assert_eq!(parent.unwrap().native(), &w("C:\\"));
        assert_eq!(leaf.native(), &w("Windows"));
    }

    #[test]
    fn parent_path_and_has_parent() {
        let p = FilePath::from_utf8("C:\\Windows\\System32");
        assert!(p.has_parent_path());
        assert_eq!(p.parent_path().native(), &w("C:\\Windows"));

        let root = FilePath::from_utf8("C:\\");
        assert!(!root.has_parent_path());
        assert!(root.parent_path().native().is_empty());
    }

    #[test]
    fn append_relative_component() {
        assert_eq!(
            (FilePath::from_utf8("C:\\x").join(&FilePath::from_utf8("y"))).native(),
            &w("C:\\x\\y")
        );
        assert_eq!(
            (FilePath::from_utf8("C:\\").join(&FilePath::from_utf8("y"))).native(),
            &w("C:\\y")
        );
        assert_eq!(
            (FilePath::from_utf8("/usr").join(&FilePath::from_utf8("bin"))).native(),
            &w("/usr/bin")
        );
        assert_eq!(
            (FilePath::from_utf8("").join(&FilePath::from_utf8("y"))).native(),
            &w("y")
        );
    }

    #[test]
    fn append_absolute_replaces() {
        let result = FilePath::from_utf8("C:\\x").join(&FilePath::from_utf8("D:\\y"));
        assert_eq!(result.native(), &w("D:\\y"));
    }

    #[test]
    fn div_operator_joins() {
        let result = &FilePath::from_utf8("C:\\x") / &FilePath::from_utf8("y");
        assert_eq!(result.native(), &w("C:\\x\\y"));

        let owned = FilePath::from_utf8("/usr") / FilePath::from_utf8("bin");
        assert_eq!(owned.native(), &w("/usr/bin"));
    }

    // ------------------------------------------------------------------
    // M-FS-PATH-3: name comparison by surface (D6/D22).
    // ------------------------------------------------------------------

    fn casing() -> windows_text::AsciiOrdinalCasing {
        windows_text::AsciiOrdinalCasing
    }

    #[test]
    fn windows_equivalence_folds_case() {
        let a = FilePath::from_utf8("Foo");
        let b = FilePath::from_utf8("foo");
        let c = casing();
        assert!(a.equivalent(&b, PathSurface::Windows, &c));
        assert!(!a.precedes(&b, PathSurface::Windows, &c));
        assert!(!b.precedes(&a, PathSurface::Windows, &c));
    }

    #[test]
    fn posix_equivalence_is_case_sensitive() {
        let a = FilePath::from_utf8("Foo");
        let b = FilePath::from_utf8("foo");
        let c = casing();
        assert!(!a.equivalent(&b, PathSurface::Posix, &c));
        // Distinct under POSIX: exactly one of the two orderings holds.
        assert_ne!(
            a.precedes(&b, PathSurface::Posix, &c),
            b.precedes(&a, PathSurface::Posix, &c)
        );
    }

    #[test]
    fn comparison_preserves_stored_case() {
        let a = FilePath::from_utf8("C:\\Windows\\System32");
        let b = FilePath::from_utf8("c:\\windows\\system32");
        let c = casing();
        assert!(a.equivalent(&b, PathSurface::Windows, &c));
        assert_eq!(a.native(), &w("C:\\Windows\\System32"));
        assert_eq!(b.native(), &w("c:\\windows\\system32"));
    }

    #[test]
    fn equivalence_consistent_with_ordering() {
        let c = casing();
        let a = FilePath::from_utf8("alpha");
        let b = FilePath::from_utf8("beta");
        assert!(!a.equivalent(&b, PathSurface::Windows, &c));
        assert!(a.precedes(&b, PathSurface::Windows, &c));
        assert!(!b.precedes(&a, PathSurface::Windows, &c));

        let g1 = FilePath::from_utf8("GAMMA");
        let g2 = FilePath::from_utf8("gamma");
        assert!(g1.equivalent(&g2, PathSurface::Windows, &c));
        assert!(!g1.precedes(&g2, PathSurface::Windows, &c));
        assert!(!g2.precedes(&g1, PathSurface::Windows, &c));
    }

    #[test]
    fn posix_ordering_is_ordinal() {
        // Uppercase letters sort before lowercase in ordinal (code-unit) order.
        let c = casing();
        let upper = FilePath::from_utf8("Z");
        let lower = FilePath::from_utf8("a");
        assert!(upper.precedes(&lower, PathSurface::Posix, &c));
        assert!(!lower.precedes(&upper, PathSurface::Posix, &c));
    }

    // ------------------------------------------------------------------
    // M-FS-PATH-4: table-driven canonicalization sweep + properties.
    // ------------------------------------------------------------------

    const CANON_TABLE: &[(PathSurface, &str, &str)] = &[
        // Windows: ordinary cases.
        (PathSurface::Windows, "C:\\Windows\\System32", "C:\\Windows\\System32"),
        (PathSurface::Windows, "C:/Windows/System32", "C:\\Windows\\System32"),
        (PathSurface::Windows, "C:\\\\Windows\\\\\\System32", "C:\\Windows\\System32"),
        (PathSurface::Windows, "C:\\Windows\\", "C:\\Windows"),
        (PathSurface::Windows, "C:\\", "C:\\"),
        (PathSurface::Windows, "C:\\a\\.\\b\\..\\c", "C:\\a\\c"),
        (PathSurface::Windows, "relative\\path", "relative\\path"),
        (PathSurface::Windows, "foo\\\\bar", "foo\\bar"),
        (PathSurface::Windows, ".\\foo", "foo"),
        (PathSurface::Windows, "a\\b\\..\\..\\c", "c"),
        (PathSurface::Windows, "C:foo\\bar", "C:foo\\bar"),
        // Windows: edges.
        (PathSurface::Windows, "..\\..\\a\\b", "..\\..\\a\\b"),
        (
            PathSurface::Windows,
            "\\\\server\\share\\dir\\file",
            "\\\\server\\share\\dir\\file",
        ),
        (PathSurface::Windows, "\\\\server\\share\\", "\\\\server\\share\\"),
        (PathSurface::Windows, "\\\\.\\PhysicalDrive0", "\\\\.\\PhysicalDrive0"),
        (PathSurface::Windows, "\\\\?\\C:\\a\\..\\b", "\\\\?\\C:\\a\\..\\b"),
        (PathSurface::Windows, "", ""),
        (PathSurface::Windows, "C:\\a\\b\\c\\d\\e\\..\\..\\f", "C:\\a\\b\\c\\f"),
        (PathSurface::Windows, "C:\\foo.", "C:\\foo."),
        (PathSurface::Windows, "C:\\foo ", "C:\\foo "),
        // POSIX: ordinary cases.
        (PathSurface::Posix, "/usr/bin", "/usr/bin"),
        (PathSurface::Posix, "/usr//local/../bin", "/usr/bin"),
        (PathSurface::Posix, "a/./b/../c", "a/c"),
        (PathSurface::Posix, "/", "/"),
        (PathSurface::Posix, "usr/local/bin", "usr/local/bin"),
        // POSIX: edges.
        (PathSurface::Posix, "//foo/bar", "/foo/bar"),
        (PathSurface::Posix, "a\\b", "a\\b"),
        (PathSurface::Posix, "/a/b/c/../../d", "/a/d"),
    ];

    #[test]
    fn canonicalization_table() {
        for (i, (surface, input, expected)) in CANON_TABLE.iter().enumerate() {
            assert_eq!(normal(input, *surface), w(expected), "canon_table row {i}");
        }
    }

    #[test]
    fn canonicalization_is_idempotent() {
        for (i, (surface, input, _)) in CANON_TABLE.iter().enumerate() {
            let once = FilePath::from_utf8(input).lexically_normal(*surface).unwrap();
            let twice = once.lexically_normal(*surface).unwrap();
            assert_eq!(once.native(), twice.native(), "canon_table row {i}");
        }
    }

    #[test]
    fn extended_literal_differs_from_normalized_sibling() {
        let literal = normal("\\\\?\\C:\\a\\..\\b", PathSurface::Windows);
        let sibling = normal("C:\\a\\..\\b", PathSurface::Windows);
        assert_eq!(literal, w("\\\\?\\C:\\a\\..\\b"));
        assert_eq!(sibling, w("C:\\b"));
        assert_ne!(literal, sibling);
    }

    #[test]
    fn dotdot_past_root_rejected_both_surfaces() {
        assert!(matches!(
            FilePath::from_utf8("C:\\a\\..\\..\\b")
                .lexically_normal(PathSurface::Windows)
                .unwrap_err(),
            FilesystemError::InvalidPath(_)
        ));
        assert!(matches!(
            FilePath::from_utf8("/a/../../b")
                .lexically_normal(PathSurface::Posix)
                .unwrap_err(),
            FilesystemError::InvalidPath(_)
        ));
    }
}

