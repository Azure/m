// Copyright (c) Microsoft Corporation.

//! The web request/response handler surface (M8) — the **safe** side of the
//! in-process response-path insertion (`windows-win32-shim` SHIM-D18).
//!
//! This is the web analogue of the registry [`Surface`](crate::Surface) seam: a
//! single object-safe handler trait, [`RequestHandler`], plus cross-cutting
//! decorators ([`IdentityHandler`], [`JournalingHandler`]) written once over it
//! exactly as [`PassThrough`](crate::PassThrough) / `Buffered` are written over
//! `Surface` (D4). The decorators wrap a *real* handler — in production the
//! host's own request handler reached through the shim's activation-seam
//! interception (SHIM-D18); in tests a stub.
//!
//! The endpoint of M8 is the **identity** path: code runs at the host's
//! per-request notification points but changes nothing (D25 "off" = identity).
//! Redirection is layered on later by swapping the decorator, never by editing
//! call sites.
//!
//! The models here are plain data, independent of any Windows or COM type
//! (`IHttpContext`, `HTTP_REQUEST`, …): the unsafe glue that translates the
//! host's per-request calls into these borrowed models lives in the shim's
//! ABI-boundary module (SHIM-D18 / SHIM-D2), never here — this crate stays
//! `#![forbid(unsafe_code)]`.

/// A single HTTP header. Names are structural metadata; **values are treated as
/// potentially-PII** (D28) and are never copied into the observation stream by
/// the journaling layer.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Header {
    name: String,
    value: String,
}

impl Header {
    /// Construct a header from a name and value.
    #[must_use]
    pub fn new(name: impl Into<String>, value: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            value: value.into(),
        }
    }

    /// The header name (structural metadata).
    #[must_use]
    pub fn name(&self) -> &str {
        &self.name
    }

    /// The header value (potentially PII; not observed — D28).
    #[must_use]
    pub fn value(&self) -> &str {
        &self.value
    }
}

/// An inbound HTTP request, modeled as plain data independent of any Windows or
/// COM type. A [`RequestHandler`] **borrows** this; it never owns the host's
/// underlying buffers.
#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct HttpRequest {
    method: String,
    url: String,
    headers: Vec<Header>,
    body: Vec<u8>,
}

impl HttpRequest {
    /// A request with the given method and URL and no headers or body.
    #[must_use]
    pub fn new(method: impl Into<String>, url: impl Into<String>) -> Self {
        Self {
            method: method.into(),
            url: url.into(),
            headers: Vec::new(),
            body: Vec::new(),
        }
    }

    /// Builder: append a header.
    #[must_use]
    pub fn with_header(mut self, name: impl Into<String>, value: impl Into<String>) -> Self {
        self.headers.push(Header::new(name, value));
        self
    }

    /// Builder: set the request body.
    #[must_use]
    pub fn with_body(mut self, body: impl Into<Vec<u8>>) -> Self {
        self.body = body.into();
        self
    }

    /// The request method (e.g. `GET`).
    #[must_use]
    pub fn method(&self) -> &str {
        &self.method
    }

    /// The request URL / target.
    #[must_use]
    pub fn url(&self) -> &str {
        &self.url
    }

    /// The request headers.
    #[must_use]
    pub fn headers(&self) -> &[Header] {
        &self.headers
    }

    /// The request body bytes.
    #[must_use]
    pub fn body(&self) -> &[u8] {
        &self.body
    }
}

/// The outbound HTTP response. It is presented to handlers **mutably** so a
/// future redirecting handler can alter it; the [`IdentityHandler`] leaves it
/// untouched.
#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct HttpResponse {
    status: u16,
    headers: Vec<Header>,
    body: Vec<u8>,
}

impl HttpResponse {
    /// A response with the given status code and no headers or body.
    #[must_use]
    pub fn new(status: u16) -> Self {
        Self {
            status,
            headers: Vec::new(),
            body: Vec::new(),
        }
    }

    /// Builder: append a header.
    #[must_use]
    pub fn with_header(mut self, name: impl Into<String>, value: impl Into<String>) -> Self {
        self.headers.push(Header::new(name, value));
        self
    }

    /// Builder: set the response body.
    #[must_use]
    pub fn with_body(mut self, body: impl Into<Vec<u8>>) -> Self {
        self.body = body.into();
        self
    }

    /// The response status code.
    #[must_use]
    pub fn status(&self) -> u16 {
        self.status
    }

    /// Replace the response status code.
    pub fn set_status(&mut self, status: u16) {
        self.status = status;
    }

    /// The response headers.
    #[must_use]
    pub fn headers(&self) -> &[Header] {
        &self.headers
    }

    /// Append a response header.
    pub fn push_header(&mut self, name: impl Into<String>, value: impl Into<String>) {
        self.headers.push(Header::new(name, value));
    }

    /// The response body bytes.
    #[must_use]
    pub fn body(&self) -> &[u8] {
        &self.body
    }

    /// Replace the response body.
    pub fn set_body(&mut self, body: impl Into<Vec<u8>>) {
        self.body = body.into();
    }
}

/// The disposition a handler returns at a notification point — the safe analogue
/// of the host's request-notification status (IIS `REQUEST_NOTIFICATION_STATUS`).
/// [`Continue`](Disposition::Continue) lets the pipeline proceed to the next
/// handler; [`FinishRequest`](Disposition::FinishRequest) short-circuits it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Disposition {
    /// Proceed to the next handler in the pipeline.
    Continue,
    /// Stop the pipeline and complete the request now.
    FinishRequest,
}

/// The web request/response handler seam (M8-1): the per-request notification
/// points the host drives, modeled once and surface-agnostically. Decorators
/// (logging/journaling/substitution) are written against this trait, regardless
/// of the concrete handler underneath — the web analogue of
/// [`Surface`](crate::Surface).
///
/// The trait is object-safe (all methods take `&mut self` and concrete
/// arguments) so a composed stack can be type-erased at the ABI boundary.
pub trait RequestHandler {
    /// Called when the host begins processing a request, before the response is
    /// produced. The request is borrowed read-only.
    fn on_begin_request(&mut self, request: &HttpRequest) -> Disposition;

    /// Called as the host is about to send the response. The response is
    /// borrowed mutably so a handler *may* alter it; identity does not.
    fn on_send_response(&mut self, response: &mut HttpResponse) -> Disposition;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn request_builder_round_trips() {
        let req = HttpRequest::new("GET", "/index.html")
            .with_header("Host", "example.test")
            .with_body(b"hello".to_vec());
        assert_eq!(req.method(), "GET");
        assert_eq!(req.url(), "/index.html");
        assert_eq!(req.headers().len(), 1);
        assert_eq!(req.headers()[0].name(), "Host");
        assert_eq!(req.headers()[0].value(), "example.test");
        assert_eq!(req.body(), b"hello");
    }

    #[test]
    fn response_is_mutable() {
        let mut resp = HttpResponse::new(200).with_header("Content-Type", "text/plain");
        assert_eq!(resp.status(), 200);
        resp.set_status(404);
        resp.push_header("X-Trace", "1");
        resp.set_body(b"missing".to_vec());
        assert_eq!(resp.status(), 404);
        assert_eq!(resp.headers().len(), 2);
        assert_eq!(resp.body(), b"missing");
    }

    /// A leaf handler used across the M8 tests: records the calls it sees and
    /// returns a fixed disposition, standing in for the host's real handler.
    struct StubHandler {
        begins: u32,
        sends: u32,
    }

    impl StubHandler {
        fn new() -> Self {
            Self {
                begins: 0,
                sends: 0,
            }
        }
    }

    impl RequestHandler for StubHandler {
        fn on_begin_request(&mut self, _request: &HttpRequest) -> Disposition {
            self.begins += 1;
            Disposition::Continue
        }

        fn on_send_response(&mut self, _response: &mut HttpResponse) -> Disposition {
            self.sends += 1;
            Disposition::Continue
        }
    }

    #[test]
    fn leaf_handler_sees_notifications() {
        let mut h = StubHandler::new();
        let req = HttpRequest::new("GET", "/");
        let mut resp = HttpResponse::new(200);
        assert_eq!(h.on_begin_request(&req), Disposition::Continue);
        assert_eq!(h.on_send_response(&mut resp), Disposition::Continue);
        assert_eq!((h.begins, h.sends), (1, 1));
    }
}
