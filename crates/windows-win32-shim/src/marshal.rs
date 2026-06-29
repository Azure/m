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
//! anywhere. Bodies are owned byte vectors; in the marshaled JSON they are encoded
//! as base64 strings (BC-2) — ~3x more compact than a JSON number array and the
//! standard binary-in-JSON form for the eventual cross-process payload.

use base64::engine::general_purpose::STANDARD as BASE64;
use serde::{Deserialize, Serialize};

use api_journal::{RawStr, Seam};

/// serde `with` adapter encoding a `Vec<u8>` body as a base64 string (RFC 4648
/// standard alphabet) in the marshaled JSON and decoding it back. Empty bodies are
/// elided by the field's `skip_serializing_if`, so this only runs for non-empty
/// payloads. A malformed base64 string is a deserialization error.
mod base64_body {
    use super::BASE64;
    use base64::Engine as _;
    use serde::{Deserialize, Deserializer, Serializer};

    pub(super) fn serialize<S: Serializer>(bytes: &[u8], serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(&BASE64.encode(bytes))
    }

    pub(super) fn deserialize<'de, D: Deserializer<'de>>(
        deserializer: D,
    ) -> Result<Vec<u8>, D::Error> {
        let text = String::deserialize(deserializer)?;
        BASE64
            .decode(text.as_bytes())
            .map_err(serde::de::Error::custom)
    }
}

/// One raw header `name`/`value` pair, exactly as observed on the wire (no
/// safelist filtering — that is the worker's job).
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct RawHeader {
    /// The header name (raw, encoding-tagged; never transcoded by the producer).
    pub name: RawStr,
    /// The literal header value (raw, encoding-tagged).
    pub value: RawStr,
}

/// A raw intercepted request/response interaction, marshaled for the off-thread
/// (eventually out-of-process) worker. Position-independent: it owns all its data
/// and serializes to JSON.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Interaction {
    /// Which seam observed the interaction.
    pub seam: Seam,
    /// The HTTP method/verb.
    pub method: RawStr,
    /// Destination scheme (`http`/`https`) — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub scheme: Option<RawStr>,
    /// Destination host — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host: Option<RawStr>,
    /// Destination port — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub port: Option<u16>,
    /// The raw request target (path plus any `?query`), unsplit.
    pub target: RawStr,
    /// Raw request headers (names and literal values).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub request_headers: Vec<RawHeader>,
    /// Raw request body bytes (base64-encoded in the marshaled JSON).
    #[serde(default, with = "base64_body", skip_serializing_if = "Vec::is_empty")]
    pub request_body: Vec<u8>,
    /// The HTTP response status code.
    pub status: u16,
    /// Raw response headers (names and literal values).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub response_headers: Vec<RawHeader>,
    /// Raw response body bytes (base64-encoded in the marshaled JSON).
    #[serde(default, with = "base64_body", skip_serializing_if = "Vec::is_empty")]
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
    use base64::Engine as _;

    fn header(name: &str, value: &str) -> RawHeader {
        RawHeader {
            name: name.into(),
            value: value.into(),
        }
    }

    fn egress_sample() -> Interaction {
        Interaction {
            seam: Seam::Egress,
            method: "POST".into(),
            scheme: Some("https".into()),
            host: Some("api.example".into()),
            port: Some(443),
            target: "/custom/cat?pattern=c.t".into(),
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
            method: "GET".into(),
            target: "/healthz".into(),
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
            method: "DELETE".into(),
            target: "/x".into(),
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
    fn bodies_are_base64_strings_not_number_arrays() {
        let interaction = egress_sample();
        let json = interaction.to_json().unwrap();
        // The body is a base64 JSON string, not a `[123,34,...]` number array.
        let expected = BASE64.encode(br#"{"words":["cat"]}"#);
        assert!(
            json.contains(&format!("\"request_body\":\"{expected}\"")),
            "expected base64 body in {json}"
        );
        assert!(!json.contains("request_body\":["), "body must not be a number array");
    }

    #[test]
    fn non_utf8_bytes_survive_the_base64_encoding() {
        let interaction = Interaction {
            seam: Seam::Inbound,
            method: "POST".into(),
            target: "/bin".into(),
            request_body: vec![0x00, 0xFF, 0x80, 0x7F],
            status: 200,
            timestamp_ms: 1,
            ..Default::default()
        };
        let back = Interaction::from_json(&interaction.to_json().unwrap()).unwrap();
        assert_eq!(back.request_body, vec![0x00, 0xFF, 0x80, 0x7F]);
    }

    #[test]
    fn malformed_base64_body_is_a_parse_error() {
        // A body field that is not valid base64 must fail to deserialize rather than
        // silently yield garbage.
        let json = r#"{"seam":"inbound","method":"POST","target":"/x","request_body":"!!!not base64!!!","status":200,"timestamp_ms":1}"#;
        assert!(Interaction::from_json(json).is_err());
    }
}
