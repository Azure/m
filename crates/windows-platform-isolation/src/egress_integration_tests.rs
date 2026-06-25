// Copyright (c) Microsoft Corporation.

//! End-to-end composition test (M11-6) for the egress surface (D31).
//!
//! Composes the [`Egress`] facade over the D25 decorator stack — redirect /
//! observe / buffer / replay / block — atop an in-memory leaf service, and
//! asserts the modes the owner asked for hold when stacked the way the shim
//! (MW17) will compose them from a `.pilcfg` `egress` section:
//!
//! * **passthrough** is byte-identical to the bare leaf,
//! * **redirect** rewrites the destination while **observe** records the
//!   *original* target without altering the response,
//! * **buffer** isolates mutations until commit, and
//! * **replay over block** serves preloaded fixtures and denies the rest
//!   (fully offline).
//!
//! Lives as an in-crate `#[cfg(test)]` module (not a `tests/` crate) to match
//! `integration_tests.rs`.

#![cfg(test)]

use crate::egress::{Egress, EgressRequest, EgressResponse, EgressSurface, Scheme};
use crate::egress_decorator::{
    BlockingEgress, BufferedEgress, EgressObservation, EgressObserver, EgressOutcome,
    ObservingEgress, RedirectRule, RedirectingEgress,
};
use crate::egress_error::{EgressError, EgressResult};
use crate::egress_replay::{ReplayEgress, ReplayMiss, ReplaySet};

/// An in-memory leaf service standing in for `LiveEgress`: records the requests
/// it received and echoes the request path back as a `200` body.
#[derive(Default)]
struct MemoryService {
    seen: Vec<EgressRequest>,
}

impl EgressSurface for MemoryService {
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        self.seen.push(req.clone());
        let echo = req.path.to_utf8().unwrap_or_default().into_bytes();
        Ok(EgressResponse::new(200, echo))
    }
}

/// A recording observer.
#[derive(Default)]
struct Recorder {
    events: Vec<EgressObservation>,
}

impl EgressObserver for Recorder {
    fn observe(&mut self, observation: &EgressObservation) {
        self.events.push(observation.clone());
    }
}

fn get(host: &str, port: u16, path: &str) -> EgressRequest {
    EgressRequest::http(Scheme::Http, host, port, "GET", path)
}

#[test]
fn facade_passthrough_is_identical_to_the_bare_leaf() {
    // The facade over the bare leaf must be indistinguishable from calling the
    // leaf directly (D25 "off" = true identity).
    let mut bare = MemoryService::default();
    let bare_resp = bare.send(&get("h", 80, "/p")).unwrap();

    let mut facade = Egress::new(MemoryService::default());
    let facade_resp = facade.send(&get("h", 80, "/p")).unwrap();

    assert_eq!(bare_resp, facade_resp);
    assert_eq!(facade_resp.status, 200);
    assert_eq!(facade_resp.body, b"/p");
}

#[test]
fn redirect_then_observe_composed_through_the_facade() {
    // Egress( Observe( Redirect( Memory ) ) ): the leaf sees the rewritten
    // destination, the observer sees the ORIGINAL target, the response is
    // unaltered.
    let redirect = RedirectingEgress::new(MemoryService::default())
        .with_rule(RedirectRule::new("localhost", Some(8019), "127.0.0.1", 18019));
    let observe = ObservingEgress::new(redirect, Recorder::default());
    let mut egress = Egress::new(observe);

    let resp = egress.send(&get("localhost", 8019, "/goal")).unwrap();
    assert_eq!(resp.status, 200);
    assert_eq!(resp.body, b"/goal"); // unaltered

    let (redirect, recorder) = egress.into_surface().into_parts();
    // Observer saw the original authority.
    assert_eq!(recorder.events.len(), 1);
    assert_eq!(recorder.events[0].authority, "localhost:8019");
    assert_eq!(recorder.events[0].outcome, EgressOutcome::Status(200));
    // Leaf saw the redirected destination.
    let seen = &redirect.into_inner().seen[0];
    assert_eq!(seen.host.to_utf8().unwrap(), "127.0.0.1");
    assert_eq!(seen.port, 18019);
}

#[test]
fn buffer_isolates_mutations_until_commit_through_the_facade() {
    let mut egress = Egress::new(BufferedEgress::new(MemoryService::default()));

    // A mutation is captured, not sent.
    let ack =
        egress.send(&EgressRequest::http(Scheme::Http, "h", 80, "POST", "/w")).unwrap();
    assert_eq!(ack.status, 202);
    assert!(egress.surface().is_dirty());

    // A read falls through to the (so-far untouched) leaf.
    egress.send(&get("h", 80, "/r")).unwrap();

    // Commit replays the buffered mutation onto the leaf.
    egress.surface().commit().expect("commit");
    let leaf = egress.into_surface().into_inner();
    let verbs: Vec<String> =
        leaf.seen.iter().map(|r| r.verb.to_utf8().unwrap()).collect();
    assert_eq!(verbs, vec!["GET".to_string(), "POST".to_string()]);
}

#[test]
fn replay_over_block_serves_fixtures_and_denies_the_rest() {
    // Egress( Replay( Block ) ) with ReadThrough miss → a fully offline service:
    // known requests are replayed from fixtures, everything else is blocked.
    let fixtures = ReplaySet::new().with(
        "GET",
        "/custom/widget",
        EgressResponse::new(200, br#"{"exists":true}"#.to_vec()),
    );
    let replay = ReplayEgress::new(BlockingEgress, fixtures, ReplayMiss::ReadThrough);
    let mut egress = Egress::new(replay);

    let hit = egress.send(&get("merriam", 80, "/custom/widget")).unwrap();
    assert_eq!(hit.status, 200);
    assert_eq!(hit.body, br#"{"exists":true}"#);

    let miss = egress.send(&get("merriam", 80, "/custom/other"));
    assert_eq!(miss, Err(EgressError::Blocked));
}

#[test]
fn replay_from_artifact_composed_through_the_facade() {
    let xml = r#"<Platform><Egress>
        <Fixture verb="GET" path="/custom/widget" status="200"><Body>{"exists":true}</Body></Fixture>
    </Egress></Platform>"#;
    let fixtures = ReplaySet::from_artifact(xml).expect("parse fixtures");
    let mut egress = Egress::new(ReplayEgress::new(BlockingEgress, fixtures, ReplayMiss::Block));

    let hit = egress.send(&get("merriam", 80, "/custom/widget")).unwrap();
    assert_eq!(hit.status, 200);
    assert_eq!(hit.body, br#"{"exists":true}"#);
    assert!(matches!(
        egress.send(&get("merriam", 80, "/missing")),
        Err(EgressError::NoFixture(_))
    ));
}
