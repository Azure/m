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

/// A transparent handler decorator (M8-2): forwards both notification points to
/// the inner handler and returns its disposition unchanged. This is the web
/// analogue of [`PassThrough`](crate::PassThrough) and the **D25 "off"**
/// behavior — code is resident on the response path but alters nothing (the "no
/// behavior change today" endpoint). It never inspects or mutates the response.
pub struct IdentityHandler<H: RequestHandler> {
    inner: H,
}

impl<H: RequestHandler> IdentityHandler<H> {
    /// Wrap an inner handler.
    pub fn new(inner: H) -> Self {
        Self { inner }
    }

    /// Borrow the inner handler.
    #[must_use]
    pub fn inner(&self) -> &H {
        &self.inner
    }

    /// Recover the inner handler.
    pub fn into_inner(self) -> H {
        self.inner
    }
}

impl<H: RequestHandler> RequestHandler for IdentityHandler<H> {
    fn on_begin_request(&mut self, request: &HttpRequest) -> Disposition {
        self.inner.on_begin_request(request)
    }

    fn on_send_response(&mut self, response: &mut HttpResponse) -> Disposition {
        self.inner.on_send_response(response)
    }
}

/// A single observation recorded by the [`JournalingHandler`] (M8-3).
///
/// **PII-first (D28):** an event carries only *structural metadata* — the
/// method, URL, status, and header **names**. Header **values** and the request
/// / response **body** are never copied into the stream, because those are the
/// likeliest PII carriers. (The URL itself can carry PII in a query string; that
/// is a recorded limitation — for now the full URL is observed and finer
/// redaction is deferred to when the record format is designed.)
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ObservedEvent {
    /// The host began a request.
    BeginRequest {
        /// The request method (e.g. `GET`).
        method: String,
        /// The request URL / target.
        url: String,
        /// The request header names (values excluded — D28).
        header_names: Vec<String>,
    },
    /// The host is about to send a response.
    SendResponse {
        /// The response status code.
        status: u16,
        /// The response header names (values excluded — D28).
        header_names: Vec<String>,
    },
}

/// The sink the [`JournalingHandler`] reports [`ObservedEvent`]s to. This is the
/// safe seam through which the session's observation log is fed; the shim's
/// session supplies the concrete implementation (SHIM-D18). Kept deliberately
/// minimal — one method — so the storage target is separable from the handler.
pub trait ObservationSink {
    /// Record one observation.
    fn observe(&mut self, event: ObservedEvent);
}

/// A volume-control policy (D29): a set of known-safe `(method, url)` pairs whose
/// observations are **suppressed** so the high-traffic, low-information requests
/// do not flood the log. Suppression affects only what is *recorded*; it never
/// changes what the handler *returns* (the response is always forwarded
/// unchanged). Matching is exact (method and URL compared verbatim).
#[derive(Clone, Debug, Default)]
pub struct VolumePolicy {
    suppressed: Vec<(String, String)>,
}

impl VolumePolicy {
    /// A policy that suppresses nothing (records every exchange).
    #[must_use]
    pub fn record_all() -> Self {
        Self::default()
    }

    /// Mark a `(method, url)` pair as known-safe, suppressing its observations.
    pub fn suppress(&mut self, method: impl Into<String>, url: impl Into<String>) {
        self.suppressed.push((method.into(), url.into()));
    }

    /// Whether an exchange for `(method, url)` should be recorded (`true`) or is
    /// suppressed as known-safe (`false`).
    #[must_use]
    pub fn records(&self, method: &str, url: &str) -> bool {
        !self
            .suppressed
            .iter()
            .any(|(m, u)| m == method && u == url)
    }
}

/// A journaling handler decorator (M8-3): records each exchange to an
/// [`ObservationSink`] (subject to the [`VolumePolicy`], D29) then forwards the
/// notification to the inner handler **unchanged**. This is the **D25 "record"**
/// behavior — observe without altering the response. It slots into the D4
/// decorator stack exactly as the registry/filesystem decorators do.
pub struct JournalingHandler<H: RequestHandler, S: ObservationSink> {
    inner: H,
    sink: S,
    policy: VolumePolicy,
    /// Whether the in-flight request is being recorded (decided at
    /// `on_begin_request` from the policy, reused at `on_send_response` so the
    /// paired response observation honors the same suppression).
    record_current: bool,
}

impl<H: RequestHandler, S: ObservationSink> JournalingHandler<H, S> {
    /// Wrap an inner handler with a sink and volume policy.
    pub fn new(inner: H, sink: S, policy: VolumePolicy) -> Self {
        Self {
            inner,
            sink,
            policy,
            record_current: true,
        }
    }

    /// Borrow the inner handler.
    #[must_use]
    pub fn inner(&self) -> &H {
        &self.inner
    }

    /// Borrow the observation sink (e.g. to inspect what was recorded).
    #[must_use]
    pub fn sink(&self) -> &S {
        &self.sink
    }

    /// Recover the inner handler and sink.
    pub fn into_parts(self) -> (H, S) {
        (self.inner, self.sink)
    }
}

/// Collect header names without copying values (PII-first, D28).
fn header_names(headers: &[Header]) -> Vec<String> {
    headers.iter().map(|h| h.name().to_owned()).collect()
}

impl<H: RequestHandler, S: ObservationSink> RequestHandler for JournalingHandler<H, S> {
    fn on_begin_request(&mut self, request: &HttpRequest) -> Disposition {
        self.record_current = self.policy.records(request.method(), request.url());
        if self.record_current {
            self.sink.observe(ObservedEvent::BeginRequest {
                method: request.method().to_owned(),
                url: request.url().to_owned(),
                header_names: header_names(request.headers()),
            });
        }
        self.inner.on_begin_request(request)
    }

    fn on_send_response(&mut self, response: &mut HttpResponse) -> Disposition {
        // Forward first so the journal reflects the response as it actually
        // *leaves* (after the inner handler has produced it), mirroring how
        // `on_begin_request` records the request as it *enters*.
        let disposition = self.inner.on_send_response(response);
        if self.record_current {
            self.sink.observe(ObservedEvent::SendResponse {
                status: response.status(),
                header_names: header_names(response.headers()),
            });
        }
        disposition
    }
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

    /// A leaf handler that *does* alter the response and returns a chosen
    /// disposition, so a decorator's pass-through fidelity can be proven.
    struct MutatingHandler {
        new_status: u16,
        disposition: Disposition,
    }

    impl RequestHandler for MutatingHandler {
        fn on_begin_request(&mut self, _request: &HttpRequest) -> Disposition {
            Disposition::Continue
        }

        fn on_send_response(&mut self, response: &mut HttpResponse) -> Disposition {
            response.set_status(self.new_status);
            response.push_header("X-Handler", "mutating");
            self.disposition
        }
    }

    #[test]
    fn identity_forwards_dispositions_and_counts() {
        let mut h = IdentityHandler::new(StubHandler::new());
        let req = HttpRequest::new("POST", "/submit");
        let mut resp = HttpResponse::new(200);
        assert_eq!(h.on_begin_request(&req), Disposition::Continue);
        assert_eq!(h.on_send_response(&mut resp), Disposition::Continue);
        // Identity adds nothing of its own: the inner handler saw both calls.
        assert_eq!((h.inner().begins, h.inner().sends), (1, 1));
    }

    #[test]
    fn identity_is_byte_identical_to_undecorated() {
        let req = HttpRequest::new("GET", "/page");
        // Bare inner handler.
        let mut bare = MutatingHandler {
            new_status: 503,
            disposition: Disposition::FinishRequest,
        };
        let mut bare_resp = HttpResponse::new(200);
        let bare_begin = bare.on_begin_request(&req);
        let bare_send = bare.on_send_response(&mut bare_resp);
        // Same inner handler behind identity.
        let mut wrapped = IdentityHandler::new(MutatingHandler {
            new_status: 503,
            disposition: Disposition::FinishRequest,
        });
        let mut wrapped_resp = HttpResponse::new(200);
        let wrapped_begin = wrapped.on_begin_request(&req);
        let wrapped_send = wrapped.on_send_response(&mut wrapped_resp);
        // Dispositions and the resulting response are identical.
        assert_eq!(bare_begin, wrapped_begin);
        assert_eq!(bare_send, wrapped_send);
        assert_eq!(bare_resp, wrapped_resp);
    }

    /// A sink that collects events into a vector for inspection.
    #[derive(Default)]
    struct VecSink {
        events: Vec<ObservedEvent>,
    }

    impl ObservationSink for VecSink {
        fn observe(&mut self, event: ObservedEvent) {
            self.events.push(event);
        }
    }

    #[test]
    fn journaling_observes_without_altering_response() {
        let mut h = JournalingHandler::new(
            MutatingHandler {
                new_status: 204,
                disposition: Disposition::Continue,
            },
            VecSink::default(),
            VolumePolicy::record_all(),
        );
        let req = HttpRequest::new("GET", "/api").with_header("Accept", "text/html");
        // Compare the journaled response against the bare handler's output.
        let mut expected = HttpResponse::new(200);
        MutatingHandler {
            new_status: 204,
            disposition: Disposition::Continue,
        }
        .on_send_response(&mut expected);

        let mut resp = HttpResponse::new(200);
        h.on_begin_request(&req);
        h.on_send_response(&mut resp);

        // Response is exactly what the inner handler produced — unaltered.
        assert_eq!(resp, expected);
        // Two events observed: begin then send, names only (no values).
        assert_eq!(
            h.sink().events,
            vec![
                ObservedEvent::BeginRequest {
                    method: "GET".to_owned(),
                    url: "/api".to_owned(),
                    header_names: vec!["Accept".to_owned()],
                },
                ObservedEvent::SendResponse {
                    status: 204,
                    header_names: vec!["X-Handler".to_owned()],
                },
            ]
        );
    }

    #[test]
    fn volume_policy_suppresses_known_safe_but_still_forwards() {
        let mut policy = VolumePolicy::record_all();
        policy.suppress("GET", "/health");
        let mut h = JournalingHandler::new(StubHandler::new(), VecSink::default(), policy);

        let req = HttpRequest::new("GET", "/health");
        let mut resp = HttpResponse::new(200);
        assert_eq!(h.on_begin_request(&req), Disposition::Continue);
        assert_eq!(h.on_send_response(&mut resp), Disposition::Continue);

        // Suppressed: nothing recorded, but the inner handler still ran.
        assert!(h.sink().events.is_empty());
        assert_eq!((h.inner().begins, h.inner().sends), (1, 1));
    }
}
