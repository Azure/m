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

use crate::rawstr::RawStr;
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
    /// The parameter name (raw, encoding-tagged; never transcoded by the producer).
    pub name: RawStr,
    /// The inferred scalar shape of the observed value (`String`/`Integer`/`Number`/`Bool`).
    pub value: BodyShape,
}

/// One observed header. The `value` is retained only for the content-negotiation safelist
/// (`Content-Type`, `Accept`); for every other header just the name is kept.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct HeaderField {
    /// The header name, raw and encoding-tagged (case preserved; compare via decoded form).
    pub name: RawStr,
    /// The literal value, present only for safelisted content-negotiation headers.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub value: Option<RawStr>,
}

/// A single observed request/response interaction.
///
/// Construct with a struct literal using [`Default`] for the unset fields, e.g.
/// `JournalRecord { seam: Seam::Egress, method: "GET".into(), ..Default::default() }`.
///
/// Not `Eq`: the optional body-example fields hold arbitrary JSON (which is not `Eq`).
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct JournalRecord {
    /// Which seam observed the interaction.
    pub seam: Seam,
    /// The HTTP method/verb (e.g. `GET`, `POST`).
    pub method: RawStr,

    /// Destination scheme (`http`/`https`) — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub scheme: Option<RawStr>,
    /// Destination host (no scheme or port) — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host: Option<RawStr>,
    /// Destination TCP port — egress only.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub port: Option<u16>,

    /// The request path, literal and without the query string (e.g. `/custom/{word}` is an
    /// inferred template, but here the *observed* concrete path such as `/custom/cat`).
    pub path: RawStr,
    /// Observed query parameters (names + value shapes).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub query: Vec<QueryParam>,

    /// Observed request headers (names; values only for the safelist).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub request_headers: Vec<HeaderField>,
    /// The shape of the request body.
    #[serde(default, skip_serializing_if = "is_empty_shape")]
    pub request_body: BodyShape,
    /// A literal example request body, captured only under `bodies: full-with-pii`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub request_body_example: Option<serde_json::Value>,

    /// The HTTP response status code.
    pub status: u16,
    /// Observed response headers (names; values only for the safelist).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub response_headers: Vec<HeaderField>,
    /// The shape of the response body.
    #[serde(default, skip_serializing_if = "is_empty_shape")]
    pub response_body: BodyShape,
    /// A literal example response body, captured only under `bodies: full-with-pii`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub response_body_example: Option<serde_json::Value>,

    /// Best-effort capture time, milliseconds since the Unix epoch (0 if unavailable).
    pub timestamp_ms: u64,
    /// Per-process journaling-session id, so records from different processes/machines can
    /// be grouped and de-duplicated after gathering.
    pub session_id: u64,
    /// Monotonic per-session sequence number.
    pub seq: u64,
}

impl JournalRecord {
    /// Find a header's retained value by case-insensitive name, decoded lossily.
    #[must_use]
    pub fn request_content_type(&self) -> Option<String> {
        header_value(&self.request_headers, "content-type")
    }

    /// Find the response `Content-Type` value, decoded lossily, if one was captured.
    #[must_use]
    pub fn response_content_type(&self) -> Option<String> {
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

/// Extract a literal example body for `bodies: full-with-pii` capture.
///
/// Returns the parsed JSON value of a JSON body (content type contains `json`, or none is
/// given), or `None` for an empty or non-JSON body. Unlike [`BodyShape::derive`], this keeps
/// the literal values, so it is only ever called under the opt-in `full-with-pii` capture mode.
#[must_use]
pub fn derive_example(bytes: &[u8], content_type: Option<&str>) -> Option<serde_json::Value> {
    if bytes.is_empty() {
        return None;
    }
    let looks_jsonish = match content_type {
        Some(ct) => ct.to_ascii_lowercase().contains("json"),
        None => true,
    };
    if !looks_jsonish {
        return None;
    }
    serde_json::from_slice::<serde_json::Value>(bytes).ok()
}

fn header_value(headers: &[HeaderField], name_lower: &str) -> Option<String> {
    headers
        .iter()
        .find(|h| h.name.to_string_lossy().eq_ignore_ascii_case(name_lower))
        .and_then(|h| h.value.as_ref().map(RawStr::to_string_lossy))
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
            request_body_example: None,
            response_body_example: None,
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
            "method": {"enc":"raw","b64":"R0VU"},
            "path": {"enc":"raw","b64":"L2hlYWx0aHo="},
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
            record.request_content_type().as_deref(),
            Some("application/json; charset=utf-8")
        );
        assert_eq!(record.response_content_type().as_deref(), Some("application/json"));
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

    #[test]
    fn derive_example_keeps_literal_json() {
        let value = derive_example(br#"{"word":"cat","exists":true}"#, Some("application/json"))
            .expect("example");
        assert_eq!(value["word"], serde_json::json!("cat"));
        assert_eq!(value["exists"], serde_json::json!(true));

        assert_eq!(
            derive_example(b"[1,2,3]", None),
            Some(serde_json::json!([1, 2, 3]))
        );
        assert_eq!(derive_example(b"42", None), Some(serde_json::json!(42)));
    }

    #[test]
    fn derive_example_is_none_for_empty_or_non_json() {
        assert_eq!(derive_example(b"", Some("application/json")), None);
        assert_eq!(derive_example(b"<html>", Some("text/html")), None);
        assert_eq!(derive_example(b"{not json", Some("application/json")), None);
    }

    #[test]
    fn record_round_trips_with_body_examples() {
        let record = JournalRecord {
            seam: Seam::Inbound,
            method: "POST".into(),
            path: "/custom/cat".into(),
            request_body: BodyShape::String,
            request_body_example: derive_example(br#"{"words":["a"]}"#, None),
            status: 200,
            response_body: sample_object(),
            response_body_example: derive_example(br#"{"word":"cat","exists":true}"#, None),
            timestamp_ms: 1,
            session_id: 2,
            seq: 3,
            ..Default::default()
        };
        assert!(record.request_body_example.is_some());
        let json = serde_json::to_string(&record).expect("serialize");
        let back: JournalRecord = serde_json::from_str(&json).expect("deserialize");
        assert_eq!(record, back);
    }

    #[test]
    fn examples_are_omitted_when_absent() {
        let record = JournalRecord {
            seam: Seam::Inbound,
            method: "GET".into(),
            path: "/healthz".into(),
            status: 200,
            ..Default::default()
        };
        let json = serde_json::to_string(&record).expect("serialize");
        assert!(!json.contains("request_body_example"), "{json}");
        assert!(!json.contains("response_body_example"), "{json}");
    }
}
