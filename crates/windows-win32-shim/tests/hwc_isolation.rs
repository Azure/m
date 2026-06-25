// Copyright (c) Microsoft Corporation.

//! MW15-6 genuine end-to-end isolation proof (SHIM-D19).
//!
//! This drives the full runtime isolation proof: it links the **unmodified**
//! `wordy` REST module against the alias object, genuinely `WebCoreActivate`s
//! Hostable Web Core with a buffered host sidecar, and confirms over real HTTP
//! that the module's custom-store namespace mutations land in the shim overlay
//! (SHIM-D13 / platform-isolation D30) and never touch the live filesystem. The
//! heavy lifting lives in [`hwcproof/run-hwcproof.ps1`], which this test invokes
//! and whose exit code it interprets (0 = PASS, 1 = FAIL, 2 = SKIP).
//!
//! It is `#[ignore]`d by default because it needs the HWC feature installed and
//! permission to reserve `http://localhost:8080/` (elevation or a one-time
//! `netsh http add urlacl` grant), and it builds an aliased `wordy.dll` on the
//! fly. Run it explicitly:
//!
//! ```text
//! cargo test -p windows-win32-shim --test hwc_isolation -- --ignored --nocapture
//! ```

#![cfg(windows)]

use std::path::PathBuf;
use std::process::Command;

/// Exit code the harness returns when it cannot run the genuine proof (HWC
/// absent, or the site URL cannot be reserved). Treated as an ignore, not a
/// failure, so the gate never depends on host configuration.
const HARNESS_SKIP: i32 = 2;

/// Absolute path to the HWC engine; its absence means the genuine proof cannot
/// run on this machine.
fn hwebcore_path() -> PathBuf {
    let windir = std::env::var_os("windir")
        .or_else(|| std::env::var_os("SystemRoot"))
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(r"C:\Windows"));
    windir.join(r"System32\inetsrv\hwebcore.dll")
}

/// Path to the orchestration script shipped beside this crate.
fn harness_script() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("hwcproof").join("run-hwcproof.ps1")
}

#[test]
#[ignore = "genuine HWC: needs the HWC feature + a URL reservation; run with --ignored"]
fn hwc_isolation_proof_buffers_module_filesystem_off_the_live_disk() {
    let hwebcore = hwebcore_path();
    if !hwebcore.is_file() {
        eprintln!(
            "ignored: HWC engine not installed ({}); install IIS-HostableWebCore to run this proof",
            hwebcore.display()
        );
        return;
    }

    let script = harness_script();
    assert!(script.is_file(), "harness script missing: {}", script.display());

    let status = Command::new("powershell.exe")
        .args([
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            &script.display().to_string(),
            "-Variant",
            "isolated",
        ])
        .status()
        .expect("failed to launch run-hwcproof.ps1");

    match status.code() {
        Some(0) => { /* PASS: the harness asserted overlay isolation end to end. */ }
        Some(code) if code == HARNESS_SKIP => {
            eprintln!(
                "ignored: HWC present but the genuine proof could not run \
                 (URL unbindable — run elevated or add a urlacl); harness exit {code}"
            );
        }
        other => panic!(
            "genuine HWC isolation proof failed (harness exit {:?}); \
             re-run `run-hwcproof.ps1 -Variant isolated` to see the report",
            other
        ),
    }
}
