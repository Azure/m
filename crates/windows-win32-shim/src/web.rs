// Copyright (c) Microsoft Corporation.

//! The safe web-host activation policy state (MW11, SHIM-D18) — the
//! platform-independent half of the in-process response-path seam.
//!
//! The exported [`mRegisterModule`](crate::mwinweb) body and the shim
//! `CHttpModule` notifications route through this state to decide whether a
//! traversal is journaled. It holds **no** Windows or IIS type: the unsafe vtable
//! glue that translates the host's per-request calls lives in
//! [`crate::mwinweb`]; the per-request bridge into platform-isolation's safe
//! `RequestHandler` surface (M8) lands in MW12. This module is therefore
//! ungated and unit-testable on any platform, mirroring [`crate::com`].
//!
//! MW11 is the **pass-through** cut: every notification continues the host
//! pipeline unchanged. The mode gates only observation — [`WebMode::Off`] is a
//! silent identity, [`WebMode::Observe`] records each step — exactly as the COM
//! family gates substitution/observation (SHIM-D17). Installation is
//! unconditional in both modes; being resident on the path is the point.

use std::sync::{Arc, Mutex};

use windows_platform_isolation::{
    Disposition, HttpRequest, HttpResponse, IsolationMode, ObservationSink, ObservedEvent,
    RequestHandler, VolumePolicy, WebSession,
};

use crate::journal::{JournalSink, JournalingHandler};

/// A single observed web-host activation or per-request notification (SHIM-D18 /
/// D29). The variant is the seam point; observation never changes the host's
/// behavior. A future substituting mode will carry richer events; MW11 records
/// only that the traversal occurred.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum WebEvent {
    /// The host called `RegisterModule` and the shim installed its module
    /// factory for the begin-request / send-response notifications.
    RegisterModule,
    /// The host called `IHttpModuleFactory::GetHttpModule`; the shim vended a
    /// `CHttpModule`.
    GetHttpModule,
    /// The shim `CHttpModule::OnBeginRequest` ran (and continued the pipeline).
    BeginRequest,
    /// The shim `CHttpModule::OnSendResponse` ran (and continued the pipeline).
    SendResponse,
}

/// The seam the session reports every web-host notification to (default
/// [`NullWebSink`]). Kept minimal — one method — so the storage target is
/// separable from the ABI bodies. Implementations must be `Send` because the
/// session is free-threaded and holds the sink behind its lock. Mirrors
/// [`ComObservationSink`](crate::com::ComObservationSink); the two are
/// intentionally separate event channels.
pub trait WebObservationSink: Send {
    /// Record one observed web-host notification.
    fn observe(&mut self, event: WebEvent);
}

/// The default web observation sink: records nothing (the off / first-cut
/// posture).
#[derive(Debug, Default)]
pub struct NullWebSink;

impl WebObservationSink for NullWebSink {
    fn observe(&mut self, _event: WebEvent) {}
}

/// How the web-host shim behaves (SHIM-D18, driven by session mode SHIM-D13 /
/// D25). The default is [`WebMode::Off`] — a silent identity pass-through that
/// adds nothing. Mirrors [`ComMode`](crate::com::ComMode); a future substituting
/// mode that rewrites the response is reserved, not yet modeled.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum WebMode {
    /// Silent identity: install the module, continue every notification, observe
    /// nothing (the D24 identity posture, "no behavior change today").
    #[default]
    Off,
    /// Observing identity: continue every notification unchanged but report each
    /// step to the observation sink (D29).
    Observe,
}

/// The terminal handler the shim's decorators sit above: it represents the
/// host's own downstream pipeline, so every notification continues and the host
/// proceeds with its real handling. The pass-through cut inserts only
/// non-mutating decorators over this leaf, which is why the response is
/// unchanged (MW12, D25 "off").
#[derive(Clone, Copy, Debug, Default)]
pub struct ContinueLeaf;

impl RequestHandler for ContinueLeaf {
    fn on_begin_request(&mut self, _request: &HttpRequest) -> Disposition {
        Disposition::Continue
    }

    fn on_send_response(&mut self, _response: &mut HttpResponse) -> Disposition {
        Disposition::Continue
    }
}

/// An [`ObservationSink`] that appends each [`ObservedEvent`] to the session's
/// shared observation log (MW12-4). The journaling decorator drives it on the
/// request thread; the log is held behind an `Arc<Mutex<..>>` shared with the
/// session so it survives the per-request handler and can be inspected (or, in a
/// future cut, drained).
pub struct LogSink {
    log: Arc<Mutex<Vec<ObservedEvent>>>,
}

impl ObservationSink for LogSink {
    fn observe(&mut self, event: ObservedEvent) {
        self.log.lock().expect("observation log poisoned").push(event);
    }
}

/// The session-held web policy: the mode and the observation sink. Composed like
/// the COM state (SHIM-D17) and held behind the session lock. MW11 has no
/// substitution registry — the pass-through cut needs only mode + sink.
pub struct WebState {
    /// The active behavior mode.
    pub mode: WebMode,
    sink: Box<dyn WebObservationSink>,
    /// The shared per-request observation log the journaling stack feeds
    /// (MW12-4). Held behind `Arc<Mutex<..>>` so the per-request [`LogSink`] and
    /// the session see the same buffer.
    observation_log: Arc<Mutex<Vec<ObservedEvent>>>,
    /// The volume policy the journaling stack honors (D29). Defaults to
    /// recording every exchange.
    policy: VolumePolicy,
    /// The inbound API-journal sink (AJ-B4). When present, `build_handler` wraps
    /// the per-request stack so each inbound exchange is journaled to NDJSON,
    /// independent of the legacy observation [`mode`](WebState::mode).
    journal_sink: Option<Arc<JournalSink>>,
}

impl WebState {
    /// A default web state: [`WebMode::Off`] and a [`NullWebSink`] (the
    /// fully-transparent first-cut posture).
    #[must_use]
    pub fn new() -> Self {
        Self {
            mode: WebMode::Off,
            sink: Box::new(NullWebSink),
            observation_log: Arc::new(Mutex::new(Vec::new())),
            policy: VolumePolicy::record_all(),
            journal_sink: None,
        }
    }

    /// Replace the observation sink (the session installs the real sink here).
    pub fn set_sink(&mut self, sink: Box<dyn WebObservationSink>) {
        self.sink = sink;
    }

    /// Set the volume policy the journaling stack honors (D29). Defaults to
    /// recording every exchange.
    pub fn set_volume_policy(&mut self, policy: VolumePolicy) {
        self.policy = policy;
    }

    /// Install the inbound API-journal sink (AJ-B4), or clear it with `None`.
    /// When present, [`build_handler`](WebState::build_handler) wraps the
    /// per-request handler stack so each inbound exchange is journaled.
    pub fn set_journal_sink(&mut self, sink: Option<Arc<JournalSink>>) {
        self.journal_sink = sink;
    }

    /// A clone of the shared observation-log handle. The journaling stack
    /// appends to it; callers (and tests) read what was recorded.
    #[must_use]
    pub fn observation_log(&self) -> Arc<Mutex<Vec<ObservedEvent>>> {
        self.observation_log.clone()
    }

    /// Report `event` to the sink when the mode observes ([`WebMode::Observe`]);
    /// a pure no-op in [`WebMode::Off`].
    pub fn observe(&mut self, event: WebEvent) {
        if self.mode != WebMode::Off {
            self.sink.observe(event);
        }
    }

    /// Record that the host registered the shim module ([`WebEvent::RegisterModule`]).
    /// Installation is unconditional; this only journals the step.
    pub fn on_register_module(&mut self) {
        self.observe(WebEvent::RegisterModule);
    }

    /// Record that the host acquired a shim `CHttpModule`
    /// ([`WebEvent::GetHttpModule`]).
    pub fn on_get_http_module(&mut self) {
        self.observe(WebEvent::GetHttpModule);
    }

    /// Record an `OnBeginRequest` traversal ([`WebEvent::BeginRequest`]). The
    /// pass-through disposition (continue) is fixed by the ABI body.
    pub fn on_begin_request(&mut self) {
        self.observe(WebEvent::BeginRequest);
    }

    /// Record an `OnSendResponse` traversal ([`WebEvent::SendResponse`]).
    pub fn on_send_response(&mut self) {
        self.observe(WebEvent::SendResponse);
    }

    /// Build the per-request handler stack the shim `CHttpModule` drives (MW12).
    /// The mode selects the decorator stack over the [`ContinueLeaf`]: in
    /// [`WebMode::Off`] an identity pass-through (D25 "off"); in
    /// [`WebMode::Observe`] the journaling stack (D25 "record") that reports each
    /// exchange to the session log subject to the volume policy (D29). Neither
    /// stack mutates the response.
    #[must_use]
    pub fn build_handler(&self) -> Box<dyn RequestHandler> {
        let mode = match self.mode {
            WebMode::Off => IsolationMode::Off,
            WebMode::Observe => IsolationMode::Record,
        };
        let sink = LogSink {
            log: self.observation_log.clone(),
        };
        let base: Box<dyn RequestHandler> =
            Box::new(WebSession::new(mode).wrap(ContinueLeaf, sink, self.policy.clone()));
        // When an inbound journal sink is installed, wrap the stack so each
        // exchange is journaled regardless of the observation mode (AJ-B4).
        match &self.journal_sink {
            Some(journal) => Box::new(JournalingHandler::new(base, Arc::clone(journal))),
            None => base,
        }
    }
}

impl Default for WebState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    /// A sink that records every event for inspection.
    #[derive(Clone, Default)]
    struct RecordingSink {
        events: Arc<Mutex<Vec<WebEvent>>>,
    }

    impl WebObservationSink for RecordingSink {
        fn observe(&mut self, event: WebEvent) {
            self.events.lock().expect("sink poisoned").push(event);
        }
    }

    #[test]
    fn off_mode_observes_nothing() {
        let sink = RecordingSink::default();
        let mut state = WebState::new();
        state.set_sink(Box::new(sink.clone()));
        // Mode defaults to Off.
        state.on_register_module();
        state.on_get_http_module();
        state.on_begin_request();
        state.on_send_response();
        assert!(sink.events.lock().expect("sink poisoned").is_empty());
    }

    #[test]
    fn observe_mode_records_each_step_in_order() {
        let sink = RecordingSink::default();
        let mut state = WebState::new();
        state.mode = WebMode::Observe;
        state.set_sink(Box::new(sink.clone()));
        state.on_register_module();
        state.on_get_http_module();
        state.on_begin_request();
        state.on_send_response();
        let events = sink.events.lock().expect("sink poisoned").clone();
        assert_eq!(
            events,
            vec![
                WebEvent::RegisterModule,
                WebEvent::GetHttpModule,
                WebEvent::BeginRequest,
                WebEvent::SendResponse,
            ]
        );
    }

    #[test]
    fn default_state_is_off_with_null_sink() {
        let mut state = WebState::default();
        assert_eq!(state.mode, WebMode::Off);
        // Observing in Off mode is a no-op even with the default sink.
        state.on_begin_request();
    }

    // --- MW12-4: journaling stack -------------------------------------------

    #[test]
    fn observe_mode_journals_each_exchange_into_the_log() {
        let mut state = WebState::new();
        state.mode = WebMode::Observe;
        let log = state.observation_log();
        let mut handler = state.build_handler();

        let request = HttpRequest::new("GET", "/page");
        assert_eq!(handler.on_begin_request(&request), Disposition::Continue);
        let mut response = HttpResponse::new(200);
        assert_eq!(handler.on_send_response(&mut response), Disposition::Continue);

        let events = log.lock().expect("log poisoned").clone();
        assert_eq!(
            events,
            vec![
                ObservedEvent::BeginRequest {
                    method: "GET".to_owned(),
                    url: "/page".to_owned(),
                    header_names: Vec::new(),
                },
                ObservedEvent::SendResponse {
                    status: 200,
                    header_names: Vec::new(),
                },
            ]
        );
    }

    #[test]
    fn off_mode_journals_nothing() {
        let state = WebState::new();
        let log = state.observation_log();
        let mut handler = state.build_handler();

        handler.on_begin_request(&HttpRequest::new("GET", "/page"));
        handler.on_send_response(&mut HttpResponse::new(200));

        assert!(log.lock().expect("log poisoned").is_empty());
    }

    #[test]
    fn volume_policy_suppresses_known_safe_exchanges() {
        let mut state = WebState::new();
        state.mode = WebMode::Observe;
        let mut policy = VolumePolicy::record_all();
        policy.suppress("GET", "/health");
        state.set_volume_policy(policy);
        let log = state.observation_log();
        let mut handler = state.build_handler();

        // The suppressed exchange records nothing (begin and send both elided).
        handler.on_begin_request(&HttpRequest::new("GET", "/health"));
        handler.on_send_response(&mut HttpResponse::new(200));
        assert!(log.lock().expect("log poisoned").is_empty());

        // A non-suppressed exchange still records.
        handler.on_begin_request(&HttpRequest::new("GET", "/page"));
        handler.on_send_response(&mut HttpResponse::new(200));
        assert_eq!(log.lock().expect("log poisoned").len(), 2);
    }

    #[test]
    fn build_handler_journals_inbound_exchange_when_sink_installed() {
        use crate::journal::JournalSink;
        use crate::pilcfg::ApiJournalConfig;
        use api_journal::{Seam, read_records};
        use std::fs::File;
        use std::io::BufReader;
        use std::time::{SystemTime, UNIX_EPOCH};

        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let path = std::env::temp_dir().join(format!(
            "web-inbound-{}-{nanos}.ndjson",
            std::process::id()
        ));
        let cfg = ApiJournalConfig {
            enabled: true,
            path: path.to_string_lossy().into_owned(),
            ..Default::default()
        };
        // Mode stays Off: API journaling is independent of the observation mode.
        let mut state = WebState::new();
        state.set_journal_sink(JournalSink::from_config(&cfg));
        let mut handler = state.build_handler();

        handler.on_begin_request(&HttpRequest::new("GET", "/healthz"));
        handler.on_send_response(&mut HttpResponse::new(200));

        // Inbound dispatch is non-blocking (AC-4); drop the handler to join the worker.
        drop(handler);
        let file = File::open(&path).expect("journal exists");
        let (records, _) = read_records(BufReader::new(file));
        let _ = std::fs::remove_file(&path);

        assert_eq!(records.len(), 1);
        assert_eq!(records[0].seam, Seam::Inbound);
        assert_eq!(records[0].method, "GET");
        assert_eq!(records[0].path, "/healthz");
        assert_eq!(records[0].status, 200);
    }
}
