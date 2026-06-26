// Copyright (c) Microsoft Corporation.

//! Synthesizing OpenAPI operations from observed traffic (AJ-E2).
//!
//! [`synthesize`] turns a journal of observed interactions into an OpenAPI
//! [`Document`]: it folds concrete paths onto templates (via
//! [`TemplateSet`](crate::infer::TemplateSet), preferring any existing
//! templates), groups records by `(template, method)`, and builds one
//! [`Operation`] per group —
//!
//! - **path parameters** from the template's `{…}` segments;
//! - **query / header parameters** from observed names (a parameter is `required`
//!   only if it appeared on every observation of the operation; header values are
//!   strings, query value schemas come from the observed value shapes);
//! - a **request body** from the merged request shapes per media type;
//! - **responses** per observed status, each with the merged response-body schema.
//!
//! Body shapes across observations are combined with
//! [`BodyShape::merge`](api_journal::BodyShape::merge), so the synthesized schema
//! describes every sample (optional fields, widened numbers, `anyOf` unions), and
//! rendered to OpenAPI schemas via [`render_schema`](crate::schema::render_schema).

use std::collections::{BTreeMap, BTreeSet, HashSet};

use api_journal::{BodyShape, JournalRecord};

use crate::infer::TemplateSet;
use crate::model::{
    Content, Document, MediaType, Operation, Parameter, ParameterIn, PathItem, RequestBody,
    Response, Responses, Schema, SimpleType,
};
use crate::path::PathTemplate;
use crate::schema::render_schema;
use crate::validate::STANDARD_HEADERS;

/// Synthesize a document from observed records, folding paths onto `existing`
/// templates where they match and inferring new templates otherwise.
#[must_use]
pub fn synthesize(records: &[JournalRecord], existing: &[PathTemplate]) -> Document {
    let observed: Vec<String> = records.iter().map(|record| record.path.clone()).collect();
    let templates = TemplateSet::infer(&observed, existing);

    // Group records by template path, then by method.
    let mut by_path: BTreeMap<String, BTreeMap<String, Vec<&JournalRecord>>> = BTreeMap::new();
    for record in records {
        if let Some((template, _matched)) = templates.assign(&record.path) {
            by_path
                .entry(template.raw().to_string())
                .or_default()
                .entry(record.method.to_ascii_uppercase())
                .or_default()
                .push(record);
        }
    }

    let mut document = Document::new("synthesized", "0.1.0");
    for (path, methods) in &by_path {
        let mut item = PathItem::default();
        if let Some(template) = templates.templates().iter().find(|t| t.raw() == path) {
            for name in template.param_names() {
                item.parameters.push(Parameter {
                    name: name.to_string(),
                    location: ParameterIn::Path,
                    required: true,
                    description: None,
                    schema: Some(Schema::of_type(SimpleType::String)),
                });
            }
        }
        for (method, group) in methods {
            if let Some(slot) = item.operation_slot_mut(method) {
                *slot = Some(synth_operation(group));
            }
        }
        document.paths.insert(path.clone(), item);
    }
    document
}

/// Build one operation from the records that exercised it.
fn synth_operation(records: &[&JournalRecord]) -> Operation {
    let total = records.len();
    let mut parameters = Vec::new();
    synth_query_params(records, total, &mut parameters);
    synth_header_params(records, total, &mut parameters);
    Operation {
        parameters,
        request_body: synth_request_body(records, total),
        responses: synth_responses(records),
        ..Operation::default()
    }
}

/// Query parameters: one per observed name, with the value schema merged across
/// observations and `required` set when seen on every observation.
fn synth_query_params(records: &[&JournalRecord], total: usize, out: &mut Vec<Parameter>) {
    let mut shapes: BTreeMap<String, (BodyShape, usize)> = BTreeMap::new();
    for record in records {
        let mut seen = HashSet::new();
        for query in &record.query {
            if seen.insert(query.name.clone()) {
                let entry = shapes
                    .entry(query.name.clone())
                    .or_insert((BodyShape::Unknown, 0));
                entry.0 = entry.0.clone().merge(query.value.clone());
                entry.1 += 1;
            }
        }
    }
    for (name, (shape, count)) in shapes {
        out.push(Parameter {
            name,
            location: ParameterIn::Query,
            required: count == total,
            description: None,
            schema: render_schema(&shape),
        });
    }
}

/// Header parameters: one per observed non-standard request header (case-
/// insensitive), value schema `string`, `required` when seen on every observation.
fn synth_header_params(records: &[&JournalRecord], total: usize, out: &mut Vec<Parameter>) {
    let mut counts: BTreeMap<String, usize> = BTreeMap::new();
    let mut display: BTreeMap<String, String> = BTreeMap::new();
    for record in records {
        let mut seen = HashSet::new();
        for header in &record.request_headers {
            let lower = header.name.to_ascii_lowercase();
            if STANDARD_HEADERS.contains(&lower.as_str()) {
                continue;
            }
            if seen.insert(lower.clone()) {
                *counts.entry(lower.clone()).or_default() += 1;
                display.entry(lower.clone()).or_insert_with(|| header.name.clone());
            }
        }
    }
    for (lower, count) in counts {
        out.push(Parameter {
            name: display[&lower].clone(),
            location: ParameterIn::Header,
            required: count == total,
            description: None,
            schema: Some(Schema::of_type(SimpleType::String)),
        });
    }
}

/// A request body merged per media type, or `None` if no body was ever observed.
fn synth_request_body(records: &[&JournalRecord], total: usize) -> Option<RequestBody> {
    let mut by_media: BTreeMap<String, BodyShape> = BTreeMap::new();
    let mut examples: BTreeMap<String, serde_json::Value> = BTreeMap::new();
    let mut with_body = 0;
    for record in records {
        if no_body(&record.request_body) {
            continue;
        }
        with_body += 1;
        let media = media_or_default(record.request_content_type());
        let entry = by_media.entry(media.clone()).or_insert(BodyShape::Unknown);
        *entry = entry.clone().merge(record.request_body.clone());
        if let Some(example) = &record.request_body_example {
            examples.entry(media).or_insert_with(|| example.clone());
        }
    }
    if by_media.is_empty() {
        return None;
    }
    let mut content = Content::new();
    for (media, shape) in by_media {
        let example = examples.get(&media).cloned();
        content.insert(
            media,
            MediaType {
                schema: render_schema(&shape),
                example,
            },
        );
    }
    Some(RequestBody {
        description: None,
        required: with_body == total,
        content,
    })
}

/// Responses by status, each with its merged response-body schema per media type.
fn synth_responses(records: &[&JournalRecord]) -> Responses {
    let statuses: BTreeSet<u16> = records.iter().map(|record| record.status).collect();
    let mut by_status: BTreeMap<u16, BTreeMap<String, BodyShape>> = BTreeMap::new();
    let mut examples: BTreeMap<(u16, String), serde_json::Value> = BTreeMap::new();
    for record in records {
        if no_body(&record.response_body) {
            continue;
        }
        let media = media_or_default(record.response_content_type());
        let entry = by_status
            .entry(record.status)
            .or_default()
            .entry(media.clone())
            .or_insert(BodyShape::Unknown);
        *entry = entry.clone().merge(record.response_body.clone());
        if let Some(example) = &record.response_body_example {
            examples
                .entry((record.status, media))
                .or_insert_with(|| example.clone());
        }
    }

    let mut responses = Responses::new();
    for status in statuses {
        let mut content = Content::new();
        if let Some(media_map) = by_status.get(&status) {
            for (media, shape) in media_map {
                let example = examples.get(&(status, media.clone())).cloned();
                content.insert(
                    media.clone(),
                    MediaType {
                        schema: render_schema(shape),
                        example,
                    },
                );
            }
        }
        responses.insert(
            status.to_string(),
            Response {
                description: status_phrase(status).to_string(),
                content,
                ..Response::default()
            },
        );
    }
    responses
}

fn no_body(shape: &BodyShape) -> bool {
    matches!(shape, BodyShape::Empty | BodyShape::Unknown)
}

/// The bare media type (lowercased, without parameters), defaulting to JSON.
fn media_or_default(content_type: Option<&str>) -> String {
    content_type
        .map(|raw| {
            raw.split(';')
                .next()
                .unwrap_or(raw)
                .trim()
                .to_ascii_lowercase()
        })
        .filter(|media| !media.is_empty())
        .unwrap_or_else(|| "application/json".to_string())
}

/// A reason phrase for common status codes (responses require a description).
fn status_phrase(status: u16) -> &'static str {
    match status {
        200 => "OK",
        201 => "Created",
        202 => "Accepted",
        204 => "No Content",
        301 => "Moved Permanently",
        302 => "Found",
        304 => "Not Modified",
        400 => "Bad Request",
        401 => "Unauthorized",
        403 => "Forbidden",
        404 => "Not Found",
        409 => "Conflict",
        422 => "Unprocessable Entity",
        429 => "Too Many Requests",
        500 => "Internal Server Error",
        502 => "Bad Gateway",
        503 => "Service Unavailable",
        _ => "Response",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use api_journal::{Field, HeaderField, JournalRecord, QueryParam, Seam};
    use crate::model::{ParameterIn, SchemaType, SimpleType};
    use std::collections::BTreeMap;

    fn json_ct() -> Vec<HeaderField> {
        vec![HeaderField {
            name: "Content-Type".into(),
            value: Some("application/json".into()),
        }]
    }

    fn obj(fields: &[(&str, BodyShape, bool)]) -> BodyShape {
        let mut map = BTreeMap::new();
        for (name, shape, required) in fields {
            map.insert(
                (*name).to_string(),
                Field {
                    shape: shape.clone(),
                    required: *required,
                },
            );
        }
        BodyShape::Object(map)
    }

    fn record(method: &str, path: &str, status: u16, body: BodyShape) -> JournalRecord {
        JournalRecord {
            seam: Seam::Inbound,
            method: method.into(),
            path: path.into(),
            status,
            response_headers: json_ct(),
            response_body: body,
            ..Default::default()
        }
    }

    #[test]
    fn synthesizes_a_templated_item_path_with_path_parameter() {
        let records = vec![
            record(
                "GET",
                "/custom/cat",
                200,
                obj(&[("word", BodyShape::String, true), ("exists", BodyShape::Bool, true)]),
            ),
            record(
                "GET",
                "/custom/dog",
                200,
                obj(&[("word", BodyShape::String, true), ("exists", BodyShape::Bool, true)]),
            ),
        ];
        let doc = synthesize(&records, &[]);
        let item = doc.paths.get("/custom/{id}").expect("templated path");
        // The path parameter is declared and required.
        assert_eq!(item.parameters.len(), 1);
        assert_eq!(item.parameters[0].name, "id");
        assert_eq!(item.parameters[0].location, ParameterIn::Path);
        assert!(item.parameters[0].required);
        // The GET 200 response carries the merged object schema.
        let operation = item.operation("GET").expect("GET");
        let response = operation.responses.get("200").expect("200");
        let schema = response.content["application/json"].schema.as_ref().unwrap();
        assert_eq!(schema.schema_type, Some(SchemaType::Single(SimpleType::Object)));
        assert!(schema.properties.contains_key("word"));
        assert!(schema.properties.contains_key("exists"));
    }

    #[test]
    fn synthesizes_query_parameter_with_required_reflecting_frequency() {
        let mut with = record("GET", "/custom", 200, BodyShape::Empty);
        with.query = vec![QueryParam {
            name: "pattern".into(),
            value: BodyShape::String,
        }];
        let without = record("GET", "/custom", 200, BodyShape::Empty);
        let doc = synthesize(&[with, without], &[]);
        let operation = doc.paths["/custom"].operation("GET").unwrap();
        let param = operation
            .parameters
            .iter()
            .find(|p| p.name == "pattern")
            .expect("pattern param");
        assert_eq!(param.location, ParameterIn::Query);
        // Seen on only one of two observations -> optional.
        assert!(!param.required);
        assert_eq!(param.schema, Some(Schema::of_type(SimpleType::String)));
    }

    #[test]
    fn synthesizes_request_body_from_merged_shapes() {
        let mut post = record("POST", "/spellcheck", 200, BodyShape::Empty);
        post.request_headers = json_ct();
        post.request_body = obj(&[(
            "words",
            BodyShape::Array(Box::new(BodyShape::String)),
            true,
        )]);
        let doc = synthesize(&[post], &[]);
        let operation = doc.paths["/spellcheck"].operation("POST").unwrap();
        let body = operation.request_body.as_ref().expect("request body");
        assert!(body.required);
        let schema = body.content["application/json"].schema.as_ref().unwrap();
        assert_eq!(schema.schema_type, Some(SchemaType::Single(SimpleType::Object)));
        let words = schema.properties.get("words").expect("words");
        assert_eq!(words.schema_type, Some(SchemaType::Single(SimpleType::Array)));
    }

    #[test]
    fn synthesizes_multiple_response_statuses() {
        let ok = record("GET", "/r", 200, obj(&[("a", BodyShape::String, true)]));
        let missing = record("GET", "/r", 404, obj(&[("error", BodyShape::String, true)]));
        let doc = synthesize(&[ok, missing], &[]);
        let operation = doc.paths["/r"].operation("GET").unwrap();
        assert!(operation.responses.contains_key("200"));
        assert!(operation.responses.contains_key("404"));
        assert_eq!(operation.responses["404"].description, "Not Found");
    }

    #[test]
    fn header_parameters_exclude_standard_headers() {
        let mut get = record("GET", "/h", 200, BodyShape::Empty);
        get.request_headers = vec![
            HeaderField {
                name: "Accept".into(),
                value: None,
            },
            HeaderField {
                name: "X-Wordy-User".into(),
                value: None,
            },
        ];
        let doc = synthesize(&[get], &[]);
        let operation = doc.paths["/h"].operation("GET").unwrap();
        let headers: Vec<&str> = operation
            .parameters
            .iter()
            .filter(|p| p.location == ParameterIn::Header)
            .map(|p| p.name.as_str())
            .collect();
        assert_eq!(headers, ["X-Wordy-User"]);
    }

    #[test]
    fn optional_object_field_is_reflected_across_samples() {
        // `exists` present in one sample, absent in the other -> optional in schema.
        let with = record(
            "GET",
            "/custom/cat",
            200,
            obj(&[("word", BodyShape::String, true), ("exists", BodyShape::Bool, true)]),
        );
        let without = record(
            "GET",
            "/custom/dog",
            200,
            obj(&[("word", BodyShape::String, true)]),
        );
        let doc = synthesize(&[with, without], &[]);
        let operation = doc.paths["/custom/{id}"].operation("GET").unwrap();
        let schema = operation.responses["200"].content["application/json"]
            .schema
            .as_ref()
            .unwrap();
        // `word` seen in both -> required; `exists` only once -> not required.
        assert_eq!(schema.required, vec!["word".to_string()]);
        assert!(schema.properties.contains_key("exists"));
    }

    #[test]
    fn synthesizes_example_from_a_captured_full_body() {
        let mut record = record("GET", "/custom/cat", 200, obj(&[("word", BodyShape::String, true)]));
        record.response_body_example = Some(serde_json::json!({"word": "cat", "exists": true}));
        let doc = synthesize(&[record], &[]);
        // A single observation stays a literal path.
        let operation = doc.paths["/custom/cat"].operation("GET").unwrap();
        let media = &operation.responses["200"].content["application/json"];
        assert_eq!(
            media.example,
            Some(serde_json::json!({"word": "cat", "exists": true}))
        );
    }
}
