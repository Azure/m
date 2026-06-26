// Copyright (c) Microsoft Corporation.

//! MW18-4 end-to-end egress-isolation proof (SHIM-D23).
//!
//! This drives the validation-tier capstone: it links the `wordy-relay-probe`
//! bin (which exercises `wordy`'s **real** `merriam` relay) against the alias
//! object, runs a genuine `merriam` service, and confirms over real HTTP that
//! the `.pilcfg` egress modes isolate `wordy`'s outbound calls — **redirect**
//! diverts a dead port into the live `merriam`, **buffer** captures the POST
//! with `merriam` untouched, **replay** serves a fixture with no network, and a
//! non-aliased control reaches the real (dead) target and fails. The heavy
//! lifting lives in [`egressrelayproof/run-egressrelayproof.ps1`], which this
//! test invokes and whose exit code it interprets (0 = PASS, 1 = FAIL, 2 = SKIP).
//!
//! It is `#[ignore]`d by default because it builds aliased/native probe variants
//! on the fly and needs permission to bind a loopback `http.sys` URL for
//! `merriam` (elevation or a one-time `netsh http add urlacl`). Run it explicitly:
//!
//! ```text
//! cargo test -p windows-win32-shim --test egress_relay -- --ignored --nocapture
//! ```

#![cfg(windows)]

use std::path::PathBuf;
use std::process::Command;

/// Exit code the harness returns when it cannot run the proof (the `merriam`
/// URL cannot be reserved). Treated as an ignore, not a failure.
const HARNESS_SKIP: i32 = 2;

/// Path to the orchestration script shipped beside this crate.
fn harness_script() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("egressrelayproof")
        .join("run-egressrelayproof.ps1")
}

#[test]
#[ignore = "builds aliased probes + needs an http.sys URL reservation; run with --ignored"]
fn egress_relay_isolation_proof_holds_across_modes() {
    let script = harness_script();
    assert!(script.is_file(), "harness script missing: {}", script.display());

    let status = Command::new("powershell.exe")
        .args([
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            &script.display().to_string(),
        ])
        .status()
        .expect("failed to launch run-egressrelayproof.ps1");

    match status.code() {
        Some(0) => { /* PASS: redirect / buffer / replay / control all held. */ }
        Some(code) if code == HARNESS_SKIP => {
            eprintln!(
                "ignored: the egress-relay proof could not run \
                 (merriam URL unbindable — run elevated or add a urlacl); harness exit {code}"
            );
        }
        other => panic!(
            "egress-relay isolation proof failed (harness exit {:?}); \
             re-run `run-egressrelayproof.ps1` to see the report",
            other
        ),
    }
}
