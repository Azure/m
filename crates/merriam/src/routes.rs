// Copyright (c) Microsoft Corporation.

//! Request routing for `merriam` — the REST dictionary-store surface (MER-D4).
//!
//! This is the safe, listener-independent core that turns a decoded HTTP
//! request into a response, the peer of `wordy::routes::Service`. The http.sys
//! ABI boundary (MW18-2.3, Windows only) decodes the inbound request into an
//! [`HttpRequest`], asks a [`Service`] to [`dispatch`](Service::dispatch) it,
//! and writes the resulting [`Outcome`] back. No listener types appear here, so
//! the whole surface is unit-testable off any listener.
//!
//! The routes are a **1:1 subset of `wordy`'s custom-dictionary routes**, with
//! byte-identical JSON bodies, so the MW18-3 relay (`wordy` → `merriam` over
//! WinHTTP) maps a `wordy` custom-dict call directly onto the matching `merriam`
//! call:
//!
//! | Method & path           | Result body                  |
//! |-------------------------|------------------------------|
//! | `GET /healthz`          | `{"status":"ok"}`            |
//! | `GET /custom[?pattern=]`| `{"matches":[…]}`            |
//! | `GET /custom/{word}`    | `{"word":…,"exists":bool}`   |
//! | `POST /custom/{word}`   | `{"word":…,"added":bool}`    |
//! | `DELETE /custom/{word}` | `{"word":…,"removed":bool}`  |
//!
//! The caller is identified by the `X-Wordy-User` header (defaulting to
//! `default`) and the locale by the `X-Wordy-Locale` header (defaulting to
//! `en-US`), matching `wordy`'s conventions. Unknown method/path pairs yield
//! [`Outcome::Continue`].

use regex::Regex;
use serde::Serialize;

use crate::store::{Store, StoreError};

/// HTTP 200.
pub const STATUS_OK: u16 = 200;
/// HTTP 400.
pub const STATUS_BAD_REQUEST: u16 = 400;
/// HTTP 500.
pub const STATUS_INTERNAL_ERROR: u16 = 500;

/// The JSON media type written on every modeled response body.
pub const CONTENT_TYPE_JSON: &str = "application/json";

/// The request header identifying the calling user.
pub const USER_HEADER: &str = "X-Wordy-User";
/// The request header carrying the locale.
pub const LOCALE_HEADER: &str = "X-Wordy-Locale";
/// The user used when [`USER_HEADER`] is absent or blank.
pub const DEFAULT_USER: &str = "default";
/// The locale used when [`LOCALE_HEADER`] is absent or blank.
pub const DEFAULT_LOCALE: &str = "en-US";

/// The path prefix for per-word custom-dictionary routes.
const CUSTOM_WORD_PREFIX: &str = "/custom/";

/// Upper bound on the number of words returned in an enumeration response.
const MAX_MATCHES: usize = 1000;

/// A decoded inbound request, listener-agnostic.
#[derive(Debug, Clone, Default)]
pub struct HttpRequest {
    /// The HTTP method (e.g. `"GET"`).
    pub method: String,
    /// The request URL, absolute or origin-relative (`/path?query`).
    pub url: String,
    /// The request body (empty when none).
    pub body: String,
    /// The `X-Wordy-User` header value, if present.
    pub user: Option<String>,
    /// The `X-Wordy-Locale` header value, if present.
    pub locale: Option<String>,
}

/// A response `merriam` wants the listener to send.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HttpResponse {
    /// HTTP status code.
    pub status: u16,
    /// HTTP reason phrase.
    pub reason: &'static str,
    /// Response content type.
    pub content_type: &'static str,
    /// Response body (may be empty).
    pub body: String,
}

/// What `merriam` decided to do with a request.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Outcome {
    /// Send this response and finish the request.
    Respond(HttpResponse),
    /// `merriam` does not handle this request.
    Continue,
}

// --- JSON models (byte-identical to `wordy`'s custom-dict responses) --------

#[derive(Serialize)]
struct MatchesResponse {
    matches: Vec<String>,
}

#[derive(Serialize)]
struct AddedResponse {
    word: String,
    added: bool,
}

#[derive(Serialize)]
struct RemovedResponse {
    word: String,
    removed: bool,
}

#[derive(Serialize)]
struct ExistsResponse {
    word: String,
    exists: bool,
}

#[derive(Serialize)]
struct StatusResponse {
    status: &'static str,
}

#[derive(Serialize)]
struct ErrorResponse {
    error: String,
}

/// The `merriam` REST service: a [`Store`] wired to the routing surface.
pub struct Service {
    store: Store,
}

impl Service {
    /// Build a service over the given store.
    pub fn new(store: Store) -> Self {
        Service { store }
    }

    /// The store this service serves (for tests / introspection).
    pub fn store(&self) -> &Store {
        &self.store
    }

    /// Route `req` to a handler, or decline it with [`Outcome::Continue`].
    pub fn dispatch(&self, req: &HttpRequest) -> Outcome {
        let path = path_of(&req.url);
        let query = query_of(&req.url);
        let user = resolve(req.user.as_deref(), DEFAULT_USER);
        let locale = resolve(req.locale.as_deref(), DEFAULT_LOCALE);

        let response = match (req.method.as_str(), path) {
            ("GET", "/healthz") => health(),
            ("GET", "/custom") => self.custom_enum(&locale, &user, query),
            ("POST", p) if p.starts_with(CUSTOM_WORD_PREFIX) => {
                self.custom_add(&locale, &user, &word_segment(p))
            }
            ("DELETE", p) if p.starts_with(CUSTOM_WORD_PREFIX) => {
                self.custom_remove(&locale, &user, &word_segment(p))
            }
            ("GET", p) if p.starts_with(CUSTOM_WORD_PREFIX) => {
                self.custom_exists(&locale, &user, &word_segment(p))
            }
            _ => return Outcome::Continue,
        };
        Outcome::Respond(response)
    }

    /// `GET /custom?pattern=`: enumerate the caller's dictionary, optionally
    /// filtered by a full-match regex.
    fn custom_enum(&self, locale: &str, user: &str, query: &str) -> HttpResponse {
        let words = match self.store.list(locale, user) {
            Ok(words) => words,
            Err(e) => return store_error(&e),
        };
        let matches = match query_param(query, "pattern") {
            Some(pattern) => {
                let regex = match Regex::new(&format!("^(?:{pattern})$")) {
                    Ok(regex) => regex,
                    Err(e) => return bad_request(format!("invalid pattern: {e}")),
                };
                words.into_iter().filter(|w| regex.is_match(w)).collect()
            }
            None => words,
        };
        json_ok(&MatchesResponse { matches: capped(matches) })
    }

    /// `POST /custom/{word}`: add a word.
    fn custom_add(&self, locale: &str, user: &str, word: &str) -> HttpResponse {
        match self.store.add(locale, user, word) {
            Ok(added) => json_ok(&AddedResponse { word: word.to_string(), added }),
            Err(e) => store_error(&e),
        }
    }

    /// `DELETE /custom/{word}`: remove a word.
    fn custom_remove(&self, locale: &str, user: &str, word: &str) -> HttpResponse {
        match self.store.remove(locale, user, word) {
            Ok(removed) => json_ok(&RemovedResponse { word: word.to_string(), removed }),
            Err(e) => store_error(&e),
        }
    }

    /// `GET /custom/{word}`: test membership.
    fn custom_exists(&self, locale: &str, user: &str, word: &str) -> HttpResponse {
        match self.store.contains(locale, user, word) {
            Ok(exists) => json_ok(&ExistsResponse { word: word.to_string(), exists }),
            Err(e) => store_error(&e),
        }
    }
}

// --- Response helpers ------------------------------------------------------

/// Resolve a header value to a non-empty string, defaulting when absent/blank.
fn resolve(header: Option<&str>, default: &str) -> String {
    match header {
        Some(v) if !v.trim().is_empty() => v.trim().to_string(),
        _ => default.to_string(),
    }
}

/// The health response.
fn health() -> HttpResponse {
    json_ok(&StatusResponse { status: "ok" })
}

/// Serialize `value` into a `200 OK` JSON response.
fn json_ok<T: Serialize>(value: &T) -> HttpResponse {
    HttpResponse {
        status: STATUS_OK,
        reason: "OK",
        content_type: CONTENT_TYPE_JSON,
        body: to_json(value),
    }
}

/// A `400 Bad Request` JSON error response.
fn bad_request(message: impl Into<String>) -> HttpResponse {
    json_error(STATUS_BAD_REQUEST, "Bad Request", message.into())
}

/// A `500 Internal Server Error` JSON error response.
fn server_error(message: impl Into<String>) -> HttpResponse {
    json_error(STATUS_INTERNAL_ERROR, "Internal Server Error", message.into())
}

/// Map a store error to a response: an invalid word is a client error (400);
/// anything else is a server error (500).
fn store_error(error: &StoreError) -> HttpResponse {
    match error {
        StoreError::InvalidWord(_) => bad_request(error.to_string()),
        StoreError::Io(_) => server_error(error.to_string()),
    }
}

/// Build a JSON error response.
fn json_error(status: u16, reason: &'static str, message: String) -> HttpResponse {
    HttpResponse {
        status,
        reason,
        content_type: CONTENT_TYPE_JSON,
        body: to_json(&ErrorResponse { error: message }),
    }
}

/// Serialize a model to JSON, falling back to a fixed error document on the
/// (practically impossible) serialization failure.
fn to_json<T: Serialize>(value: &T) -> String {
    serde_json::to_string(value)
        .unwrap_or_else(|_| String::from(r#"{"error":"serialization failed"}"#))
}

/// Truncate a result list to [`MAX_MATCHES`].
fn capped(mut words: Vec<String>) -> Vec<String> {
    words.truncate(MAX_MATCHES);
    words
}

// --- URL parsing (mirrors `wordy::routes`) ---------------------------------

/// Extract the path component of a request URL (strips `scheme://authority` and
/// any `?query` / `#fragment`). An empty or authority-only input yields `"/"`.
pub fn path_of(url: &str) -> &str {
    let after_authority = match url.find("://") {
        Some(scheme_end) => {
            let rest = &url[scheme_end + "://".len()..];
            match rest.find('/') {
                Some(path_start) => &rest[path_start..],
                None => "/",
            }
        }
        None => url,
    };
    let path_end = after_authority.find(['?', '#']).unwrap_or(after_authority.len());
    let path = &after_authority[..path_end];
    if path.is_empty() { "/" } else { path }
}

/// Extract the query component of a request URL, or `""` if there is none.
pub fn query_of(url: &str) -> &str {
    let query_start = match url.find('?') {
        Some(i) => i + 1,
        None => return "",
    };
    let rest = &url[query_start..];
    let end = rest.find('#').unwrap_or(rest.len());
    &rest[..end]
}

/// Find a query parameter's decoded value by (decoded) key.
fn query_param(query: &str, key: &str) -> Option<String> {
    for pair in query.split('&') {
        if pair.is_empty() {
            continue;
        }
        let (raw_key, raw_value) = pair.split_once('=').unwrap_or((pair, ""));
        if percent_decode(raw_key, true) == key {
            return Some(percent_decode(raw_value, true));
        }
    }
    None
}

/// The decoded `{word}` from a `/custom/{word}` path.
fn word_segment(path: &str) -> String {
    percent_decode(&path[CUSTOM_WORD_PREFIX.len()..], false)
}

/// Percent-decode a URL component. When `plus_as_space` is set (query strings),
/// `+` decodes to a space. Invalid escapes are passed through literally.
fn percent_decode(input: &str, plus_as_space: bool) -> String {
    let bytes = input.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        match bytes[i] {
            b'%' if i + 2 < bytes.len() => match (hex_value(bytes[i + 1]), hex_value(bytes[i + 2])) {
                (Some(hi), Some(lo)) => {
                    out.push((hi << 4) | lo);
                    i += 3;
                }
                _ => {
                    out.push(bytes[i]);
                    i += 1;
                }
            },
            b'+' if plus_as_space => {
                out.push(b' ');
                i += 1;
            }
            byte => {
                out.push(byte);
                i += 1;
            }
        }
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// Parse a single hex digit byte to its `0..16` value.
fn hex_value(byte: u8) -> Option<u8> {
    match byte {
        b'0'..=b'9' => Some(byte - b'0'),
        b'a'..=b'f' => Some(byte - b'a' + 10),
        b'A'..=b'F' => Some(byte - b'A' + 10),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};

    struct TempDir {
        path: std::path::PathBuf,
    }
    impl TempDir {
        fn new() -> Self {
            static COUNTER: AtomicU32 = AtomicU32::new(0);
            let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
            let mut path = std::env::temp_dir();
            path.push(format!("merriam-routes-{}-{}", std::process::id(), unique));
            TempDir { path }
        }
    }
    impl Drop for TempDir {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.path);
        }
    }

    fn service() -> (TempDir, Service) {
        let tmp = TempDir::new();
        let store = Store::new(&tmp.path);
        (tmp, Service::new(store))
    }

    fn request(method: &str, url: &str) -> HttpRequest {
        HttpRequest {
            method: method.into(),
            url: url.into(),
            ..Default::default()
        }
    }

    fn respond(outcome: Outcome) -> HttpResponse {
        match outcome {
            Outcome::Respond(r) => r,
            Outcome::Continue => panic!("expected a response, got Continue"),
        }
    }

    #[test]
    fn healthz_is_ok() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&request("GET", "/healthz")));
        assert_eq!(r.status, STATUS_OK);
        assert_eq!(r.body, r#"{"status":"ok"}"#);
    }

    #[test]
    fn unknown_route_continues() {
        let (_t, svc) = service();
        assert_eq!(svc.dispatch(&request("GET", "/nope")), Outcome::Continue);
        assert_eq!(svc.dispatch(&request("PUT", "/custom/x")), Outcome::Continue);
    }

    #[test]
    fn add_returns_added_true_then_false() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&request("POST", "/custom/widget")));
        assert_eq!(r.status, STATUS_OK);
        assert_eq!(r.body, r#"{"word":"widget","added":true}"#);

        let r = respond(svc.dispatch(&request("POST", "/custom/widget")));
        assert_eq!(r.body, r#"{"word":"widget","added":false}"#);
    }

    #[test]
    fn exists_reflects_membership() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&request("GET", "/custom/gadget")));
        assert_eq!(r.body, r#"{"word":"gadget","exists":false}"#);
        svc.dispatch(&request("POST", "/custom/gadget"));
        let r = respond(svc.dispatch(&request("GET", "/custom/gadget")));
        assert_eq!(r.body, r#"{"word":"gadget","exists":true}"#);
    }

    #[test]
    fn remove_reports_existence() {
        let (_t, svc) = service();
        svc.dispatch(&request("POST", "/custom/gizmo"));
        let r = respond(svc.dispatch(&request("DELETE", "/custom/gizmo")));
        assert_eq!(r.body, r#"{"word":"gizmo","removed":true}"#);
        let r = respond(svc.dispatch(&request("DELETE", "/custom/gizmo")));
        assert_eq!(r.body, r#"{"word":"gizmo","removed":false}"#);
    }

    #[test]
    fn enum_lists_sorted_matches() {
        let (_t, svc) = service();
        for w in ["zeta", "alpha", "mu"] {
            svc.dispatch(&request("POST", &format!("/custom/{w}")));
        }
        let r = respond(svc.dispatch(&request("GET", "/custom")));
        assert_eq!(r.body, r#"{"matches":["alpha","mu","zeta"]}"#);
    }

    #[test]
    fn enum_pattern_filters() {
        let (_t, svc) = service();
        for w in ["cat", "car", "dog"] {
            svc.dispatch(&request("POST", &format!("/custom/{w}")));
        }
        let r = respond(svc.dispatch(&request("GET", "/custom?pattern=ca.")));
        assert_eq!(r.body, r#"{"matches":["car","cat"]}"#);
    }

    #[test]
    fn enum_bad_pattern_is_400() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&request("GET", "/custom?pattern=(")));
        assert_eq!(r.status, STATUS_BAD_REQUEST);
    }

    #[test]
    fn empty_word_add_is_400() {
        let (_t, svc) = service();
        // "/custom/%20" decodes to a single space -> invalid word.
        let r = respond(svc.dispatch(&request("POST", "/custom/%20")));
        assert_eq!(r.status, STATUS_BAD_REQUEST);
    }

    #[test]
    fn user_header_scopes_the_dictionary() {
        let (_t, svc) = service();
        let mut alice = request("POST", "/custom/apple");
        alice.user = Some("alice".into());
        svc.dispatch(&alice);

        // Bob (default-less explicit) sees nothing.
        let mut bob = request("GET", "/custom");
        bob.user = Some("bob".into());
        let r = respond(svc.dispatch(&bob));
        assert_eq!(r.body, r#"{"matches":[]}"#);

        let mut alice_list = request("GET", "/custom");
        alice_list.user = Some("alice".into());
        let r = respond(svc.dispatch(&alice_list));
        assert_eq!(r.body, r#"{"matches":["apple"]}"#);
    }

    #[test]
    fn locale_header_scopes_the_dictionary() {
        let (_t, svc) = service();
        let mut us = request("POST", "/custom/color");
        us.locale = Some("en-US".into());
        svc.dispatch(&us);

        let mut gb = request("GET", "/custom");
        gb.locale = Some("en-GB".into());
        let r = respond(svc.dispatch(&gb));
        assert_eq!(r.body, r#"{"matches":[]}"#);
    }

    #[test]
    fn percent_encoded_word_is_decoded() {
        let (_t, svc) = service();
        // "%C3%A9" is UTF-8 for "é".
        let r = respond(svc.dispatch(&request("POST", "/custom/caf%C3%A9")));
        assert_eq!(r.body, "{\"word\":\"café\",\"added\":true}");
    }

    #[test]
    fn path_of_handles_absolute_and_relative() {
        assert_eq!(path_of("http://host:80/custom/x?y=1"), "/custom/x");
        assert_eq!(path_of("/custom?pattern=a"), "/custom");
        assert_eq!(path_of("http://host"), "/");
    }
}
