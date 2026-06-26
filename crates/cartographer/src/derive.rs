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

use api_journal::{JournalRecord, Seam};

use crate::environment::{Actor, Actors, Binding, Provenance};

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
/// [`LOCAL_ACTOR`]. Observed evidence (counts, timestamps) is attached in EM-B3.
#[must_use]
pub fn derive_actors(records: &[JournalRecord]) -> Actors {
    let mut actors = Actors::new();
    let mut saw_local = false;

    for record in records {
        match record.seam {
            Seam::Egress => {
                saw_local = true;
                let Some(host) = record.host.as_deref() else {
                    continue;
                };
                let actor = actors
                    .entry(authority(host, record.port))
                    .or_insert_with(|| Actor {
                        provenance: Provenance::derived("egress destination"),
                        ..Default::default()
                    });
                ensure_binding(actor, record.scheme.as_deref(), host, record.port);
            }
            Seam::Inbound => {
                saw_local = true;
                ensure_inbound_client_actor(&mut actors);
            }
        }
    }

    if saw_local {
        ensure_local_actor(&mut actors);
    }
    actors
}

/// Ensure `actor` carries a [`Binding`] for `(scheme, host, port)`, adding one if
/// absent. Observed evidence is attached later (EM-B3).
fn ensure_binding(actor: &mut Actor, scheme: Option<&str>, host: &str, port: Option<u16>) {
    let present = actor.bindings.iter().any(|binding| {
        binding.scheme.as_deref() == scheme
            && binding.host.as_deref() == Some(host)
            && binding.port == port
    });
    if !present {
        actor.bindings.push(Binding {
            scheme: scheme.map(str::to_string),
            host: Some(host.to_string()),
            port,
            ..Default::default()
        });
    }
}

/// Ensure the single local-process actor exists.
fn ensure_local_actor(actors: &mut Actors) {
    actors
        .entry(LOCAL_ACTOR.to_string())
        .or_insert_with(|| Actor {
            title: Some("the observed process".to_string()),
            provenance: Provenance::derived("the observed process (egress client / inbound server)"),
            ..Default::default()
        });
}

/// Ensure the single anonymous inbound-caller actor exists.
fn ensure_inbound_client_actor(actors: &mut Actors) {
    actors
        .entry(INBOUND_CLIENT_ACTOR.to_string())
        .or_insert_with(|| Actor {
            title: Some("anonymous inbound callers".to_string()),
            provenance: Provenance::derived("inbound caller seam (anonymous)"),
            ..Default::default()
        });
}
