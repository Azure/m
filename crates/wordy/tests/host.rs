// Copyright (c) Microsoft Corporation.

//! Integration tests for `wordy` (MW13-5).
//!
//! These drive the full REST surface end-to-end through the public
//! [`wordy::routes::Service`] — the same host-agnostic core the IIS boundary
//! invokes — over a scratch custom-dictionary store at integration scale, and
//! assert the dictionary behaviors. The genuine Hostable Web Core path is
//! exercised separately by the `wordy-host` pre-flight binary, which is run here
//! and gated to report-only when HWC is absent.
//!
//! The IIS ABI boundary itself (decode body/header → dispatch → write JSON body)
//! is covered by the in-crate emulated-host unit tests in `src/iis.rs`; genuine
//! `WebCoreActivate` + live HTTP awaits the real-`httpserv.h` vtable pinning
//! (MW16).

use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicU32, Ordering};

use wordy::custom::CustomDictionary;
use wordy::routes::{HttpRequest, Outcome, STATUS_BAD_REQUEST, STATUS_OK, Service};
use wordy::words::Locale;

/// A unique scratch directory under the OS temp dir, removed on drop.
struct TempDir {
    path: PathBuf,
}

impl TempDir {
    fn new() -> Self {
        static COUNTER: AtomicU32 = AtomicU32::new(0);
        let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
        let mut path = std::env::temp_dir();
        path.push(format!("wordy-itest-{}-{}", std::process::id(), unique));
        std::fs::create_dir_all(&path).expect("create scratch dir");
        TempDir { path }
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}

fn service(tmp: &TempDir) -> Service {
    Service::new(CustomDictionary::new(&tmp.path), Locale::EnUs)
}

fn req(method: &str, url: &str, body: &str, user: Option<&str>) -> HttpRequest {
    HttpRequest {
        method: method.into(),
        url: url.into(),
        body: body.into(),
        user: user.map(str::to_string),
    }
}

/// Dispatch and unwrap the response body + status, panicking on `Continue`.
fn call(svc: &Service, request: &HttpRequest) -> (u16, String) {
    match svc.dispatch(request) {
        Outcome::Respond(r) => (r.status, r.body),
        Outcome::Continue => panic!("expected a response for {}", request.url),
    }
}

#[test]
fn read_only_routes_end_to_end() {
    let tmp = TempDir::new();
    let svc = service(&tmp);

    // Health.
    let (status, body) = call(&svc, &req("GET", "/healthz", "", None));
    assert_eq!(status, STATUS_OK);
    assert!(body.contains("\"status\""));

    // Spellcheck a batch of correct and misspelled words.
    let spellcheck_body = r#"{"words":["hello","world","dictionary","helo","wrold","teh"]}"#;
    let (status, body) = call(&svc, &req("POST", "/spellcheck", spellcheck_body, None));
    assert_eq!(status, STATUS_OK);
    assert!(body.contains("\"hello\""));
    assert!(body.contains("\"correct\":true"));
    assert!(body.contains("\"correct\":false"));
    // A close suggestion is offered for a misspelling.
    assert!(body.contains("hello"));

    // Anagram.
    let (status, body) = call(
        &svc,
        &req("POST", "/anagram", r#"{"template":"c.t","tray":"a","wildcards":0}"#, None),
    );
    assert_eq!(status, STATUS_OK);
    assert!(body.contains("cat"));
    assert!(!body.contains("cot"));

    // Shared enumeration by regex.
    let (status, body) = call(&svc, &req("GET", "/shared?pattern=c.t", "", None));
    assert_eq!(status, STATUS_OK);
    assert!(body.contains("cat"));
    assert!(body.contains("cut"));

    // Bad inputs are client errors.
    assert_eq!(call(&svc, &req("POST", "/spellcheck", "nope", None)).0, STATUS_BAD_REQUEST);
    assert_eq!(call(&svc, &req("GET", "/shared", "", None)).0, STATUS_BAD_REQUEST);
    assert_eq!(
        call(&svc, &req("POST", "/anagram", r#"{"template":"c#t"}"#, None)).0,
        STATUS_BAD_REQUEST
    );

    // Unknown route declines.
    assert!(matches!(
        svc.dispatch(&req("GET", "/nope", "", None)),
        Outcome::Continue
    ));
}

#[test]
fn custom_dictionary_lifecycle_end_to_end() {
    let tmp = TempDir::new();
    let svc = service(&tmp);

    // Add, confirm, enumerate, remove for the default user.
    let (status, body) = call(&svc, &req("POST", "/custom/widget", "", None));
    assert_eq!(status, STATUS_OK);
    assert!(body.contains("\"added\":true"));

    assert!(call(&svc, &req("GET", "/custom/widget", "", None)).1.contains("\"exists\":true"));
    assert!(call(&svc, &req("GET", "/custom", "", None)).1.contains("widget"));

    assert!(
        call(&svc, &req("DELETE", "/custom/widget", "", None))
            .1
            .contains("\"removed\":true")
    );
    assert!(call(&svc, &req("GET", "/custom/widget", "", None)).1.contains("\"exists\":false"));

    // Per-user isolation.
    call(&svc, &req("POST", "/custom/secret", "", Some("alice")));
    assert!(
        call(&svc, &req("GET", "/custom/secret", "", Some("bob")))
            .1
            .contains("\"exists\":false")
    );
    assert!(
        call(&svc, &req("GET", "/custom/secret", "", Some("alice")))
            .1
            .contains("\"exists\":true")
    );
}

#[test]
fn custom_dictionary_at_integration_scale() {
    let tmp = TempDir::new();
    let svc = service(&tmp);
    let user = Some("scale-user");

    // Add several hundred words.
    const COUNT: usize = 500;
    for i in 0..COUNT {
        let (status, body) = call(&svc, &req("POST", &format!("/custom/word{i}"), "", user));
        assert_eq!(status, STATUS_OK);
        assert!(body.contains("\"added\":true"));
    }

    // Enumerate and verify the count (results are capped at 1000, so 500 is full).
    let store = CustomDictionary::new(&tmp.path);
    let listed = store
        .list(Locale::EnUs, &wordy::custom::Principal::from_header(user))
        .expect("list custom words");
    assert_eq!(listed.len(), COUNT);

    // A pattern filter narrows the set.
    let (status, body) = call(&svc, &req("GET", "/custom?pattern=word1", "", user));
    assert_eq!(status, STATUS_OK);
    assert!(body.contains("word1"));
    assert!(!body.contains("word2\""));

    // Remove them all.
    for i in 0..COUNT {
        assert!(
            call(&svc, &req("DELETE", &format!("/custom/word{i}"), "", user))
                .1
                .contains("\"removed\":true")
        );
    }
    let remaining = store
        .list(Locale::EnUs, &wordy::custom::Principal::from_header(user))
        .expect("list custom words");
    assert!(remaining.is_empty());
}

#[test]
fn hwc_activator_preflight_runs_and_is_gated() {
    // The `wordy-host` pre-flight is safe to run everywhere: it reports HWC
    // readiness and exits 0 whether or not HWC is installed.
    let exe = env!("CARGO_BIN_EXE_wordy-host");
    let output = Command::new(exe)
        .output()
        .expect("run wordy-host pre-flight");
    assert!(
        output.status.success(),
        "wordy-host should exit 0, got {:?}",
        output.status
    );
    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(stdout.contains("Hostable Web Core host for wordy"));

    // When HWC is genuinely present, the engine is discovered; otherwise the
    // assertion is skipped (gated).
    let hwebcore = std::env::var("windir")
        .or_else(|_| std::env::var("SystemRoot"))
        .map(|w| PathBuf::from(w).join(r"System32\inetsrv\hwebcore.dll"));
    match hwebcore {
        Ok(path) if path.is_file() => {
            assert!(
                stdout.contains("[found]   HWC engine"),
                "HWC is installed; pre-flight should report it. stdout:\n{stdout}"
            );
        }
        _ => {
            eprintln!("HWC engine absent; skipping genuine-host discovery assertion");
        }
    }
}
