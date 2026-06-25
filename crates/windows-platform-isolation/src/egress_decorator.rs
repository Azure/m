// Copyright (c) Microsoft Corporation.

//! Egress decorators written once over the [`EgressSurface`] seam (D4/D25/D31).
//!
//! Each mode the owner asked for is a decorator over the single
//! [`send`](EgressSurface::send) verb, mirroring the registry/filesystem
//! decorator stacks:
//!
//! - [`RedirectingEgress`] — rewrite the destination (scheme / host / port) by
//!   rule, then delegate to the inner surface ("URL / IP / port changed").
//! - [`ObservingEgress`] — record shape/metadata only (verb, authority, path,
//!   outcome — **never** header values or body, D28) to an [`EgressObserver`]
//!   (D29), then forward the inner result unchanged.
//! - [`BlockingEgress`] — deny every request with [`EgressError::Blocked`].
//!
//! The buffer (D30 peer) and replay (D15) decorators land in later M11 items.

use crate::Utf16;
use crate::egress::{EgressRequest, EgressResponse, EgressSurface, Scheme};
use crate::egress_error::{EgressError, EgressResult};

// ----------------------------------------------------------------- Redirect

/// A single redirection rule: match an outbound request by destination
/// authority and rewrite its scheme / host / port. This is the surface model of
/// a `.pilcfg` `egress` redirection (`{from, to}`); the shim builds these from
/// the sidecar (MW17).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RedirectRule {
    from_host: String,
    from_port: Option<u16>,
    to_scheme: Option<Scheme>,
    to_host: String,
    to_port: u16,
}

impl RedirectRule {
    /// A rule matching `from_host` (compared ASCII-case-insensitively) on
    /// `from_port` (`None` = any port) and rewriting the destination to
    /// `to_host:to_port`, keeping the original scheme.
    #[must_use]
    pub fn new(
        from_host: impl Into<String>,
        from_port: Option<u16>,
        to_host: impl Into<String>,
        to_port: u16,
    ) -> Self {
        Self {
            from_host: from_host.into(),
            from_port,
            to_scheme: None,
            to_host: to_host.into(),
            to_port,
        }
    }

    /// Also rewrite the scheme to `scheme`.
    #[must_use]
    pub fn with_scheme(mut self, scheme: Scheme) -> Self {
        self.to_scheme = Some(scheme);
        self
    }

    /// Whether this rule matches `req`'s destination. An ill-formed-UTF-16 host
    /// never matches (D9: it cannot be compared as text).
    fn matches(&self, req: &EgressRequest) -> bool {
        if self.from_port.is_some_and(|port| port != req.port) {
            return false;
        }
        match req.host.to_utf8() {
            Ok(host) => host.eq_ignore_ascii_case(&self.from_host),
            Err(_) => false,
        }
    }

    /// Rewrite `req`'s destination per this rule.
    fn apply(&self, req: &mut EgressRequest) {
        if let Some(scheme) = self.to_scheme {
            req.scheme = scheme;
        }
        req.host = Utf16::from_utf8(&self.to_host);
        req.port = self.to_port;
    }
}

/// A redirecting decorator: rewrites each request's destination by the first
/// matching [`RedirectRule`], then delegates to the inner surface. A request
/// matching no rule is forwarded unchanged.
pub struct RedirectingEgress<S: EgressSurface> {
    inner: S,
    rules: Vec<RedirectRule>,
}

impl<S: EgressSurface> RedirectingEgress<S> {
    /// Wrap an inner surface with no rules (an identity until rules are added).
    pub fn new(inner: S) -> Self {
        Self { inner, rules: Vec::new() }
    }

    /// Builder: append a rule.
    #[must_use]
    pub fn with_rule(mut self, rule: RedirectRule) -> Self {
        self.rules.push(rule);
        self
    }

    /// Append a rule in place.
    pub fn push_rule(&mut self, rule: RedirectRule) {
        self.rules.push(rule);
    }

    /// Recover the inner surface.
    pub fn into_inner(self) -> S {
        self.inner
    }
}

impl<S: EgressSurface> EgressSurface for RedirectingEgress<S> {
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        let mut rewritten = req.clone();
        for rule in &self.rules {
            if rule.matches(&rewritten) {
                rule.apply(&mut rewritten);
                break;
            }
        }
        self.inner.send(&rewritten)
    }
}

// ----------------------------------------------------------------- Observe

/// The outcome of an observed exchange (D28-safe: status code or error kind
/// only, never payload).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EgressOutcome {
    /// The inner surface produced a response with this HTTP status.
    Status(u32),
    /// The inner surface returned an error (kind elided — PII-first, D28).
    Error,
}

/// One egress observation (D29), **PII-first** (D28): shape and metadata only —
/// the verb, the `host:port` authority, the request path, and the outcome. It
/// deliberately carries **no** header values and **no** body.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct EgressObservation {
    /// The HTTP verb (best-effort UTF-8; ill-formed renders as `"?"`).
    pub verb: String,
    /// The `host:port` authority (best-effort; `"?"` if the host is ill-formed).
    pub authority: String,
    /// The request path (best-effort UTF-8; ill-formed renders as `"?"`).
    pub path: String,
    /// The exchange outcome.
    pub outcome: EgressOutcome,
}

/// The egress observation sink (D29): one method, so the storage target is
/// separable from the surface. The egress peer of [`ObservationSink`](crate::ObservationSink).
pub trait EgressObserver {
    /// Record one observation.
    fn observe(&mut self, observation: &EgressObservation);
}

/// An observing decorator: forwards every request to the inner surface
/// **unchanged** and records a PII-first [`EgressObservation`] of the exchange.
/// This is the D25 "record" behavior for egress.
pub struct ObservingEgress<S: EgressSurface, O: EgressObserver> {
    inner: S,
    observer: O,
}

impl<S: EgressSurface, O: EgressObserver> ObservingEgress<S, O> {
    /// Wrap an inner surface, recording to `observer`.
    pub fn new(inner: S, observer: O) -> Self {
        Self { inner, observer }
    }

    /// Recover the inner surface and observer.
    pub fn into_parts(self) -> (S, O) {
        (self.inner, self.observer)
    }
}

/// Best-effort UTF-8 of a stored string for metadata logging; ill-formed
/// UTF-16 renders as `"?"` so observation never fails or leaks raw units.
fn meta(s: &Utf16) -> String {
    s.to_utf8().unwrap_or_else(|_| "?".to_string())
}

impl<S: EgressSurface, O: EgressObserver> EgressSurface for ObservingEgress<S, O> {
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        let result = self.inner.send(req);
        let outcome = match &result {
            Ok(resp) => EgressOutcome::Status(resp.status),
            Err(_) => EgressOutcome::Error,
        };
        let authority = req.authority().unwrap_or_else(|| "?".to_string());
        self.observer.observe(&EgressObservation {
            verb: meta(&req.verb),
            authority,
            path: meta(&req.path),
            outcome,
        });
        result
    }
}

// ------------------------------------------------------------------- Block

/// A blocking surface: denies every request with [`EgressError::Blocked`]. The
/// network is never contacted. The `block` mode and a leaf for negative-path
/// testing.
#[derive(Clone, Copy, Debug, Default)]
pub struct BlockingEgress;

impl EgressSurface for BlockingEgress {
    fn send(&mut self, _req: &EgressRequest) -> EgressResult<EgressResponse> {
        Err(EgressError::Blocked)
    }
}

// ------------------------------------------------------------------ Buffer

/// The default synthetic status returned for a buffered (captured-but-not-sent)
/// mutation: `202 Accepted`.
const DEFAULT_ACK_STATUS: u32 = 202;

/// A write-buffering decorator (D30 peer): **mutating** requests (non-safe
/// verbs) are captured in an in-memory journal and answered with a synthetic
/// ack — the inner surface (and thus the destination) is **never** contacted
/// for them until [`commit`](BufferedEgress::commit). **Safe** requests
/// (`GET`/`HEAD`/`OPTIONS`/`TRACE`) fall through to the inner surface, so the
/// inner choice controls read behavior: `BufferedEgress<LiveEgress>` buffers
/// writes while reads hit the live network, whereas `BufferedEgress<BlockingEgress>`
/// buffers writes and denies reads (a fully offline "memory buffered" service).
///
/// The journal *is* the request verb stream (D4): `commit` replays the captured
/// mutations onto the inner surface in order; `rollback` discards them.
pub struct BufferedEgress<S: EgressSurface> {
    inner: S,
    journal: Vec<EgressRequest>,
    ack_status: u32,
}

impl<S: EgressSurface> BufferedEgress<S> {
    /// Wrap an inner surface, acking buffered mutations with `202 Accepted`.
    pub fn new(inner: S) -> Self {
        Self { inner, journal: Vec::new(), ack_status: DEFAULT_ACK_STATUS }
    }

    /// Builder: use `status` as the synthetic ack for buffered mutations.
    #[must_use]
    pub fn with_ack_status(mut self, status: u32) -> Self {
        self.ack_status = status;
        self
    }

    /// The captured (not-yet-committed) mutating requests, in order.
    #[must_use]
    pub fn journal(&self) -> &[EgressRequest] {
        &self.journal
    }

    /// Whether any mutation is buffered and uncommitted.
    #[must_use]
    pub fn is_dirty(&self) -> bool {
        !self.journal.is_empty()
    }

    /// Replay every buffered mutation onto the inner surface in order, clearing
    /// the journal. Stops at and returns the first inner error (the remaining
    /// journal is left intact for inspection / retry).
    pub fn commit(&mut self) -> EgressResult<()> {
        let pending = core::mem::take(&mut self.journal);
        let mut remaining = pending.into_iter();
        for req in remaining.by_ref() {
            if let Err(e) = self.inner.send(&req) {
                // Put back the failing request and the rest, then report.
                self.journal.push(req);
                self.journal.extend(remaining);
                return Err(e);
            }
        }
        Ok(())
    }

    /// Discard the buffered mutations without touching the inner surface.
    pub fn rollback(&mut self) {
        self.journal.clear();
    }

    /// Recover the inner surface (dropping any uncommitted journal).
    pub fn into_inner(self) -> S {
        self.inner
    }
}

impl<S: EgressSurface> EgressSurface for BufferedEgress<S> {
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        if req.is_mutating() {
            self.journal.push(req.clone());
            Ok(EgressResponse::empty(self.ack_status))
        } else {
            self.inner.send(req)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A leaf surface that records the requests it received and returns a canned
    /// response, so tests can assert what a decorator forwarded.
    struct RecordingSurface {
        seen: Vec<EgressRequest>,
        status: u32,
    }
    impl RecordingSurface {
        fn new(status: u32) -> Self {
            Self { seen: Vec::new(), status }
        }
    }
    impl EgressSurface for RecordingSurface {
        fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
            self.seen.push(req.clone());
            Ok(EgressResponse::new(self.status, b"ok".to_vec()))
        }
    }

    /// A surface that always errors, to exercise the observe error path.
    struct FailingSurface;
    impl EgressSurface for FailingSurface {
        fn send(&mut self, _req: &EgressRequest) -> EgressResult<EgressResponse> {
            Err(EgressError::Os(12029))
        }
    }

    fn req(host: &str, port: u16, verb: &str, path: &str) -> EgressRequest {
        EgressRequest::http(Scheme::Http, host, port, verb, path)
    }

    #[test]
    fn redirect_rewrites_matching_authority() {
        let inner = RecordingSurface::new(200);
        let mut surface = RedirectingEgress::new(inner)
            .with_rule(RedirectRule::new("localhost", Some(8019), "127.0.0.1", 18019));
        surface.send(&req("localhost", 8019, "GET", "/p")).unwrap();
        let inner = surface.into_inner();
        let seen = &inner.seen[0];
        assert_eq!(seen.host.to_utf8().unwrap(), "127.0.0.1");
        assert_eq!(seen.port, 18019);
        assert_eq!(seen.scheme, Scheme::Http); // unchanged
        assert_eq!(seen.path.to_utf8().unwrap(), "/p"); // path preserved
    }

    #[test]
    fn redirect_can_change_scheme() {
        let inner = RecordingSurface::new(200);
        let mut surface = RedirectingEgress::new(inner).with_rule(
            RedirectRule::new("api", None, "fixture", 9443).with_scheme(Scheme::Https),
        );
        surface.send(&req("api", 443, "GET", "/")).unwrap();
        let seen = &surface.into_inner().seen[0];
        assert_eq!(seen.scheme, Scheme::Https);
        assert_eq!(seen.host.to_utf8().unwrap(), "fixture");
        assert_eq!(seen.port, 9443);
    }

    #[test]
    fn redirect_matches_any_port_when_unspecified() {
        let inner = RecordingSurface::new(200);
        let mut surface = RedirectingEgress::new(inner)
            .with_rule(RedirectRule::new("imds", None, "stub", 1));
        surface.send(&req("imds", 80, "GET", "/")).unwrap();
        surface.send(&req("imds", 12345, "GET", "/")).unwrap();
        for seen in &surface.into_inner().seen {
            assert_eq!(seen.host.to_utf8().unwrap(), "stub");
            assert_eq!(seen.port, 1);
        }
    }

    #[test]
    fn redirect_host_match_is_case_insensitive() {
        let inner = RecordingSurface::new(200);
        let mut surface = RedirectingEgress::new(inner)
            .with_rule(RedirectRule::new("LocalHost", None, "stub", 1));
        surface.send(&req("localhost", 80, "GET", "/")).unwrap();
        assert_eq!(surface.into_inner().seen[0].host.to_utf8().unwrap(), "stub");
    }

    #[test]
    fn redirect_passes_non_matching_through_unchanged() {
        let inner = RecordingSurface::new(200);
        let mut surface = RedirectingEgress::new(inner)
            .with_rule(RedirectRule::new("other", None, "stub", 1));
        surface.send(&req("localhost", 8080, "GET", "/keep")).unwrap();
        let seen = &surface.into_inner().seen[0];
        assert_eq!(seen.host.to_utf8().unwrap(), "localhost");
        assert_eq!(seen.port, 8080);
    }

    #[test]
    fn redirect_first_matching_rule_wins() {
        let inner = RecordingSurface::new(200);
        let mut surface = RedirectingEgress::new(inner)
            .with_rule(RedirectRule::new("h", None, "first", 1))
            .with_rule(RedirectRule::new("h", None, "second", 2));
        surface.send(&req("h", 80, "GET", "/")).unwrap();
        let seen = &surface.into_inner().seen[0];
        assert_eq!(seen.host.to_utf8().unwrap(), "first");
        assert_eq!(seen.port, 1);
    }

    #[test]
    fn observe_forwards_unchanged_and_records_status() {
        struct Sink(Vec<EgressObservation>);
        impl EgressObserver for Sink {
            fn observe(&mut self, o: &EgressObservation) {
                self.0.push(o.clone());
            }
        }
        let inner = RecordingSurface::new(204);
        let mut surface = ObservingEgress::new(inner, Sink(Vec::new()));
        let resp = surface.send(&req("h", 8019, "POST", "/v")).unwrap();
        assert_eq!(resp.status, 204); // forwarded unchanged
        let (inner, sink) = surface.into_parts();
        assert_eq!(inner.seen.len(), 1); // forwarded to inner
        assert_eq!(sink.0.len(), 1);
        let o = &sink.0[0];
        assert_eq!(o.verb, "POST");
        assert_eq!(o.authority, "h:8019");
        assert_eq!(o.path, "/v");
        assert_eq!(o.outcome, EgressOutcome::Status(204));
    }

    #[test]
    fn observe_records_error_outcome() {
        struct Sink(Vec<EgressObservation>);
        impl EgressObserver for Sink {
            fn observe(&mut self, o: &EgressObservation) {
                self.0.push(o.clone());
            }
        }
        let mut surface = ObservingEgress::new(FailingSurface, Sink(Vec::new()));
        assert!(surface.send(&req("h", 80, "GET", "/")).is_err());
        let (_, sink) = surface.into_parts();
        assert_eq!(sink.0[0].outcome, EgressOutcome::Error);
    }

    #[test]
    fn block_denies_every_request() {
        let mut surface = BlockingEgress;
        assert_eq!(
            surface.send(&req("h", 80, "GET", "/")),
            Err(EgressError::Blocked)
        );
        assert_eq!(
            surface.send(&req("h", 80, "POST", "/")),
            Err(EgressError::Blocked)
        );
    }

    #[test]
    fn decorators_compose() {
        // observe( redirect( recording ) ): the observation reflects the
        // ORIGINAL request target, while the inner sees the rewritten one.
        struct Sink(Vec<EgressObservation>);
        impl EgressObserver for Sink {
            fn observe(&mut self, o: &EgressObservation) {
                self.0.push(o.clone());
            }
        }
        let inner = RecordingSurface::new(200);
        let redirect = RedirectingEgress::new(inner)
            .with_rule(RedirectRule::new("localhost", Some(8019), "127.0.0.1", 18019));
        let mut surface = ObservingEgress::new(redirect, Sink(Vec::new()));
        surface.send(&req("localhost", 8019, "GET", "/q")).unwrap();
        let (redirect, sink) = surface.into_parts();
        // observed the original authority...
        assert_eq!(sink.0[0].authority, "localhost:8019");
        // ...inner received the redirected one.
        let seen = &redirect.into_inner().seen[0];
        assert_eq!(seen.host.to_utf8().unwrap(), "127.0.0.1");
        assert_eq!(seen.port, 18019);
    }

    #[test]
    fn buffered_mutation_is_captured_not_sent() {
        let mut surface = BufferedEgress::new(RecordingSurface::new(200));
        let resp = surface.send(&req("h", 80, "POST", "/w")).unwrap();
        assert_eq!(resp.status, 202); // synthetic ack
        assert!(resp.body.is_empty());
        assert!(surface.is_dirty());
        assert_eq!(surface.journal().len(), 1);
        assert_eq!(surface.journal()[0].verb.to_utf8().unwrap(), "POST");
        // The inner surface was never contacted.
        assert!(surface.into_inner().seen.is_empty());
    }

    #[test]
    fn buffered_read_falls_through_to_inner() {
        let mut surface = BufferedEgress::new(RecordingSurface::new(200));
        let resp = surface.send(&req("h", 80, "GET", "/r")).unwrap();
        assert_eq!(resp.status, 200); // inner's response
        assert!(!surface.is_dirty()); // reads are not buffered
        assert_eq!(surface.into_inner().seen.len(), 1);
    }

    #[test]
    fn buffered_read_over_block_denies() {
        // BufferedEgress<BlockingEgress>: writes buffered, reads denied — a fully
        // offline "memory buffered" service.
        let mut surface = BufferedEgress::new(BlockingEgress);
        assert!(surface.send(&req("h", 80, "POST", "/w")).is_ok()); // buffered ack
        assert_eq!(surface.send(&req("h", 80, "GET", "/r")), Err(EgressError::Blocked));
        assert_eq!(surface.journal().len(), 1);
    }

    #[test]
    fn commit_replays_mutations_onto_inner_in_order() {
        let mut surface = BufferedEgress::new(RecordingSurface::new(200));
        surface.send(&req("h", 80, "POST", "/a")).unwrap();
        surface.send(&req("h", 80, "DELETE", "/b")).unwrap();
        assert_eq!(surface.journal().len(), 2);
        surface.commit().expect("commit");
        assert!(!surface.is_dirty());
        let seen = &surface.into_inner().seen;
        assert_eq!(seen.len(), 2);
        assert_eq!(seen[0].path.to_utf8().unwrap(), "/a");
        assert_eq!(seen[1].path.to_utf8().unwrap(), "/b");
    }

    #[test]
    fn rollback_discards_without_touching_inner() {
        let mut surface = BufferedEgress::new(RecordingSurface::new(200));
        surface.send(&req("h", 80, "POST", "/a")).unwrap();
        surface.rollback();
        assert!(!surface.is_dirty());
        assert!(surface.into_inner().seen.is_empty());
    }

    #[test]
    fn ack_status_is_configurable() {
        let mut surface = BufferedEgress::new(RecordingSurface::new(200)).with_ack_status(200);
        let resp = surface.send(&req("h", 80, "PUT", "/x")).unwrap();
        assert_eq!(resp.status, 200);
    }

    #[test]
    fn commit_propagates_inner_error_and_preserves_journal() {
        // FailingSurface errors on every send, so commit fails replaying the first
        // mutation and the journal is preserved for inspection/retry.
        let mut surface = BufferedEgress::new(FailingSurface);
        surface.send(&req("h", 80, "POST", "/a")).unwrap();
        surface.send(&req("h", 80, "POST", "/b")).unwrap();
        assert_eq!(surface.commit(), Err(EgressError::Os(12029)));
        assert_eq!(surface.journal().len(), 2); // both preserved
        assert!(surface.is_dirty());
    }
}
