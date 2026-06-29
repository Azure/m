// Copyright (c) Microsoft Corporation.

//! Generate a sample `wordy` API journal and the OpenAPI spec cartographer
//! synthesizes from it — handy for eyeballing what capture + synthesis produce.
//!
//! ```text
//! cargo run -p cartographer --example wordy -- <out_dir>
//! ```
//!
//! Writes, into `<out_dir>` (default: the current directory):
//! - `wordy-journal.ndjson` — the captured journal (one interaction per line),
//! - `wordy-openapi.yaml` / `wordy-openapi.json` — the synthesized OpenAPI 3.1 doc,
//! - `wordy-environment.yaml` — the observed-environment descriptor (D-CART-4:
//!   actors / roles / channels), whose channels reference the OpenAPI doc.
//!
//! The records mirror what the win32 shim's inbound (IIS) seam captures for
//! `wordy` under `bodies: full-with-pii`: shapes-only structure plus a literal example
//! body. Identity headers (`X-Wordy-User`/`X-Wordy-Locale`) keep only their names,
//! exactly as the shapes-only capture policy retains them.

use std::fs::{File, create_dir_all};
use std::io::BufWriter;
use std::path::PathBuf;

use api_journal::{
    BodyShape, HeaderField, JournalRecord, QueryParam, Seam, derive_example, infer_scalar,
    write_record,
};
use cartographer::{
    SpecFormat, derive_environment, serialize_document, serialize_environment, synthesize,
};

/// The JSON content type used throughout `wordy`.
const JSON: &str = "application/json";

/// Derive both the shapes-only [`BodyShape`] and a literal example from a JSON body.
fn body(json: &str) -> (BodyShape, Option<serde_json::Value>) {
    let bytes = json.as_bytes();
    (
        BodyShape::derive(bytes, Some(JSON)),
        derive_example(bytes, Some(JSON)),
    )
}

fn header(name: &str, value: Option<&str>) -> HeaderField {
    HeaderField {
        name: name.into(),
        value: value.map(Into::into),
    }
}

/// Build one observed inbound interaction.
fn mk(
    method: &str,
    path: &str,
    query: &[(&str, &str)],
    request: Option<&str>,
    response: &str,
) -> JournalRecord {
    let (request_body, request_body_example) = match request {
        Some(json) => body(json),
        None => (BodyShape::Empty, None),
    };
    let (response_body, response_body_example) = body(response);

    // Identity headers are captured by name only (not on the content-negotiation
    // safelist); Content-Type values are retained.
    let mut request_headers = vec![header("X-Wordy-User", None), header("X-Wordy-Locale", None)];
    if request.is_some() {
        request_headers.push(header("Content-Type", Some(JSON)));
    }

    JournalRecord {
        seam: Seam::Inbound,
        method: method.into(),
        path: path.into(),
        query: query
            .iter()
            .map(|(name, sample)| QueryParam {
                name: (*name).into(),
                value: infer_scalar(sample),
            })
            .collect(),
        request_headers,
        request_body,
        request_body_example,
        status: 200,
        response_headers: vec![header("Content-Type", Some(JSON))],
        response_body,
        response_body_example,
        timestamp_ms: 1_700_000_000_000,
        session_id: 0x00C0_FFEE,
        seq: 0,
        ..Default::default()
    }
}

/// A representative journal covering `wordy`'s full route surface.
fn wordy_journal() -> Vec<JournalRecord> {
    let mut records = vec![
        mk("GET", "/healthz", &[], None, r#"{"status":"ok"}"#),
        mk(
            "POST",
            "/spellcheck",
            &[],
            Some(r#"{"words":["cat","dxg"]}"#),
            r#"{"results":[{"word":"cat","correct":true,"suggestions":[]},{"word":"dxg","correct":false,"suggestions":["dog","dig","dux"]}]}"#,
        ),
        mk(
            "POST",
            "/anagram",
            &[],
            Some(r#"{"template":"c.t","tray":"a","wildcards":0}"#),
            r#"{"matches":["cat","cot"]}"#,
        ),
        mk("GET", "/shared", &[("pattern", "c.t")], None, r#"{"matches":["cat","cot","cut"]}"#),
        mk("GET", "/custom", &[("pattern", ".*")], None, r#"{"matches":["frobnicate","wat"]}"#),
        mk("GET", "/custom/frobnicate", &[], None, r#"{"word":"frobnicate","exists":true}"#),
        mk("GET", "/custom/wat", &[], None, r#"{"word":"wat","exists":false}"#),
        mk("POST", "/custom/frobnicate", &[], None, r#"{"word":"frobnicate","added":true}"#),
        mk("POST", "/custom/wat", &[], None, r#"{"word":"wat","added":false}"#),
        mk("DELETE", "/custom/frobnicate", &[], None, r#"{"word":"frobnicate","removed":true}"#),
        mk("DELETE", "/custom/wat", &[], None, r#"{"word":"wat","removed":false}"#),
    ];
    for (seq, record) in records.iter_mut().enumerate() {
        record.seq = seq as u64;
    }
    records
}

fn main() {
    let out = std::env::args()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."));
    create_dir_all(&out).expect("create output directory");

    let records = wordy_journal();

    // Write the captured journal as NDJSON.
    let journal_path = out.join("wordy-journal.ndjson");
    {
        let mut writer = BufWriter::new(File::create(&journal_path).expect("create journal"));
        for record in &records {
            write_record(&mut writer, record).expect("write record");
        }
    }

    // Synthesize the OpenAPI document and write it in both formats.
    let document = synthesize(&records, &[]);
    std::fs::write(
        out.join("wordy-openapi.yaml"),
        serialize_document(&document, SpecFormat::Yaml).expect("serialize yaml"),
    )
    .expect("write yaml");
    std::fs::write(
        out.join("wordy-openapi.json"),
        serialize_document(&document, SpecFormat::Json).expect("serialize json"),
    )
    .expect("write json");

    // Derive the observed-environment descriptor (actors / roles / channels), whose
    // channels point their contract at the OpenAPI doc above, and write it as YAML.
    let environment = derive_environment(&records, Some("wordy-openapi.yaml"));
    std::fs::write(
        out.join("wordy-environment.yaml"),
        serialize_environment(&environment, SpecFormat::Yaml).expect("serialize environment"),
    )
    .expect("write environment");

    println!(
        "wrote {} journal records, the synthesized spec, and the environment descriptor to {}",
        records.len(),
        out.display()
    );
}
