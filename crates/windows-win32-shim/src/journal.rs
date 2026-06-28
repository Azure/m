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
    Disposition, EgressRequest, EgressResponse, EgressResult, EgressSurface, Header, HttpRequest,
    HttpResponse, RequestHandler, Utf16,
};
use windows_threadpool::{WaitGate, submit_once};

use crate::marshal::{Interaction, Outcome, RawHeader};
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
    /// journaled, just without body structure). `Shapes` and `FullWithPii` both derive a
    /// shapes-only skeleton, inspecting at most `max_body_bytes`; `FullWithPii`
    /// additionally captures a literal example via [`body_example`](Self::body_example).
    #[must_use]
    pub fn body_shape(&self, bytes: &[u8], content_type: Option<&str>) -> BodyShape {
        body_shape_for(self.bodies, bytes, content_type, self.max_body_bytes)
    }

    /// Capture a literal example body for `bodies: full-with-pii`, else `None`.
    ///
    /// Under [`BodyCapture::FullWithPii`] this returns the parsed JSON body (the
    /// shapes-only skeleton is still produced by [`body_shape`](Self::body_shape));
    /// under `Shapes`/`None` it returns `None`. Examples are literal user data,
    /// captured only under the opt-in `full-with-pii` mode, and only up to `max_body_bytes`.
    #[must_use]
    pub fn body_example(
        &self,
        bytes: &[u8],
        content_type: Option<&str>,
    ) -> Option<serde_json::Value> {
        match self.bodies {
            BodyCapture::FullWithPii => {
                let slice = if bytes.len() > self.max_body_bytes {
                    &bytes[..self.max_body_bytes]
                } else {
                    bytes
                };
                api_journal::derive_example(slice, content_type)
            }
            BodyCapture::Shapes | BodyCapture::None => None,
        }
    }

    /// The leading `min(len, max_body_bytes)` bytes of a body — the most the seam
    /// ever needs to marshal.
    ///
    /// The worker derives both the body shape and the optional `full-with-pii`
    /// example from at most `max_body_bytes` leading bytes, so carrying any more
    /// across the off-thread (eventually cross-process) hop is wasted transport.
    /// Truncating here is behavior-preserving: [`body_shape`](Self::body_shape) and
    /// [`body_example`](Self::body_example) slice to the same cap, so a body capped
    /// at the seam yields the identical record a full body would.
    #[must_use]
    pub fn capped_body(&self, bytes: &[u8]) -> Vec<u8> {
        let n = bytes.len().min(self.max_body_bytes);
        bytes[..n].to_vec()
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
        // Shapes and FullWithPii both derive a shapes-only skeleton for now; a body
        // larger than the cap is inspected only up to the cap (and a JSON body
        // truncated mid-token will simply read as opaque).
        BodyCapture::Shapes | BodyCapture::FullWithPii => {
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
            // Off-thread (SHIM-D25): marshal the raw context and hand it to the
            // worker, blocking until it finishes. The reduction to the on-disk
            // record now lives in the worker, not on this calling thread.
            let _ = dispatch_off_thread(sink, egress_interaction(sink, req, response));
        }
        result
    }
}

/// Marshal a raw [`Seam::Egress`] request/response into a position-independent
/// [`Interaction`]. No reduction happens here — every header and the literal body
/// bytes (capped at `max_body_bytes`, the most the worker inspects) are carried as
/// is; the worker decides what to keep. The timestamp is sampled now, at
/// interception, so it reflects the interaction's time rather than the worker's.
fn egress_interaction(sink: &JournalSink, req: &EgressRequest, resp: &EgressResponse) -> Interaction {
    Interaction {
        seam: Seam::Egress,
        method: req.verb.to_utf8().unwrap_or_else(|_| "?".to_string()),
        scheme: Some(req.scheme.as_str().to_string()),
        host: req.host.to_utf8().ok(),
        port: Some(req.port),
        target: req.path.to_utf8().unwrap_or_default(),
        request_headers: raw_egress_headers(&req.headers),
        request_body: sink.capped_body(&req.body),
        status: u16::try_from(resp.status).unwrap_or(0),
        response_headers: raw_egress_headers(&resp.headers),
        response_body: sink.capped_body(&resp.body),
        timestamp_ms: now_ms(),
    }
}

/// Convert raw egress `(name, value)` UTF-16 header pairs into position-independent
/// [`RawHeader`]s, preserving every header and its literal value (the safelist is
/// applied later, in the worker).
fn raw_egress_headers(headers: &[(Utf16, Utf16)]) -> Vec<RawHeader> {
    headers
        .iter()
        .map(|(name, value)| RawHeader {
            name: name.to_utf8().unwrap_or_else(|_| "?".to_string()),
            value: value.to_utf8().unwrap_or_default(),
        })
        .collect()
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

// === Off-thread worker (OT-3) ===

/// Reduce raw `(name, value)` header pairs to journal [`HeaderField`]s, retaining
/// literal values only for content-negotiation headers, and return the observed
/// `Content-Type` (used to key body-shape derivation). This is the seam-agnostic
/// reduction the off-thread worker applies to a marshaled interaction.
fn header_fields(headers: &[RawHeader]) -> (Vec<HeaderField>, Option<String>) {
    let mut fields = Vec::with_capacity(headers.len());
    let mut content_type = None;
    for header in headers {
        let lower = header.name.to_ascii_lowercase();
        let retained = if CONTENT_NEGOTIATION_HEADERS.contains(&lower.as_str()) {
            Some(header.value.clone())
        } else {
            None
        };
        if lower == "content-type" {
            content_type = retained.clone();
        }
        fields.push(HeaderField {
            name: header.name.clone(),
            value: retained,
        });
    }
    (fields, content_type)
}

/// Perform the journaling for one marshaled [`Interaction`] and return the JSON
/// [`Outcome`] reply.
///
/// This is the worker that will eventually run out of process: it owns the entire
/// reduction — split path/query, safelist headers, derive body shapes and the
/// optional `full-with-pii` example — so the calling thread carries none of that
/// policy. The on-disk record matches the one the inline decorators produce.
/// Fail-soft: a request that does not parse yields a non-journaled outcome rather
/// than an error (consistent with the shim's never-break-the-host posture).
#[must_use]
pub fn handle_interaction(request_json: &str, sink: &JournalSink) -> String {
    let outcome = match Interaction::from_json(request_json) {
        Ok(interaction) => {
            sink.record(interaction_record(sink, interaction));
            Outcome { journaled: true }
        }
        Err(_) => Outcome { journaled: false },
    };
    outcome
        .to_json()
        .unwrap_or_else(|_| r#"{"journaled":false}"#.to_string())
}

/// Build the [`JournalRecord`] for a marshaled interaction, applying the sink's
/// body-capture policy. `session_id`/`seq` are stamped by [`JournalSink::record`];
/// `timestamp_ms` is carried from the interaction (the interception instant) so it
/// survives the off-thread hop rather than being re-sampled in the worker.
fn interaction_record(sink: &JournalSink, interaction: Interaction) -> JournalRecord {
    let (path, query) = split_path_query(&interaction.target);
    let (request_headers, request_ct) = header_fields(&interaction.request_headers);
    let (response_headers, response_ct) = header_fields(&interaction.response_headers);
    JournalRecord {
        seam: interaction.seam,
        method: interaction.method,
        scheme: interaction.scheme,
        host: interaction.host,
        port: interaction.port,
        path,
        query,
        request_headers,
        request_body: sink.body_shape(&interaction.request_body, request_ct.as_deref()),
        request_body_example: sink.body_example(&interaction.request_body, request_ct.as_deref()),
        status: interaction.status,
        response_headers,
        response_body: sink.body_shape(&interaction.response_body, response_ct.as_deref()),
        response_body_example: sink.body_example(&interaction.response_body, response_ct.as_deref()),
        timestamp_ms: interaction.timestamp_ms,
        ..Default::default()
    }
}

// === Off-thread dispatcher (OT-4) ===

/// Signals a [`WaitGate`] on drop so the dispatcher's worker always releases the
/// waiting host thread — on normal completion *and* while unwinding from a panic
/// in the worker (RS-2). Combined with the thread pool's panic containment (RS-1),
/// a worker panic can neither abort the host nor deadlock the caller: the waiter
/// observes no reply and returns a not-journaled outcome.
struct SignalGuard(WaitGate);

impl Drop for SignalGuard {
    fn drop(&mut self) {
        self.0.signal();
    }
}

/// Marshal a raw [`Interaction`], hand it to the off-thread worker, and block the
/// calling thread until the worker finishes — returning the worker's [`Outcome`].
///
/// This is the first migration step (SHIM-D25 / milestone OT): the host thread no
/// longer journals inline. It marshals the position-independent context, enqueues
/// [`handle_interaction`] on a Windows thread-pool work item, and waits on a
/// `WaitOnAddress` latch ([`WaitGate`]) for the worker to signal completion. The
/// caller's contract is honored by *always* blocking for now; a later stage will
/// make fire-and-forget seams truly asynchronous, and a stage after that will move
/// the worker out of process (the marshaled JSON is the eventual IPC payload).
///
/// Fail-soft (mwin32 D5): if the context cannot be marshaled the interaction is
/// dropped; if the pool refuses the work item the worker runs inline so a record
/// is never silently lost.
#[must_use]
pub fn dispatch_off_thread(sink: &Arc<JournalSink>, interaction: Interaction) -> Outcome {
    let request = match interaction.to_json() {
        Ok(json) => json,
        // A context that cannot be marshaled cannot cross to the worker; drop it.
        Err(_) => return Outcome { journaled: false },
    };

    // The worker writes its JSON reply into this slot before signaling the latch;
    // the store-before-signal / wait-before-load ordering hands the reply across.
    let reply_slot: Arc<Mutex<Option<String>>> = Arc::new(Mutex::new(None));
    let gate = WaitGate::new();

    let worker_sink = Arc::clone(sink);
    let worker_slot = Arc::clone(&reply_slot);
    let worker_gate = gate.clone();
    let submitted = submit_once(move || {
        // Signal the latch from an RAII guard so the waiter is released even if the
        // worker panics (RS-2); the pool contains the panic itself (RS-1). On the
        // normal path the guard drops last, after the reply is in the slot.
        let _signal = SignalGuard(worker_gate);
        let reply = handle_interaction(&request, &worker_sink);
        *worker_slot
            .lock()
            .unwrap_or_else(|poison| poison.into_inner()) = Some(reply);
    });

    let Ok(_work) = submitted else {
        // The pool refused the item; journal inline so nothing is lost.
        let request = interaction.to_json().unwrap_or_default();
        let reply = handle_interaction(&request, sink);
        return Outcome::from_json(&reply).unwrap_or_default();
    };

    // Block until the worker finishes. `_work` additionally joins on drop, so the
    // callback can never outlive this frame.
    gate.wait();
    let reply = reply_slot
        .lock()
        .unwrap_or_else(|poison| poison.into_inner())
        .take()
        .unwrap_or_default();
    Outcome::from_json(&reply).unwrap_or_default()
}

// === Inbound journaling decorator (AJ-B4) ===

/// The raw request half captured at `on_begin_request`, held until the matching
/// `on_send_response` so a single [`Seam::Inbound`] interaction describes the whole
/// exchange. No reduction happens here — the literal headers and body bytes are
/// carried so the off-thread worker can reduce them (SHIM-D25).
struct PendingInbound {
    method: String,
    /// The raw request target (path plus any `?query`), unsplit.
    target: String,
    request_headers: Vec<RawHeader>,
    request_body: Vec<u8>,
    /// Interception time (ms since the Unix epoch), sampled when the request began.
    timestamp_ms: u64,
}

/// A [`RequestHandler`] decorator that journals each inbound (IIS) exchange as a
/// [`Seam::Inbound`] record. It snapshots the request at `on_begin_request` and
/// emits the record at `on_send_response`, when both halves are known.
///
/// The handler stack is rebuilt per request (the host calls `build_handler` on
/// each `GetHttpModule`), so a fresh decorator instance backs each request and
/// the pending request is never crossed between requests.
pub struct JournalingHandler {
    inner: Box<dyn RequestHandler>,
    sink: Arc<JournalSink>,
    pending: Option<PendingInbound>,
}

impl JournalingHandler {
    /// Wrap `inner`, journaling each exchange through `sink`.
    #[must_use]
    pub fn new(inner: Box<dyn RequestHandler>, sink: Arc<JournalSink>) -> Self {
        Self {
            inner,
            sink,
            pending: None,
        }
    }
}

impl RequestHandler for JournalingHandler {
    fn on_begin_request(&mut self, request: &HttpRequest) -> Disposition {
        // Marshal the raw request half; the worker (not this thread) reduces it.
        // The body is capped to what the worker inspects (`max_body_bytes`).
        self.pending = Some(PendingInbound {
            method: request.method().to_string(),
            target: request.url().to_string(),
            request_headers: raw_http_headers(request.headers()),
            request_body: self.sink.capped_body(request.body()),
            timestamp_ms: now_ms(),
        });
        self.inner.on_begin_request(request)
    }

    fn on_send_response(&mut self, response: &mut HttpResponse) -> Disposition {
        // Let the inner stack run first so the journaled response reflects any
        // downstream mutation (the identity stack mutates nothing).
        let disposition = self.inner.on_send_response(response);
        if let Some(pending) = self.pending.take() {
            // Off-thread (SHIM-D25): assemble the raw interaction and hand it to the
            // worker, blocking until it finishes. Inbound has no destination
            // authority — the service is the host.
            let interaction = Interaction {
                seam: Seam::Inbound,
                method: pending.method,
                scheme: None,
                host: None,
                port: None,
                target: pending.target,
                request_headers: pending.request_headers,
                request_body: pending.request_body,
                status: response.status(),
                response_headers: raw_http_headers(response.headers()),
                response_body: self.sink.capped_body(response.body()),
                timestamp_ms: pending.timestamp_ms,
            };
            let _ = dispatch_off_thread(&self.sink, interaction);
        }
        disposition
    }
}

/// Convert raw inbound [`Header`]s into position-independent [`RawHeader`]s,
/// preserving every header and its literal value (the safelist is applied later,
/// in the worker).
fn raw_http_headers(headers: &[Header]) -> Vec<RawHeader> {
    headers
        .iter()
        .map(|header| RawHeader {
            name: header.name().to_string(),
            value: header.value().to_string(),
        })
        .collect()
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

    fn config(path: &std::path::Path) -> ApiJournalConfig {
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
        // FullWithPii currently behaves like Shapes.
        let full = body_shape_for(BodyCapture::FullWithPii, json, Some("application/json"), 4096);
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

    /// A terminal inbound handler that continues the pipeline unchanged.
    struct ContinueLeaf;
    impl RequestHandler for ContinueLeaf {
        fn on_begin_request(&mut self, _request: &HttpRequest) -> Disposition {
            Disposition::Continue
        }
        fn on_send_response(&mut self, _response: &mut HttpResponse) -> Disposition {
            Disposition::Continue
        }
    }

    #[test]
    fn inbound_decorator_journals_an_exchange() {
        let path = temp_path("inbound");
        let sink = JournalSink::from_config(&config(&path)).expect("sink");
        let mut handler = JournalingHandler::new(Box::new(ContinueLeaf), Arc::clone(&sink));

        let request = HttpRequest::new("POST", "/custom/cat?pattern=c.t")
            .with_header("Content-Type", "application/json")
            .with_header("X-Wordy-User", "alice")
            .with_body(br#"{"words":["a"]}"#.to_vec());
        assert_eq!(handler.on_begin_request(&request), Disposition::Continue);

        let mut response = HttpResponse::new(200)
            .with_header("Content-Type", "application/json")
            .with_body(br#"{"word":"cat","exists":true}"#.to_vec());
        assert_eq!(handler.on_send_response(&mut response), Disposition::Continue);

        let file = File::open(&path).expect("journal exists");
        let (records, stats) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(stats, ReadStats::default());
        assert_eq!(records.len(), 1);
        let r = &records[0];
        assert_eq!(r.seam, Seam::Inbound);
        assert_eq!(r.method, "POST");
        // Inbound records carry no destination authority.
        assert_eq!(r.scheme, None);
        assert_eq!(r.host, None);
        assert_eq!(r.port, None);
        assert_eq!(r.path, "/custom/cat");
        assert_eq!(
            r.query,
            vec![QueryParam {
                name: "pattern".into(),
                value: BodyShape::String
            }]
        );
        assert_eq!(r.request_content_type(), Some("application/json"));
        assert!(matches!(r.request_body, BodyShape::Object(_)));
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
    fn inbound_decorator_without_begin_records_nothing() {
        let path = temp_path("inbound-nobegin");
        let sink = JournalSink::from_config(&config(&path)).expect("sink");
        let mut handler = JournalingHandler::new(Box::new(ContinueLeaf), sink);
        let mut response = HttpResponse::new(200);
        assert_eq!(handler.on_send_response(&mut response), Disposition::Continue);
        assert!(
            !path.exists(),
            "a response with no prior request journals nothing"
        );
    }

    fn full_config(path: &std::path::Path) -> ApiJournalConfig {
        ApiJournalConfig {
            enabled: true,
            path: path.to_string_lossy().into_owned(),
            bodies: BodyCapture::FullWithPii,
            ..Default::default()
        }
    }

    #[test]
    fn body_example_only_under_full_mode() {
        let path = temp_path("example");
        let full = JournalSink::from_config(&full_config(&path)).expect("sink");
        let example = full
            .body_example(br#"{"word":"cat"}"#, Some("application/json"))
            .expect("example");
        assert_eq!(example["word"], serde_json::json!("cat"));

        // The default (shapes) config captures no example.
        let shapes = JournalSink::from_config(&config(&path)).expect("sink");
        assert!(shapes
            .body_example(br#"{"word":"cat"}"#, Some("application/json"))
            .is_none());
    }

    #[test]
    fn egress_decorator_captures_example_under_full_mode() {
        let path = temp_path("egress-full");
        let sink = JournalSink::from_config(&full_config(&path)).expect("sink");
        let inner = CannedEgress {
            ok: Some(canned(200, "application/json", br#"{"word":"cat","exists":true}"#)),
        };
        let mut deco = JournalingEgress::new(inner, Some(Arc::clone(&sink)));
        let req = EgressRequest::http(Scheme::Http, "merriam.local", 8080, "GET", "/custom/cat");
        deco.send(&req).expect("send");

        let file = File::open(&path).expect("journal exists");
        let (records, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(records.len(), 1);
        let example = records[0]
            .response_body_example
            .as_ref()
            .expect("response example captured under full-with-pii mode");
        assert_eq!(example["word"], serde_json::json!("cat"));
        assert_eq!(example["exists"], serde_json::json!(true));
    }

    #[test]
    fn inbound_decorator_captures_example_under_full_mode() {
        let path = temp_path("inbound-full");
        let sink = JournalSink::from_config(&full_config(&path)).expect("sink");
        let mut handler = JournalingHandler::new(Box::new(ContinueLeaf), Arc::clone(&sink));

        let request = HttpRequest::new("POST", "/spellcheck")
            .with_header("Content-Type", "application/json")
            .with_body(br#"{"words":["a"]}"#.to_vec());
        handler.on_begin_request(&request);
        let mut response = HttpResponse::new(200)
            .with_header("Content-Type", "application/json")
            .with_body(br#"{"results":[]}"#.to_vec());
        handler.on_send_response(&mut response);

        let file = File::open(&path).expect("journal exists");
        let (records, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(records.len(), 1);
        assert_eq!(
            records[0].request_body_example.as_ref().unwrap()["words"],
            serde_json::json!(["a"])
        );
        assert!(records[0].response_body_example.is_some());
    }

    // === Off-thread worker (OT-3) ===

    #[test]
    fn worker_reduces_and_journals_egress_interaction() {
        let path = temp_path("worker-egress");
        let sink = JournalSink::from_config(&full_config(&path)).expect("sink");

        let interaction = Interaction {
            seam: Seam::Egress,
            method: "POST".into(),
            scheme: Some("https".into()),
            host: Some("api.example".into()),
            port: Some(443),
            target: "/custom/cat?pattern=c.t".into(),
            request_headers: vec![
                RawHeader { name: "Content-Type".into(), value: "application/json".into() },
                RawHeader { name: "X-Wordy-User".into(), value: "alice".into() },
            ],
            request_body: br#"{"words":["cat"]}"#.to_vec(),
            status: 200,
            response_headers: vec![RawHeader {
                name: "Content-Type".into(),
                value: "application/json".into(),
            }],
            response_body: br#"{"ok":true}"#.to_vec(),
            timestamp_ms: 12_345,
        };
        let reply = handle_interaction(&interaction.to_json().unwrap(), &sink);
        assert_eq!(Outcome::from_json(&reply).unwrap(), Outcome { journaled: true });

        let file = File::open(&path).expect("journal exists");
        let (records, stats) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(stats, ReadStats::default());
        assert_eq!(records.len(), 1);
        let r = &records[0];
        assert_eq!(r.seam, Seam::Egress);
        assert_eq!(r.method, "POST");
        assert_eq!(r.scheme.as_deref(), Some("https"));
        assert_eq!(r.host.as_deref(), Some("api.example"));
        assert_eq!(r.port, Some(443));
        // Path and query are split; the query value is reduced to a scalar shape.
        assert_eq!(r.path, "/custom/cat");
        assert_eq!(r.query.len(), 1);
        assert_eq!(r.query[0].name, "pattern");
        assert_eq!(r.query[0].value, infer_scalar("c.t"));
        // Content-Type value retained; the non-safelisted header keeps only its name.
        assert_eq!(r.request_headers[0].value.as_deref(), Some("application/json"));
        assert_eq!(r.request_headers[1].name, "X-Wordy-User");
        assert!(r.request_headers[1].value.is_none());
        // Bodies are reduced exactly as the sink's policy dictates, and the
        // interception timestamp survives the marshal hop.
        assert_eq!(
            r.request_body,
            sink.body_shape(br#"{"words":["cat"]}"#, Some("application/json"))
        );
        assert!(r.response_body_example.is_some());
        assert_eq!(r.timestamp_ms, 12_345);
    }

    #[test]
    fn worker_handles_inbound_interaction_without_authority() {
        let path = temp_path("worker-inbound");
        let sink = JournalSink::from_config(&config(&path)).expect("sink");

        let interaction = Interaction {
            seam: Seam::Inbound,
            method: "GET".into(),
            target: "/healthz".into(),
            status: 204,
            timestamp_ms: 7,
            ..Default::default()
        };
        let reply = handle_interaction(&interaction.to_json().unwrap(), &sink);
        assert_eq!(Outcome::from_json(&reply).unwrap(), Outcome { journaled: true });

        let file = File::open(&path).expect("journal exists");
        let (records, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(records.len(), 1);
        let r = &records[0];
        assert_eq!(r.seam, Seam::Inbound);
        assert_eq!(r.path, "/healthz");
        assert!(r.query.is_empty());
        assert!(r.scheme.is_none() && r.host.is_none() && r.port.is_none());
        assert_eq!(r.status, 204);
    }

    #[test]
    fn worker_is_fail_soft_on_unparseable_request() {
        let path = temp_path("worker-badjson");
        let sink = JournalSink::from_config(&config(&path)).expect("sink");
        let reply = handle_interaction("not json", &sink);
        assert_eq!(Outcome::from_json(&reply).unwrap(), Outcome { journaled: false });
        assert!(!path.exists(), "an unparseable request journals nothing");
    }

    // === Off-thread dispatcher (OT-4) ===

    #[test]
    fn dispatch_off_thread_journals_synchronously() {
        let path = temp_path("dispatch-sync");
        let sink = JournalSink::from_config(&full_config(&path)).expect("sink");

        let interaction = Interaction {
            seam: Seam::Egress,
            method: "POST".into(),
            scheme: Some("https".into()),
            host: Some("api.example".into()),
            port: Some(443),
            target: "/custom/cat?pattern=c.t".into(),
            request_headers: vec![RawHeader {
                name: "Content-Type".into(),
                value: "application/json".into(),
            }],
            request_body: br#"{"words":["cat"]}"#.to_vec(),
            status: 200,
            response_headers: vec![RawHeader {
                name: "Content-Type".into(),
                value: "application/json".into(),
            }],
            response_body: br#"{"ok":true}"#.to_vec(),
            timestamp_ms: 999,
        };
        let outcome = dispatch_off_thread(&sink, interaction);
        assert_eq!(outcome, Outcome { journaled: true });

        // The work ran on a pool thread, but dispatch blocked on the latch until it
        // finished — so the record is already durably on disk right here.
        let file = File::open(&path).expect("journal exists");
        let (records, stats) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(stats, ReadStats::default());
        assert_eq!(records.len(), 1);
        assert_eq!(records[0].path, "/custom/cat");
        assert_eq!(records[0].seam, Seam::Egress);
        assert_eq!(records[0].timestamp_ms, 999);
        assert!(records[0].response_body_example.is_some());
    }

    #[test]
    fn repeated_dispatch_each_blocks_and_journals_in_order() {
        let path = temp_path("dispatch-many");
        let sink = JournalSink::from_config(&config(&path)).expect("sink");

        for i in 0..20 {
            let interaction = Interaction {
                seam: Seam::Inbound,
                method: "GET".into(),
                target: format!("/i{i}"),
                status: 200,
                timestamp_ms: 1,
                ..Default::default()
            };
            assert_eq!(
                dispatch_off_thread(&sink, interaction),
                Outcome { journaled: true }
            );
        }

        let file = File::open(&path).expect("journal exists");
        let (records, stats) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(stats, ReadStats::default());
        assert_eq!(records.len(), 20);
        // Each dispatch blocked to completion before the next began, so records are
        // in submission order with a contiguous, monotonic sequence.
        for (i, r) in records.iter().enumerate() {
            assert_eq!(r.path, format!("/i{i}"));
            assert_eq!(r.seq, i as u64);
        }
    }

    // === Off-thread vs direct-worker parity (OT-7) ===

    /// Zero the per-sink / wall-clock bookkeeping fields that are not part of the
    /// reduction under test, so two records can be compared for structural equality.
    fn normalized(mut r: JournalRecord) -> JournalRecord {
        r.session_id = 0;
        r.seq = 0;
        r.timestamp_ms = 0;
        r
    }

    #[test]
    fn egress_decorator_matches_direct_worker() {
        // The off-thread egress decorator marshals a request/response...
        let path_a = temp_path("ot7-egress-deco");
        let sink_a = JournalSink::from_config(&full_config(&path_a)).expect("sink");
        let inner = CannedEgress {
            ok: Some(canned(200, "application/json", br#"{"ok":true}"#)),
        };
        let mut deco = JournalingEgress::new(inner, Some(Arc::clone(&sink_a)));
        let mut req = EgressRequest::http(
            Scheme::Http,
            "merriam.local",
            8080,
            "POST",
            "/custom/cat?pattern=c.t",
        );
        req.headers.push((
            Utf16::from_utf8("Content-Type"),
            Utf16::from_utf8("application/json"),
        ));
        req.headers
            .push((Utf16::from_utf8("X-Wordy-User"), Utf16::from_utf8("alice")));
        req.body = br#"{"words":["cat"]}"#.to_vec();
        deco.send(&req).expect("send ok");
        let file = File::open(&path_a).expect("journal a");
        let (records_a, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path_a);

        // ...the direct worker, fed the equivalent raw interaction, must produce the
        // identical record (the decorator captured exactly the raw context).
        let path_b = temp_path("ot7-egress-worker");
        let sink_b = JournalSink::from_config(&full_config(&path_b)).expect("sink");
        let interaction = Interaction {
            seam: Seam::Egress,
            method: "POST".into(),
            scheme: Some("http".into()),
            host: Some("merriam.local".into()),
            port: Some(8080),
            target: "/custom/cat?pattern=c.t".into(),
            request_headers: vec![
                RawHeader { name: "Content-Type".into(), value: "application/json".into() },
                RawHeader { name: "X-Wordy-User".into(), value: "alice".into() },
            ],
            request_body: br#"{"words":["cat"]}"#.to_vec(),
            status: 200,
            response_headers: vec![RawHeader {
                name: "Content-Type".into(),
                value: "application/json".into(),
            }],
            response_body: br#"{"ok":true}"#.to_vec(),
            timestamp_ms: 0,
        };
        let _ = handle_interaction(&interaction.to_json().unwrap(), &sink_b);
        let file = File::open(&path_b).expect("journal b");
        let (records_b, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path_b);

        assert_eq!(records_a.len(), 1);
        assert_eq!(records_b.len(), 1);
        assert_eq!(
            normalized(records_a[0].clone()),
            normalized(records_b[0].clone())
        );
    }

    #[test]
    fn inbound_decorator_matches_direct_worker() {
        let path_a = temp_path("ot7-inbound-deco");
        let sink_a = JournalSink::from_config(&full_config(&path_a)).expect("sink");
        let mut handler = JournalingHandler::new(Box::new(ContinueLeaf), Arc::clone(&sink_a));
        let request = HttpRequest::new("POST", "/spellcheck?lang=en")
            .with_header("Content-Type", "application/json")
            .with_body(br#"{"words":["a"]}"#.to_vec());
        handler.on_begin_request(&request);
        let mut response = HttpResponse::new(200)
            .with_header("Content-Type", "application/json")
            .with_body(br#"{"results":[]}"#.to_vec());
        handler.on_send_response(&mut response);
        let file = File::open(&path_a).expect("journal a");
        let (records_a, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path_a);

        let path_b = temp_path("ot7-inbound-worker");
        let sink_b = JournalSink::from_config(&full_config(&path_b)).expect("sink");
        let interaction = Interaction {
            seam: Seam::Inbound,
            method: "POST".into(),
            target: "/spellcheck?lang=en".into(),
            request_headers: vec![RawHeader {
                name: "Content-Type".into(),
                value: "application/json".into(),
            }],
            request_body: br#"{"words":["a"]}"#.to_vec(),
            status: 200,
            response_headers: vec![RawHeader {
                name: "Content-Type".into(),
                value: "application/json".into(),
            }],
            response_body: br#"{"results":[]}"#.to_vec(),
            timestamp_ms: 0,
            ..Default::default()
        };
        let _ = handle_interaction(&interaction.to_json().unwrap(), &sink_b);
        let file = File::open(&path_b).expect("journal b");
        let (records_b, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path_b);

        assert_eq!(records_a.len(), 1);
        assert_eq!(records_b.len(), 1);
        assert_eq!(
            normalized(records_a[0].clone()),
            normalized(records_b[0].clone())
        );
    }

    // === Bounded marshaled bodies (BC-1) ===

    fn capped_config(path: &std::path::Path, max_body_bytes: usize) -> ApiJournalConfig {
        ApiJournalConfig {
            enabled: true,
            path: path.to_string_lossy().into_owned(),
            max_body_bytes,
            ..Default::default()
        }
    }

    #[test]
    fn capped_body_truncates_to_max() {
        let path = temp_path("capped");
        let sink = JournalSink::from_config(&capped_config(&path, 8)).expect("sink");
        // Longer than the cap → truncated to the leading `max_body_bytes`.
        assert_eq!(sink.capped_body(b"0123456789ABCDEF"), b"01234567");
        // Exactly the cap → unchanged.
        assert_eq!(sink.capped_body(b"01234567"), b"01234567");
        // Shorter than the cap → unchanged.
        assert_eq!(sink.capped_body(b"abc"), b"abc");
        // Empty → empty.
        assert!(sink.capped_body(b"").is_empty());
    }

    #[test]
    fn over_cap_body_through_off_thread_matches_worker_capping() {
        // The egress decorator caps the body at the seam; the worker would also cap
        // it internally. Both must yield the identical on-disk record — proving the
        // seam-side truncation changes nothing the worker computes.
        let big = br#"{"name":"abcdefghijklmnop"}"#; // > 8 bytes
        let cap = 8;

        let path_a = temp_path("bc1-deco");
        let sink_a = JournalSink::from_config(&capped_config(&path_a, cap)).expect("sink");
        let inner = CannedEgress {
            ok: Some(canned(200, "application/json", br#"{"ok":true}"#)),
        };
        let mut deco = JournalingEgress::new(inner, Some(Arc::clone(&sink_a)));
        let mut req =
            EgressRequest::http(Scheme::Http, "merriam.local", 8080, "POST", "/upload");
        req.headers.push((
            Utf16::from_utf8("Content-Type"),
            Utf16::from_utf8("application/json"),
        ));
        req.body = big.to_vec();
        deco.send(&req).expect("send ok");
        let file = File::open(&path_a).expect("journal a");
        let (records_a, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path_a);

        // Feed the worker the FULL, uncapped body; it caps internally to the same
        // bound. (Same small cap so both observe the same leading bytes.)
        let path_b = temp_path("bc1-worker");
        let sink_b = JournalSink::from_config(&capped_config(&path_b, cap)).expect("sink");
        let interaction = Interaction {
            seam: Seam::Egress,
            method: "POST".into(),
            scheme: Some("http".into()),
            host: Some("merriam.local".into()),
            port: Some(8080),
            target: "/upload".into(),
            request_headers: vec![RawHeader {
                name: "Content-Type".into(),
                value: "application/json".into(),
            }],
            request_body: big.to_vec(),
            status: 200,
            response_headers: vec![RawHeader {
                name: "Content-Type".into(),
                value: "application/json".into(),
            }],
            response_body: br#"{"ok":true}"#.to_vec(),
            timestamp_ms: 0,
        };
        let _ = handle_interaction(&interaction.to_json().unwrap(), &sink_b);
        let file = File::open(&path_b).expect("journal b");
        let (records_b, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path_b);

        assert_eq!(records_a.len(), 1);
        assert_eq!(records_b.len(), 1);
        // The body, cut mid-token by the cap, reads as opaque either way.
        assert_eq!(records_a[0].request_body, BodyShape::Opaque);
        assert_eq!(
            normalized(records_a[0].clone()),
            normalized(records_b[0].clone())
        );
    }

    // === Cross-thread remoting robustness (RS-2) ===

    #[test]
    fn worker_panic_wakes_the_waiter_and_is_contained() {
        // Mirrors `dispatch_off_thread`'s worker structure: the `SignalGuard` releases
        // the waiting host thread even when the worker panics before producing a reply
        // (RS-2), and the pool contains the panic so the process survives (RS-1).
        // Without either fix this test would deadlock on `wait()` or abort the process.
        // The panic message on stderr is expected.
        let gate = WaitGate::new();
        let worker_gate = gate.clone();
        let work = submit_once(move || {
            let _signal = SignalGuard(worker_gate);
            panic!("worker boom before producing a reply");
        })
        .expect("submit");
        gate.wait(); // must return, not deadlock
        assert!(gate.is_signaled());
        work.wait(); // pool survived the contained panic
    }

    // === Cross-thread remoting concurrency stress (RS-3) ===

    #[test]
    fn concurrent_mixed_clients_journal_off_thread_without_loss() {
        // A mix of egress and inbound "clients" hammer the off-thread dispatch from
        // many host threads at once. Every interception marshals its raw context and
        // submits a work item to the pool, then blocks on its *own* `WaitGate`. The
        // invariants under load: every record lands (no loss), no line tears, and
        // there is no cross-talk between the per-call gate/slot pairs — distinct
        // paths and a contiguous sequence prove it. RS-3.
        const EGRESS_CLIENTS: usize = 8;
        const INBOUND_CLIENTS: usize = 8;
        const REQUESTS_PER_CLIENT: usize = 50;

        let path = temp_path("rs3-concurrent-mixed");
        let sink = JournalSink::from_config(&config(&path)).expect("sink");

        let mut clients = Vec::new();

        // Egress clients: each owns a decorator over a canned backing and sends many
        // requests through the off-thread journaling path.
        for t in 0..EGRESS_CLIENTS {
            let sink = Arc::clone(&sink);
            clients.push(thread::spawn(move || {
                let inner = CannedEgress {
                    ok: Some(canned(200, "application/json", br#"{"ok":true}"#)),
                };
                let mut deco = JournalingEgress::new(inner, Some(sink));
                for i in 0..REQUESTS_PER_CLIENT {
                    let mut req = EgressRequest::http(
                        Scheme::Http,
                        "merriam.local",
                        8080,
                        "POST",
                        &format!("/egress/t{t}/i{i}"),
                    );
                    req.headers.push((
                        Utf16::from_utf8("Content-Type"),
                        Utf16::from_utf8("application/json"),
                    ));
                    req.body = br#"{"words":["cat"]}"#.to_vec();
                    deco.send(&req).expect("send ok");
                }
            }));
        }

        // Inbound clients: each drives fresh per-request handlers (as the IIS module
        // rebuilds per request) through the same off-thread path.
        for t in 0..INBOUND_CLIENTS {
            let sink = Arc::clone(&sink);
            clients.push(thread::spawn(move || {
                for i in 0..REQUESTS_PER_CLIENT {
                    let mut handler =
                        JournalingHandler::new(Box::new(ContinueLeaf), Arc::clone(&sink));
                    let request = HttpRequest::new("GET", format!("/inbound/t{t}/i{i}"));
                    handler.on_begin_request(&request);
                    let mut response = HttpResponse::new(200);
                    handler.on_send_response(&mut response);
                }
            }));
        }

        for c in clients {
            c.join().expect("client thread joined");
        }

        let file = File::open(&path).expect("journal exists");
        let (records, stats) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        let expected = (EGRESS_CLIENTS + INBOUND_CLIENTS) * REQUESTS_PER_CLIENT;
        // No torn lines and no lost records.
        assert_eq!(stats, ReadStats::default());
        assert_eq!(records.len(), expected);
        // Every distinct path was journaled exactly once — no cross-talk, no dupes.
        let paths: std::collections::BTreeSet<&str> =
            records.iter().map(|r| r.path.as_str()).collect();
        assert_eq!(paths.len(), expected);
        // Sequence numbers form a contiguous 0..expected set despite the contention.
        let seqs: std::collections::BTreeSet<u64> = records.iter().map(|r| r.seq).collect();
        assert_eq!(seqs.len(), expected);
        assert_eq!(*seqs.iter().next_back().unwrap(), (expected as u64) - 1);
    }
}
