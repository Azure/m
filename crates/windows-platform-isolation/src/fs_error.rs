// Copyright (c) Microsoft Corporation.

//! The filesystem surface's error type (D14): a hand-rolled, surface-specific
//! `enum`, deliberately *not* shared with [`RegistryError`](crate::RegistryError)
//! and *not* derived from the C++ `error_handling` types. Each surface owns its
//! own contract.

use core::fmt;

/// Errors produced by the filesystem isolation surface.
///
/// `#[non_exhaustive]` because later milestones add failure modes (existence,
/// access, live-provider OS errors) as the surface grows; callers must keep a
/// wildcard arm.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum FilesystemError {
    /// A stored OS path or name was not well-formed UTF-16, so it cannot be
    /// produced as UTF-8 at the public boundary (D9). The raw code units are
    /// preserved internally without loss; only egress to `String` fails.
    IllFormedUtf16,
    /// A path was lexically invalid for the requested operation — e.g. a `..`
    /// component that underflows a fully qualified root during normalization
    /// (D22). The string describes what went wrong; it is diagnostic, not a
    /// stable contract.
    InvalidPath(String),
    /// No filesystem node exists at the requested path (the directory or file
    /// is absent, or a tombstone shadows it).
    NotFound,
    /// A serialized C++ PIL filesystem artifact could not be decoded — the XML
    /// was not well-formed or a required attribute was missing or unparseable.
    /// The string is diagnostic, not a stable contract.
    MalformedArtifact(String),
}

impl fmt::Display for FilesystemError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::IllFormedUtf16 => f.write_str("stored path is not well-formed UTF-16"),
            Self::InvalidPath(detail) => write!(f, "invalid path: {detail}"),
            Self::NotFound => f.write_str("no filesystem node exists at the path"),
            Self::MalformedArtifact(detail) => write!(f, "malformed filesystem artifact: {detail}"),
        }
    }
}

impl std::error::Error for FilesystemError {}

/// Result alias for the filesystem surface.
pub type FilesystemResult<T> = core::result::Result<T, FilesystemError>;
