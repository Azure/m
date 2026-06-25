// Copyright (c) Microsoft Corporation.

//! Replay egress (D15 / D25 replay): serve canned [`EgressResponse`]s from a
//! preloaded fixture set keyed by `(verb, path)` — the owner's "system state
//! pre-loaded" mode. A request with no matching fixture follows the configured
//! [`ReplayMiss`] policy: deny (fully offline) or fall through to the inner
//! surface (replay the known, go live for the rest).
//!
//! The fixture artifact format is owned here (Design Autonomy) and shaped like
//! the registry / filesystem artifacts (`<Platform><Egress>…`), parsed through
//! the same read-only `roxmltree` DOM so the loader contains no `unsafe`.

use crate::Utf16;
use crate::egress::{EgressRequest, EgressResponse, EgressSurface};
use crate::egress_error::{EgressError, EgressResult};

/// What a [`ReplayEgress`] does when no fixture matches a request.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReplayMiss {
    /// Deny the request with [`EgressError::NoFixture`] — a fully offline replay
    /// that never contacts a destination.
    Block,
    /// Fall through to the inner surface — replay the known requests and let the
    /// rest go live.
    ReadThrough,
}

/// A preloaded set of canned responses keyed by `(verb, path)`. Matching is
/// verb-ASCII-case-insensitive and path-exact (the path includes any query
/// string). Insertion order is the match order, so the first matching fixture
/// wins.
#[derive(Clone, Debug, Default)]
pub struct ReplaySet {
    fixtures: Vec<(String, String, EgressResponse)>,
}

impl ReplaySet {
    /// An empty set.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Builder: add a fixture for `(verb, path)`.
    #[must_use]
    pub fn with(mut self, verb: &str, path: &str, response: EgressResponse) -> Self {
        self.add(verb, path, response);
        self
    }

    /// Add a fixture for `(verb, path)` in place.
    pub fn add(&mut self, verb: &str, path: &str, response: EgressResponse) {
        self.fixtures.push((verb.to_ascii_uppercase(), path.to_string(), response));
    }

    /// Absorb every fixture from `other`, appending them after this set's own
    /// (so this set's fixtures keep match priority). Used to merge several
    /// fixture artifacts loaded from a directory.
    pub fn extend(&mut self, other: ReplaySet) {
        self.fixtures.extend(other.fixtures);
    }

    /// Number of fixtures.
    #[must_use]
    pub fn len(&self) -> usize {
        self.fixtures.len()
    }

    /// Whether the set is empty.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.fixtures.is_empty()
    }

    /// The first fixture matching `req`'s verb (ASCII-case-insensitive) and path
    /// (exact). An ill-formed-UTF-16 verb or path never matches (D9).
    fn lookup(&self, req: &EgressRequest) -> Option<&EgressResponse> {
        let verb = req.verb.to_utf8().ok()?.to_ascii_uppercase();
        let path = req.path.to_utf8().ok()?;
        self.fixtures
            .iter()
            .find(|(v, p, _)| *v == verb && *p == path)
            .map(|(_, _, resp)| resp)
    }

    /// Parse a replay-fixture artifact (the shared on-disk format owned here).
    ///
    /// The loader accepts being handed either the `<Platform>` root or an
    /// `<Egress>` element directly. Each `<Fixture verb path status>` becomes one
    /// canned response; optional `<Header name value/>` children populate the
    /// response headers and an optional `<Body>` child supplies the (UTF-8) body.
    ///
    /// # Errors
    ///
    /// [`EgressError::MalformedFixture`] if the XML is not well-formed, a required
    /// attribute is missing, or `status` is not a number.
    pub fn from_artifact(xml: &str) -> EgressResult<Self> {
        let doc = roxmltree::Document::parse(xml)
            .map_err(|e| EgressError::MalformedFixture(format!("XML parse error: {e}")))?;
        let root = doc.root_element();
        let egress = if root.has_tag_name("Egress") {
            Some(root)
        } else {
            root.children().find(|n| n.is_element() && n.has_tag_name("Egress"))
        };

        let mut set = Self::new();
        let Some(egress) = egress else {
            return Ok(set);
        };

        for fixture in egress.children().filter(|n| n.is_element() && n.has_tag_name("Fixture")) {
            let verb = required_attr(&fixture, "verb")?;
            let path = required_attr(&fixture, "path")?;
            let status_str = required_attr(&fixture, "status")?;
            let status: u32 = status_str.parse().map_err(|_| {
                EgressError::MalformedFixture(format!("invalid status: {status_str:?}"))
            })?;

            let mut headers = Vec::new();
            let mut body = Vec::new();
            for child in fixture.children().filter(roxmltree::Node::is_element) {
                if child.has_tag_name("Header") {
                    let name = required_attr(&child, "name")?;
                    let value = required_attr(&child, "value")?;
                    headers.push((Utf16::from_utf8(name), Utf16::from_utf8(value)));
                } else if child.has_tag_name("Body") {
                    body = child.text().unwrap_or("").as_bytes().to_vec();
                }
                // Unknown elements are ignored for forward compatibility.
            }

            set.add(verb, path, EgressResponse { status, headers, body });
        }

        Ok(set)
    }
}

/// Required-attribute accessor mirroring the registry/filesystem loaders.
fn required_attr<'a>(
    node: &roxmltree::Node<'a, '_>,
    name: &str,
) -> EgressResult<&'a str> {
    node.attribute(name).ok_or_else(|| {
        EgressError::MalformedFixture(format!("<{}> missing required '{name}'", node.tag_name().name()))
    })
}

/// A replay decorator: serves canned responses from a [`ReplaySet`], applying
/// the [`ReplayMiss`] policy when nothing matches.
pub struct ReplayEgress<S: EgressSurface> {
    inner: S,
    set: ReplaySet,
    miss: ReplayMiss,
}

impl<S: EgressSurface> ReplayEgress<S> {
    /// Wrap an inner surface with a fixture set and a miss policy.
    pub fn new(inner: S, set: ReplaySet, miss: ReplayMiss) -> Self {
        Self { inner, set, miss }
    }

    /// Recover the inner surface.
    pub fn into_inner(self) -> S {
        self.inner
    }
}

impl<S: EgressSurface> EgressSurface for ReplayEgress<S> {
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        if let Some(resp) = self.set.lookup(req) {
            return Ok(resp.clone());
        }
        match self.miss {
            ReplayMiss::Block => {
                let verb = req.verb.to_utf8().unwrap_or_else(|_| "?".to_string());
                let path = req.path.to_utf8().unwrap_or_else(|_| "?".to_string());
                Err(EgressError::NoFixture(format!("{verb} {path}")))
            }
            ReplayMiss::ReadThrough => self.inner.send(req),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::egress::Scheme;

    /// An inner surface that records what it received and returns a canned 200,
    /// so read-through can be observed.
    struct RecordingSurface {
        seen: Vec<EgressRequest>,
    }
    impl EgressSurface for RecordingSurface {
        fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
            self.seen.push(req.clone());
            Ok(EgressResponse::new(200, b"live".to_vec()))
        }
    }

    fn req(verb: &str, path: &str) -> EgressRequest {
        EgressRequest::http(Scheme::Http, "h", 80, verb, path)
    }

    fn set() -> ReplaySet {
        ReplaySet::new()
            .with("GET", "/custom/widget", EgressResponse::new(200, br#"{"exists":true}"#.to_vec()))
            .with("GET", "/missing", EgressResponse::empty(404))
    }

    #[test]
    fn replay_serves_a_matching_fixture() {
        let mut surface = ReplayEgress::new(
            RecordingSurface { seen: Vec::new() },
            set(),
            ReplayMiss::Block,
        );
        let resp = surface.send(&req("GET", "/custom/widget")).unwrap();
        assert_eq!(resp.status, 200);
        assert_eq!(resp.body, br#"{"exists":true}"#);
        // Inner never contacted on a hit.
        assert!(surface.into_inner().seen.is_empty());
    }

    #[test]
    fn replay_matches_verb_case_insensitively() {
        let mut surface =
            ReplayEgress::new(RecordingSurface { seen: Vec::new() }, set(), ReplayMiss::Block);
        assert_eq!(surface.send(&req("get", "/missing")).unwrap().status, 404);
    }

    #[test]
    fn replay_miss_block_denies() {
        let mut surface =
            ReplayEgress::new(RecordingSurface { seen: Vec::new() }, set(), ReplayMiss::Block);
        assert_eq!(
            surface.send(&req("GET", "/unknown")),
            Err(EgressError::NoFixture("GET /unknown".to_string()))
        );
    }

    #[test]
    fn replay_miss_read_through_goes_to_inner() {
        let mut surface = ReplayEgress::new(
            RecordingSurface { seen: Vec::new() },
            set(),
            ReplayMiss::ReadThrough,
        );
        let resp = surface.send(&req("POST", "/new")).unwrap();
        assert_eq!(resp.status, 200);
        assert_eq!(resp.body, b"live");
        assert_eq!(surface.into_inner().seen.len(), 1);
    }

    #[test]
    fn ill_formed_target_never_matches() {
        let mut r = req("GET", "/custom/widget");
        r.path = Utf16::from_units(vec![0xD800]);
        let mut surface =
            ReplayEgress::new(RecordingSurface { seen: Vec::new() }, set(), ReplayMiss::Block);
        assert!(matches!(surface.send(&r), Err(EgressError::NoFixture(_))));
    }

    #[test]
    fn from_artifact_parses_fixtures_with_headers_and_body() {
        let xml = r#"<?xml version="1.0" encoding="utf-8"?>
            <Platform>
              <Egress>
                <Fixture verb="GET" path="/custom/widget" status="200">
                  <Header name="Content-Type" value="application/json"/>
                  <Body>{"exists":true}</Body>
                </Fixture>
                <Fixture verb="DELETE" path="/custom/widget" status="204"/>
              </Egress>
            </Platform>"#;
        let parsed = ReplaySet::from_artifact(xml).expect("parse");
        assert_eq!(parsed.len(), 2);

        let mut surface =
            ReplayEgress::new(crate::egress_decorator::BlockingEgress, parsed, ReplayMiss::Block);
        let resp = surface.send(&req("GET", "/custom/widget")).unwrap();
        assert_eq!(resp.status, 200);
        assert_eq!(resp.body, br#"{"exists":true}"#);
        assert_eq!(resp.headers.len(), 1);
        assert_eq!(resp.headers[0].0.to_utf8().unwrap(), "Content-Type");
        assert_eq!(resp.headers[0].1.to_utf8().unwrap(), "application/json");

        let del = surface.send(&req("DELETE", "/custom/widget")).unwrap();
        assert_eq!(del.status, 204);
        assert!(del.body.is_empty());
    }

    #[test]
    fn from_artifact_accepts_egress_root_directly() {
        let xml = r#"<Egress><Fixture verb="GET" path="/p" status="200"/></Egress>"#;
        let parsed = ReplaySet::from_artifact(xml).expect("parse");
        assert_eq!(parsed.len(), 1);
    }

    #[test]
    fn from_artifact_absent_egress_is_empty() {
        let parsed = ReplaySet::from_artifact("<Platform></Platform>").expect("parse");
        assert!(parsed.is_empty());
    }

    #[test]
    fn from_artifact_rejects_malformed_xml() {
        assert!(matches!(
            ReplaySet::from_artifact("<Egress><Fixture"),
            Err(EgressError::MalformedFixture(_))
        ));
    }

    #[test]
    fn from_artifact_rejects_missing_attr() {
        assert!(matches!(
            ReplaySet::from_artifact(r#"<Egress><Fixture verb="GET" status="200"/></Egress>"#),
            Err(EgressError::MalformedFixture(_))
        ));
    }

    #[test]
    fn from_artifact_rejects_non_numeric_status() {
        assert!(matches!(
            ReplaySet::from_artifact(r#"<Egress><Fixture verb="GET" path="/p" status="ok"/></Egress>"#),
            Err(EgressError::MalformedFixture(_))
        ));
    }
}
