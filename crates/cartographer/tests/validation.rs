// Copyright (c) Microsoft Corporation.

//! AJ-D5 milestone integration: load a fixture spec (YAML) and a fixture journal
//! (NDJSON) from real files, validate the journal stream against the spec, and
//! assert the resulting diagnostic set — including a clean run with none.

use std::collections::BTreeMap;
use std::fs::File;
use std::io::{BufReader, BufWriter};
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use api_journal::{
    BodyShape, Field, HeaderField, JournalRecord, QueryParam, Seam, read_records, write_record,
};
use cartographer::{
    BufferSink, Content, DiagnosticCode, Document, MediaType, Operation, Parameter, ParameterIn,
    PathItem, ReportFormat, Response, Responses, Schema, SchemaType, SimpleType, SpecFormat,
    SpecIndex, load_path, render_diagnostics, serialize_document, validate_stream,
};

fn object(fields: &[(&str, SimpleType, bool)]) -> Schema {
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

fn json_200(schema: Schema) -> Responses {
    let mut content = Content::new();
    content.insert("application/json".to_string(), MediaType { schema: Some(schema), example: None });
    let mut responses = Responses::new();
    responses.insert(
        "200".to_string(),
        Response {
            description: "ok".to_string(),
            content,
            ..Response::default()
        },
    );
    responses
}

/// A merriam-like spec.
fn spec() -> Document {
    let mut doc = Document::new("merriam", "1.0.0");

    doc.paths.insert(
        "/healthz".to_string(),
        PathItem {
            get: Some(Operation {
                responses: json_200(object(&[("status", SimpleType::String, true)])),
                ..Operation::default()
            }),
            ..PathItem::default()
        },
    );

    let mut matches_schema = Schema {
        schema_type: Some(SchemaType::Single(SimpleType::Object)),
        ..Schema::default()
    };
    matches_schema.properties.insert(
        "matches".to_string(),
        Schema {
            schema_type: Some(SchemaType::Single(SimpleType::Array)),
            items: Some(Box::new(Schema::of_type(SimpleType::String))),
            ..Schema::default()
        },
    );
    matches_schema.required.push("matches".to_string());
    doc.paths.insert(
        "/custom".to_string(),
        PathItem {
            get: Some(Operation {
                parameters: vec![Parameter {
                    name: "pattern".to_string(),
                    location: ParameterIn::Query,
                    required: false,
                    description: None,
                    schema: Some(Schema::of_type(SimpleType::String)),
                }],
                responses: json_200(matches_schema),
                ..Operation::default()
            }),
            ..PathItem::default()
        },
    );

    doc.paths.insert(
        "/custom/{word}".to_string(),
        PathItem {
            get: Some(Operation {
                responses: json_200(object(&[
                    ("word", SimpleType::String, true),
                    ("exists", SimpleType::Boolean, true),
                ])),
                ..Operation::default()
            }),
            post: Some(Operation {
                responses: json_200(object(&[
                    ("word", SimpleType::String, true),
                    ("added", SimpleType::Boolean, true),
                ])),
                ..Operation::default()
            }),
            ..PathItem::default()
        },
    );

    doc
}

fn json_ct() -> Vec<HeaderField> {
    vec![HeaderField {
        name: "Content-Type".into(),
        value: Some("application/json".into()),
    }]
}

fn obj_body(fields: &[(&str, BodyShape, bool)]) -> BodyShape {
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

fn temp_dir() -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let dir = std::env::temp_dir().join(format!("cartographer-d5-{}-{nanos}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("create temp dir");
    dir
}

#[test]
fn validation_over_fixture_files_produces_the_expected_diagnostics() {
    let dir = temp_dir();

    // Write the spec as YAML and load it back.
    let spec_path = dir.join("merriam.yaml");
    std::fs::write(&spec_path, serialize_document(&spec(), SpecFormat::Yaml).unwrap()).unwrap();
    let loaded = load_path(&spec_path);
    assert!(loaded.errors.is_empty(), "{:?}", loaded.errors);
    let documents: Vec<Document> = loaded.specs.into_iter().map(|s| s.document).collect();

    // A journal mixing conforming and violating interactions.
    let mut records = vec![
        record("GET", "/healthz", 200, obj_body(&[("status", BodyShape::String, true)])),
        record(
            "GET",
            "/custom/cat",
            200,
            obj_body(&[("word", BodyShape::String, true), ("exists", BodyShape::Bool, true)]),
        ),
        record(
            "POST",
            "/custom/dog",
            200,
            obj_body(&[("word", BodyShape::String, true), ("added", BodyShape::Bool, true)]),
        ),
        // Undocumented path.
        record("GET", "/admin", 200, BodyShape::Empty),
        // Undeclared status (DELETE/500 not declared; in fact DELETE not declared at all).
        record("DELETE", "/custom/cat", 500, BodyShape::Empty),
        // Response schema mismatch: missing the required `exists`.
        record("GET", "/custom/mouse", 200, obj_body(&[("word", BodyShape::String, true)])),
    ];
    // An enumerate with a declared query parameter (clean).
    let mut enumerate = record(
        "GET",
        "/custom",
        200,
        obj_body(&[("matches", BodyShape::Array(Box::new(BodyShape::String)), true)]),
    );
    enumerate.query = vec![QueryParam {
        name: "pattern".into(),
        value: BodyShape::String,
    }];
    records.push(enumerate);

    // Persist the journal as NDJSON and read it back.
    let journal_path = dir.join("capture.ndjson");
    {
        let mut writer = BufWriter::new(File::create(&journal_path).unwrap());
        for r in &records {
            write_record(&mut writer, r).unwrap();
        }
    }
    let (loaded_records, stats) = read_records(BufReader::new(File::open(&journal_path).unwrap()));
    assert_eq!(stats.malformed, 0);
    assert_eq!(loaded_records.len(), records.len());

    let index = SpecIndex::from_documents(&documents);
    let diagnostics = validate_stream(&index, &loaded_records);

    std::fs::remove_dir_all(&dir).ok();

    // The expected findings: undocumented /admin, undocumented DELETE operation,
    // and the response-schema mismatch on /custom/{word}.
    let codes: Vec<DiagnosticCode> = diagnostics.iter().map(|d| d.code).collect();
    assert!(
        codes.contains(&DiagnosticCode::UndocumentedPath),
        "{diagnostics:?}"
    );
    assert!(
        codes.contains(&DiagnosticCode::UndocumentedOperation),
        "{diagnostics:?}"
    );
    assert!(
        codes.contains(&DiagnosticCode::ResponseSchemaMismatch),
        "{diagnostics:?}"
    );
    // The four conforming interactions contributed nothing.
    assert_eq!(diagnostics.len(), 3, "{diagnostics:?}");

    // Rendering works in both modes.
    let mut text = BufferSink::new();
    render_diagnostics(&diagnostics, ReportFormat::Text, &mut text);
    assert_eq!(text.lines().len(), 3);

    let mut ndjson = BufferSink::new();
    render_diagnostics(&diagnostics, ReportFormat::Ndjson, &mut ndjson);
    for line in ndjson.lines() {
        let _: serde_json::Value = serde_json::from_str(line).expect("valid ndjson");
    }
}

#[test]
fn a_clean_journal_yields_no_diagnostics() {
    let documents = vec![spec()];
    let index = SpecIndex::from_documents(&documents);
    let records = vec![
        record("GET", "/healthz", 200, obj_body(&[("status", BodyShape::String, true)])),
        record(
            "GET",
            "/custom/cat",
            200,
            obj_body(&[("word", BodyShape::String, true), ("exists", BodyShape::Bool, true)]),
        ),
    ];
    assert!(validate_stream(&index, &records).is_empty());
}
