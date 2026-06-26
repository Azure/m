// Copyright (c) Microsoft Corporation.

//! Deriving the environment descriptor's participants from journal records
//! (D-CART-4, milestone EM-B).
//!
//! The journal records *observations*; this module turns them into the descriptor's
//! **actors** — the concrete participants we saw — and their network **bindings**.
//! Roles and channels (the abstraction layer) are derived separately (EM-C).
//!
//! Three kinds of actor arise:
//!
//! - **Remote egress servers** — one per observed authority (`host:port`) the
//!   process called out to, each carrying a [`Binding`] per distinct
//!   `(scheme, host, port)`.
//! - **The local process** ([`LOCAL_ACTOR`]) — the egress *client* and/or inbound
//!   *server*. Its own address is never captured (egress records carry the
//!   destination; inbound records carry no listen address), so it has no bindings;
//!   per D-CART-4 bindings are optional evidence.
//! - **Inbound callers** ([`INBOUND_CLIENT_ACTOR`]) — the anonymous remote caller
//!   population on the inbound seam. Callers are not individually identified
//!   (D-CART-4: richer attribution is deferred and gated on PII-A), so they
//!   collapse to a single actor with no binding.

use std::collections::{BTreeMap, BTreeSet};

use api_journal::{JournalRecord, Seam};

use crate::environment::{
    Actor, Actors, Binding, Channel, Environment, Observed, Provenance, Role, RolePart,
};

/// Actor id for the observed local process — the egress *client* and/or the
/// inbound *server*. Its own network binding is not captured, so this actor has no
/// [`Binding`]s.
pub const LOCAL_ACTOR: &str = "local";

/// Actor id for the anonymous inbound caller population — one per inbound seam.
/// Remote callers are not individually identified (D-CART-4: richer inbound-caller
/// attribution is deferred and gated on PII-A), so they collapse to one actor.
pub const INBOUND_CLIENT_ACTOR: &str = "inbound-client";

/// The authority key (`host:port`, or just `host` when no port was observed) that
/// identifies a remote endpoint actor.
fn authority(host: &str, port: Option<u16>) -> String {
    match port {
        Some(port) => format!("{host}:{port}"),
        None => host.to_string(),
    }
}

/// Derive the [`Actors`] of an environment descriptor from journal records.
///
/// Egress records yield one remote *server* actor per observed authority
/// (`host:port`), each carrying a [`Binding`] per distinct `(scheme, host, port)`.
/// Inbound records yield the single anonymous [`INBOUND_CLIENT_ACTOR`]. Either seam
/// means the observed process participated, so it becomes the single
/// [`LOCAL_ACTOR`]. Each binding and actor carries the observed evidence
/// (interaction counts and the first/last timestamp) attributed to it.
#[must_use]
pub fn derive_actors(records: &[JournalRecord]) -> Actors {
    let mut actors = Actors::new();
    // The local process participates in every record; its evidence is accumulated
    // here and applied once the actor is created (after the scan).
    let mut local = Observed::default();

    for record in records {
        accumulate(&mut local, record.timestamp_ms);
        match record.seam {
            Seam::Egress => {
                let Some(host) = record.host.as_deref() else {
                    continue;
                };
                let actor = actors
                    .entry(authority(host, record.port))
                    .or_insert_with(|| Actor {
                        provenance: Provenance::derived("egress destination"),
                        ..Default::default()
                    });
                accumulate(&mut actor.observed, record.timestamp_ms);
                let binding = binding_for(actor, record.scheme.as_deref(), host, record.port);
                accumulate(&mut binding.observed, record.timestamp_ms);
            }
            Seam::Inbound => {
                let client = inbound_client_actor(&mut actors);
                accumulate(&mut client.observed, record.timestamp_ms);
            }
        }
    }

    if local.interactions > 0 {
        local_actor(&mut actors).observed = local;
    }
    actors
}

/// Add one interaction (and widen the first/last time window) to `observed`. A
/// zero timestamp means "unavailable" and does not move the window.
fn accumulate(observed: &mut Observed, timestamp_ms: u64) {
    observed.interactions += 1;
    if timestamp_ms != 0 {
        observed.first_ms = Some(
            observed
                .first_ms
                .map_or(timestamp_ms, |first| first.min(timestamp_ms)),
        );
        observed.last_ms = Some(
            observed
                .last_ms
                .map_or(timestamp_ms, |last| last.max(timestamp_ms)),
        );
    }
}

/// The [`Binding`] of `actor` for `(scheme, host, port)`, created if absent.
fn binding_for<'a>(
    actor: &'a mut Actor,
    scheme: Option<&str>,
    host: &str,
    port: Option<u16>,
) -> &'a mut Binding {
    let existing = actor.bindings.iter().position(|binding| {
        binding.scheme.as_deref() == scheme
            && binding.host.as_deref() == Some(host)
            && binding.port == port
    });
    let index = match existing {
        Some(index) => index,
        None => {
            actor.bindings.push(Binding {
                scheme: scheme.map(str::to_string),
                host: Some(host.to_string()),
                port,
                ..Default::default()
            });
            actor.bindings.len() - 1
        }
    };
    &mut actor.bindings[index]
}

/// The single local-process actor, created if absent.
fn local_actor(actors: &mut Actors) -> &mut Actor {
    actors
        .entry(LOCAL_ACTOR.to_string())
        .or_insert_with(|| Actor {
            title: Some("the observed process".to_string()),
            provenance: Provenance::derived("the observed process (egress client / inbound server)"),
            ..Default::default()
        })
}

/// The single anonymous inbound-caller actor, created if absent.
fn inbound_client_actor(actors: &mut Actors) -> &mut Actor {
    actors
        .entry(INBOUND_CLIENT_ACTOR.to_string())
        .or_insert_with(|| Actor {
            title: Some("anonymous inbound callers".to_string()),
            provenance: Provenance::derived("inbound caller seam (anonymous)"),
            ..Default::default()
        })
}

/// Derive the full environment descriptor (actors and roles) from journal records.
///
/// Actors come from [`derive_actors`]; each part an actor plays (client/server)
/// becomes one coarse [`Role`] (D-CART-4, EM-C1), and the actor's `plays` lists the
/// role ids. A role is the substitution unit, so the local process — which plays
/// both the inbound *server* and the egress *client* — yields two roles. Channels
/// (EM-C2) link the roles; contract links and security are layered on in EM-C3..C4.
#[must_use]
pub fn derive_environment(records: &[JournalRecord]) -> Environment {
    let mut environment = Environment::new("observed environment", "0.0.0");
    environment.actors = derive_actors(records);

    for (actor_id, parts) in actor_parts(records) {
        for part in parts {
            let id = role_id(part, &actor_id);
            environment.roles.insert(
                id.clone(),
                Role {
                    plays: vec![part],
                    provenance: Provenance::derived(role_basis(part, &actor_id)),
                    ..Default::default()
                },
            );
            if let Some(actor) = environment.actors.get_mut(&actor_id) {
                actor.plays.push(id);
            }
        }
    }

    derive_channels(records, &mut environment);
    environment
}

/// Map each actor id to the set of parts (client/server) it was observed playing.
fn actor_parts(records: &[JournalRecord]) -> BTreeMap<String, BTreeSet<RolePart>> {
    let mut parts: BTreeMap<String, BTreeSet<RolePart>> = BTreeMap::new();
    for record in records {
        match record.seam {
            Seam::Egress => {
                parts
                    .entry(LOCAL_ACTOR.to_string())
                    .or_default()
                    .insert(RolePart::Client);
                if let Some(host) = record.host.as_deref() {
                    parts
                        .entry(authority(host, record.port))
                        .or_default()
                        .insert(RolePart::Server);
                }
            }
            Seam::Inbound => {
                parts
                    .entry(LOCAL_ACTOR.to_string())
                    .or_default()
                    .insert(RolePart::Server);
                parts
                    .entry(INBOUND_CLIENT_ACTOR.to_string())
                    .or_default()
                    .insert(RolePart::Client);
            }
        }
    }
    parts
}

/// The deterministic role id for `actor` acting as `part` (e.g. `client:local`,
/// `server:api.example:443`).
fn role_id(part: RolePart, actor_id: &str) -> String {
    format!("{}:{actor_id}", part_str(part))
}

/// The lowercase token for a [`RolePart`] (matching its serialized form).
fn part_str(part: RolePart) -> &'static str {
    match part {
        RolePart::Client => "client",
        RolePart::Server => "server",
    }
}

/// A human-readable provenance basis for the role of `actor` acting as `part`.
fn role_basis(part: RolePart, actor_id: &str) -> &'static str {
    match part {
        RolePart::Server if actor_id == LOCAL_ACTOR => {
            "the observed process serving inbound requests"
        }
        RolePart::Client if actor_id == LOCAL_ACTOR => "the observed process making egress calls",
        RolePart::Client if actor_id == INBOUND_CLIENT_ACTOR => {
            "the anonymous inbound caller population"
        }
        RolePart::Server => "an egress dependency",
        RolePart::Client => "a client participant",
    }
}

/// Derive the role→role [`Channel`]s carrying the observed interactions. Egress
/// records yield a `client:local → server:{authority}` edge; inbound records yield
/// a `client:inbound-client → server:local` edge. A channel's `protocol` is the
/// observed scheme when uniform, and is left unset when schemes are mixed.
fn derive_channels(records: &[JournalRecord], environment: &mut Environment) {
    let mut schemes: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
    for record in records {
        let (from, to) = match record.seam {
            Seam::Egress => {
                let Some(host) = record.host.as_deref() else {
                    continue;
                };
                (
                    role_id(RolePart::Client, LOCAL_ACTOR),
                    role_id(RolePart::Server, &authority(host, record.port)),
                )
            }
            Seam::Inbound => (
                role_id(RolePart::Client, INBOUND_CLIENT_ACTOR),
                role_id(RolePart::Server, LOCAL_ACTOR),
            ),
        };
        let id = channel_id(&from, &to);
        let channel = environment
            .channels
            .entry(id.clone())
            .or_insert_with(|| Channel {
                from,
                to,
                provenance: Provenance::derived(channel_basis(record.seam)),
                ..Default::default()
            });
        accumulate(&mut channel.observed, record.timestamp_ms);
        if let Some(scheme) = record.scheme.as_deref() {
            schemes.entry(id).or_default().insert(scheme.to_string());
        }
    }

    for (id, scheme_set) in schemes {
        if scheme_set.len() != 1 {
            continue;
        }
        if let Some(channel) = environment.channels.get_mut(&id) {
            channel.protocol = scheme_set.into_iter().next();
        }
    }
}

/// The deterministic channel id for an edge from role `from` to role `to`.
fn channel_id(from: &str, to: &str) -> String {
    format!("{from}->{to}")
}

/// A human-readable provenance basis for a channel observed on `seam`.
fn channel_basis(seam: Seam) -> &'static str {
    match seam {
        Seam::Egress => "observed egress traffic to a dependency",
        Seam::Inbound => "observed inbound traffic from callers",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Build an egress record to `scheme://host:port` captured at `ts`.
    fn egress(scheme: &str, host: &str, port: u16, ts: u64) -> JournalRecord {
        JournalRecord {
            seam: Seam::Egress,
            method: "GET".to_string(),
            scheme: Some(scheme.to_string()),
            host: Some(host.to_string()),
            port: Some(port),
            path: "/x".to_string(),
            status: 200,
            timestamp_ms: ts,
            ..Default::default()
        }
    }

    /// Build an inbound record captured at `ts`.
    fn inbound(ts: u64) -> JournalRecord {
        JournalRecord {
            seam: Seam::Inbound,
            method: "POST".to_string(),
            path: "/y".to_string(),
            status: 200,
            timestamp_ms: ts,
            ..Default::default()
        }
    }

    #[test]
    fn empty_journal_has_no_actors() {
        assert!(derive_actors(&[]).is_empty());
    }

    #[test]
    fn single_egress_yields_local_and_remote() {
        let actors = derive_actors(&[egress("https", "api.example", 443, 1000)]);
        assert_eq!(actors.len(), 2);
        assert!(actors.contains_key(LOCAL_ACTOR));
        let remote = &actors["api.example:443"];
        assert_eq!(remote.bindings.len(), 1);
        let binding = &remote.bindings[0];
        assert_eq!(binding.scheme.as_deref(), Some("https"));
        assert_eq!(binding.host.as_deref(), Some("api.example"));
        assert_eq!(binding.port, Some(443));
        assert_eq!(binding.observed.interactions, 1);
    }

    #[test]
    fn repeated_egress_to_one_authority_sums_on_the_binding() {
        let actors = derive_actors(&[
            egress("https", "api.example", 443, 1000),
            egress("https", "api.example", 443, 2000),
            egress("https", "api.example", 443, 1500),
        ]);
        let remote = &actors["api.example:443"];
        assert_eq!(remote.bindings.len(), 1);
        assert_eq!(remote.bindings[0].observed.interactions, 3);
        assert_eq!(remote.observed.interactions, 3);
        assert_eq!(remote.bindings[0].observed.first_ms, Some(1000));
        assert_eq!(remote.bindings[0].observed.last_ms, Some(2000));
    }

    #[test]
    fn distinct_authorities_yield_distinct_actors() {
        let actors = derive_actors(&[
            egress("https", "a.example", 443, 1),
            egress("http", "b.example", 8080, 2),
        ]);
        assert_eq!(actors.len(), 3); // local + 2 remotes
        assert!(actors.contains_key("a.example:443"));
        assert!(actors.contains_key("b.example:8080"));
    }

    #[test]
    fn same_host_different_ports_are_distinct_actors() {
        let actors = derive_actors(&[
            egress("http", "h.example", 80, 1),
            egress("https", "h.example", 443, 2),
        ]);
        assert!(actors.contains_key("h.example:80"));
        assert!(actors.contains_key("h.example:443"));
    }

    #[test]
    fn same_authority_different_schemes_share_an_actor_with_two_bindings() {
        let actors = derive_actors(&[
            egress("https", "h.example", 443, 1),
            egress("http", "h.example", 443, 2),
        ]);
        let remote = &actors["h.example:443"];
        assert_eq!(remote.bindings.len(), 2);
        // The actor aggregate is the sum of its per-scheme bindings.
        assert_eq!(remote.observed.interactions, 2);
    }

    #[test]
    fn hostless_egress_counts_local_only() {
        let mut record = egress("https", "ignored", 443, 5);
        record.host = None;
        record.port = None;
        record.scheme = None;
        let actors = derive_actors(&[record]);
        assert_eq!(actors.len(), 1);
        assert_eq!(actors[LOCAL_ACTOR].observed.interactions, 1);
    }

    #[test]
    fn egress_without_port_keys_by_host_only() {
        let mut record = egress("https", "noport.example", 0, 1);
        record.port = None;
        let actors = derive_actors(&[record]);
        assert!(actors.contains_key("noport.example"));
        assert_eq!(actors["noport.example"].bindings[0].port, None);
    }

    #[test]
    fn single_inbound_yields_local_and_anonymous_client() {
        let actors = derive_actors(&[inbound(1000)]);
        assert_eq!(actors.len(), 2);
        assert!(actors.contains_key(LOCAL_ACTOR));
        let client = &actors[INBOUND_CLIENT_ACTOR];
        assert!(client.bindings.is_empty());
        assert_eq!(client.observed.interactions, 1);
    }

    #[test]
    fn mixed_journal_local_aggregates_every_record() {
        let actors = derive_actors(&[
            egress("https", "api.example", 443, 1000),
            inbound(2000),
            egress("https", "api.example", 443, 3000),
        ]);
        assert_eq!(actors.len(), 3); // local + remote + inbound-client
        assert_eq!(actors[LOCAL_ACTOR].observed.interactions, 3);
        assert_eq!(actors["api.example:443"].observed.interactions, 2);
        assert_eq!(actors[INBOUND_CLIENT_ACTOR].observed.interactions, 1);
    }

    #[test]
    fn observed_window_is_min_and_max() {
        let actors = derive_actors(&[inbound(3000), inbound(1000), inbound(2000)]);
        let local = &actors[LOCAL_ACTOR].observed;
        assert_eq!(local.interactions, 3);
        assert_eq!(local.first_ms, Some(1000));
        assert_eq!(local.last_ms, Some(3000));
    }

    #[test]
    fn zero_timestamp_does_not_set_window() {
        let actors = derive_actors(&[inbound(0)]);
        let local = &actors[LOCAL_ACTOR].observed;
        assert_eq!(local.interactions, 1);
        assert_eq!(local.first_ms, None);
        assert_eq!(local.last_ms, None);
    }

    #[test]
    fn window_ignores_zero_timestamps_among_real_ones() {
        let actors = derive_actors(&[inbound(0), inbound(5000), inbound(0)]);
        let local = &actors[LOCAL_ACTOR].observed;
        assert_eq!(local.interactions, 3);
        assert_eq!(local.first_ms, Some(5000));
        assert_eq!(local.last_ms, Some(5000));
    }

    #[test]
    fn actors_carry_their_derived_provenance() {
        let actors = derive_actors(&[egress("https", "api.example", 443, 1), inbound(2)]);
        assert_eq!(
            actors[LOCAL_ACTOR].provenance,
            Provenance::derived("the observed process (egress client / inbound server)")
        );
        assert_eq!(
            actors["api.example:443"].provenance,
            Provenance::derived("egress destination")
        );
        assert_eq!(
            actors[INBOUND_CLIENT_ACTOR].provenance,
            Provenance::derived("inbound caller seam (anonymous)")
        );
    }
}

