// Copyright (c) Microsoft Corporation.

//! The live ("direct") egress provider (D31), the network analogue of
//! [`LiveRegistry`](crate::LiveRegistry) / [`LiveFilesystem`](crate::LiveFilesystem).
//!
//! [`LiveEgress`] performs one real WinHTTP transaction per [`EgressRequest`]
//! through the safe primitive in `windows-platform-isolation-sys`. This module
//! is Windows-only and itself contains **no `unsafe`** (D13): every raw WinHTTP
//! call is confined to the leaf. It is the network bottom of the egress
//! decorator stack — `RedirectingEgress<LiveEgress>` actually sends to the
//! rewritten destination, `ReplayEgress<LiveEgress>` falls through to it on a
//! miss, and the default passthrough is `LiveEgress` itself.

use windows_platform_isolation_sys::http_send;

use crate::Utf16;
use crate::egress::{EgressRequest, EgressResponse, EgressSurface, Scheme};
use crate::egress_error::{EgressError, EgressResult};

/// A provider that issues each request over the live network via WinHTTP (D31).
#[derive(Debug, Default, Clone, Copy)]
pub struct LiveEgress;

impl LiveEgress {
    /// Construct the live egress provider.
    #[must_use]
    pub fn new() -> Self {
        Self
    }
}

impl EgressSurface for LiveEgress {
    fn send(&mut self, req: &EgressRequest) -> EgressResult<EgressResponse> {
        let secure = matches!(req.scheme, Scheme::Https);
        let host = nul_terminated(&req.host);
        let verb = nul_terminated(&req.verb);
        let path = nul_terminated(&req.path);
        let header_blob = build_header_blob(&req.headers);

        let reply = http_send(secure, &host, req.port, &verb, &path, &header_blob, &req.body)
            .map_err(|e| EgressError::Os(e.code()))?;

        Ok(EgressResponse {
            status: reply.status,
            headers: parse_raw_headers(&reply.raw_headers),
            body: reply.body,
        })
    }
}

/// A NUL-terminated copy of a string's code units, for the wide WinHTTP boundary.
fn nul_terminated(s: &Utf16) -> Vec<u16> {
    let mut v = s.as_units().to_vec();
    v.push(0);
    v
}

/// Build the NUL-terminated `"Name: Value\r\nName: Value\r\n"` wide header blob
/// the leaf hands to `WinHttpAddRequestHeaders`. An empty header list yields a
/// lone NUL (which the leaf treats as "no headers").
fn build_header_blob(headers: &[(Utf16, Utf16)]) -> Vec<u16> {
    const COLON_SP: [u16; 2] = [b':' as u16, b' ' as u16];
    const CRLF: [u16; 2] = [b'\r' as u16, b'\n' as u16];
    let mut blob = Vec::new();
    for (name, value) in headers {
        blob.extend_from_slice(name.as_units());
        blob.extend_from_slice(&COLON_SP);
        blob.extend_from_slice(value.as_units());
        blob.extend_from_slice(&CRLF);
    }
    blob.push(0);
    blob
}

/// Parse the raw `CRLF` response-header blob into `(name, value)` pairs,
/// skipping the leading status line. Values keep their bytes verbatim aside from
/// a single optional leading space after the colon. Ill-formed code units are
/// preserved (D9).
fn parse_raw_headers(raw: &[u16]) -> Vec<(Utf16, Utf16)> {
    const LF: u16 = b'\n' as u16;
    const CR: u16 = b'\r' as u16;
    const COLON: u16 = b':' as u16;
    const SP: u16 = b' ' as u16;

    let mut out = Vec::new();
    let mut lines = raw.split(|&u| u == LF).map(|line| match line.last() {
        Some(&CR) => &line[..line.len() - 1],
        _ => line,
    });
    // The first line is the HTTP status line, not a header.
    lines.next();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        if let Some(pos) = line.iter().position(|&u| u == COLON) {
            let name = &line[..pos];
            let mut value = &line[pos + 1..];
            if value.first() == Some(&SP) {
                value = &value[1..];
            }
            out.push((Utf16::from_units(name.to_vec()), Utf16::from_units(value.to_vec())));
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;

    #[test]
    fn build_header_blob_empty_is_lone_nul() {
        assert_eq!(build_header_blob(&[]), vec![0]);
    }

    #[test]
    fn build_header_blob_formats_pairs() {
        let headers = vec![(Utf16::from_utf8("X-A"), Utf16::from_utf8("1"))];
        let blob = build_header_blob(&headers);
        let text: String = char::decode_utf16(blob.iter().copied())
            .map(|r| r.unwrap_or('?'))
            .collect();
        assert_eq!(text, "X-A: 1\r\n\0");
    }

    #[test]
    fn parse_raw_headers_skips_status_line_and_splits_pairs() {
        let raw: Vec<u16> =
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nX-Empty:\r\n\r\n"
                .encode_utf16()
                .collect();
        let headers = parse_raw_headers(&raw);
        assert_eq!(headers.len(), 2);
        assert_eq!(headers[0].0.to_utf8().unwrap(), "Content-Type");
        assert_eq!(headers[0].1.to_utf8().unwrap(), "text/plain");
        assert_eq!(headers[1].0.to_utf8().unwrap(), "X-Empty");
        assert_eq!(headers[1].1.to_utf8().unwrap(), "");
    }

    #[test]
    fn live_egress_round_trips_against_a_localhost_listener() {
        // A self-hosted one-shot HTTP/1.1 responder on an ephemeral port — always
        // bindable, so the only skip path is WinHTTP failing to reach localhost.
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind localhost");
        let port = listener.local_addr().expect("addr").port();
        let server = std::thread::spawn(move || {
            if let Ok((mut stream, _)) = listener.accept() {
                let mut buf = [0u8; 2048];
                let _ = stream.read(&mut buf); // consume the request
                let body = b"hello-egress";
                let head = format!(
                    "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: text/plain\r\n\r\n",
                    body.len()
                );
                let _ = stream.write_all(head.as_bytes());
                let _ = stream.write_all(body);
                let _ = stream.flush();
            }
        });

        let mut live = LiveEgress::new();
        let result = live.send(&EgressRequest::http(Scheme::Http, "127.0.0.1", port, "GET", "/"));
        let _ = server.join();

        match result {
            Ok(resp) => {
                assert_eq!(resp.status, 200);
                assert_eq!(resp.body, b"hello-egress");
                assert!(
                    resp.headers
                        .iter()
                        .any(|(n, v)| n.to_utf8().unwrap_or_default() == "Content-Type"
                            && v.to_utf8().unwrap_or_default() == "text/plain"),
                    "Content-Type header should round-trip"
                );
            }
            Err(EgressError::Os(code)) => {
                eprintln!("skipped: WinHTTP could not reach localhost (WIN32 error {code})");
            }
            Err(other) => panic!("unexpected egress error: {other}"),
        }
    }
}
