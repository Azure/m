// Copyright (c) Microsoft Corporation.

//! The egress (network-client) operation model and the [`EgressSurface`] seam
//! (D31): the outbound counterpart of the registry [`Surface`](crate::Surface)
//! and filesystem [`FsSurface`](crate::FsSurface) seams, realizing D27 (network
//! is the majority of the surface).
//!
//! An outbound call is a whole [`EgressRequest`] value; a reply is a whole
//! [`EgressResponse`]. The surface deliberately knows nothing about the
//! multi-call WinHTTP handle lifecycle (`WinHttpOpen → … → ReadData`): the
//! `windows-win32-shim` WinHTTP front-end reassembles that lifecycle into one
//! request at the send boundary and drains the chosen response back across the
//! read calls. The D4/D25 decorator stack — redirect / buffer / replay / block /
//! observe — is built on this single [`send`](EgressSurface::send) verb in later
//! M11 items; the network bottom (`LiveEgress`, real WinHTTP) is the only
//! `unsafe`-leaf consumer and is Windows-only.

use crate::Utf16;
use crate::egress_error::EgressResult;

/// The wire scheme of an outbound request.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Scheme {
    /// Plaintext HTTP.
    Http,
    /// TLS (HTTPS).
    Https,
}

impl Scheme {
    /// The default TCP port for the scheme (`80` / `443`).
    #[must_use]
    pub fn default_port(self) -> u16 {
        match self {
            Scheme::Http => 80,
            Scheme::Https => 443,
        }
    }

    /// The lowercase URL scheme token (`"http"` / `"https"`).
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Scheme::Http => "http",
            Scheme::Https => "https",
        }
    }
}

/// The client stack an outbound request flows over. HTTP (WinHTTP) is the only
/// transport this milestone serves; `#[non_exhaustive]` reserves SOAP/WWSAPI
/// (`webservices.dll`) for the deferred peer seam (D31), which must be
/// intercepted at the app's `Ws*` import boundary rather than via WinHTTP.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum EgressTransport {
    /// An HTTP request issued through a WinHTTP-shaped client.
    Http,
}

/// One outbound request, modeled as data (D31). This is also the journaling /
/// replay verb stream for the buffered and replay decorators (D4/D25).
///
/// `host` / `verb` / `path` and the header name/value pairs are stored as
/// [`Utf16`] (D7/D9: internal UTF-16LE, ill-formed-tolerant), since they
/// ultimately cross the wide-string WinHTTP boundary; `body` is raw bytes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EgressRequest {
    /// The client stack (HTTP for this milestone).
    pub transport: EgressTransport,
    /// The wire scheme.
    pub scheme: Scheme,
    /// The destination host (no scheme, no port).
    pub host: Utf16,
    /// The destination TCP port.
    pub port: u16,
    /// The HTTP verb (`GET`, `POST`, …), matching WinHTTP's `pwszVerb`.
    pub verb: Utf16,
    /// The request target (path plus any query string).
    pub path: Utf16,
    /// The request headers, in order, as `(name, value)` pairs.
    pub headers: Vec<(Utf16, Utf16)>,
    /// The request body (empty for a bodyless request).
    pub body: Vec<u8>,
}

impl EgressRequest {
    /// Build an HTTP request with no headers and an empty body.
    #[must_use]
    pub fn http(scheme: Scheme, host: &str, port: u16, verb: &str, path: &str) -> Self {
        Self {
            transport: EgressTransport::Http,
            scheme,
            host: Utf16::from_utf8(host),
            port,
            verb: Utf16::from_utf8(verb),
            path: Utf16::from_utf8(path),
            headers: Vec::new(),
            body: Vec::new(),
        }
    }

    /// The `host:port` authority, for redirect matching and observation. Returns
    /// `None` if the stored host is not well-formed UTF-16 (D9).
    #[must_use]
    pub fn authority(&self) -> Option<String> {
        Some(format!("{}:{}", self.host.to_utf8().ok()?, self.port))
    }

    /// Whether the verb is an HTTP **safe** method (`GET`/`HEAD`/`OPTIONS`/
    /// `TRACE`) — one a server contract treats as read-only. Compared
    /// ASCII-case-insensitively; an ill-formed-UTF-16 verb reads as not safe.
    #[must_use]
    pub fn is_safe_verb(&self) -> bool {
        match self.verb.to_utf8() {
            Ok(v) => {
                matches!(v.to_ascii_uppercase().as_str(), "GET" | "HEAD" | "OPTIONS" | "TRACE")
            }
            Err(_) => false,
        }
    }

    /// Whether the request **mutates** server state (any non-safe verb). The
    /// buffered decorator (M11-3) captures these and synthesizes an ack rather
    /// than forwarding them.
    #[must_use]
    pub fn is_mutating(&self) -> bool {
        !self.is_safe_verb()
    }
}

/// One outbound reply, modeled as data (D31).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EgressResponse {
    /// The HTTP status code (e.g. `200`).
    pub status: u32,
    /// The response headers, in order, as `(name, value)` pairs.
    pub headers: Vec<(Utf16, Utf16)>,
    /// The response body.
    pub body: Vec<u8>,
}

impl EgressResponse {
    /// Build a response with the given status and body and no headers.
    #[must_use]
    pub fn new(status: u32, body: Vec<u8>) -> Self {
        Self { status, headers: Vec::new(), body }
    }

    /// A synthetic empty-bodied `status` response (used by the buffer / block
    /// modes to acknowledge without contacting a destination).
    #[must_use]
    pub fn empty(status: u32) -> Self {
        Self { status, headers: Vec::new(), body: Vec::new() }
    }
}

/// The egress decorator/provider seam (D31): one object-safe method through which
/// every outbound request flows. The D25 modes (passthrough / redirect / buffer /
/// replay / block / observe) are implemented once as `EgressSurface` wrappers,
/// independent of the network bottom.
pub trait EgressSurface {
    /// Send a request and produce a response (or a surface error).
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse>;
}

/// A thin facade over a composed [`EgressSurface`] — the egress peer of the
/// registry [`Registry`](crate::Registry) and filesystem
/// [`Filesystem`](crate::Filesystem) facades. It holds whatever decorator stack
/// the session composed (passthrough / redirect / buffer / replay / block /
/// observe over a `LiveEgress` bottom) and forwards [`send`](Egress::send),
/// keeping call sites free of the concrete surface type.
pub struct Egress<S: EgressSurface> {
    surface: S,
}

impl<S: EgressSurface> Egress<S> {
    /// Wrap a composed surface.
    pub fn new(surface: S) -> Self {
        Self { surface }
    }

    /// Send one request through the composed surface.
    ///
    /// # Errors
    ///
    /// Propagates the surface's [`EgressError`](crate::EgressError).
    pub fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        self.surface.send(req)
    }

    /// Borrow the underlying surface (e.g. to inspect a buffer's journal).
    pub fn surface(&mut self) -> &mut S {
        &mut self.surface
    }

    /// Recover the underlying surface.
    pub fn into_surface(self) -> S {
        self.surface
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::egress_error::EgressError;

    fn req(verb: &str) -> EgressRequest {
        EgressRequest::http(Scheme::Http, "localhost", 8080, verb, "/x")
    }

    #[test]
    fn scheme_defaults_and_tokens() {
        assert_eq!(Scheme::Http.default_port(), 80);
        assert_eq!(Scheme::Https.default_port(), 443);
        assert_eq!(Scheme::Http.as_str(), "http");
        assert_eq!(Scheme::Https.as_str(), "https");
    }

    #[test]
    fn http_constructor_fills_defaults() {
        let r = EgressRequest::http(Scheme::Https, "example.com", 443, "GET", "/a?b=c");
        assert_eq!(r.transport, EgressTransport::Http);
        assert_eq!(r.scheme, Scheme::Https);
        assert_eq!(r.host.to_utf8().unwrap(), "example.com");
        assert_eq!(r.port, 443);
        assert_eq!(r.verb.to_utf8().unwrap(), "GET");
        assert_eq!(r.path.to_utf8().unwrap(), "/a?b=c");
        assert!(r.headers.is_empty());
        assert!(r.body.is_empty());
    }

    #[test]
    fn authority_formats_host_and_port() {
        assert_eq!(req("GET").authority().as_deref(), Some("localhost:8080"));
    }

    #[test]
    fn authority_is_none_for_ill_formed_host() {
        let mut r = req("GET");
        r.host = Utf16::from_units(vec![0xD800]); // unpaired high surrogate
        assert_eq!(r.authority(), None);
    }

    #[test]
    fn safe_verbs_are_recognized() {
        for v in ["GET", "HEAD", "OPTIONS", "TRACE"] {
            assert!(req(v).is_safe_verb(), "{v} should be safe");
            assert!(!req(v).is_mutating(), "{v} should not mutate");
        }
    }

    #[test]
    fn mutating_verbs_are_recognized() {
        for v in ["POST", "PUT", "DELETE", "PATCH", "CONNECT"] {
            assert!(!req(v).is_safe_verb(), "{v} should not be safe");
            assert!(req(v).is_mutating(), "{v} should mutate");
        }
    }

    #[test]
    fn verb_classification_is_case_insensitive() {
        assert!(req("get").is_safe_verb());
        assert!(req("Get").is_safe_verb());
        assert!(req("post").is_mutating());
    }

    #[test]
    fn ill_formed_verb_reads_as_mutating() {
        let mut r = req("GET");
        r.verb = Utf16::from_units(vec![0xDC00]); // unpaired low surrogate
        assert!(!r.is_safe_verb());
        assert!(r.is_mutating());
    }

    #[test]
    fn response_constructors() {
        let body = b"hello".to_vec();
        let r = EgressResponse::new(200, body.clone());
        assert_eq!(r.status, 200);
        assert_eq!(r.body, body);
        assert!(r.headers.is_empty());

        let e = EgressResponse::empty(204);
        assert_eq!(e.status, 204);
        assert!(e.body.is_empty());
        assert!(e.headers.is_empty());
    }

    #[test]
    fn requests_compare_by_value() {
        assert_eq!(req("GET"), req("GET"));
        assert_ne!(req("GET"), req("POST"));
    }

    /// A trivial in-test surface proving the trait is usable: echoes the request
    /// target back as a 200 body.
    struct EchoSurface;
    impl EgressSurface for EchoSurface {
        fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
            let target =
                req.path.to_utf8().map_err(|_| EgressError::IllFormedUtf16)?;
            Ok(EgressResponse::new(200, target.into_bytes()))
        }
    }

    #[test]
    fn surface_trait_is_object_safe_and_usable() {
        let mut surface: Box<dyn EgressSurface> = Box::new(EchoSurface);
        let resp = surface.send(&req("GET")).expect("echo send");
        assert_eq!(resp.status, 200);
        assert_eq!(resp.body, b"/x");
    }

    #[test]
    fn surface_propagates_errors() {
        let mut r = req("GET");
        r.path = Utf16::from_units(vec![0xD800]);
        let mut surface = EchoSurface;
        assert_eq!(surface.send(&r), Err(EgressError::IllFormedUtf16));
    }
}
