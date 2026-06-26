// Copyright (c) Microsoft Corporation.

//! The `merriam` relay client (WD-D13): `wordy`'s custom-dictionary operations
//! expressed as ordinary WinHTTP REST calls to the `merriam` service.
//!
//! [`MerriamClient`] builds the `merriam` request for each operation
//! (`add`/`remove`/`contains`/`list`), sends it over the [`winhttp`](crate::winhttp)
//! transport, and parses the JSON response. `wordy` keeps its shared-dictionary
//! CPU work; only the *storage* of the per-user custom dictionary moves out to
//! `merriam`. Because the transport is plain WinHTTP, the shim's egress seam
//! (windows-win32-shim MW17) can isolate these calls without `wordy` knowing
//! (SHIM-D19).

use std::fmt;

use serde::Deserialize;

use crate::winhttp;
/// The default `merriam` host when `MERRIAM_HOST` is unset.
pub const DEFAULT_HOST: &str = "127.0.0.1";
/// The default `merriam` port when `MERRIAM_PORT` is unset.
pub const DEFAULT_PORT: u16 = 8099;

/// A failed relay operation.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RelayError {
    /// The WinHTTP transport failed with this `WIN32_ERROR`.
    Transport(u32),
    /// `merriam` answered with a non-200 status.
    Upstream {
        /// The HTTP status code.
        status: u16,
        /// The response body (for diagnostics / error propagation).
        body: String,
    },
    /// The response body could not be parsed as the expected JSON.
    Parse(String),
}

impl fmt::Display for RelayError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RelayError::Transport(code) => write!(f, "merriam transport error (os error {code})"),
            RelayError::Upstream { status, body } => {
                write!(f, "merriam returned {status}: {body}")
            }
            RelayError::Parse(msg) => write!(f, "merriam response parse error: {msg}"),
        }
    }
}

impl std::error::Error for RelayError {}

#[derive(Deserialize)]
struct AddedResponse {
    added: bool,
}

#[derive(Deserialize)]
struct RemovedResponse {
    removed: bool,
}

#[derive(Deserialize)]
struct ExistsResponse {
    exists: bool,
}

#[derive(Deserialize)]
struct MatchesResponse {
    matches: Vec<String>,
}

/// A client that relays custom-dictionary operations to a `merriam` instance.
#[derive(Clone, Debug)]
pub struct MerriamClient {
    host: String,
    port: u16,
}

impl MerriamClient {
    /// Build a client targeting `host:port`.
    pub fn new(host: impl Into<String>, port: u16) -> Self {
        MerriamClient { host: host.into(), port }
    }

    /// Build a client from `MERRIAM_HOST` / `MERRIAM_PORT` (defaulting to
    /// [`DEFAULT_HOST`] / [`DEFAULT_PORT`]).
    pub fn from_env() -> Self {
        let host = std::env::var("MERRIAM_HOST").unwrap_or_else(|_| DEFAULT_HOST.to_string());
        let port = std::env::var("MERRIAM_PORT")
            .ok()
            .and_then(|p| p.parse().ok())
            .unwrap_or(DEFAULT_PORT);
        MerriamClient { host, port }
    }

    /// The configured `merriam` authority (`host:port`).
    pub fn authority(&self) -> String {
        format!("{}:{}", self.host, self.port)
    }

    /// Issue one relay call, returning `(status, body)` or a transport error.
    fn call(
        &self,
        verb: &str,
        path: &str,
        locale: &str,
        user: &str,
    ) -> Result<(u16, String), RelayError> {
        let headers = [("X-Wordy-User", user), ("X-Wordy-Locale", locale)];
        winhttp::request(&self.host, self.port, verb, path, &headers).map_err(RelayError::Transport)
    }

    /// Parse a `200` JSON body, or surface a non-200 as [`RelayError::Upstream`].
    fn parse_ok<T: for<'de> Deserialize<'de>>(
        status: u16,
        body: String,
    ) -> Result<T, RelayError> {
        if status != 200 {
            return Err(RelayError::Upstream { status, body });
        }
        serde_json::from_str(&body).map_err(|e| RelayError::Parse(e.to_string()))
    }

    /// `POST /custom/{word}` — add a word, returning whether it was newly added.
    ///
    /// # Errors
    /// [`RelayError`] on transport failure, a non-200 status, or a parse error.
    pub fn add(&self, locale: &str, user: &str, word: &str) -> Result<bool, RelayError> {
        let (status, body) = self.call("POST", &custom_word_path(word), locale, user)?;
        let parsed: AddedResponse = Self::parse_ok(status, body)?;
        Ok(parsed.added)
    }

    /// `DELETE /custom/{word}` — remove a word, returning whether it existed.
    ///
    /// # Errors
    /// [`RelayError`] on transport failure, a non-200 status, or a parse error.
    pub fn remove(&self, locale: &str, user: &str, word: &str) -> Result<bool, RelayError> {
        let (status, body) = self.call("DELETE", &custom_word_path(word), locale, user)?;
        let parsed: RemovedResponse = Self::parse_ok(status, body)?;
        Ok(parsed.removed)
    }

    /// `GET /custom/{word}` — test membership.
    ///
    /// # Errors
    /// [`RelayError`] on transport failure, a non-200 status, or a parse error.
    pub fn contains(&self, locale: &str, user: &str, word: &str) -> Result<bool, RelayError> {
        let (status, body) = self.call("GET", &custom_word_path(word), locale, user)?;
        let parsed: ExistsResponse = Self::parse_ok(status, body)?;
        Ok(parsed.exists)
    }

    /// `GET /custom` — list the caller's custom dictionary (ascending).
    ///
    /// # Errors
    /// [`RelayError`] on transport failure, a non-200 status, or a parse error.
    pub fn list(&self, locale: &str, user: &str) -> Result<Vec<String>, RelayError> {
        let (status, body) = self.call("GET", "/custom", locale, user)?;
        let parsed: MatchesResponse = Self::parse_ok(status, body)?;
        Ok(parsed.matches)
    }
}

/// A [`CustomStore`](crate::store::CustomStore) backed by a [`MerriamClient`]:
/// the production wiring of `wordy`'s route handlers to the `merriam` service.
///
/// It bridges the route-level types (`Locale`, `Principal`) onto the relay's
/// plain-string REST API and maps [`RelayError`] onto
/// [`StoreError`](crate::store::StoreError).
#[derive(Clone, Debug)]
pub struct RelayStore {
    client: MerriamClient,
}

impl RelayStore {
    /// Wrap a configured [`MerriamClient`].
    pub fn new(client: MerriamClient) -> Self {
        RelayStore { client }
    }

    /// Build a relay store from `MERRIAM_HOST` / `MERRIAM_PORT`.
    pub fn from_env() -> Self {
        RelayStore { client: MerriamClient::from_env() }
    }
}

impl From<RelayError> for crate::store::StoreError {
    fn from(error: RelayError) -> Self {
        match error {
            RelayError::Transport(code) => {
                crate::store::StoreError::Transport(format!("os error {code}"))
            }
            RelayError::Upstream { status, body } => {
                crate::store::StoreError::Upstream { status, body }
            }
            RelayError::Parse(message) => crate::store::StoreError::Transport(message),
        }
    }
}

impl crate::store::CustomStore for RelayStore {
    fn add(
        &self,
        locale: crate::words::Locale,
        user: &crate::identity::Principal,
        word: &str,
    ) -> Result<bool, crate::store::StoreError> {
        self.client.add(locale.slug(), user.user_id().as_str(), word).map_err(Into::into)
    }

    fn remove(
        &self,
        locale: crate::words::Locale,
        user: &crate::identity::Principal,
        word: &str,
    ) -> Result<bool, crate::store::StoreError> {
        self.client.remove(locale.slug(), user.user_id().as_str(), word).map_err(Into::into)
    }

    fn contains(
        &self,
        locale: crate::words::Locale,
        user: &crate::identity::Principal,
        word: &str,
    ) -> Result<bool, crate::store::StoreError> {
        self.client.contains(locale.slug(), user.user_id().as_str(), word).map_err(Into::into)
    }

    fn list(
        &self,
        locale: crate::words::Locale,
        user: &crate::identity::Principal,
    ) -> Result<Vec<String>, crate::store::StoreError> {
        self.client.list(locale.slug(), user.user_id().as_str()).map_err(Into::into)
    }
}

/// Build the `/custom/{word}` path with the word percent-encoded so any byte is
/// path-safe (`merriam` percent-decodes the segment).
fn custom_word_path(word: &str) -> String {
    let mut path = String::from("/custom/");
    for &byte in word.as_bytes() {
        if byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.' | b'~') {
            path.push(byte as char);
        } else {
            path.push('%');
            path.push_str(&format!("{byte:02X}"));
        }
    }
    path
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;
    use std::thread::JoinHandle;

    /// A one-shot HTTP stub: accepts a single connection, captures the request,
    /// and returns a canned `(status, body)`. [`captured`](StubServer::captured)
    /// joins the thread and yields the raw request text for assertions.
    struct StubServer {
        port: u16,
        handle: JoinHandle<String>,
    }

    impl StubServer {
        fn spawn(status: u16, reason: &str, body: &str) -> StubServer {
            let listener = TcpListener::bind("127.0.0.1:0").expect("bind stub");
            let port = listener.local_addr().unwrap().port();
            let reason = reason.to_string();
            let body = body.to_string();
            let handle = std::thread::spawn(move || {
                let (mut stream, _) = listener.accept().expect("accept");
                let mut buf = [0u8; 4096];
                let n = stream.read(&mut buf).unwrap_or(0);
                let captured = String::from_utf8_lossy(&buf[..n]).into_owned();
                let response = format!(
                    "HTTP/1.1 {status} {reason}\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
                    body.len()
                );
                let _ = stream.write_all(response.as_bytes());
                captured
            });
            StubServer { port, handle }
        }

        fn captured(self) -> String {
            self.handle.join().expect("stub thread")
        }
    }

    #[test]
    fn add_sends_post_with_headers_and_parses_added() {
        let stub = StubServer::spawn(200, "OK", r#"{"word":"widget","added":true}"#);
        let client = MerriamClient::new("127.0.0.1", stub.port);
        let added = client.add("en-US", "tester", "widget").expect("add");
        assert!(added);
        let req = stub.captured();
        assert!(req.starts_with("POST /custom/widget HTTP/1.1"), "request line: {req}");
        assert!(req.contains("X-Wordy-User: tester"));
        assert!(req.contains("X-Wordy-Locale: en-US"));
    }

    #[test]
    fn remove_sends_delete_and_parses_removed() {
        let stub = StubServer::spawn(200, "OK", r#"{"word":"widget","removed":true}"#);
        let client = MerriamClient::new("127.0.0.1", stub.port);
        let removed = client.remove("en-US", "tester", "widget").expect("remove");
        assert!(removed);
        assert!(stub.captured().starts_with("DELETE /custom/widget HTTP/1.1"));
    }

    #[test]
    fn contains_sends_get_and_parses_exists() {
        let stub = StubServer::spawn(200, "OK", r#"{"word":"widget","exists":false}"#);
        let client = MerriamClient::new("127.0.0.1", stub.port);
        let exists = client.contains("en-US", "tester", "widget").expect("contains");
        assert!(!exists);
        assert!(stub.captured().starts_with("GET /custom/widget HTTP/1.1"));
    }

    #[test]
    fn list_sends_get_and_parses_matches() {
        let stub = StubServer::spawn(200, "OK", r#"{"matches":["alpha","beta"]}"#);
        let client = MerriamClient::new("127.0.0.1", stub.port);
        let words = client.list("en-US", "tester").expect("list");
        assert_eq!(words, vec!["alpha", "beta"]);
        assert!(stub.captured().starts_with("GET /custom HTTP/1.1"));
    }

    #[test]
    fn non_200_is_upstream_error() {
        let stub = StubServer::spawn(400, "Bad Request", r#"{"error":"invalid word"}"#);
        let client = MerriamClient::new("127.0.0.1", stub.port);
        let err = client.add("en-US", "tester", "  ").unwrap_err();
        match err {
            RelayError::Upstream { status, .. } => assert_eq!(status, 400),
            other => panic!("expected Upstream, got {other:?}"),
        }
        let _ = stub.captured();
    }

    #[test]
    fn special_characters_in_word_are_percent_encoded() {
        let stub = StubServer::spawn(200, "OK", r#"{"word":"a b","added":true}"#);
        let client = MerriamClient::new("127.0.0.1", stub.port);
        client.add("en-US", "tester", "a b/c").expect("add");
        let req = stub.captured();
        // space -> %20, slash -> %2F
        assert!(req.starts_with("POST /custom/a%20b%2Fc HTTP/1.1"), "request line: {req}");
    }

    #[test]
    fn transport_error_when_nobody_listening() {
        // Reserve then drop a port so nothing is listening.
        let port = {
            let l = TcpListener::bind("127.0.0.1:0").unwrap();
            l.local_addr().unwrap().port()
        };
        let client = MerriamClient::new("127.0.0.1", port);
        let err = client.list("en-US", "tester").unwrap_err();
        assert!(matches!(err, RelayError::Transport(_)), "got {err:?}");
    }

    #[test]
    fn custom_word_path_encodes_unreserved_verbatim() {
        assert_eq!(custom_word_path("Widget-1_2.3~"), "/custom/Widget-1_2.3~");
        assert_eq!(custom_word_path("a/b c"), "/custom/a%2Fb%20c");
    }
}
