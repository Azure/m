// Copyright (c) Microsoft Corporation.

//! The egress surface's error type (D14): a hand-rolled, surface-specific `enum`,
//! deliberately *not* shared with the registry / filesystem / web errors. The
//! egress surface owns its own contract (D31).

use core::fmt;

/// Errors produced by the egress (network-client) isolation surface (D31).
///
/// `#[non_exhaustive]` because later M11 items add failure modes (live WinHTTP
/// status, replay-fixture decode) as the surface grows; callers must keep a
/// wildcard arm.
#[derive(Debug, Clone, PartialEq, Eq)]
#[non_exhaustive]
pub enum EgressError {
    /// The request was not well-formed for the transport — e.g. an empty host,
    /// or a verb/scheme the surface cannot represent. The string is diagnostic,
    /// not a stable contract.
    InvalidRequest(String),
    /// A blocking surface denied the request (the `block` mode).
    Blocked,
    /// A replay surface had no fixture for the request and no fall-through inner
    /// to satisfy it. The string names the unmatched target, diagnostically.
    NoFixture(String),
    /// A live-provider WinHTTP call failed with the carried `WIN32_ERROR` status
    /// code (mirror of [`FilesystemError::Os`](crate::FilesystemError::Os)). Used
    /// only by the Windows live egress provider; the in-memory surfaces never
    /// produce it.
    Os(u32),
    /// A serialized replay-fixture artifact could not be decoded. The string is
    /// diagnostic, not a stable contract.
    MalformedFixture(String),
    /// A stored host / path / header was not well-formed UTF-16, so it cannot be
    /// produced as UTF-8 at the public boundary (D9). The raw code units are
    /// preserved internally; only egress to `String` fails.
    IllFormedUtf16,
}

impl fmt::Display for EgressError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidRequest(detail) => write!(f, "invalid egress request: {detail}"),
            Self::Blocked => f.write_str("egress request blocked by policy"),
            Self::NoFixture(target) => write!(f, "no replay fixture for {target}"),
            Self::Os(code) => write!(f, "OS egress error {code}"),
            Self::MalformedFixture(detail) => write!(f, "malformed egress fixture: {detail}"),
            Self::IllFormedUtf16 => {
                f.write_str("stored egress string is not well-formed UTF-16")
            }
        }
    }
}

impl std::error::Error for EgressError {}

/// Result alias for the egress surface.
pub type EgressResult<T> = core::result::Result<T, EgressError>;
