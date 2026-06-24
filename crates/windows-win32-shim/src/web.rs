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

/// The session-held web policy: the mode and the observation sink. Composed like
/// the COM state (SHIM-D17) and held behind the session lock. MW11 has no
/// substitution registry — the pass-through cut needs only mode + sink.
pub struct WebState {
    /// The active behavior mode.
    pub mode: WebMode,
    sink: Box<dyn WebObservationSink>,
}

impl WebState {
    /// A default web state: [`WebMode::Off`] and a [`NullWebSink`] (the
    /// fully-transparent first-cut posture).
    #[must_use]
    pub fn new() -> Self {
        Self {
            mode: WebMode::Off,
            sink: Box::new(NullWebSink),
        }
    }

    /// Replace the observation sink (the session installs the real sink here).
    pub fn set_sink(&mut self, sink: Box<dyn WebObservationSink>) {
        self.sink = sink;
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
}
