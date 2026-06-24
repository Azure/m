// Copyright (c) Microsoft Corporation.

//! Pure request routing for `wordy`.
//!
//! This module is the safe, platform-independent core that decides what `wordy`
//! should do with an incoming request. The IIS ABI boundary (`iis`, Windows
//! only) decodes the host request into a method + URL and asks [`dispatch`] for
//! an [`Outcome`]; it then realizes that outcome against the host response. No
//! Windows or host types appear here, so the routing logic is fully unit-testable
//! on any platform.
//!
//! MW13-1 seeds the dispatcher with a single health route; later milestones grow
//! it into the full dictionary surface (spell-check, anagram, suggestions, and
//! per-user custom dictionaries).

/// The standard HTTP success status, returned by the health route.
pub const STATUS_OK: u16 = 200;

/// What `wordy` decided to do with a request.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Outcome {
    /// Respond immediately with the given status and reason phrase, finishing the
    /// request without invoking any later module in the pipeline.
    Respond {
        /// HTTP status code to set on the response.
        status: u16,
        /// HTTP reason phrase to accompany the status.
        reason: &'static str,
    },
    /// `wordy` does not handle this request; let the host continue its pipeline.
    Continue,
}

/// Decide how to handle a request given its method and (possibly absolute) URL.
///
/// The URL may be an absolute form (`http://host/path?query`) or an
/// origin-relative form (`/path?query`); [`path_of`] is used to extract the
/// comparable path component either way.
pub fn dispatch(method: &str, url: &str) -> Outcome {
    let path = path_of(url);
    match (method, path) {
        ("GET", "/healthz") => Outcome::Respond {
            status: STATUS_OK,
            reason: "OK",
        },
        _ => Outcome::Continue,
    }
}

/// Extract the path component of a request URL.
///
/// Strips an optional `scheme://authority` prefix and any `?query` / `#fragment`
/// suffix, returning the leading path. An input that is already a bare path is
/// returned (minus its query/fragment). An empty or schemeless-authority-only
/// input yields `"/"`.
pub fn path_of(url: &str) -> &str {
    // Drop a leading "scheme://authority" if present.
    let after_authority = match url.find("://") {
        Some(scheme_end) => {
            let rest = &url[scheme_end + "://".len()..];
            match rest.find('/') {
                Some(path_start) => &rest[path_start..],
                // Authority with no path (e.g. "http://host") → root.
                None => "/",
            }
        }
        None => url,
    };

    // Trim a query and/or fragment suffix.
    let path_end = after_authority
        .find(['?', '#'])
        .unwrap_or(after_authority.len());
    let path = &after_authority[..path_end];

    if path.is_empty() { "/" } else { path }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn health_route_responds_200_for_get() {
        assert_eq!(
            dispatch("GET", "/healthz"),
            Outcome::Respond {
                status: STATUS_OK,
                reason: "OK"
            }
        );
    }

    #[test]
    fn health_route_responds_for_absolute_url() {
        assert_eq!(
            dispatch("GET", "http://localhost:8080/healthz"),
            Outcome::Respond {
                status: STATUS_OK,
                reason: "OK"
            }
        );
    }

    #[test]
    fn health_route_responds_when_url_carries_a_query() {
        assert_eq!(
            dispatch("GET", "/healthz?verbose=1"),
            Outcome::Respond {
                status: STATUS_OK,
                reason: "OK"
            }
        );
    }

    #[test]
    fn health_route_ignores_non_get_methods() {
        assert_eq!(dispatch("POST", "/healthz"), Outcome::Continue);
        assert_eq!(dispatch("DELETE", "/healthz"), Outcome::Continue);
    }

    #[test]
    fn unknown_routes_continue() {
        assert_eq!(dispatch("GET", "/"), Outcome::Continue);
        assert_eq!(dispatch("GET", "/words"), Outcome::Continue);
        assert_eq!(dispatch("GET", "/healthz/extra"), Outcome::Continue);
    }

    #[test]
    fn path_of_returns_bare_path() {
        assert_eq!(path_of("/healthz"), "/healthz");
    }

    #[test]
    fn path_of_strips_scheme_and_authority() {
        assert_eq!(path_of("https://host:443/a/b"), "/a/b");
    }

    #[test]
    fn path_of_strips_query_and_fragment() {
        assert_eq!(path_of("/a/b?x=1&y=2"), "/a/b");
        assert_eq!(path_of("/a/b#frag"), "/a/b");
        assert_eq!(path_of("https://host/a?x=1#f"), "/a");
    }

    #[test]
    fn path_of_handles_authority_only() {
        assert_eq!(path_of("http://host"), "/");
    }

    #[test]
    fn path_of_handles_empty_input() {
        assert_eq!(path_of(""), "/");
    }

    #[test]
    fn path_of_handles_root() {
        assert_eq!(path_of("/"), "/");
        assert_eq!(path_of("http://host/"), "/");
    }
}
