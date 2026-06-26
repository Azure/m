// Copyright (c) Microsoft Corporation.

//! Validating observed traffic against the existing specs (AJ-D3).
//!
//! [`validate_record`] compares one observed [`JournalRecord`] against a
//! [`SpecIndex`] (the merged path/operation set of the loaded specs) and emits a
//! [`Diagnostic`] for each deviation:
//!
//! - [`UndocumentedPath`](DiagnosticCode::UndocumentedPath) — the path matches no
//!   template;
//! - [`UndocumentedOperation`](DiagnosticCode::UndocumentedOperation) — the method
//!   is not declared on the matched path;
//! - [`UndeclaredStatus`](DiagnosticCode::UndeclaredStatus) — the response status
//!   is not declared;
//! - [`UndeclaredParameter`](DiagnosticCode::UndeclaredParameter) /
//!   [`UndeclaredHeader`](DiagnosticCode::UndeclaredHeader) — an observed query
//!   parameter or non-standard request header is not declared;
//! - [`RequestSchemaMismatch`](DiagnosticCode::RequestSchemaMismatch) /
//!   [`ResponseSchemaMismatch`](DiagnosticCode::ResponseSchemaMismatch) — the
//!   observed body shape does not conform to the declared schema;
//! - [`TypeMismatch`](DiagnosticCode::TypeMismatch) — the body's top-level type
//!   disagrees with the declared type.
//!
//! Body conformance is shape-based: the observed [`BodyShape`] is checked against
//! the declared [`Schema`] for wrong types, missing required fields, and
//! undocumented fields, recursing through objects and arrays. `$ref` schemas are
//! treated as permissive (resolution is not modeled yet).

use std::collections::{HashMap, HashSet};

use api_journal::{BodyShape, JournalRecord};

use crate::diagnostics::{Diagnostic, DiagnosticCode, Location, Severity};
use crate::model::{
    Content, Document, Operation, ParameterIn, PathItem, Response, Responses, Schema, SchemaType,
    SimpleType,
};
use crate::path::{PathMatch, PathTemplate};

/// Request headers that are transport-level rather than part of the API contract;
/// observing one undeclared is not a finding.
const STANDARD_HEADERS: &[&str] = &[
    "accept",
    "accept-encoding",
    "accept-language",
    "authorization",
    "cache-control",
    "connection",
    "content-length",
    "content-type",
    "cookie",
    "date",
    "expect",
    "host",
    "te",
    "transfer-encoding",
    "upgrade",
    "user-agent",
    "via",
    "x-forwarded-for",
    "x-forwarded-host",
    "x-forwarded-proto",
    "x-real-ip",
];

/// The merged path/operation index of one or more loaded specs.
pub struct SpecIndex<'a> {
    entries: Vec<(PathTemplate, &'a PathItem)>,
}

impl<'a> SpecIndex<'a> {
    /// Build an index over every path in every document.
    #[must_use]
    pub fn from_documents(documents: &'a [Document]) -> Self {
        let mut entries = Vec::new();
        for document in documents {
            for (path, item) in &document.paths {
                entries.push((PathTemplate::parse(path), item));
            }
        }
        Self { entries }
    }

    /// The most specific template (and its path item) matching a concrete path.
    fn find(&self, concrete: &str) -> Option<(&PathTemplate, &'a PathItem, PathMatch)> {
        let mut best: Option<(&PathTemplate, &'a PathItem, PathMatch)> = None;
        for (template, item) in &self.entries {
            let Some(matched) = template.matches(concrete) else {
                continue;
            };
            let is_better = match &best {
                None => true,
                Some((current, _, _)) => {
                    (template.param_count(), template.raw())
                        < (current.param_count(), current.raw())
                }
            };
            if is_better {
                best = Some((template, item, matched));
            }
        }
        best
    }
}

/// Validate one observed record against the spec index, returning every
/// deviation as a diagnostic (empty when the record fully conforms).
#[must_use]
pub fn validate_record(index: &SpecIndex, record: &JournalRecord) -> Vec<Diagnostic> {
    let mut diagnostics = Vec::new();

    let Some((template, item, _matched)) = index.find(&record.path) else {
        diagnostics.push(Diagnostic::new(
            Severity::Error,
            DiagnosticCode::UndocumentedPath,
            Location::path(record.path.clone()),
            "no matching path template in the spec",
        ));
        return diagnostics;
    };
    let path = template.raw().to_string();

    let Some(operation) = item.operation(&record.method) else {
        diagnostics.push(Diagnostic::new(
            Severity::Error,
            DiagnosticCode::UndocumentedOperation,
            Location::operation(path, record.method.clone()),
            format!("method {} is not declared on this path", record.method),
        ));
        return diagnostics;
    };

    check_status(&path, record, operation, &mut diagnostics);
    check_query(&path, record, item, operation, &mut diagnostics);
    check_headers(&path, record, item, operation, &mut diagnostics);
    check_request_body(&path, record, operation, &mut diagnostics);
    check_response_body(&path, record, operation, &mut diagnostics);

    diagnostics
}

/// Validate a whole journal stream, deduplicating identical findings and summing
/// their observation counts.
///
/// The same violation observed across many records collapses to one diagnostic
/// whose `count` is the number of observations. The result is sorted
/// deterministically (by location, then code, then message) so output does not
/// depend on record order.
#[must_use]
pub fn validate_stream(index: &SpecIndex, records: &[JournalRecord]) -> Vec<Diagnostic> {
    let mut order: Vec<Diagnostic> = Vec::new();
    let mut seen: HashMap<(Severity, DiagnosticCode, Location, String), usize> = HashMap::new();
    for record in records {
        for diagnostic in validate_record(index, record) {
            let key = (
                diagnostic.severity,
                diagnostic.code,
                diagnostic.location.clone(),
                diagnostic.message.clone(),
            );
            match seen.get(&key) {
                Some(&position) => order[position].count += diagnostic.count,
                None => {
                    seen.insert(key, order.len());
                    order.push(diagnostic);
                }
            }
        }
    }
    order.sort_by(|a, b| {
        (
            a.location.path.as_str(),
            a.location.method.as_deref(),
            a.location.status,
            a.code.as_str(),
            a.message.as_str(),
        )
            .cmp(&(
                b.location.path.as_str(),
                b.location.method.as_deref(),
                b.location.status,
                b.code.as_str(),
                b.message.as_str(),
            ))
    });
    order
}

fn check_status(
    path: &str,
    record: &JournalRecord,
    operation: &Operation,
    out: &mut Vec<Diagnostic>,
) {
    if select_response(&operation.responses, record.status).is_none() {
        out.push(Diagnostic::new(
            Severity::Error,
            DiagnosticCode::UndeclaredStatus,
            Location::response(path.to_string(), record.method.clone(), record.status),
            "response status is not declared on this operation",
        ));
    }
}

fn check_query(
    path: &str,
    record: &JournalRecord,
    item: &PathItem,
    operation: &Operation,
    out: &mut Vec<Diagnostic>,
) {
    // Query parameter names are case-sensitive.
    let declared: HashSet<&str> = item
        .parameters
        .iter()
        .chain(&operation.parameters)
        .filter(|p| p.location == ParameterIn::Query)
        .map(|p| p.name.as_str())
        .collect();
    for param in &record.query {
        if !declared.contains(param.name.as_str()) {
            out.push(Diagnostic::new(
                Severity::Warning,
                DiagnosticCode::UndeclaredParameter,
                Location::operation(path.to_string(), record.method.clone()),
                format!("query parameter '{}' is not declared", param.name),
            ));
        }
    }
}

fn check_headers(
    path: &str,
    record: &JournalRecord,
    item: &PathItem,
    operation: &Operation,
    out: &mut Vec<Diagnostic>,
) {
    // Header names are case-insensitive.
    let declared: HashSet<String> = item
        .parameters
        .iter()
        .chain(&operation.parameters)
        .filter(|p| p.location == ParameterIn::Header)
        .map(|p| p.name.to_ascii_lowercase())
        .collect();
    for header in &record.request_headers {
        let lower = header.name.to_ascii_lowercase();
        if STANDARD_HEADERS.contains(&lower.as_str()) || declared.contains(&lower) {
            continue;
        }
        out.push(Diagnostic::new(
            Severity::Warning,
            DiagnosticCode::UndeclaredHeader,
            Location::operation(path.to_string(), record.method.clone()),
            format!("request header '{}' is not declared", header.name),
        ));
    }
}

fn check_request_body(
    path: &str,
    record: &JournalRecord,
    operation: &Operation,
    out: &mut Vec<Diagnostic>,
) {
    if no_body(&record.request_body) {
        return;
    }
    let Some(request_body) = &operation.request_body else {
        return;
    };
    let Some(schema) = select_schema(&request_body.content, record.request_content_type()) else {
        return;
    };
    let location = Location::operation(path.to_string(), record.method.clone());
    emit_mismatches(
        &check_body(&record.request_body, schema),
        DiagnosticCode::RequestSchemaMismatch,
        &location,
        out,
    );
}

fn check_response_body(
    path: &str,
    record: &JournalRecord,
    operation: &Operation,
    out: &mut Vec<Diagnostic>,
) {
    if no_body(&record.response_body) {
        return;
    }
    let Some(response) = select_response(&operation.responses, record.status) else {
        return; // an undeclared status was already reported.
    };
    let Some(schema) = select_schema(&response.content, record.response_content_type()) else {
        return;
    };
    let location = Location::response(path.to_string(), record.method.clone(), record.status);
    emit_mismatches(
        &check_body(&record.response_body, schema),
        DiagnosticCode::ResponseSchemaMismatch,
        &location,
        out,
    );
}

/// Convert body mismatches into diagnostics under `mismatch_code`, except a
/// root-level type disagreement, which is the dedicated `TypeMismatch`.
fn emit_mismatches(
    mismatches: &[Mismatch],
    mismatch_code: DiagnosticCode,
    location: &Location,
    out: &mut Vec<Diagnostic>,
) {
    for mismatch in mismatches {
        let (severity, code, message) = match mismatch {
            Mismatch::Type { pointer, expected, observed } if pointer.is_empty() => (
                Severity::Error,
                DiagnosticCode::TypeMismatch,
                format!("body type expected {expected}, observed {observed}"),
            ),
            Mismatch::Type { pointer, expected, observed } => (
                Severity::Error,
                mismatch_code,
                format!("type at {pointer} expected {expected}, observed {observed}"),
            ),
            Mismatch::MissingRequired { pointer, field } => (
                Severity::Error,
                mismatch_code,
                format!("missing required field '{field}' at {}", display_pointer(pointer)),
            ),
            Mismatch::UndocumentedField { pointer, field } => (
                Severity::Warning,
                mismatch_code,
                format!("undocumented field '{field}' at {}", display_pointer(pointer)),
            ),
        };
        out.push(Diagnostic::new(severity, code, location.clone(), message));
    }
}

fn display_pointer(pointer: &str) -> String {
    if pointer.is_empty() {
        "root".to_string()
    } else {
        pointer.to_string()
    }
}

/// A single body-conformance discrepancy.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Mismatch {
    /// The observed type disagrees with the declared type at `pointer`.
    Type {
        pointer: String,
        expected: String,
        observed: String,
    },
    /// A declared-required field is absent at `pointer`.
    MissingRequired { pointer: String, field: String },
    /// An observed field is not declared at `pointer` (the schema lists
    /// properties but not this one).
    UndocumentedField { pointer: String, field: String },
}

/// Check an observed shape against a declared schema, collecting mismatches.
fn check_body(shape: &BodyShape, schema: &Schema) -> Vec<Mismatch> {
    let mut out = Vec::new();
    check(shape, schema, "", &mut out);
    out
}

fn check(shape: &BodyShape, schema: &Schema, pointer: &str, out: &mut Vec<Mismatch>) {
    // A union shape must conform under every alternative it presents.
    if let BodyShape::Union(members) = shape {
        for member in members {
            check(member, schema, pointer, out);
        }
        return;
    }
    // Shapes we cannot meaningfully check against, and permissive schemas.
    if matches!(shape, BodyShape::Empty | BodyShape::Unknown | BodyShape::Opaque) {
        return;
    }
    if schema.reference.is_some() {
        return; // $ref resolution is not modeled yet; treat as permissive.
    }
    if !schema.any_of.is_empty() {
        let conforms = schema.any_of.iter().any(|alt| {
            let mut probe = Vec::new();
            check(shape, alt, pointer, &mut probe);
            probe.is_empty()
        });
        if !conforms {
            out.push(Mismatch::Type {
                pointer: pointer.to_string(),
                expected: "anyOf".to_string(),
                observed: shape_kind(shape).to_string(),
            });
        }
        return;
    }
    let Some(schema_type) = &schema.schema_type else {
        return; // no declared type → permissive.
    };
    let allowed = allowed_types(schema_type);
    if !type_matches(shape, &allowed) {
        out.push(Mismatch::Type {
            pointer: pointer.to_string(),
            expected: types_text(&allowed),
            observed: shape_kind(shape).to_string(),
        });
        return; // a type mismatch makes deeper checks meaningless.
    }
    match shape {
        BodyShape::Object(fields) => {
            for required in &schema.required {
                if !fields.contains_key(required) {
                    out.push(Mismatch::MissingRequired {
                        pointer: pointer.to_string(),
                        field: required.clone(),
                    });
                }
            }
            for (name, field) in fields {
                if let Some(property_schema) = schema.properties.get(name) {
                    check(&field.shape, property_schema, &format!("{pointer}.{name}"), out);
                } else if !schema.properties.is_empty() {
                    out.push(Mismatch::UndocumentedField {
                        pointer: pointer.to_string(),
                        field: name.clone(),
                    });
                }
            }
        }
        BodyShape::Array(element) => {
            if let Some(items) = &schema.items {
                check(element, items, &format!("{pointer}[]"), out);
            }
        }
        _ => {}
    }
}

fn no_body(shape: &BodyShape) -> bool {
    matches!(shape, BodyShape::Empty | BodyShape::Unknown)
}

fn shape_kind(shape: &BodyShape) -> &'static str {
    match shape {
        BodyShape::Empty => "empty",
        BodyShape::Unknown => "unknown",
        BodyShape::Opaque => "opaque",
        BodyShape::Null => "null",
        BodyShape::Bool => "boolean",
        BodyShape::Integer => "integer",
        BodyShape::Number => "number",
        BodyShape::String => "string",
        BodyShape::Array(_) => "array",
        BodyShape::Object(_) => "object",
        BodyShape::Union(_) => "union",
    }
}

fn allowed_types(schema_type: &SchemaType) -> Vec<SimpleType> {
    match schema_type {
        SchemaType::Single(single) => vec![*single],
        SchemaType::Multiple(many) => many.clone(),
    }
}

fn type_matches(shape: &BodyShape, allowed: &[SimpleType]) -> bool {
    let accepts = |wanted: SimpleType| allowed.contains(&wanted);
    match shape {
        BodyShape::Object(_) => accepts(SimpleType::Object),
        BodyShape::Array(_) => accepts(SimpleType::Array),
        BodyShape::Bool => accepts(SimpleType::Boolean),
        BodyShape::String => accepts(SimpleType::String),
        BodyShape::Null => accepts(SimpleType::Null),
        // An integer is a number; the reverse is not guaranteed.
        BodyShape::Integer => accepts(SimpleType::Integer) || accepts(SimpleType::Number),
        BodyShape::Number => accepts(SimpleType::Number),
        // These are short-circuited before reaching here.
        BodyShape::Empty | BodyShape::Unknown | BodyShape::Opaque | BodyShape::Union(_) => true,
    }
}

fn types_text(allowed: &[SimpleType]) -> String {
    let names: Vec<&str> = allowed.iter().map(|t| simple_type_name(*t)).collect();
    names.join("|")
}

fn simple_type_name(simple: SimpleType) -> &'static str {
    match simple {
        SimpleType::Null => "null",
        SimpleType::Boolean => "boolean",
        SimpleType::Object => "object",
        SimpleType::Array => "array",
        SimpleType::Number => "number",
        SimpleType::String => "string",
        SimpleType::Integer => "integer",
    }
}

/// Select the response for a status: exact code, then `NXX` range, then `default`.
fn select_response(responses: &Responses, status: u16) -> Option<&Response> {
    if let Some(response) = responses.get(&status.to_string()) {
        return Some(response);
    }
    let range_upper = format!("{}XX", status / 100);
    if let Some(response) = responses.get(&range_upper) {
        return Some(response);
    }
    let range_lower = format!("{}xx", status / 100);
    if let Some(response) = responses.get(&range_lower) {
        return Some(response);
    }
    responses.get("default")
}

/// Select the schema for the observed content type: an exact media-type match,
/// else the sole entry, else `application/json`, else the first entry.
fn select_schema<'a>(content: &'a Content, content_type: Option<&str>) -> Option<&'a Schema> {
    if content.is_empty() {
        return None;
    }
    if let Some(observed) = content_type {
        let key = media_key(observed);
        for (declared, media) in content {
            if media_key(declared) == key {
                return media.schema.as_ref();
            }
        }
    }
    if content.len() == 1 {
        return content.values().next().and_then(|media| media.schema.as_ref());
    }
    content
        .get("application/json")
        .and_then(|media| media.schema.as_ref())
        .or_else(|| content.values().next().and_then(|media| media.schema.as_ref()))
}

/// The bare media type (before any `;` parameters), lowercased.
fn media_key(media_type: &str) -> String {
    media_type
        .split(';')
        .next()
        .unwrap_or(media_type)
        .trim()
        .to_ascii_lowercase()
}

#[cfg(test)]
mod tests {
    use super::*;
    use api_journal::{Field, HeaderField, JournalRecord, QueryParam, Seam};
    use crate::model::{
        Content, Document, MediaType, Operation, Parameter, ParameterIn, PathItem, RequestBody,
        Response, Responses, Schema, SchemaType, SimpleType,
    };
    use std::collections::BTreeMap;

    fn object_schema(fields: &[(&str, SimpleType, bool)]) -> Schema {
        let mut schema = Schema {
            schema_type: Some(SchemaType::Single(SimpleType::Object)),
            ..Schema::default()
        };
        for (name, ty, required) in fields {
            schema
                .properties
                .insert((*name).to_string(), Schema::of_type(*ty));
            if *required {
                schema.required.push((*name).to_string());
            }
        }
        schema
    }

    fn json_content(schema: Schema) -> Content {
        let mut content = Content::new();
        content.insert(
            "application/json".to_string(),
            MediaType { schema: Some(schema) },
        );
        content
    }

    /// A spec with `GET /custom/{word}` declaring a 200 object response with a
    /// required `word` field and an `exists` boolean.
    fn spec() -> Document {
        let mut responses = Responses::new();
        responses.insert(
            "200".to_string(),
            Response {
                description: "membership".to_string(),
                content: json_content(object_schema(&[
                    ("word", SimpleType::String, true),
                    ("exists", SimpleType::Boolean, true),
                ])),
                ..Response::default()
            },
        );
        let operation = Operation {
            parameters: vec![Parameter {
                name: "pattern".to_string(),
                location: ParameterIn::Query,
                required: false,
                description: None,
                schema: Some(Schema::of_type(SimpleType::String)),
            }],
            responses,
            ..Operation::default()
        };
        let item = PathItem {
            get: Some(operation),
            ..PathItem::default()
        };
        let mut document = Document::new("merriam", "1.0.0");
        document.paths.insert("/custom/{word}".to_string(), item);
        document
    }

    fn object_shape(fields: &[(&str, BodyShape, bool)]) -> BodyShape {
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

    fn conforming_response_body() -> BodyShape {
        object_shape(&[
            ("word", BodyShape::String, true),
            ("exists", BodyShape::Bool, true),
        ])
    }

    fn base_record() -> JournalRecord {
        JournalRecord {
            seam: Seam::Inbound,
            method: "GET".into(),
            path: "/custom/cat".into(),
            status: 200,
            response_headers: vec![HeaderField {
                name: "Content-Type".into(),
                value: Some("application/json".into()),
            }],
            response_body: conforming_response_body(),
            ..Default::default()
        }
    }

    fn validate(documents: &[Document], record: &JournalRecord) -> Vec<Diagnostic> {
        let index = SpecIndex::from_documents(documents);
        validate_record(&index, record)
    }

    fn codes(diagnostics: &[Diagnostic]) -> Vec<DiagnosticCode> {
        diagnostics.iter().map(|d| d.code).collect()
    }

    #[test]
    fn a_fully_documented_record_yields_no_diagnostics() {
        let docs = vec![spec()];
        assert!(validate(&docs, &base_record()).is_empty());
    }

    #[test]
    fn undocumented_path_when_nothing_matches() {
        let docs = vec![spec()];
        let mut record = base_record();
        record.path = "/unknown/thing".into();
        assert_eq!(codes(&validate(&docs, &record)), [DiagnosticCode::UndocumentedPath]);
    }

    #[test]
    fn undocumented_operation_when_method_missing() {
        let docs = vec![spec()];
        let mut record = base_record();
        record.method = "DELETE".into();
        assert_eq!(
            codes(&validate(&docs, &record)),
            [DiagnosticCode::UndocumentedOperation]
        );
    }

    #[test]
    fn undeclared_status_when_status_not_listed() {
        let docs = vec![spec()];
        let mut record = base_record();
        record.status = 418;
        // The 418 response has no declared schema, so only the status fires.
        assert_eq!(codes(&validate(&docs, &record)), [DiagnosticCode::UndeclaredStatus]);
    }

    #[test]
    fn undeclared_parameter_when_query_not_declared() {
        let docs = vec![spec()];
        let mut record = base_record();
        record.query = vec![QueryParam {
            name: "limit".into(),
            value: BodyShape::Integer,
        }];
        assert_eq!(
            codes(&validate(&docs, &record)),
            [DiagnosticCode::UndeclaredParameter]
        );
    }

    #[test]
    fn declared_query_parameter_is_accepted() {
        let docs = vec![spec()];
        let mut record = base_record();
        record.query = vec![QueryParam {
            name: "pattern".into(),
            value: BodyShape::String,
        }];
        assert!(validate(&docs, &record).is_empty());
    }

    #[test]
    fn undeclared_header_excludes_standard_headers() {
        let docs = vec![spec()];
        let mut record = base_record();
        record.request_headers = vec![
            HeaderField {
                name: "Accept".into(),
                value: None,
            },
            HeaderField {
                name: "X-Wordy-User".into(),
                value: None,
            },
        ];
        // `Accept` is standard; `X-Wordy-User` is undeclared.
        let diagnostics = validate(&docs, &record);
        assert_eq!(codes(&diagnostics), [DiagnosticCode::UndeclaredHeader]);
        assert!(diagnostics[0].message.contains("X-Wordy-User"));
    }

    #[test]
    fn response_schema_mismatch_on_missing_required_field() {
        let docs = vec![spec()];
        let mut record = base_record();
        // Drop the required `exists` field.
        record.response_body = object_shape(&[("word", BodyShape::String, true)]);
        let diagnostics = validate(&docs, &record);
        assert_eq!(codes(&diagnostics), [DiagnosticCode::ResponseSchemaMismatch]);
        assert!(diagnostics[0].message.contains("exists"));
    }

    #[test]
    fn response_schema_mismatch_on_undocumented_field() {
        let docs = vec![spec()];
        let mut record = base_record();
        record.response_body = object_shape(&[
            ("word", BodyShape::String, true),
            ("exists", BodyShape::Bool, true),
            ("surprise", BodyShape::Integer, true),
        ]);
        let diagnostics = validate(&docs, &record);
        assert_eq!(codes(&diagnostics), [DiagnosticCode::ResponseSchemaMismatch]);
        assert!(diagnostics[0].message.contains("surprise"));
        assert_eq!(diagnostics[0].severity, Severity::Warning);
    }

    #[test]
    fn type_mismatch_on_wrong_top_level_type() {
        let docs = vec![spec()];
        let mut record = base_record();
        // The 200 schema is an object; observe an array instead.
        record.response_body = BodyShape::Array(Box::new(BodyShape::String));
        let diagnostics = validate(&docs, &record);
        assert_eq!(codes(&diagnostics), [DiagnosticCode::TypeMismatch]);
        assert!(diagnostics[0].message.contains("object"));
    }

    #[test]
    fn request_schema_mismatch_on_field_type() {
        // A spec with a POST body schema `{ words: [string] }`.
        let mut responses = Responses::new();
        responses.insert(
            "200".to_string(),
            Response {
                description: "ok".to_string(),
                ..Response::default()
            },
        );
        let mut props = BTreeMap::new();
        props.insert(
            "words".to_string(),
            Schema {
                schema_type: Some(SchemaType::Single(SimpleType::Array)),
                items: Some(Box::new(Schema::of_type(SimpleType::String))),
                ..Schema::default()
            },
        );
        let request_schema = Schema {
            schema_type: Some(SchemaType::Single(SimpleType::Object)),
            properties: props,
            required: vec!["words".to_string()],
            ..Schema::default()
        };
        let operation = Operation {
            request_body: Some(RequestBody {
                content: json_content(request_schema),
                ..RequestBody::default()
            }),
            responses,
            ..Operation::default()
        };
        let item = PathItem {
            post: Some(operation),
            ..PathItem::default()
        };
        let mut document = Document::new("wordy", "1.0.0");
        document.paths.insert("/spellcheck".to_string(), item);

        let mut record = base_record();
        record.method = "POST".into();
        record.path = "/spellcheck".into();
        record.request_headers = vec![HeaderField {
            name: "Content-Type".into(),
            value: Some("application/json".into()),
        }];
        // words is an array of integers, not strings.
        let mut request_fields = BTreeMap::new();
        request_fields.insert(
            "words".to_string(),
            Field {
                shape: BodyShape::Array(Box::new(BodyShape::Integer)),
                required: true,
            },
        );
        record.request_body = BodyShape::Object(request_fields);

        let diagnostics = validate(&[document], &record);
        assert_eq!(codes(&diagnostics), [DiagnosticCode::RequestSchemaMismatch]);
        assert!(diagnostics[0].message.contains("words[]"));
    }

    #[test]
    fn nullable_and_integer_to_number_conform() {
        // A schema accepting a nullable number for `count`.
        let mut responses = Responses::new();
        let mut props = BTreeMap::new();
        props.insert(
            "count".to_string(),
            Schema {
                schema_type: Some(SchemaType::Multiple(vec![
                    SimpleType::Number,
                    SimpleType::Null,
                ])),
                ..Schema::default()
            },
        );
        responses.insert(
            "200".to_string(),
            Response {
                description: "ok".to_string(),
                content: json_content(Schema {
                    schema_type: Some(SchemaType::Single(SimpleType::Object)),
                    properties: props,
                    ..Schema::default()
                }),
                ..Response::default()
            },
        );
        let item = PathItem {
            get: Some(Operation {
                responses,
                ..Operation::default()
            }),
            ..PathItem::default()
        };
        let mut document = Document::new("svc", "1");
        document.paths.insert("/m".to_string(), item);

        let mut record = base_record();
        record.path = "/m".into();
        // count observed as an integer (a number) — conforms.
        record.response_body = object_shape(&[("count", BodyShape::Integer, true)]);
        assert!(validate(&[document], &record).is_empty());
    }

    #[test]
    fn stream_aggregates_identical_violations_with_a_count() {
        let docs = vec![spec()];
        let index = SpecIndex::from_documents(&docs);
        // Three records hitting the same undocumented path.
        let mut records = Vec::new();
        for word in ["a", "b", "c"] {
            let mut record = base_record();
            record.path = format!("/unknown/{word}");
            // Different concrete paths, but each matches nothing -> same finding
            // keyed on the (identical) message and path? The path differs, so
            // they are distinct findings.
            records.push(record);
        }
        let diagnostics = validate_stream(&index, &records);
        // Distinct concrete paths -> three distinct UndocumentedPath findings.
        assert_eq!(diagnostics.len(), 3);
        assert!(diagnostics.iter().all(|d| d.count == 1));

        // Now the same concrete path repeated collapses to one with count 3.
        let repeated: Vec<_> = (0..3)
            .map(|_| {
                let mut record = base_record();
                record.path = "/unknown/same".into();
                record
            })
            .collect();
        let collapsed = validate_stream(&index, &repeated);
        assert_eq!(collapsed.len(), 1);
        assert_eq!(collapsed[0].code, DiagnosticCode::UndocumentedPath);
        assert_eq!(collapsed[0].count, 3);
    }

    #[test]
    fn stream_is_order_independent() {
        let docs = vec![spec()];
        let index = SpecIndex::from_documents(&docs);

        let mut undocumented = base_record();
        undocumented.method = "DELETE".into();
        let mut bad_status = base_record();
        bad_status.status = 418;

        let forward = validate_stream(&index, &[undocumented.clone(), bad_status.clone()]);
        let reverse = validate_stream(&index, &[bad_status, undocumented]);
        assert_eq!(forward, reverse);
        assert_eq!(forward.len(), 2);
    }

    #[test]
    fn stream_of_clean_records_yields_nothing() {
        let docs = vec![spec()];
        let index = SpecIndex::from_documents(&docs);
        let records = vec![base_record(), base_record()];
        assert!(validate_stream(&index, &records).is_empty());
    }
}
