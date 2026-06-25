// Copyright (c) Microsoft Corporation.

//! The WinHTTP request-lifecycle reassembly engine (MW17 / SHIM-D22).
//!
//! WinHTTP is a stateful, handle-based, multi-call API: a single logical request
//! is assembled across `WinHttpOpen → Connect → OpenRequest → AddRequestHeaders
//! → SendRequest`, and the reply is drained across `ReceiveResponse →
//! QueryHeaders → QueryDataAvailable → ReadData`. The egress surface
//! ([`EgressSurface`]) trades in **whole** request/response values, so this
//! engine bridges the two: it interns each `HINTERNET` as a sentinel, accumulates
//! the per-handle transaction state, captures one [`EgressRequest`] at the send
//! boundary, runs it through the surface, and serves the resulting
//! [`EgressResponse`] back across the read calls. This is the same
//! replay-state-in-a-handle shape as the `FindFirstFile`/`FindNext` enumeration
//! (SHIM-D14).
//!
//! The engine is surface-generic (testable over a fake sender, no live network);
//! the `extern "C"` `mWinHttp*` exports marshal raw pointers/buffers onto it and
//! the session selects the backing from `.pilcfg` (MW17-3). Per the egress model
//! the transaction is request/response-buffered, not streamed: `SendRequest`
//! drives the whole exchange and the body is delivered from memory — so even the
//! passthrough backing (`LiveEgress`) reassembles-and-resends rather than
//! forwarding 1:1 (a documented characteristic, SHIM-D22).

use std::collections::HashMap;

use windows_platform_isolation::{
    EgressError, EgressRequest, EgressResult, EgressSurface, EgressTransport, Scheme, Utf16,
};

/// An interned WinHTTP handle value (the opaque token the C ABI hands back as an
/// `HINTERNET`). Never zero, so the C ABI can reserve null for failure.
pub type EgressHandle = usize;

/// The state behind one interned `HINTERNET`.
enum HandleState {
    /// A `WinHttpOpen` session handle.
    Session,
    /// A `WinHttpConnect` connection handle bound to a destination authority.
    Connection { host: Utf16, port: u16 },
    /// A `WinHttpOpenRequest` request handle accumulating one transaction.
    Request(RequestTxn),
}

/// The per-request transaction accumulated across the WinHTTP lifecycle.
struct RequestTxn {
    scheme: Scheme,
    host: Utf16,
    port: u16,
    verb: Utf16,
    path: Utf16,
    headers: Vec<(Utf16, Utf16)>,
    /// `Some` once `send` has run: the response being drained by the read calls.
    drain: Option<Drain>,
}

/// The response captured at `send`, drained across the read calls.
struct Drain {
    status: u32,
    headers: Vec<(Utf16, Utf16)>,
    body: Vec<u8>,
    /// Byte offset of the next `read_data`.
    pos: usize,
}

/// The WinHTTP reassembly engine over an [`EgressSurface`] backing.
pub struct EgressEngine<S: EgressSurface> {
    backing: S,
    next: EgressHandle,
    handles: HashMap<EgressHandle, HandleState>,
}

impl<S: EgressSurface> EgressEngine<S> {
    /// Build an engine over `backing`.
    pub fn new(backing: S) -> Self {
        Self { backing, next: 1, handles: HashMap::new() }
    }

    /// Borrow the backing surface (e.g. to inspect a buffer's journal).
    pub fn backing(&mut self) -> &mut S {
        &mut self.backing
    }

    /// Recover the backing surface.
    pub fn into_backing(self) -> S {
        self.backing
    }

    /// The number of live interned handles (for leak assertions in tests).
    #[must_use]
    pub fn live_handles(&self) -> usize {
        self.handles.len()
    }

    fn alloc(&mut self, state: HandleState) -> EgressHandle {
        let handle = self.next;
        self.next += 1;
        self.handles.insert(handle, state);
        handle
    }

    /// `WinHttpOpen`: mint a session handle.
    pub fn open(&mut self) -> EgressHandle {
        self.alloc(HandleState::Session)
    }

    /// `WinHttpConnect`: mint a connection handle bound to `host:port`. `None` if
    /// `session` is not a live session handle.
    pub fn connect(&mut self, session: EgressHandle, host: Utf16, port: u16) -> Option<EgressHandle> {
        match self.handles.get(&session) {
            Some(HandleState::Session) => Some(self.alloc(HandleState::Connection { host, port })),
            _ => None,
        }
    }

    /// `WinHttpOpenRequest`: mint a request handle on `connection`, beginning a
    /// transaction. `secure` selects HTTPS. `None` if `connection` is not a live
    /// connection handle.
    pub fn open_request(
        &mut self,
        connection: EgressHandle,
        verb: Utf16,
        path: Utf16,
        secure: bool,
    ) -> Option<EgressHandle> {
        let (host, port) = match self.handles.get(&connection) {
            Some(HandleState::Connection { host, port }) => (host.clone(), *port),
            _ => return None,
        };
        let scheme = if secure { Scheme::Https } else { Scheme::Http };
        Some(self.alloc(HandleState::Request(RequestTxn {
            scheme,
            host,
            port,
            verb,
            path,
            headers: Vec::new(),
            drain: None,
        })))
    }

    /// `WinHttpAddRequestHeaders`: append headers to the pending request.
    /// `false` if `request` is not a live, not-yet-sent request handle.
    pub fn add_headers(&mut self, request: EgressHandle, headers: Vec<(Utf16, Utf16)>) -> bool {
        match self.handles.get_mut(&request) {
            Some(HandleState::Request(txn)) if txn.drain.is_none() => {
                txn.headers.extend(headers);
                true
            }
            _ => false,
        }
    }

    /// `WinHttpSendRequest`: capture the accumulated transaction (plus any
    /// `extra_headers` and the `body`) as one [`EgressRequest`], run it through
    /// the backing, and stash the response for the read calls.
    ///
    /// # Errors
    ///
    /// [`EgressError::InvalidRequest`] if `request` is not a live, not-yet-sent
    /// request handle; otherwise the backing's error.
    pub fn send(
        &mut self,
        request: EgressHandle,
        extra_headers: Vec<(Utf16, Utf16)>,
        body: Vec<u8>,
    ) -> EgressResult<()> {
        let egress_request = match self.handles.get_mut(&request) {
            Some(HandleState::Request(txn)) if txn.drain.is_none() => {
                txn.headers.extend(extra_headers);
                EgressRequest {
                    transport: EgressTransport::Http,
                    scheme: txn.scheme,
                    host: txn.host.clone(),
                    port: txn.port,
                    verb: txn.verb.clone(),
                    path: txn.path.clone(),
                    headers: txn.headers.clone(),
                    body,
                }
            }
            _ => {
                return Err(EgressError::InvalidRequest(
                    "send on a handle that is not a pending request".to_string(),
                ));
            }
        };

        let response = self.backing.send(&egress_request)?;

        if let Some(HandleState::Request(txn)) = self.handles.get_mut(&request) {
            txn.drain = Some(Drain {
                status: response.status,
                headers: response.headers,
                body: response.body,
                pos: 0,
            });
        }
        Ok(())
    }

    /// `WinHttpReceiveResponse`: confirm a response is ready (it was obtained at
    /// [`send`](Self::send) time).
    ///
    /// # Errors
    ///
    /// [`EgressError::InvalidRequest`] if `request` has not been sent.
    pub fn receive_response(&self, request: EgressHandle) -> EgressResult<()> {
        match self.handles.get(&request) {
            Some(HandleState::Request(txn)) if txn.drain.is_some() => Ok(()),
            _ => Err(EgressError::InvalidRequest(
                "receive_response before a successful send".to_string(),
            )),
        }
    }

    fn drain(&self, request: EgressHandle) -> Option<&Drain> {
        match self.handles.get(&request) {
            Some(HandleState::Request(txn)) => txn.drain.as_ref(),
            _ => None,
        }
    }

    /// `WinHttpQueryHeaders(WINHTTP_QUERY_STATUS_CODE)`: the response status, or
    /// `None` if the request has no response yet.
    #[must_use]
    pub fn status(&self, request: EgressHandle) -> Option<u32> {
        self.drain(request).map(|d| d.status)
    }

    /// The response headers, or `None` if the request has no response yet.
    #[must_use]
    pub fn response_headers(&self, request: EgressHandle) -> Option<&[(Utf16, Utf16)]> {
        self.drain(request).map(|d| d.headers.as_slice())
    }

    /// `WinHttpQueryDataAvailable`: the number of body bytes not yet read, or
    /// `None` if the request has no response yet.
    #[must_use]
    pub fn data_available(&self, request: EgressHandle) -> Option<usize> {
        self.drain(request).map(|d| d.body.len() - d.pos)
    }

    /// `WinHttpReadData`: copy up to `max` body bytes from the current position,
    /// advancing it. Returns the bytes read (empty once drained), or `None` if
    /// the request has no response yet.
    pub fn read_data(&mut self, request: EgressHandle, max: usize) -> Option<Vec<u8>> {
        match self.handles.get_mut(&request) {
            Some(HandleState::Request(txn)) => {
                let drain = txn.drain.as_mut()?;
                let end = (drain.pos + max).min(drain.body.len());
                let chunk = drain.body[drain.pos..end].to_vec();
                drain.pos = end;
                Some(chunk)
            }
            _ => None,
        }
    }

    /// `WinHttpCloseHandle`: free an interned handle. `false` if it was not live.
    pub fn close(&mut self, handle: EgressHandle) -> bool {
        self.handles.remove(&handle).is_some()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use windows_platform_isolation::{
        BlockingEgress, BufferedEgress, EgressResponse, RedirectRule, RedirectingEgress,
    };

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    /// A backing that records the request it was sent and returns a canned reply.
    struct CannedSurface {
        seen: Vec<EgressRequest>,
        status: u32,
        body: Vec<u8>,
    }
    impl CannedSurface {
        fn new(status: u32, body: &[u8]) -> Self {
            Self { seen: Vec::new(), status, body: body.to_vec() }
        }
    }
    impl EgressSurface for CannedSurface {
        fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
            self.seen.push(req.clone());
            Ok(EgressResponse {
                status: self.status,
                headers: vec![(w("Content-Type"), w("application/json"))],
                body: self.body.clone(),
            })
        }
    }

    /// Drive a full GET lifecycle and return the engine + request handle.
    fn drive_get(
        engine: &mut EgressEngine<impl EgressSurface>,
        host: &str,
        port: u16,
        path: &str,
    ) -> EgressHandle {
        let session = engine.open();
        let conn = engine.connect(session, w(host), port).expect("connect");
        let req = engine.open_request(conn, w("GET"), w(path), false).expect("open_request");
        engine.send(req, Vec::new(), Vec::new()).expect("send");
        req
    }

    #[test]
    fn full_lifecycle_reassembles_and_drains() {
        let mut engine = EgressEngine::new(CannedSurface::new(200, b"hello-body"));
        let req = drive_get(&mut engine, "localhost", 8080, "/p");

        engine.receive_response(req).expect("receive");
        assert_eq!(engine.status(req), Some(200));
        assert_eq!(
            engine.response_headers(req).unwrap()[0].0.to_utf8().unwrap(),
            "Content-Type"
        );

        // Drain the body in two chunks.
        assert_eq!(engine.data_available(req), Some(10));
        assert_eq!(engine.read_data(req, 4).unwrap(), b"hell");
        assert_eq!(engine.data_available(req), Some(6));
        assert_eq!(engine.read_data(req, 100).unwrap(), b"o-body");
        assert_eq!(engine.data_available(req), Some(0));
        assert_eq!(engine.read_data(req, 100).unwrap(), b"");

        // The backing saw the reassembled request.
        let seen = &engine.backing().seen[0];
        assert_eq!(seen.scheme, Scheme::Http);
        assert_eq!(seen.host.to_utf8().unwrap(), "localhost");
        assert_eq!(seen.port, 8080);
        assert_eq!(seen.verb.to_utf8().unwrap(), "GET");
        assert_eq!(seen.path.to_utf8().unwrap(), "/p");
    }

    #[test]
    fn accumulated_headers_and_body_reach_the_request() {
        let mut engine = EgressEngine::new(CannedSurface::new(201, b""));
        let session = engine.open();
        let conn = engine.connect(session, w("h"), 80).unwrap();
        let req = engine.open_request(conn, w("POST"), w("/v"), false).unwrap();
        assert!(engine.add_headers(req, vec![(w("X-A"), w("1"))]));
        engine.send(req, vec![(w("X-B"), w("2"))], b"payload".to_vec()).unwrap();

        let seen = &engine.backing().seen[0];
        assert_eq!(seen.verb.to_utf8().unwrap(), "POST");
        assert_eq!(seen.body, b"payload");
        assert_eq!(seen.headers.len(), 2);
        assert_eq!(seen.headers[0].0.to_utf8().unwrap(), "X-A");
        assert_eq!(seen.headers[1].0.to_utf8().unwrap(), "X-B");
    }

    #[test]
    fn secure_open_request_selects_https() {
        let mut engine = EgressEngine::new(CannedSurface::new(200, b""));
        let session = engine.open();
        let conn = engine.connect(session, w("h"), 443).unwrap();
        let req = engine.open_request(conn, w("GET"), w("/"), true).unwrap();
        engine.send(req, Vec::new(), Vec::new()).unwrap();
        assert_eq!(engine.backing().seen[0].scheme, Scheme::Https);
    }

    #[test]
    fn invalid_handle_hierarchy_is_rejected() {
        let mut engine = EgressEngine::new(CannedSurface::new(200, b""));
        // connect on a non-session handle.
        assert_eq!(engine.connect(999, w("h"), 80), None);
        let session = engine.open();
        // open_request on a session (not a connection) handle.
        assert_eq!(engine.open_request(session, w("GET"), w("/"), false), None);
        // send on a non-request handle.
        assert!(engine.send(session, Vec::new(), Vec::new()).is_err());
        // queries before send.
        let conn = engine.connect(session, w("h"), 80).unwrap();
        let req = engine.open_request(conn, w("GET"), w("/"), false).unwrap();
        assert_eq!(engine.status(req), None);
        assert!(engine.receive_response(req).is_err());
    }

    #[test]
    fn close_frees_handles() {
        let mut engine = EgressEngine::new(CannedSurface::new(200, b""));
        let session = engine.open();
        let conn = engine.connect(session, w("h"), 80).unwrap();
        let req = engine.open_request(conn, w("GET"), w("/"), false).unwrap();
        assert_eq!(engine.live_handles(), 3);
        assert!(engine.close(req));
        assert!(engine.close(conn));
        assert!(engine.close(session));
        assert!(!engine.close(session)); // already gone
        assert_eq!(engine.live_handles(), 0);
    }

    #[test]
    fn redirect_backing_rewrites_the_destination_through_the_engine() {
        let canned = CannedSurface::new(200, b"ok");
        let backing = RedirectingEgress::new(canned)
            .with_rule(RedirectRule::new("localhost", Some(8019), "127.0.0.1", 18019));
        let mut engine = EgressEngine::new(backing);
        let req = drive_get(&mut engine, "localhost", 8019, "/goal");
        assert_eq!(engine.status(req), Some(200));
        let canned = engine.into_backing().into_inner();
        let seen = &canned.seen[0];
        assert_eq!(seen.host.to_utf8().unwrap(), "127.0.0.1");
        assert_eq!(seen.port, 18019);
    }

    #[test]
    fn buffer_backing_isolates_mutations_through_the_engine() {
        let mut engine = EgressEngine::new(BufferedEgress::new(CannedSurface::new(200, b"")));
        let session = engine.open();
        let conn = engine.connect(session, w("h"), 80).unwrap();
        let req = engine.open_request(conn, w("POST"), w("/w"), false).unwrap();
        engine.send(req, Vec::new(), b"data".to_vec()).unwrap();
        // Buffered ack is 202; the inner canned surface was never contacted.
        assert_eq!(engine.status(req), Some(202));
        assert!(engine.backing().is_dirty());
        assert_eq!(engine.backing().journal().len(), 1);
    }

    #[test]
    fn block_backing_fails_send_through_the_engine() {
        let mut engine = EgressEngine::new(BlockingEgress);
        let session = engine.open();
        let conn = engine.connect(session, w("h"), 80).unwrap();
        let req = engine.open_request(conn, w("GET"), w("/"), false).unwrap();
        assert!(engine.send(req, Vec::new(), Vec::new()).is_err());
        assert_eq!(engine.status(req), None);
    }
}
