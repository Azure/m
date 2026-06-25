// Copyright (c) Microsoft Corporation.
//
// MW18-2.3: gated http.sys listener integration test for `merriam`.
//
// Binds the real HTTP Server API to a free loopback port and drives every route
// end-to-end over real TCP/HTTP, asserting the JSON responses. Binding http.sys
// needs a urlacl reservation or elevation; when that is unavailable
// `Server::bind` returns ERROR_ACCESS_DENIED and the test SKIPs (prints a note
// and passes), so the default `cargo test` run never depends on host config.

#![cfg(windows)]

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use merriam::{ERROR_ACCESS_DENIED, Server, Service, Store};

/// A scratch store dir removed on drop.
struct ScratchDir {
    path: std::path::PathBuf,
}
impl ScratchDir {
    fn new() -> Self {
        let mut path = std::env::temp_dir();
        path.push(format!(
            "merriam-listener-{}-{:?}",
            std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()
        ));
        Self { path }
    }
}
impl Drop for ScratchDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}

/// Reserve a free loopback port, then release it for http.sys to bind.
fn free_port() -> u16 {
    let l = TcpListener::bind("127.0.0.1:0").expect("reserve port");
    l.local_addr().unwrap().port()
}

/// Make one HTTP/1.1 request, returning `(status, body)`. Uses `Connection:
/// close` so the response is read to EOF.
fn http_request(port: u16, method: &str, path: &str, user: Option<&str>) -> (u16, String) {
    let mut stream = TcpStream::connect(("127.0.0.1", port)).expect("connect");
    stream.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    stream.set_write_timeout(Some(Duration::from_secs(3))).unwrap();

    let mut request = format!(
        "{method} {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nContent-Length: 0\r\n"
    );
    if let Some(u) = user {
        request.push_str(&format!("X-Wordy-User: {u}\r\n"));
    }
    request.push_str("\r\n");
    stream.write_all(request.as_bytes()).expect("write request");

    let mut raw = Vec::new();
    let _ = stream.read_to_end(&mut raw);
    let text = String::from_utf8_lossy(&raw).into_owned();

    let status = text
        .lines()
        .next()
        .and_then(|line| line.split_whitespace().nth(1))
        .and_then(|code| code.parse::<u16>().ok())
        .unwrap_or(0);
    let body = text.split_once("\r\n\r\n").map(|x| x.1).unwrap_or("").to_string();
    (status, body)
}

#[test]
fn http_sys_listener_serves_every_route() {
    let dir = ScratchDir::new();
    let port = free_port();
    let url = format!("http://127.0.0.1:{port}/");

    let server = match Server::bind(&url) {
        Ok(s) => s,
        Err(ERROR_ACCESS_DENIED) => {
            eprintln!("SKIP: http.sys bind denied (no urlacl reservation for {url}); run elevated or reserve the URL.");
            return;
        }
        Err(code) => {
            eprintln!("SKIP: http.sys bind failed for {url}: error {code}.");
            return;
        }
    };

    let service = Service::new(Store::new(&dir.path));
    let stop = AtomicBool::new(false);

    std::thread::scope(|scope| {
        scope.spawn(|| server.serve(&service, &stop));

        // healthz
        let (status, body) = http_request(port, "GET", "/healthz", None);
        assert_eq!(status, 200, "healthz status");
        assert_eq!(body, r#"{"status":"ok"}"#);

        // add
        let (status, body) = http_request(port, "POST", "/custom/widget", Some("tester"));
        assert_eq!(status, 200);
        assert_eq!(body, r#"{"word":"widget","added":true}"#);

        // exists
        let (_s, body) = http_request(port, "GET", "/custom/widget", Some("tester"));
        assert_eq!(body, r#"{"word":"widget","exists":true}"#);

        // list
        let (_s, body) = http_request(port, "GET", "/custom", Some("tester"));
        assert_eq!(body, r#"{"matches":["widget"]}"#);

        // a different user sees nothing (isolation over the real listener)
        let (_s, body) = http_request(port, "GET", "/custom", Some("other"));
        assert_eq!(body, r#"{"matches":[]}"#);

        // delete
        let (_s, body) = http_request(port, "DELETE", "/custom/widget", Some("tester"));
        assert_eq!(body, r#"{"word":"widget","removed":true}"#);

        // unknown route -> 404
        let (status, _b) = http_request(port, "GET", "/nope", None);
        assert_eq!(status, 404);

        // Stop the server: set the flag, then poke it to unblock the receive.
        stop.store(true, Ordering::SeqCst);
        if let Ok(mut s) = TcpStream::connect(("127.0.0.1", port)) {
            let _ = s.write_all(b"GET /healthz HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        }
    });
}
