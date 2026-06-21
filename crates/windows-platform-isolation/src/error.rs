// Copyright (c) Microsoft Corporation.

//! The registry surface's error type (D14): a hand-rolled, surface-specific
//! `enum`. It is deliberately *not* shared with other surfaces and *not*
//! derived from the C++ `error_handling` types. Each surface owns its own
//! contract.

use core::fmt;

/// Errors produced by the registry isolation surface.
///
/// `#[non_exhaustive]` because later milestones add failure modes (e.g. value
/// type mismatches once typed values exist); callers must keep a wildcard arm.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum RegistryError {
    /// A stored OS string was not well-formed UTF-16, so it cannot be produced
    /// as UTF-8 at the public boundary (D9). The raw code units are preserved
    /// internally without loss; only egress to `String` fails.
    IllFormedUtf16,
    /// The named key does not exist.
    KeyNotFound,
    /// The named value does not exist.
    ValueNotFound,
}

impl fmt::Display for RegistryError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::IllFormedUtf16 => f.write_str("stored string is not well-formed UTF-16"),
            Self::KeyNotFound => f.write_str("registry key not found"),
            Self::ValueNotFound => f.write_str("registry value not found"),
        }
    }
}

impl std::error::Error for RegistryError {}

/// Result alias for the registry surface.
pub type Result<T> = core::result::Result<T, RegistryError>;
