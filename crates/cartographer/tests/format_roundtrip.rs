// Copyright (c) Microsoft Corporation.

//! AJ-C5 milestone integration: assemble a document by rendering `api-journal`
//! body shapes into OpenAPI schemas, then round-trip it through both JSON and
//! YAML and load both forms back from a directory — exercising the model, the
//! shape→schema renderer, and the format loader together.

use std::collections::BTreeMap;
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use api_journal::{BodyShape, Field};
use cartographer::{
    Content, Document, MediaType, Operation, PathItem, Response, Responses, SchemaType, SimpleType,
    SpecFormat, load_path, parse_document, render_schema, serialize_document,
};

/// The shape of `merriam`'s `GET /custom` response body: `{"matches": ["…"]}`.
fn matches_shape() -> BodyShape {
    let mut fields = BTreeMap::new();
    fields.insert(
        "matches".to_string(),
        Field {
            shape: BodyShape::Array(Box::new(BodyShape::String)),
            required: true,
        },
    );
    BodyShape::Object(fields)
}

/// Assemble a small document whose response schema is rendered from a body shape.
fn build_document() -> Document {
    let response_schema = render_schema(&matches_shape()).expect("response schema");
    let mut content = Content::new();
    content.insert(
        "application/json".to_string(),
        MediaType {
            schema: Some(response_schema),
            example: None,
        },
    );
    let mut responses = Responses::new();
    responses.insert(
        "200".to_string(),
        Response {
            description: "the matching custom words".to_string(),
            content,
            ..Response::default()
        },
    );
    let item = PathItem {
        get: Some(Operation {
            operation_id: Some("enumerateCustom".to_string()),
            responses,
            ..Operation::default()
        }),
        ..PathItem::default()
    };
    let mut doc = Document::new("merriam", "1.0.0");
    doc.paths.insert("/custom".to_string(), item);
    doc
}

fn temp_dir() -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let dir = std::env::temp_dir().join(format!(
        "cartographer-c5-{}-{nanos}",
        std::process::id()
    ));
    std::fs::create_dir_all(&dir).expect("create temp dir");
    dir
}

#[test]
fn document_round_trips_through_both_formats_and_loads_from_a_directory() {
    let doc = build_document();

    // In-memory round-trip through each format.
    for format in [SpecFormat::Json, SpecFormat::Yaml] {
        let text = serialize_document(&doc, format).expect("serialize");
        let back = parse_document(&text, format).expect("parse");
        assert_eq!(doc, back, "round-trip via {format:?}");
    }

    // Write both forms to a directory and load them back tolerantly.
    let dir = temp_dir();
    std::fs::write(
        dir.join("merriam.json"),
        serialize_document(&doc, SpecFormat::Json).unwrap(),
    )
    .unwrap();
    std::fs::write(
        dir.join("merriam.yaml"),
        serialize_document(&doc, SpecFormat::Yaml).unwrap(),
    )
    .unwrap();

    let outcome = load_path(&dir);
    std::fs::remove_dir_all(&dir).ok();

    assert!(outcome.errors.is_empty(), "{:?}", outcome.errors);
    assert_eq!(outcome.specs.len(), 2);
    // Both the JSON-sourced and YAML-sourced documents equal the original (and
    // therefore each other).
    for spec in &outcome.specs {
        assert_eq!(spec.document, doc, "loaded {:?}", spec.path);
    }
    assert_eq!(outcome.specs[0].document, outcome.specs[1].document);

    // The rendered response schema is the expected object-of-array.
    let operation = doc.paths["/custom"].operation("GET").expect("GET operation");
    let schema = operation.responses["200"].content["application/json"]
        .schema
        .as_ref()
        .expect("response schema");
    assert_eq!(
        schema.schema_type,
        Some(SchemaType::Single(SimpleType::Object))
    );
    let matches = schema.properties.get("matches").expect("matches property");
    assert_eq!(
        matches.schema_type,
        Some(SchemaType::Single(SimpleType::Array))
    );
    assert_eq!(
        matches.items.as_deref(),
        Some(&cartographer::Schema::of_type(SimpleType::String))
    );
}
