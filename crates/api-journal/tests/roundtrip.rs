// Copyright (c) Microsoft Corporation.

//! AJ-A5 milestone integration: round-trip several hundred varied [`JournalRecord`]s
//! through a real temporary NDJSON file (write via an append handle, read back), asserting
//! exact equality and that nothing was skipped or malformed.

use std::fs::{File, OpenOptions};
use std::io::{BufReader, BufWriter};
use std::time::{SystemTime, UNIX_EPOCH};

use api_journal::{
    BodyShape, HeaderField, JournalRecord, QueryParam, ReadStats, Seam, derive_example,
    read_records, write_record,
};

/// Build a deterministic but varied record from an index.
fn make_record(i: u64) -> JournalRecord {
    let seam = if i.is_multiple_of(2) { Seam::Egress } else { Seam::Inbound };
    let method = match i % 4 {
        0 => "GET",
        1 => "POST",
        2 => "DELETE",
        _ => "PUT",
    };
    let response_body = match i % 5 {
        0 => BodyShape::Empty,
        1 => BodyShape::derive(br#"{"status":"ok"}"#, Some("application/json")),
        2 => BodyShape::derive(br#"{"matches":["a","b","c"]}"#, Some("application/json")),
        3 => BodyShape::derive(br#"{"word":"cat","exists":true}"#, Some("application/json")),
        _ => BodyShape::derive(br#"[1,2,"x",{"k":null}]"#, Some("application/json")),
    };
    let (scheme, host, port) = if seam == Seam::Egress {
        (Some("http".into()), Some("merriam.local".into()), Some(8080))
    } else {
        (None, None, None)
    };
    let query = if i.is_multiple_of(3) {
        vec![QueryParam {
            name: "pattern".into(),
            value: BodyShape::String,
        }]
    } else {
        Vec::new()
    };
    JournalRecord {
        seam,
        method: method.into(),
        scheme,
        host,
        port,
        path: format!("/custom/word{i}").into(),
        query,
        request_headers: vec![HeaderField {
            name: "X-Wordy-User".into(),
            value: None,
        }],
        request_body: if method == "POST" {
            BodyShape::derive(br#"{"words":["a"]}"#, Some("application/json"))
        } else {
            BodyShape::Empty
        },
        status: match i % 4 {
            0 => 200,
            1 => 404,
            2 => 400,
            _ => 500,
        },
        response_headers: vec![HeaderField {
            name: "Content-Type".into(),
            value: Some("application/json".into()),
        }],
        response_body,
        request_body_example: if method == "POST" {
            derive_example(br#"{"words":["a"]}"#, Some("application/json"))
        } else {
            None
        },
        response_body_example: derive_example(br#"{"ok":true}"#, Some("application/json")),
        timestamp_ms: 1_700_000_000_000 + i,
        session_id: 0xABCD,
        seq: i,
    }
}

fn temp_path() -> std::path::PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    std::env::temp_dir().join(format!(
        "api-journal-roundtrip-{}-{}.ndjson",
        std::process::id(),
        nanos
    ))
}

#[test]
fn many_records_round_trip_through_a_real_file() {
    const COUNT: u64 = 500;
    let originals: Vec<JournalRecord> = (0..COUNT).map(make_record).collect();

    let path = temp_path();

    // Write each record through an append-mode handle, the way the shim sink will.
    {
        let file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)
            .expect("open for append");
        let mut writer = BufWriter::new(file);
        for record in &originals {
            write_record(&mut writer, record).expect("write record");
        }
    }

    // Read back through a buffered reader.
    let (recovered, stats) = {
        let file = File::open(&path).expect("open for read");
        read_records(BufReader::new(file))
    };

    let _ = std::fs::remove_file(&path);

    assert_eq!(stats, ReadStats::default(), "no lines should be skipped");
    assert_eq!(recovered.len(), originals.len());
    assert_eq!(recovered, originals, "records must round-trip byte-faithfully");
}
