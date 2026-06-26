// Copyright (c) Microsoft Corporation.

//! AJ-E5 end-to-end: feed a realistic merriam + wordy journal (empty baseline)
//! into `cartographer --update`, assert the synthesized OpenAPI 3.1 document
//! covers the expected paths / operations / statuses, and prove the round-trip is
//! consistent — the synthesized spec validates the very journal it was built from
//! with no findings.

use std::collections::BTreeMap;
use std::fs::File;
use std::io::{BufReader, BufWriter};
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use api_journal::{
    BodyShape, Field, HeaderField, JournalRecord, QueryParam, Seam, read_records, write_record,
};
use cartographer::{
    Args, ReportFormat, SpecFormat, SpecIndex, cli, parse_document, validate_stream,
};

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

fn array(element: BodyShape) -> BodyShape {
    BodyShape::Array(Box::new(element))
}

/// A response-only inbound record.
fn resp(method: &str, path: &str, status: u16, body: BodyShape) -> JournalRecord {
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

/// A record that also carries a request body.
fn with_request(mut record: JournalRecord, body: BodyShape) -> JournalRecord {
    record.request_headers = json_ct();
    record.request_body = body;
    record
}

/// A record with a query parameter.
fn with_query(mut record: JournalRecord, name: &str, value: BodyShape) -> JournalRecord {
    record.query.push(QueryParam {
        name: name.into(),
        value,
    });
    record
}

/// The full observed surface of merriam + wordy.
fn journal() -> Vec<JournalRecord> {
    let exists = || obj(&[("word", BodyShape::String, true), ("exists", BodyShape::Bool, true)]);
    let added = || obj(&[("word", BodyShape::String, true), ("added", BodyShape::Bool, true)]);
    let removed = || obj(&[("word", BodyShape::String, true), ("removed", BodyShape::Bool, true)]);
    let matches = || obj(&[("matches", array(BodyShape::String), true)]);

    vec![
        resp("GET", "/healthz", 200, obj(&[("status", BodyShape::String, true)])),
        with_query(resp("GET", "/custom", 200, matches()), "pattern", BodyShape::String),
        resp("GET", "/custom/cat", 200, exists()),
        resp("GET", "/custom/dog", 200, exists()),
        resp("POST", "/custom/cat", 200, added()),
        resp("POST", "/custom/dog", 200, added()),
        resp("DELETE", "/custom/cat", 200, removed()),
        resp("DELETE", "/custom/dog", 200, removed()),
        with_request(
            resp(
                "POST",
                "/spellcheck",
                200,
                obj(&[(
                    "results",
                    array(obj(&[
                        ("word", BodyShape::String, true),
                        ("correct", BodyShape::Bool, true),
                        ("suggestions", array(BodyShape::String), true),
                    ])),
                    true,
                )]),
            ),
            obj(&[("words", array(BodyShape::String), true)]),
        ),
        with_request(
            resp("POST", "/anagram", 200, matches()),
            obj(&[
                ("template", BodyShape::String, true),
                ("tray", BodyShape::String, true),
                ("wildcards", BodyShape::Integer, true),
            ]),
        ),
        with_query(resp("GET", "/shared", 200, matches()), "pattern", BodyShape::String),
    ]
}

fn temp_dir() -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let dir = std::env::temp_dir().join(format!("cartographer-e2e-{}-{nanos}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("create temp dir");
    dir
}

#[test]
fn synthesizes_a_spec_that_covers_and_revalidates_the_journal() {
    let dir = temp_dir();
    let records = journal();

    // Persist the journal as NDJSON (as it would arrive off-machine).
    let journal_path = dir.join("capture.ndjson");
    {
        let mut writer = BufWriter::new(File::create(&journal_path).unwrap());
        for record in &records {
            write_record(&mut writer, record).unwrap();
        }
    }

    // Run the tool: empty baseline, synthesize and write YAML.
    let out_dir = dir.join("out");
    let args = Args {
        journals: vec![journal_path.clone()],
        out: Some(out_dir.clone()),
        update: true,
        format: SpecFormat::Yaml,
        report: ReportFormat::Text,
        ..Args::default()
    };
    let mut sink = cartographer::BufferSink::new();
    let code = cli::run(&args, &mut sink);
    assert_eq!(code, 0, "run failed: {:?}", sink.lines());
    assert!(sink.lines().iter().any(|l| l.starts_with("wrote ")));

    // Load and parse the synthesized spec.
    let written = out_dir.join("openapi.yaml");
    assert!(written.is_file());
    let text = std::fs::read_to_string(&written).unwrap();
    let document = parse_document(&text, SpecFormat::Yaml).expect("parse synthesized spec");

    // Expected surface.
    assert_eq!(document.openapi, "3.1.0");
    assert!(document.paths.contains_key("/healthz"));
    assert!(document.paths.contains_key("/custom"));
    assert!(document.paths.contains_key("/custom/{id}"));
    assert!(document.paths.contains_key("/spellcheck"));
    assert!(document.paths.contains_key("/anagram"));
    assert!(document.paths.contains_key("/shared"));

    // /healthz GET 200.
    assert!(document.paths["/healthz"]
        .operation("GET")
        .map(|op| op.responses.contains_key("200"))
        .unwrap_or(false));

    // /custom GET 200 with a `pattern` query parameter.
    let custom_get = document.paths["/custom"].operation("GET").expect("GET /custom");
    assert!(custom_get.responses.contains_key("200"));
    assert!(
        custom_get
            .parameters
            .iter()
            .any(|p| p.name == "pattern"),
        "pattern query param synthesized"
    );

    // /custom/{id} declares GET, POST, DELETE — each returning 200 — and a path param.
    let item = &document.paths["/custom/{id}"];
    for method in ["GET", "POST", "DELETE"] {
        let op = item.operation(method).unwrap_or_else(|| panic!("missing {method}"));
        assert!(op.responses.contains_key("200"), "{method} 200");
    }
    assert!(
        item.parameters.iter().any(|p| p.name == "id"),
        "path parameter synthesized"
    );

    // /spellcheck POST 200 with a request body.
    let spellcheck = document.paths["/spellcheck"].operation("POST").expect("POST /spellcheck");
    assert!(spellcheck.responses.contains_key("200"));
    assert!(spellcheck.request_body.is_some(), "request body synthesized");

    // /anagram POST 200 and /shared GET 200.
    assert!(document.paths["/anagram"]
        .operation("POST")
        .map(|op| op.responses.contains_key("200"))
        .unwrap_or(false));
    assert!(document.paths["/shared"]
        .operation("GET")
        .map(|op| op.responses.contains_key("200"))
        .unwrap_or(false));

    // Round-trip: read the journal back and validate it against the SYNTHESIZED
    // spec — it must now be fully documented (no findings).
    let (recovered, stats) = read_records(BufReader::new(File::open(&journal_path).unwrap()));
    assert_eq!(stats.malformed, 0);
    let index = SpecIndex::from_documents(std::slice::from_ref(&document));
    let diagnostics = validate_stream(&index, &recovered);
    assert!(
        diagnostics.is_empty(),
        "the synthesized spec should fully document its own journal, got: {diagnostics:?}"
    );

    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn full_mode_examples_flow_into_the_synthesized_spec() {
    let dir = temp_dir();

    // A journal captured under `bodies: full-with-pii`: records carry literal example bodies.
    let mut get = resp(
        "GET",
        "/custom/cat",
        200,
        obj(&[
            ("word", BodyShape::String, true),
            ("exists", BodyShape::Bool, true),
        ]),
    );
    get.response_body_example = Some(serde_json::json!({ "word": "cat", "exists": true }));

    let mut post = with_request(
        resp("POST", "/spellcheck", 200, obj(&[("ok", BodyShape::Bool, true)])),
        obj(&[("words", array(BodyShape::String), true)]),
    );
    post.request_body_example = Some(serde_json::json!({ "words": ["cat"] }));
    post.response_body_example = Some(serde_json::json!({ "ok": true }));

    let records = vec![get, post];

    let journal_path = dir.join("full.ndjson");
    {
        let mut writer = BufWriter::new(File::create(&journal_path).unwrap());
        for record in &records {
            write_record(&mut writer, record).unwrap();
        }
    }

    let out_dir = dir.join("out");
    let args = Args {
        journals: vec![journal_path],
        out: Some(out_dir.clone()),
        update: true,
        format: SpecFormat::Yaml,
        report: ReportFormat::Text,
        ..Args::default()
    };
    let mut sink = cartographer::BufferSink::new();
    assert_eq!(cli::run(&args, &mut sink), 0, "{:?}", sink.lines());

    let text = std::fs::read_to_string(out_dir.join("openapi.yaml")).unwrap();
    let document = parse_document(&text, SpecFormat::Yaml).expect("parse");

    // The response example survived into the spec.
    let get_media = &document.paths["/custom/cat"].operation("GET").unwrap().responses["200"]
        .content["application/json"];
    assert_eq!(
        get_media.example,
        Some(serde_json::json!({ "word": "cat", "exists": true }))
    );

    // Both the request and response examples for the POST survived.
    let post_op = document.paths["/spellcheck"].operation("POST").unwrap();
    let request_media = &post_op.request_body.as_ref().unwrap().content["application/json"];
    assert_eq!(
        request_media.example,
        Some(serde_json::json!({ "words": ["cat"] }))
    );
    assert_eq!(
        post_op.responses["200"].content["application/json"].example,
        Some(serde_json::json!({ "ok": true }))
    );

    std::fs::remove_dir_all(&dir).ok();
}
