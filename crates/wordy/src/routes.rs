// Copyright (c) Microsoft Corporation.

//! Request routing for `wordy` — the REST surface.
//!
//! This module is the safe, platform-independent core that turns a decoded HTTP
//! request into a response. The IIS ABI boundary (`iis`, Windows only) decodes
//! the host request into an [`HttpRequest`], asks a [`Service`] to
//! [`dispatch`](Service::dispatch) it, and realizes the resulting [`Outcome`]
//! against the host response. No Windows or host types appear here, so the whole
//! REST surface is unit-testable on any platform.
//!
//! ## Routes (all synchronous)
//!
//! | Method & path            | Body / query                | Result body                                |
//! |--------------------------|-----------------------------|--------------------------------------------|
//! | `GET /healthz`           | —                           | `{"status":"ok"}`                          |
//! | `POST /spellcheck`       | `{"words":[…]}`             | `{"results":[{word,correct,suggestions}]}` |
//! | `POST /anagram`          | `{template,tray,wildcards}` | `{"matches":[…]}`                          |
//! | `GET /shared?pattern=`   | `pattern`                   | `{"matches":[…]}` (shared dict, regex)     |
//! | `GET /custom?pattern=`   | optional `pattern`          | `{"matches":[…]}` (this user's custom dict)|
//! | `POST /custom/{word}`    | —                           | `{"word":…,"added":bool}`                  |
//! | `DELETE /custom/{word}`  | —                           | `{"word":…,"removed":bool}`                |
//! | `GET /custom/{word}`     | —                           | `{"word":…,"exists":bool}`                 |
//!
//! Unknown method/path pairs yield [`Outcome::Continue`] so the host pipeline
//! proceeds. The caller is identified by the `X-Wordy-User` header
//! ([`crate::custom::USER_HEADER`]); custom-dictionary routes are scoped to that
//! [`Principal`].

use regex::Regex;
use serde::{Deserialize, Serialize};

use crate::custom::{CustomDictionary, Principal};
use crate::words::{AnagramQuery, Dictionary, Locale};

/// HTTP 200.
pub const STATUS_OK: u16 = 200;
/// HTTP 400.
pub const STATUS_BAD_REQUEST: u16 = 400;
/// HTTP 500.
pub const STATUS_INTERNAL_ERROR: u16 = 500;

/// The JSON media type written on every modeled response body.
pub const CONTENT_TYPE_JSON: &str = "application/json";

/// The path prefix for per-word custom-dictionary routes.
const CUSTOM_WORD_PREFIX: &str = "/custom/";

/// Upper bound on the number of words returned in any enumeration / anagram
/// response, guarding against an unbounded body (e.g. a `.*` pattern).
const MAX_MATCHES: usize = 1000;

/// Upper bound on the number of suggestions returned per misspelled word.
const MAX_SUGGESTIONS: usize = 25;

/// A decoded inbound request, host-agnostic.
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
}

/// A response `wordy` wants the host to send.
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

/// What `wordy` decided to do with a request.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Outcome {
    /// Send this response and finish the request without invoking later modules.
    Respond(HttpResponse),
    /// `wordy` does not handle this request; let the host continue its pipeline.
    Continue,
}

// --- JSON models -----------------------------------------------------------

#[derive(Deserialize)]
struct SpellcheckRequest {
    words: Vec<String>,
}

#[derive(Serialize)]
struct SpellcheckResponse {
    results: Vec<SpellcheckResult>,
}

#[derive(Serialize)]
struct SpellcheckResult {
    word: String,
    correct: bool,
    suggestions: Vec<String>,
}

#[derive(Deserialize)]
struct AnagramRequest {
    template: String,
    #[serde(default)]
    tray: String,
    #[serde(default)]
    wildcards: usize,
}

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

// --- Service ---------------------------------------------------------------

/// The `wordy` REST service: the shared dictionary plus a per-user custom
/// dictionary, wired to the routing surface.
pub struct Service {
    custom: CustomDictionary,
    locale: Locale,
}

impl Service {
    /// Build a service over the given custom-dictionary store and locale.
    pub fn new(custom: CustomDictionary, locale: Locale) -> Self {
        Service { custom, locale }
    }

    /// The shared (read-only) dictionary this service serves.
    fn dictionary(&self) -> &'static Dictionary {
        Dictionary::shared(self.locale)
    }

    /// Route `req` to a handler, or decline it with [`Outcome::Continue`].
    pub fn dispatch(&self, req: &HttpRequest) -> Outcome {
        let path = path_of(&req.url);
        let query = query_of(&req.url);
        let user = Principal::from_header(req.user.as_deref());

        let response = match (req.method.as_str(), path) {
            ("GET", "/healthz") => health(),
            ("POST", "/spellcheck") => self.spellcheck(&req.body),
            ("POST", "/anagram") => self.anagram(&req.body),
            ("GET", "/shared") => self.shared_enum(query),
            ("GET", "/custom") => self.custom_enum(&user, query),
            ("POST", p) if p.starts_with(CUSTOM_WORD_PREFIX) => {
                self.custom_add(&user, &word_segment(p))
            }
            ("DELETE", p) if p.starts_with(CUSTOM_WORD_PREFIX) => {
                self.custom_remove(&user, &word_segment(p))
            }
            ("GET", p) if p.starts_with(CUSTOM_WORD_PREFIX) => {
                self.custom_exists(&user, &word_segment(p))
            }
            _ => return Outcome::Continue,
        };
        Outcome::Respond(response)
    }

    /// `POST /spellcheck`: check each word and offer suggestions for misses.
    fn spellcheck(&self, body: &str) -> HttpResponse {
        let request: SpellcheckRequest = match serde_json::from_str(body) {
            Ok(r) => r,
            Err(e) => return bad_request(format!("invalid request body: {e}")),
        };
        let dict = self.dictionary();
        let results = request
            .words
            .into_iter()
            .map(|word| {
                let correct = dict.contains(&word);
                let mut suggestions = if correct {
                    Vec::new()
                } else {
                    dict.suggestions(&word, 1).unwrap_or_default()
                };
                suggestions.truncate(MAX_SUGGESTIONS);
                SpellcheckResult {
                    word,
                    correct,
                    suggestions,
                }
            })
            .collect();
        json_ok(&SpellcheckResponse { results })
    }

    /// `POST /anagram`: solve a positional anagram query.
    fn anagram(&self, body: &str) -> HttpResponse {
        let request: AnagramRequest = match serde_json::from_str(body) {
            Ok(r) => r,
            Err(e) => return bad_request(format!("invalid request body: {e}")),
        };
        let query = match AnagramQuery::parse(&request.template, &request.tray, request.wildcards) {
            Ok(q) => q,
            Err(e) => return bad_request(e.to_string()),
        };
        let matches = capped(self.dictionary().anagrams(&query));
        json_ok(&MatchesResponse { matches })
    }

    /// `GET /shared?pattern=`: enumerate the shared dictionary by regex.
    fn shared_enum(&self, query: &str) -> HttpResponse {
        let pattern = match query_param(query, "pattern") {
            Some(p) => p,
            None => return bad_request("missing 'pattern' query parameter"),
        };
        match self.dictionary().matching_regex(&pattern) {
            Ok(matches) => json_ok(&MatchesResponse {
                matches: capped(matches),
            }),
            Err(e) => bad_request(e.to_string()),
        }
    }

    /// `GET /custom?pattern=`: enumerate the caller's custom dictionary,
    /// optionally filtered by a full-match regex.
    fn custom_enum(&self, user: &Principal, query: &str) -> HttpResponse {
        let words = match self.custom.list(self.locale, user) {
            Ok(words) => words,
            Err(e) => return server_error(e.to_string()),
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
        json_ok(&MatchesResponse {
            matches: capped(matches),
        })
    }

    /// `POST /custom/{word}`: add a word to the caller's custom dictionary.
    fn custom_add(&self, user: &Principal, word: &str) -> HttpResponse {
        match self.custom.add(self.locale, user, word) {
            Ok(added) => json_ok(&AddedResponse {
                word: word.to_string(),
                added,
            }),
            Err(e) => store_error(e),
        }
    }

    /// `DELETE /custom/{word}`: remove a word from the caller's custom dictionary.
    fn custom_remove(&self, user: &Principal, word: &str) -> HttpResponse {
        match self.custom.remove(self.locale, user, word) {
            Ok(removed) => json_ok(&RemovedResponse {
                word: word.to_string(),
                removed,
            }),
            Err(e) => store_error(e),
        }
    }

    /// `GET /custom/{word}`: test membership in the caller's custom dictionary.
    fn custom_exists(&self, user: &Principal, word: &str) -> HttpResponse {
        match self.custom.contains(self.locale, user, word) {
            Ok(exists) => json_ok(&ExistsResponse {
                word: word.to_string(),
                exists,
            }),
            Err(e) => store_error(e),
        }
    }
}

// --- Response helpers ------------------------------------------------------

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

/// Map a custom-store I/O error to a response: an empty / invalid word is a
/// client error (400); anything else is a server error (500).
fn store_error(error: std::io::Error) -> HttpResponse {
    if error.kind() == std::io::ErrorKind::InvalidInput {
        bad_request(error.to_string())
    } else {
        server_error(error.to_string())
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

// --- URL parsing -----------------------------------------------------------

/// Extract the path component of a request URL.
///
/// Strips an optional `scheme://authority` prefix and any `?query` / `#fragment`
/// suffix. An empty or authority-only input yields `"/"`.
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
    let path_end = after_authority
        .find(['?', '#'])
        .unwrap_or(after_authority.len());
    let path = &after_authority[..path_end];
    if path.is_empty() { "/" } else { path }
}

/// Extract the query component of a request URL (the text between `?` and an
/// optional `#`), or `""` if there is none.
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
            b'%' if i + 2 < bytes.len() => {
                match (hex_value(bytes[i + 1]), hex_value(bytes[i + 2])) {
                    (Some(hi), Some(lo)) => {
                        out.push((hi << 4) | lo);
                        i += 3;
                    }
                    _ => {
                        out.push(bytes[i]);
                        i += 1;
                    }
                }
            }
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

    /// A unique scratch directory removed on drop.
    struct TempDir {
        path: std::path::PathBuf,
    }

    impl TempDir {
        fn new() -> Self {
            static COUNTER: AtomicU32 = AtomicU32::new(0);
            let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
            let mut path = std::env::temp_dir();
            path.push(format!("wordy-routes-{}-{}", std::process::id(), unique));
            std::fs::create_dir_all(&path).expect("create scratch dir");
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
        let store = CustomDictionary::new(&tmp.path);
        (tmp, Service::new(store, Locale::EnUs))
    }

    fn get(url: &str) -> HttpRequest {
        HttpRequest {
            method: "GET".into(),
            url: url.into(),
            ..Default::default()
        }
    }

    fn request(method: &str, url: &str, body: &str, user: Option<&str>) -> HttpRequest {
        HttpRequest {
            method: method.into(),
            url: url.into(),
            body: body.into(),
            user: user.map(str::to_string),
        }
    }

    fn respond(outcome: Outcome) -> HttpResponse {
        match outcome {
            Outcome::Respond(r) => r,
            Outcome::Continue => panic!("expected a response, got Continue"),
        }
    }

    fn json(response: &HttpResponse) -> serde_json::Value {
        serde_json::from_str(&response.body).expect("response body is JSON")
    }

    // --- routing basics ---------------------------------------------------

    #[test]
    fn health_route_responds_ok() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&get("/healthz")));
        assert_eq!(r.status, STATUS_OK);
        assert_eq!(json(&r)["status"], "ok");
    }

    #[test]
    fn unknown_route_continues() {
        let (_t, svc) = service();
        assert_eq!(svc.dispatch(&get("/")), Outcome::Continue);
        assert_eq!(svc.dispatch(&get("/nope")), Outcome::Continue);
        // Right path, wrong method.
        assert_eq!(
            svc.dispatch(&request("PUT", "/spellcheck", "", None)),
            Outcome::Continue
        );
    }

    // --- spellcheck -------------------------------------------------------

    #[test]
    fn spellcheck_flags_correct_and_misspelled_words() {
        let (_t, svc) = service();
        let body = r#"{"words":["hello","helo"]}"#;
        let r = respond(svc.dispatch(&request("POST", "/spellcheck", body, None)));
        assert_eq!(r.status, STATUS_OK);
        let v = json(&r);
        assert_eq!(v["results"][0]["word"], "hello");
        assert_eq!(v["results"][0]["correct"], true);
        assert_eq!(v["results"][1]["word"], "helo");
        assert_eq!(v["results"][1]["correct"], false);
        let suggestions = v["results"][1]["suggestions"].as_array().unwrap();
        assert!(suggestions.iter().any(|s| s == "hello"));
    }

    #[test]
    fn spellcheck_rejects_bad_json() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&request("POST", "/spellcheck", "not json", None)));
        assert_eq!(r.status, STATUS_BAD_REQUEST);
        assert!(json(&r)["error"].is_string());
    }

    // --- anagram ----------------------------------------------------------

    #[test]
    fn anagram_returns_matches() {
        let (_t, svc) = service();
        let body = r#"{"template":"c.t","tray":"a","wildcards":0}"#;
        let r = respond(svc.dispatch(&request("POST", "/anagram", body, None)));
        assert_eq!(r.status, STATUS_OK);
        let matches = json(&r)["matches"].as_array().unwrap().clone();
        assert!(matches.iter().any(|m| m == "cat"));
        assert!(!matches.iter().any(|m| m == "cot"));
    }

    #[test]
    fn anagram_rejects_bad_template() {
        let (_t, svc) = service();
        let body = r#"{"template":"c#t","tray":"a","wildcards":0}"#;
        let r = respond(svc.dispatch(&request("POST", "/anagram", body, None)));
        assert_eq!(r.status, STATUS_BAD_REQUEST);
    }

    // --- shared enumeration ----------------------------------------------

    #[test]
    fn shared_enum_matches_pattern() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&get("/shared?pattern=c.t")));
        assert_eq!(r.status, STATUS_OK);
        let matches = json(&r)["matches"].as_array().unwrap().clone();
        assert!(matches.iter().any(|m| m == "cat"));
        assert!(matches.iter().any(|m| m == "cut"));
    }

    #[test]
    fn shared_enum_requires_pattern() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&get("/shared")));
        assert_eq!(r.status, STATUS_BAD_REQUEST);
    }

    #[test]
    fn shared_enum_rejects_bad_regex() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&get("/shared?pattern=(unclosed")));
        assert_eq!(r.status, STATUS_BAD_REQUEST);
    }

    // --- custom dictionary lifecycle -------------------------------------

    #[test]
    fn custom_add_exists_list_remove_flow() {
        let (_t, svc) = service();
        // Add.
        let r = respond(svc.dispatch(&request("POST", "/custom/widget", "", None)));
        assert_eq!(r.status, STATUS_OK);
        assert_eq!(json(&r)["added"], true);
        // Re-add is a no-op.
        let r = respond(svc.dispatch(&request("POST", "/custom/widget", "", None)));
        assert_eq!(json(&r)["added"], false);
        // Exists.
        let r = respond(svc.dispatch(&get("/custom/widget")));
        assert_eq!(json(&r)["exists"], true);
        // Enumerate.
        let r = respond(svc.dispatch(&get("/custom")));
        let matches = json(&r)["matches"].as_array().unwrap().clone();
        assert!(matches.iter().any(|m| m == "widget"));
        // Remove.
        let r = respond(svc.dispatch(&request("DELETE", "/custom/widget", "", None)));
        assert_eq!(json(&r)["removed"], true);
        let r = respond(svc.dispatch(&get("/custom/widget")));
        assert_eq!(json(&r)["exists"], false);
    }

    #[test]
    fn custom_dictionaries_are_per_user() {
        let (_t, svc) = service();
        respond(svc.dispatch(&request("POST", "/custom/alpha", "", Some("alice"))));
        // Bob does not see Alice's word.
        let r = respond(svc.dispatch(&request("GET", "/custom/alpha", "", Some("bob"))));
        assert_eq!(json(&r)["exists"], false);
        let r = respond(svc.dispatch(&request("GET", "/custom/alpha", "", Some("alice"))));
        assert_eq!(json(&r)["exists"], true);
    }

    #[test]
    fn custom_enum_filters_by_pattern() {
        let (_t, svc) = service();
        for w in ["apple", "apply", "banana"] {
            respond(svc.dispatch(&request("POST", &format!("/custom/{w}"), "", None)));
        }
        // Five-letter words beginning "app".
        let r = respond(svc.dispatch(&get("/custom?pattern=app..")));
        let matches = json(&r)["matches"].as_array().unwrap().clone();
        assert!(matches.iter().any(|m| m == "apple"));
        assert!(matches.iter().any(|m| m == "apply"));
        assert!(!matches.iter().any(|m| m == "banana"));
    }

    #[test]
    fn custom_add_percent_decodes_word() {
        let (_t, svc) = service();
        // "%27" decodes to an apostrophe.
        let r = respond(svc.dispatch(&request("POST", "/custom/don%27t", "", None)));
        assert_eq!(r.status, STATUS_OK);
        assert_eq!(json(&r)["word"], "don't");
        let r = respond(svc.dispatch(&get("/custom/don%27t")));
        assert_eq!(json(&r)["exists"], true);
    }

    #[test]
    fn custom_add_empty_word_is_bad_request() {
        let (_t, svc) = service();
        let r = respond(svc.dispatch(&request("POST", "/custom/", "", None)));
        assert_eq!(r.status, STATUS_BAD_REQUEST);
    }

    // --- URL parsing ------------------------------------------------------

    #[test]
    fn path_and_query_split() {
        assert_eq!(path_of("/shared?pattern=c.t"), "/shared");
        assert_eq!(query_of("/shared?pattern=c.t"), "pattern=c.t");
        assert_eq!(path_of("https://host/a/b?x=1#f"), "/a/b");
        assert_eq!(query_of("https://host/a/b?x=1#f"), "x=1");
        assert_eq!(query_of("/no-query"), "");
    }

    #[test]
    fn query_param_decodes_values() {
        assert_eq!(query_param("pattern=c.t", "pattern").as_deref(), Some("c.t"));
        assert_eq!(
            query_param("a=1&pattern=x%2Ey", "pattern").as_deref(),
            Some("x.y")
        );
        assert_eq!(query_param("a=1&b=2", "pattern"), None);
    }

    #[test]
    fn percent_decode_handles_escapes_and_plus() {
        assert_eq!(percent_decode("a%20b", false), "a b");
        assert_eq!(percent_decode("a+b", true), "a b");
        assert_eq!(percent_decode("a+b", false), "a+b");
        // Invalid escape passes through.
        assert_eq!(percent_decode("a%2", false), "a%2");
    }
}
