// Copyright (c) Microsoft Corporation.

//! The [`JournalRecord`] schema (AJ-A3).
//!
//! One record describes one observed HTTP request/response interaction at one of the
//! shim's capture seams. Records are appended to a journal as NDJSON (one compact JSON
//! object per line — see the [`ndjson`](crate::ndjson) module) and later read off-machine
//! by `cartographer`.
//!
//! Bodies are reduced to [`BodyShape`]s (shapes-only by default). Surrounding metadata
//! follows the policy in `DESIGN-NOTES.md` (D-AJ-2): paths are literal; query parameters
//! keep names + value *shapes*; headers keep names, with literal values only for the
//! content-negotiation safelist (`Content-Type`, `Accept`).
//!
//! Readers ignore unknown fields, so a newer shim may add fields without breaking an older
//! reader.

use serde::{Deserialize, Serialize};

use crate::shape::BodyShape;

/// Which seam observed the interaction.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Seam {
    /// An inbound request served by the host (the service's own exposed API).
    #[default]
    Inbound,
    /// An outbound request the host made to another service (WinHTTP egress).
    Egress,
}

/// One observed query parameter: its name and the inferred *shape* of its value (not the
/// literal value).
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct QueryParam {
    /// The parameter name.
    pub name: String,
    /// The inferred scalar shape of the observed value (`String`/`Integer`/`Number`/`Bool`).
    pub value: BodyShape,
}

/// One observed header. The `value` is retained only for the content-negotiation safelist
/// (`Content-Type`, `Accept`); for every other header just the name is kept.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct HeaderField {
    /// The header name (case is preserved as observed; comparisons are case-insensitive).
    pub name: String,
    /// The literal value, present only for safelisted content-negotiation headers.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub value: Option<String>,
}

/// A single observed request/response interaction.
///
/// Construct with a struct literal using [`Default`] for the unset fields, e.g.
/// `JournalRecord { seam: Seam::Egress, method: "GET".into(), ..Default::default() }`.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct JournalRecord {
    /// Which seam observed the interaction.
    pub seam: Seam,
    /// The HTTP method/verb (e.g. `GET`, `POST`).
    pub method: String,

    /// Destination scheme (`http`/`https`) — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub scheme: Option<String>,
    /// Destination host (no scheme or port) — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host: Option<String>,
    /// Destination TCP port — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub port: Option<u16>,

    /// The request path, literal and without the query string (e.g. `/custom/{word}` is an
    /// inferred template, but here the *observed* concrete path such as `/custom/cat`).
    pub path: String,
    /// Observed query parameters (names + value shapes).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub query: Vec<QueryParam>,

    /// Observed request headers (names; values only for the safelist).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub request_headers: Vec<HeaderField>,
    /// The shape of the request body.
    #[serde(default, skip_serializing_if = "is_empty_shape")]
    pub request_body: BodyShape,

    /// The HTTP response status code.
    pub status: u16,
    /// Observed response headers (names; values only for the safelist).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub response_headers: Vec<HeaderField>,
    /// The shape of the response body.
    #[serde(default, skip_serializing_if = "is_empty_shape")]
    pub response_body: BodyShape,

    /// Best-effort capture time, milliseconds since the Unix epoch (0 if unavailable).
    pub timestamp_ms: u64,
    /// Per-process journaling-session id, so records from different processes/machines can
    /// be grouped and de-duplicated after gathering.
    pub session_id: u64,
    /// Monotonic per-session sequence number.
    pub seq: u64,
}

impl JournalRecord {
    /// Find a header's retained value by case-insensitive name, if any was captured.
    #[must_use]
    pub fn request_content_type(&self) -> Option<&str> {
        header_value(&self.request_headers, "content-type")
    }

    /// Find the response `Content-Type` value, if one was captured.
    #[must_use]
    pub fn response_content_type(&self) -> Option<&str> {
        header_value(&self.response_headers, "content-type")
    }
}

/// Infer the scalar [`BodyShape`] of a query/parameter value string.
///
/// HTTP carries all values as text; this classifies the *apparent* type so cartographer can
/// synthesize a parameter schema: integers → `Integer`, decimals → `Number`,
/// `true`/`false` → `Bool`, everything else (including empty) → `String`.
#[must_use]
pub fn infer_scalar(value: &str) -> BodyShape {
    if value.parse::<i64>().is_ok() || value.parse::<u64>().is_ok() {
        BodyShape::Integer
    } else if value.parse::<f64>().is_ok() {
        BodyShape::Number
    } else if value == "true" || value == "false" {
        BodyShape::Bool
    } else {
        BodyShape::String
    }
}

fn header_value<'a>(headers: &'a [HeaderField], name_lower: &str) -> Option<&'a str> {
    headers
        .iter()
        .find(|h| h.name.eq_ignore_ascii_case(name_lower))
        .and_then(|h| h.value.as_deref())
}

fn is_empty_shape(shape: &BodyShape) -> bool {
    matches!(shape, BodyShape::Empty)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    fn sample_object() -> BodyShape {
        let mut fields = BTreeMap::new();
        fields.insert(
            "word".to_string(),
            crate::shape::Field {
                shape: BodyShape::String,
                required: true,
            },
        );
        BodyShape::Object(fields)
    }

    #[test]
    fn egress_record_round_trips() {
        let record = JournalRecord {
            seam: Seam::Egress,
            method: "POST".into(),
            scheme: Some("http".into()),
            host: Some("merriam.local".into()),
            port: Some(8080),
            path: "/custom/cat".into(),
            query: vec![QueryParam {
                name: "pattern".into(),
                value: BodyShape::String,
            }],
            request_headers: vec![HeaderField {
                name: "X-Wordy-User".into(),
                value: None,
            }],
            request_body: BodyShape::Empty,
            status: 200,
            response_headers: vec![HeaderField {
                name: "Content-Type".into(),
                value: Some("application/json".into()),
            }],
            response_body: sample_object(),
            timestamp_ms: 1_700_000_000_000,
            session_id: 42,
            seq: 7,
        };
        let json = serde_json::to_string(&record).expect("serialize");
        let back: JournalRecord = serde_json::from_str(&json).expect("deserialize");
        assert_eq!(record, back);
    }

    #[test]
    fn inbound_record_round_trips() {
        let record = JournalRecord {
            seam: Seam::Inbound,
            method: "GET".into(),
            path: "/healthz".into(),
            status: 200,
            response_headers: vec![HeaderField {
                name: "Content-Type".into(),
                value: Some("application/json".into()),
            }],
            response_body: sample_object(),
            timestamp_ms: 1,
            session_id: 2,
            seq: 3,
            ..Default::default()
        };
        let json = serde_json::to_string(&record).expect("serialize");
        let back: JournalRecord = serde_json::from_str(&json).expect("deserialize");
        assert_eq!(record, back);
    }

    #[test]
    fn unknown_fields_are_ignored() {
        let json = r#"{
            "seam": "egress",
            "method": "GET",
            "path": "/healthz",
            "status": 200,
            "timestamp_ms": 0,
            "session_id": 0,
            "seq": 0,
            "future_field": {"nested": [1,2,3]},
            "another_unknown": "ok"
        }"#;
        let record: JournalRecord = serde_json::from_str(json).expect("tolerant deserialize");
        assert_eq!(record.seam, Seam::Egress);
        assert_eq!(record.method, "GET");
        assert_eq!(record.path, "/healthz");
    }

    #[test]
    fn empty_optionals_are_omitted_from_json() {
        let record = JournalRecord {
            seam: Seam::Inbound,
            method: "GET".into(),
            path: "/healthz".into(),
            status: 204,
            timestamp_ms: 0,
            session_id: 0,
            seq: 0,
            ..Default::default()
        };
        let json = serde_json::to_string(&record).expect("serialize");
        // None scheme/host/port, empty vecs, and Empty bodies must not appear.
        assert!(!json.contains("scheme"), "{json}");
        assert!(!json.contains("host"), "{json}");
        assert!(!json.contains("port"), "{json}");
        assert!(!json.contains("query"), "{json}");
        assert!(!json.contains("request_headers"), "{json}");
        assert!(!json.contains("request_body"), "{json}");
        assert!(!json.contains("response_body"), "{json}");
        // Always-present fields must appear.
        assert!(json.contains("\"status\":204"), "{json}");
        assert!(json.contains("\"seam\":\"inbound\""), "{json}");
    }

    #[test]
    fn content_type_accessors() {
        let record = JournalRecord {
            request_headers: vec![HeaderField {
                name: "content-type".into(),
                value: Some("application/json; charset=utf-8".into()),
            }],
            response_headers: vec![HeaderField {
                name: "Content-Type".into(),
                value: Some("application/json".into()),
            }],
            ..Default::default()
        };
        assert_eq!(
            record.request_content_type(),
            Some("application/json; charset=utf-8")
        );
        assert_eq!(record.response_content_type(), Some("application/json"));
    }

    #[test]
    fn infer_scalar_classifies() {
        assert_eq!(infer_scalar("42"), BodyShape::Integer);
        assert_eq!(infer_scalar("-7"), BodyShape::Integer);
        assert_eq!(infer_scalar("3.14"), BodyShape::Number);
        assert_eq!(infer_scalar("true"), BodyShape::Bool);
        assert_eq!(infer_scalar("false"), BodyShape::Bool);
        assert_eq!(infer_scalar("cat"), BodyShape::String);
        assert_eq!(infer_scalar(""), BodyShape::String);
    }

    #[test]
    fn seam_serializes_snake_case() {
        assert_eq!(serde_json::to_string(&Seam::Inbound).unwrap(), "\"inbound\"");
        assert_eq!(serde_json::to_string(&Seam::Egress).unwrap(), "\"egress\"");
    }
}
