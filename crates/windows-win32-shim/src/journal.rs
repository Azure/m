// Copyright (c) Microsoft Corporation.

//! The API-interaction journal sink (AJ-B2).
//!
//! When the `.pilcfg` `api_journal` block is enabled, the shim appends one NDJSON
//! [`JournalRecord`] per observed HTTP interaction to a file for later off-machine
//! post-processing by the `cartographer` tool. This module owns that writer.
//!
//! Three properties matter:
//!
//! - **Not aliased.** The sink writes through ordinary [`std::fs`]. Link-time
//!   aliasing redirects only the *client's* `WriteFile`/`CreateFileW` imports into
//!   the shim; the shim's own standard-library file I/O binds the real kernel32,
//!   so journaling never recurses back through `mWriteFile`.
//! - **Thread-safe and process-wide.** Host threads hit the egress and inbound
//!   seams concurrently; a single [`Mutex`]-guarded append handle serializes
//!   record writes so lines never interleave. The handle is opened lazily on the
//!   first record.
//! - **Fail-soft.** Like the rest of the shim (mwin32 D5), journaling must never
//!   break the host: a path that cannot be opened, or a write that fails, is
//!   silently dropped rather than propagated.

use std::fs::{File, OpenOptions};
use std::path::PathBuf;
use std::sync::Mutex;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

use api_journal::{
    BodyShape, HeaderField, JournalRecord, QueryParam, Seam, infer_scalar, write_record,
};
use windows_platform_isolation::{
    EgressRequest, EgressResponse, EgressResult, EgressSurface, Utf16,
};

use crate::pilcfg::{ApiJournalConfig, BodyCapture};

/// A process-wide, thread-safe NDJSON journal writer.
///
/// Construct via [`JournalSink::from_config`] (returns `None` when journaling is
/// disabled or no path is set) and share clones of the returned [`Arc`] with the
/// seam decorators.
#[derive(Debug)]
pub struct JournalSink {
    /// The destination NDJSON file (already `%VAR%`-expanded by pilcfg parsing).
    path: PathBuf,
    /// A per-process id stamped on every record so journals gathered from many
    /// processes/machines can be grouped and de-duplicated.
    session_id: u64,
    /// Monotonic per-process record sequence.
    seq: AtomicU64,
    /// The append handle, opened lazily on the first record.
    file: Mutex<Option<File>>,
    /// How much of each body to capture.
    bodies: BodyCapture,
    /// Maximum body bytes to inspect when deriving a shape.
    max_body_bytes: usize,
    /// Whether the inbound (IIS) seam should journal.
    capture_inbound: bool,
    /// Whether the outbound (WinHTTP egress) seam should journal.
    capture_egress: bool,
}

impl JournalSink {
    /// Build a sink from the `.pilcfg` `api_journal` configuration, or `None` when
    /// journaling is disabled or the destination path is empty.
    #[must_use]
    pub fn from_config(config: &ApiJournalConfig) -> Option<Arc<JournalSink>> {
        if !config.enabled || config.path.is_empty() {
            return None;
        }
        Some(Arc::new(JournalSink {
            path: PathBuf::from(&config.path),
            session_id: new_session_id(),
            seq: AtomicU64::new(0),
            file: Mutex::new(None),
            bodies: config.bodies,
            max_body_bytes: config.max_body_bytes,
            capture_inbound: config.capture_inbound,
            capture_egress: config.capture_egress,
        }))
    }

    /// Whether the inbound (IIS) seam should journal.
    #[must_use]
    pub fn capture_inbound(&self) -> bool {
        self.capture_inbound
    }

    /// Whether the outbound (WinHTTP egress) seam should journal.
    #[must_use]
    pub fn capture_egress(&self) -> bool {
        self.capture_egress
    }

    /// Derive a [`BodyShape`] from raw body bytes per the configured capture mode.
    ///
    /// `None` mode yields [`BodyShape::Unknown`] (the interaction is still
    /// journaled, just without body structure). `Shapes` and `Full` both derive a
    /// shapes-only skeleton today (full literal-example capture is a deferred
    /// enhancement — see `DESIGN-NOTES.md`), inspecting at most `max_body_bytes`.
    #[must_use]
    pub fn body_shape(&self, bytes: &[u8], content_type: Option<&str>) -> BodyShape {
        body_shape_for(self.bodies, bytes, content_type, self.max_body_bytes)
    }

    /// Stamp bookkeeping fields onto a record and append it to the journal.
    ///
    /// Fills in `session_id`, the next `seq`, and (when unset) `timestamp_ms`,
    /// then writes one NDJSON line. All I/O errors are swallowed (fail-soft).
    pub fn record(&self, mut record: JournalRecord) {
        record.session_id = self.session_id;
        record.seq = self.seq.fetch_add(1, Ordering::Relaxed);
        if record.timestamp_ms == 0 {
            record.timestamp_ms = now_ms();
        }

        // Tolerate a poisoned mutex: a prior panic while holding the lock must not
        // disable journaling for the rest of the process.
        let mut guard = self.file.lock().unwrap_or_else(|poison| poison.into_inner());
        if guard.is_none() {
            *guard = OpenOptions::new()
                .create(true)
                .append(true)
                .open(&self.path)
                .ok();
        }
        if let Some(file) = guard.as_mut() {
            // Fail-soft: a write error drops this record but keeps the host alive.
            let _ = write_record(file, &record);
        }
    }
}

/// Derive a [`BodyShape`] from raw bytes per a capture mode (free function so the
/// seam decorators can call it without a sink in unit tests).
#[must_use]
pub fn body_shape_for(
    mode: BodyCapture,
    bytes: &[u8],
    content_type: Option<&str>,
    max_body_bytes: usize,
) -> BodyShape {
    match mode {
        BodyCapture::None => BodyShape::Unknown,
        // Shapes and Full both derive a shapes-only skeleton for now; a body
        // larger than the cap is inspected only up to the cap (and a JSON body
        // truncated mid-token will simply read as opaque).
        BodyCapture::Shapes | BodyCapture::Full => {
            let slice = if bytes.len() > max_body_bytes {
                &bytes[..max_body_bytes]
            } else {
                bytes
            };
            BodyShape::derive(slice, content_type)
        }
    }
}

/// Milliseconds since the Unix epoch, or 0 if the clock is unavailable.
fn now_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| u64::try_from(d.as_millis()).unwrap_or(u64::MAX))
        .unwrap_or(0)
}

/// A best-effort per-process identifier mixing the pid and process start time.
fn new_session_id() -> u64 {
    let pid = u64::from(std::process::id());
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| u64::try_from(d.as_nanos()).unwrap_or(u64::MAX))
        .unwrap_or(0);
    (nanos << 16) ^ pid
}

// === Egress journaling decorator (AJ-B3) ===

/// HTTP header names whose literal values are retained (content negotiation);
/// every other header keeps only its name (api-journal D-AJ-2).
const CONTENT_NEGOTIATION_HEADERS: [&str; 2] = ["content-type", "accept"];

/// An [`EgressSurface`] decorator that, when a sink is present, journals each
/// successful outbound request/response as a [`Seam::Egress`] record.
///
/// With no sink it is a zero-overhead passthrough, so the session can wrap its
/// egress stack unconditionally and let the `.pilcfg` decide whether anything is
/// written.
pub struct JournalingEgress<S: EgressSurface> {
    inner: S,
    sink: Option<Arc<JournalSink>>,
}

impl<S: EgressSurface> JournalingEgress<S> {
    /// Wrap `inner`, journaling through `sink` when present.
    #[must_use]
    pub fn new(inner: S, sink: Option<Arc<JournalSink>>) -> Self {
        Self { inner, sink }
    }

    /// Borrow the wrapped surface (e.g. to inspect a buffer's journal in tests).
    pub fn inner(&mut self) -> &mut S {
        &mut self.inner
    }

    /// Recover the wrapped surface.
    pub fn into_inner(self) -> S {
        self.inner
    }
}

impl<S: EgressSurface> EgressSurface for JournalingEgress<S> {
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        let result = self.inner.send(req);
        // Journal only real exchanges: a transport error is not an API interaction.
        if let (Some(sink), Ok(response)) = (&self.sink, &result) {
            sink.record(egress_record(sink, req, response));
        }
        result
    }
}

/// Build a [`Seam::Egress`] record from a request/response pair. The bookkeeping
/// fields (`session_id`/`seq`/`timestamp_ms`) are stamped by
/// [`JournalSink::record`].
fn egress_record(sink: &JournalSink, req: &EgressRequest, resp: &EgressResponse) -> JournalRecord {
    let raw_path = req.path.to_utf8().unwrap_or_default();
    let (path, query) = split_path_query(&raw_path);
    let (request_headers, request_ct) = egress_header_fields(&req.headers);
    let (response_headers, response_ct) = egress_header_fields(&resp.headers);
    JournalRecord {
        seam: Seam::Egress,
        method: req.verb.to_utf8().unwrap_or_else(|_| "?".to_string()),
        scheme: Some(req.scheme.as_str().to_string()),
        host: req.host.to_utf8().ok(),
        port: Some(req.port),
        path,
        query,
        request_headers,
        request_body: sink.body_shape(&req.body, request_ct.as_deref()),
        status: u16::try_from(resp.status).unwrap_or(0),
        response_headers,
        response_body: sink.body_shape(&resp.body, response_ct.as_deref()),
        ..Default::default()
    }
}

/// Split a raw request target into its path and query parameters. Query values
/// are reduced to a scalar *shape* (not the literal value).
fn split_path_query(raw: &str) -> (String, Vec<QueryParam>) {
    match raw.split_once('?') {
        None => (raw.to_string(), Vec::new()),
        Some((path, query)) => {
            let params = query
                .split('&')
                .filter(|pair| !pair.is_empty())
                .map(|pair| {
                    let (name, value) = pair.split_once('=').unwrap_or((pair, ""));
                    QueryParam {
                        name: name.to_string(),
                        value: infer_scalar(value),
                    }
                })
                .collect();
            (path.to_string(), params)
        }
    }
}

/// Convert egress `(name, value)` header pairs into journal [`HeaderField`]s,
/// retaining literal values only for content-negotiation headers, and return the
/// observed `Content-Type` (used to key body-shape derivation).
fn egress_header_fields(headers: &[(Utf16, Utf16)]) -> (Vec<HeaderField>, Option<String>) {
    let mut fields = Vec::with_capacity(headers.len());
    let mut content_type = None;
    for (name, value) in headers {
        let name = name.to_utf8().unwrap_or_else(|_| "?".to_string());
        let lower = name.to_ascii_lowercase();
        let retained = if CONTENT_NEGOTIATION_HEADERS.contains(&lower.as_str()) {
            value.to_utf8().ok()
        } else {
            None
        };
        if lower == "content-type" {
            content_type = retained.clone();
        }
        fields.push(HeaderField { name, value: retained });
    }
    (fields, content_type)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::BufReader;
    use std::sync::Arc;
    use std::thread;

    use api_journal::{ReadStats, Seam, read_records};
    use windows_platform_isolation::{EgressError, Scheme};

    fn temp_path(tag: &str) -> PathBuf {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        std::env::temp_dir().join(format!(
            "shim-journal-{tag}-{}-{nanos}.ndjson",
            std::process::id()
        ))
    }

    fn config(path: &PathBuf) -> ApiJournalConfig {
        ApiJournalConfig {
            enabled: true,
            path: path.to_string_lossy().into_owned(),
            ..Default::default()
        }
    }

    fn sample(seam: Seam, path: &str) -> JournalRecord {
        JournalRecord {
            seam,
            method: "GET".into(),
            path: path.into(),
            status: 200,
            ..Default::default()
        }
    }

    #[test]
    fn disabled_or_pathless_config_yields_no_sink() {
        let disabled = ApiJournalConfig {
            enabled: false,
            path: "x.ndjson".into(),
            ..Default::default()
        };
        assert!(JournalSink::from_config(&disabled).is_none());

        let pathless = ApiJournalConfig {
            enabled: true,
            path: String::new(),
            ..Default::default()
        };
        assert!(JournalSink::from_config(&pathless).is_none());
    }

    #[test]
    fn records_are_appended_and_stamped() {
        let path = temp_path("stamp");
        let sink = JournalSink::from_config(&config(&path)).expect("enabled sink");

        sink.record(sample(Seam::Egress, "/a"));
        sink.record(sample(Seam::Inbound, "/b"));

        let file = File::open(&path).expect("journal exists");
        let (records, stats) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(stats, ReadStats::default());
        assert_eq!(records.len(), 2);
        // seq increments from 0; session_id is constant and non-zero stamping
        // happened (timestamp set).
        assert_eq!(records[0].seq, 0);
        assert_eq!(records[1].seq, 1);
        assert_eq!(records[0].session_id, records[1].session_id);
        assert!(records[0].timestamp_ms > 0);
        assert_eq!(records[0].path, "/a");
        assert_eq!(records[1].path, "/b");
    }

    #[test]
    fn file_is_created_lazily_on_first_record() {
        let path = temp_path("lazy");
        let sink = JournalSink::from_config(&config(&path)).expect("enabled sink");
        assert!(!path.exists(), "no file before the first record");
        sink.record(sample(Seam::Egress, "/healthz"));
        assert!(path.exists(), "file created on first record");
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn concurrent_writes_do_not_interleave_or_lose_records() {
        let path = temp_path("concurrent");
        let sink = JournalSink::from_config(&config(&path)).expect("enabled sink");

        let mut handles = Vec::new();
        for t in 0..8 {
            let sink: Arc<JournalSink> = Arc::clone(&sink);
            handles.push(thread::spawn(move || {
                for i in 0..50 {
                    sink.record(sample(Seam::Egress, &format!("/t{t}/i{i}")));
                }
            }));
        }
        for h in handles {
            h.join().expect("thread joined");
        }

        let file = File::open(&path).expect("journal exists");
        let (records, stats) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        // Every record parsed cleanly (no torn lines) and none were lost.
        assert_eq!(stats, ReadStats::default());
        assert_eq!(records.len(), 8 * 50);
    }

    #[test]
    fn unopenable_path_is_fail_soft() {
        // A path under a directory that does not exist cannot be opened; recording
        // must not panic and must not create anything.
        let mut path = std::env::temp_dir();
        path.push("shim-journal-nonexistent-dir-xyz");
        path.push("nested");
        path.push("api.ndjson");
        let sink = JournalSink::from_config(&config(&path)).expect("enabled sink");
        sink.record(sample(Seam::Egress, "/a")); // must not panic
        assert!(!path.exists());
    }

    #[test]
    fn body_shape_modes() {
        let json = br#"{"word":"cat","exists":true}"#;
        // None → Unknown.
        assert_eq!(
            body_shape_for(BodyCapture::None, json, Some("application/json"), 4096),
            BodyShape::Unknown
        );
        // Shapes → derived object skeleton.
        let shaped = body_shape_for(BodyCapture::Shapes, json, Some("application/json"), 4096);
        assert!(matches!(shaped, BodyShape::Object(_)));
        // Full currently behaves like Shapes.
        let full = body_shape_for(BodyCapture::Full, json, Some("application/json"), 4096);
        assert_eq!(shaped, full);
        // Empty body → Empty.
        assert_eq!(
            body_shape_for(BodyCapture::Shapes, b"", None, 4096),
            BodyShape::Empty
        );
    }

    #[test]
    fn body_shape_truncates_oversized_bodies() {
        // A JSON body larger than the cap is cut mid-token and reads as opaque.
        let big = br#"{"a":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}"#;
        let shape = body_shape_for(BodyCapture::Shapes, big, Some("application/json"), 5);
        assert_eq!(shape, BodyShape::Opaque);
    }

    fn canned(status: u32, ctype: &str, body: &[u8]) -> EgressResponse {
        EgressResponse {
            status,
            headers: vec![(Utf16::from_utf8("Content-Type"), Utf16::from_utf8(ctype))],
            body: body.to_vec(),
        }
    }

    struct CannedEgress {
        ok: Option<EgressResponse>,
    }
    impl EgressSurface for CannedEgress {
        fn send(&mut self, _req: &EgressRequest) -> EgressResult<EgressResponse> {
            match &self.ok {
                Some(r) => Ok(r.clone()),
                None => Err(EgressError::IllFormedUtf16),
            }
        }
    }

    #[test]
    fn egress_decorator_journals_a_successful_exchange() {
        let path = temp_path("egress");
        let sink = JournalSink::from_config(&config(&path)).expect("sink");
        let inner = CannedEgress {
            ok: Some(canned(200, "application/json", br#"{"word":"cat","exists":true}"#)),
        };
        let mut deco = JournalingEgress::new(inner, Some(Arc::clone(&sink)));

        let mut req = EgressRequest::http(
            Scheme::Http,
            "merriam.local",
            8080,
            "GET",
            "/custom/cat?pattern=c.t",
        );
        req.headers
            .push((Utf16::from_utf8("X-Wordy-User"), Utf16::from_utf8("alice")));
        deco.send(&req).expect("send ok");

        let file = File::open(&path).expect("journal exists");
        let (records, stats) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(stats, ReadStats::default());
        assert_eq!(records.len(), 1);
        let r = &records[0];
        assert_eq!(r.seam, Seam::Egress);
        assert_eq!(r.method, "GET");
        assert_eq!(r.scheme.as_deref(), Some("http"));
        assert_eq!(r.host.as_deref(), Some("merriam.local"));
        assert_eq!(r.port, Some(8080));
        assert_eq!(r.path, "/custom/cat");
        assert_eq!(
            r.query,
            vec![QueryParam {
                name: "pattern".into(),
                value: BodyShape::String
            }]
        );
        // The non-safelisted identity header keeps its name but not its value.
        let user = r
            .request_headers
            .iter()
            .find(|h| h.name == "X-Wordy-User")
            .expect("header present");
        assert_eq!(user.value, None);
        assert_eq!(r.status, 200);
        assert_eq!(r.response_content_type(), Some("application/json"));
        assert!(matches!(r.response_body, BodyShape::Object(_)));
    }

    #[test]
    fn egress_decorator_without_sink_is_passthrough() {
        let inner = CannedEgress {
            ok: Some(EgressResponse::new(200, Vec::new())),
        };
        let mut deco = JournalingEgress::new(inner, None);
        let req = EgressRequest::http(Scheme::Http, "h", 80, "GET", "/x");
        assert!(deco.send(&req).is_ok());
    }

    #[test]
    fn egress_decorator_does_not_journal_transport_errors() {
        let path = temp_path("egress-err");
        let sink = JournalSink::from_config(&config(&path)).expect("sink");
        let mut deco = JournalingEgress::new(CannedEgress { ok: None }, Some(sink));
        let req = EgressRequest::http(Scheme::Http, "h", 80, "GET", "/x");
        assert!(deco.send(&req).is_err());
        assert!(!path.exists(), "a failed send must journal nothing");
    }
}
