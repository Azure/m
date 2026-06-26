// Copyright (c) Microsoft Corporation.

//! Caller identity for `wordy`'s custom-dictionary routes (WD-D13).
//!
//! These types were extracted from the former filesystem custom store
//! (`custom.rs`) when the store moved out to `merriam` (MW18-3): they are pure,
//! platform-independent identity, with no storage concern. The caller is
//! resolved from the [`USER_HEADER`] request header and defaults to a single
//! built-in user — the "app reads its own claims" posture of a real HWC
//! application.

/// The request header `wordy` reads to identify the calling user. Absent or
/// blank, the [`DEFAULT_USER`] is used.
pub const USER_HEADER: &str = "X-Wordy-User";

/// The built-in user used when no [`USER_HEADER`] is supplied.
pub const DEFAULT_USER: &str = "default";

/// A user identity, resolved from a request header. Modeled as a newtype so
/// per-user claims can grow here later.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UserId(String);

impl UserId {
    /// Build a user id from a raw header value. Leading / trailing whitespace is
    /// trimmed; an empty result falls back to [`DEFAULT_USER`].
    pub fn new(raw: &str) -> Self {
        let trimmed = raw.trim();
        if trimmed.is_empty() {
            UserId(DEFAULT_USER.to_string())
        } else {
            UserId(trimmed.to_string())
        }
    }

    /// The raw user-id string.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl Default for UserId {
    fn default() -> Self {
        UserId(DEFAULT_USER.to_string())
    }
}

/// The authenticated (today: asserted) caller. `wordy` performs no real logon;
/// it threads a [`Principal`] through every handler as if it did, resolving it
/// from the [`USER_HEADER`] and defaulting to a single built-in user.
#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct Principal {
    user: UserId,
}

impl Principal {
    /// Build a principal for an explicit user.
    pub fn new(user: UserId) -> Self {
        Principal { user }
    }

    /// Resolve a principal from an optional [`USER_HEADER`] value, defaulting to
    /// the built-in user when absent or blank.
    pub fn from_header(header: Option<&str>) -> Self {
        Principal {
            user: UserId::new(header.unwrap_or("")),
        }
    }

    /// The user this principal represents.
    pub fn user_id(&self) -> &UserId {
        &self.user
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blank_user_falls_back_to_default() {
        assert_eq!(UserId::new("   ").as_str(), DEFAULT_USER);
        assert_eq!(UserId::new("").as_str(), DEFAULT_USER);
    }

    #[test]
    fn user_is_trimmed() {
        assert_eq!(UserId::new("  alice  ").as_str(), "alice");
    }

    #[test]
    fn principal_from_header_resolves_or_defaults() {
        assert_eq!(Principal::from_header(Some("bob")).user_id().as_str(), "bob");
        assert_eq!(Principal::from_header(None).user_id().as_str(), DEFAULT_USER);
        assert_eq!(Principal::from_header(Some("  ")).user_id().as_str(), DEFAULT_USER);
    }
}
