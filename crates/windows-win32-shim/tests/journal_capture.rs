// Copyright (c) Microsoft Corporation.

//! AJ-B5 milestone integration: a real [`ShimSession`] built from a `.pilcfg`
//! with the `api_journal` block enabled journals **both** seams — outbound
//! WinHTTP egress and inbound IIS — to one shared NDJSON file.
//!
//! This is hermetic and deterministic: egress runs in `buffer` mode, so the
//! outbound POST is acknowledged synthetically (HTTP 202) with no network, and
//! the inbound exchange is driven directly through the per-request handler stack.
//! It needs no aliased binary, no live `merriam`, and no `http.sys` reservation,
//! so it runs as an ordinary (non-`#[ignore]`) test. The aliased / live
//! end-to-end proof remains available as `egressrelayproof`.

#![cfg(windows)]

use std::fs::File;
use std::io::BufReader;
use std::time::{SystemTime, UNIX_EPOCH};

use api_journal::{Seam, read_records};
use windows_platform_isolation::{HttpRequest, HttpResponse, Utf16};
use windows_win32_shim::{ApiJournalConfig, EgressConfig, EgressMode, Pilcfg, ShimSession};

fn temp_journal_path() -> std::path::PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    std::env::temp_dir().join(format!(
        "journal-capture-{}-{nanos}.ndjson",
        std::process::id()
    ))
}

#[test]
fn session_journals_both_seams_to_one_shared_file() {
    let path = temp_journal_path();

    // A session with journaling enabled and egress in buffer mode (a mutating
    // POST is captured and acked synthetically, so no network is contacted).
    let cfg = Pilcfg {
        egress: EgressConfig {
            mode: EgressMode::Buffer,
            ..Default::default()
        },
        api_journal: ApiJournalConfig {
            enabled: true,
            path: path.to_string_lossy().into_owned(),
            ..Default::default()
        },
        ..Default::default()
    };
    let session = ShimSession::from_config(cfg);

    // --- Outbound (egress) seam: drive a POST through the WinHTTP engine. The
    // buffered backing acks it (HTTP 202) without touching the network, and the
    // journaling decorator records the exchange.
    session.with_egress(|engine| {
        let session_handle = engine.open();
        let connection = engine
            .connect(session_handle, Utf16::from_utf8("merriam.local"), 8080)
            .expect("connection handle");
        let request = engine
            .open_request(
                connection,
                Utf16::from_utf8("POST"),
                Utf16::from_utf8("/custom/cat?pattern=c.t"),
                false,
            )
            .expect("request handle");
        engine.add_headers(
            request,
            vec![(
                Utf16::from_utf8("X-Wordy-User"),
                Utf16::from_utf8("alice"),
            )],
        );
        engine
            .send(request, Vec::new(), br#"{"words":["a"]}"#.to_vec())
            .expect("buffered send is acked");
    });

    // --- Inbound seam: drive a request/response through the per-request handler
    // stack the IIS module would build per request.
    session.with_web(|web| {
        let mut handler = web.build_handler();
        handler.on_begin_request(&HttpRequest::new("GET", "/healthz"));
        handler.on_send_response(&mut HttpResponse::new(200));
    });

    // Both seams wrote to the one configured file. Flush the journal writer so the
    // queued records are on disk before reading (JW-2).
    session.flush_journal();
    let file = File::open(&path).expect("journal file exists");
    let (records, stats) = read_records(BufReader::new(file));
    let _ = std::fs::remove_file(&path);

    assert_eq!(stats.malformed, 0, "no malformed lines");
    assert_eq!(records.len(), 2, "exactly one egress + one inbound record");

    // Driven egress-first, inbound-second.
    let egress = &records[0];
    let inbound = &records[1];

    assert_eq!(egress.seam, Seam::Egress);
    assert_eq!(egress.method, "POST");
    assert!(egress.host.as_ref().is_some_and(|h| h == "merriam.local"));
    assert_eq!(egress.port, Some(8080));
    assert_eq!(egress.path, "/custom/cat");
    assert_eq!(egress.query.len(), 1);
    assert_eq!(egress.query[0].name, "pattern");
    assert_eq!(egress.status, 202, "buffered POST is acked synthetically");

    assert_eq!(inbound.seam, Seam::Inbound);
    assert_eq!(inbound.method, "GET");
    assert_eq!(inbound.path, "/healthz");
    assert_eq!(inbound.status, 200);
    // Inbound records carry no destination authority.
    assert_eq!(inbound.host, None);
    assert_eq!(inbound.scheme, None);
    assert_eq!(inbound.port, None);

    // One process-wide sink: both records share a session id and have monotonic,
    // gap-free sequence numbers in drive order.
    assert_eq!(
        egress.session_id, inbound.session_id,
        "both seams share one journal sink"
    );
    assert_eq!(egress.seq, 0);
    assert_eq!(inbound.seq, 1);
}
