// Copyright (c) Microsoft Corporation.

//! The position-independent marshaled-interaction format (SHIM-D25, OT-2).
//!
//! When the shim intercepts a web API call, it marshals the **raw** intercepted
//! context into an [`Interaction`] — a plain-data value that serializes to JSON and
//! could move across a process boundary unchanged. The off-thread worker consumes
//! it and replies with an [`Outcome`]. No reduction has happened yet: bodies are
//! raw bytes and headers keep their literal values. Reducing to the shapes-only,
//! safelisted on-disk record is the *worker's* job (so no state or policy lives on
//! the calling thread), which is why the raw context is what crosses the seam.
//!
//! This module is pure data — no `unsafe`, no OS dependency — so it is unit-testable
//! anywhere. Bodies are carried as byte vectors (serialized as JSON arrays);
//! base64 compaction of the payload is a deferred refinement.

use serde::{Deserialize, Serialize};

use api_journal::Seam;

/// One raw header `name`/`value` pair, exactly as observed on the wire (no
/// safelist filtering — that is the worker's job).
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct RawHeader {
    /// The header name.
    pub name: String,
    /// The literal header value.
    pub value: String,
}

/// A raw intercepted request/response interaction, marshaled for the off-thread
/// (eventually out-of-process) worker. Position-independent: it owns all its data
/// and serializes to JSON.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Interaction {
    /// Which seam observed the interaction.
    pub seam: Seam,
    /// The HTTP method/verb.
    pub method: String,
    /// Destination scheme (`http`/`https`) — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub scheme: Option<String>,
    /// Destination host — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host: Option<String>,
    /// Destination port — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub port: Option<u16>,
    /// The raw request target (path plus any `?query`), unsplit.
    pub target: String,
    /// Raw request headers (names and literal values).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub request_headers: Vec<RawHeader>,
    /// Raw request body bytes.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub request_body: Vec<u8>,
    /// The HTTP response status code.
    pub status: u16,
    /// Raw response headers (names and literal values).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub response_headers: Vec<RawHeader>,
    /// Raw response body bytes.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub response_body: Vec<u8>,
    /// Best-effort capture time (ms since the Unix epoch) sampled at interception,
    /// so the record reflects when the interaction happened, not when the worker ran.
    pub timestamp_ms: u64,
}

impl Interaction {
    /// Serialize to a JSON request line.
    ///
    /// # Errors
    /// Returns the `serde_json` error if serialization fails (it should not for
    /// well-formed data).
    pub fn to_json(&self) -> Result<String, serde_json::Error> {
        serde_json::to_string(self)
    }

    /// Parse from a JSON request line.
    ///
    /// # Errors
    /// Returns the `serde_json` error if `text` is not a valid interaction.
    pub fn from_json(text: &str) -> Result<Self, serde_json::Error> {
        serde_json::from_str(text)
    }
}

/// The worker's reply to a marshaled [`Interaction`]. Today an ack noting whether a
/// record was journaled; a later stage may carry a modified response (redirect /
/// replay / fault).
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Outcome {
    /// Whether the worker wrote a journal record for the interaction.
    pub journaled: bool,
}

impl Outcome {
    /// Serialize to a JSON reply line.
    ///
    /// # Errors
    /// Returns the `serde_json` error if serialization fails.
    pub fn to_json(&self) -> Result<String, serde_json::Error> {
        serde_json::to_string(self)
    }

    /// Parse from a JSON reply line.
    ///
    /// # Errors
    /// Returns the `serde_json` error if `text` is not a valid outcome.
    pub fn from_json(text: &str) -> Result<Self, serde_json::Error> {
        serde_json::from_str(text)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn header(name: &str, value: &str) -> RawHeader {
        RawHeader {
            name: name.to_string(),
            value: value.to_string(),
        }
    }

    fn egress_sample() -> Interaction {
        Interaction {
            seam: Seam::Egress,
            method: "POST".to_string(),
            scheme: Some("https".to_string()),
            host: Some("api.example".to_string()),
            port: Some(443),
            target: "/custom/cat?pattern=c.t".to_string(),
            request_headers: vec![
                header("Content-Type", "application/json"),
                header("X-Wordy-User", "alice"),
            ],
            request_body: br#"{"words":["cat"]}"#.to_vec(),
            status: 200,
            response_headers: vec![header("Content-Type", "application/json")],
            response_body: br#"{"ok":true}"#.to_vec(),
            timestamp_ms: 1_700_000_000_000,
        }
    }

    #[test]
    fn egress_interaction_round_trips() {
        let interaction = egress_sample();
        let json = interaction.to_json().expect("serialize");
        assert_eq!(Interaction::from_json(&json).expect("parse"), interaction);
    }

    #[test]
    fn inbound_interaction_round_trips_without_authority() {
        let interaction = Interaction {
            seam: Seam::Inbound,
            method: "GET".to_string(),
            target: "/healthz".to_string(),
            status: 200,
            timestamp_ms: 42,
            ..Default::default()
        };
        let json = interaction.to_json().expect("serialize");
        let back = Interaction::from_json(&json).expect("parse");
        assert_eq!(back, interaction);
        assert!(back.scheme.is_none() && back.host.is_none() && back.port.is_none());
    }

    #[test]
    fn raw_bodies_and_header_values_survive() {
        let interaction = egress_sample();
        let back = Interaction::from_json(&interaction.to_json().unwrap()).unwrap();
        // The raw body and literal header values are carried verbatim (no reduction).
        assert_eq!(back.request_body, br#"{"words":["cat"]}"#.to_vec());
        assert_eq!(back.request_headers[1].value, "alice");
    }

    #[test]
    fn empty_optional_fields_are_omitted_but_round_trip() {
        let interaction = Interaction {
            seam: Seam::Inbound,
            method: "DELETE".to_string(),
            target: "/x".to_string(),
            status: 204,
            timestamp_ms: 1,
            ..Default::default()
        };
        let json = interaction.to_json().unwrap();
        assert!(!json.contains("request_headers"));
        assert!(!json.contains("request_body"));
        assert_eq!(Interaction::from_json(&json).unwrap(), interaction);
    }

    #[test]
    fn outcome_round_trips() {
        for journaled in [true, false] {
            let outcome = Outcome { journaled };
            assert_eq!(
                Outcome::from_json(&outcome.to_json().unwrap()).unwrap(),
                outcome
            );
        }
    }

    #[test]
    fn non_utf8_bytes_survive_the_byte_array_encoding() {
        let interaction = Interaction {
            seam: Seam::Inbound,
            method: "POST".to_string(),
            target: "/bin".to_string(),
            request_body: vec![0x00, 0xFF, 0x80, 0x7F],
            status: 200,
            timestamp_ms: 1,
            ..Default::default()
        };
        let back = Interaction::from_json(&interaction.to_json().unwrap()).unwrap();
        assert_eq!(back.request_body, vec![0x00, 0xFF, 0x80, 0x7F]);
    }
}
