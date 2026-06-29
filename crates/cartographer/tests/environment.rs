// Copyright (c) Microsoft Corporation.

//! End-to-end integration test for the observed-environment descriptor
//! (D-CART-4, EM-E4): derive it from a wordy-like journal, round-trip it, confirm
//! the contract link and behavioral subdivision, and confirm that re-synthesis
//! preserves a hand-`asserted` edit.

use api_journal::{HeaderField, JournalRecord, Seam};
use cartographer::{
    Provenance, ProvenanceTier, SpecFormat, derive_environment, merge_environment,
    parse_environment, serialize_environment,
};

/// The wordy identity headers, captured by name only (values are never recorded).
fn identity_headers() -> Vec<HeaderField> {
    vec![
        HeaderField {
            name: "X-Wordy-User".into(),
            value: None,
        },
        HeaderField {
            name: "X-Wordy-Locale".into(),
            value: None,
        },
    ]
}

/// A representative inbound journal covering several `wordy` operations. The two
/// `/custom/{cat,dog}` reads exercise template grouping; the `DELETE` exercises a
/// second method on the same template.
fn wordy_journal() -> Vec<JournalRecord> {
    [
        ("GET", "/healthz"),
        ("POST", "/spellcheck"),
        ("POST", "/anagram"),
        ("GET", "/custom/cat"),
        ("GET", "/custom/dog"),
        ("DELETE", "/custom/cat"),
    ]
    .into_iter()
    .enumerate()
    .map(|(seq, (method, path))| JournalRecord {
        seam: Seam::Inbound,
        method: method.into(),
        path: path.into(),
        request_headers: identity_headers(),
        status: 200,
        timestamp_ms: 1_700_000_000_000 + seq as u64,
        seq: seq as u64,
        ..Default::default()
    })
    .collect()
}

#[test]
fn wordy_environment_round_trips_links_the_contract_and_subdivides() {
    let env = derive_environment(&wordy_journal(), Some("wordy-openapi.yaml"));

    // Round-trips through both formats.
    for format in [SpecFormat::Yaml, SpecFormat::Json] {
        let text = serialize_environment(&env, format).expect("serialize");
        assert_eq!(parse_environment(&text, format).expect("parse"), env);
    }

    // The inbound channel references the synthesized OpenAPI contract.
    let channel = &env.channels["client:inbound-client->server:local"];
    assert_eq!(
        channel.contract.as_ref().map(|c| c.reference.as_str()),
        Some("wordy-openapi.yaml")
    );

    // Behavioral subdivision: the caller role gains a child per operation, with the
    // two literal `/custom/{cat,dog}` GETs grouped under one `/custom/{id}` template.
    let caller = &env.roles["client:inbound-client"];
    assert!(
        caller
            .children
            .contains(&"client:inbound-client#POST/spellcheck".into())
    );
    assert!(
        caller
            .children
            .contains(&"client:inbound-client#GET/custom/{id}".into())
    );
    assert!(
        caller
            .children
            .contains(&"client:inbound-client#DELETE/custom/{id}".into())
    );
    let get_custom_children = caller
        .children
        .iter()
        .filter(|child| child.ends_with("GET/custom/{id}"))
        .count();
    assert_eq!(get_custom_children, 1, "GET /custom/cat and /custom/dog merge");

    // Identity headers are captured by name only on the server role's `requires`.
    let requires = env.roles["server:local"]
        .requires
        .as_ref()
        .expect("requires");
    assert!(requires.names.contains(&"X-Wordy-User".into()));
    assert!(requires.names.contains(&"X-Wordy-Locale".into()));
}

#[test]
fn re_synthesis_preserves_a_hand_asserted_role_edit() {
    // First derivation, then a human gives the server role a meaningful title and
    // asserts it (a "rename" via the human-facing title; the stable id is unchanged).
    let mut curated = derive_environment(&wordy_journal(), Some("wordy-openapi.yaml"));
    let role = curated.roles.get_mut("server:local").expect("role");
    role.title = Some("Wordy public dictionary API".into());
    role.provenance = Provenance::asserted();

    // A later capture re-derives and merges over the curated descriptor.
    let fresh = derive_environment(&wordy_journal(), Some("wordy-openapi.yaml"));
    let merged = merge_environment(fresh, &curated);

    // The human edit survives.
    let preserved = &merged.roles["server:local"];
    assert_eq!(
        preserved.title.as_deref(),
        Some("Wordy public dictionary API")
    );
    assert_eq!(preserved.provenance.tier, ProvenanceTier::Asserted);

    // Derived structure is still refreshed (the caller role keeps its children).
    assert!(
        !merged.roles["client:inbound-client"]
            .children
            .is_empty()
    );
}
