// Copyright (c) Microsoft Corporation.

//! `talky` — a Microsoft-native (WinHTTP) stress client for `wordy`.
//!
//! `talky` drives configurable load against a running `wordy` service using the
//! native WinHTTP API for transport and the Windows thread pool for concurrency
//! — the native-stack counterpart to the Rust-native `squeaky` client. All
//! parameters come from a JSON config file (default `talky.json`, or the path
//! given as the first argument); IPv4 and IPv6 endpoints are both supported.
//!
//! Because it depends on WinHTTP and `windows-threadpool`, `talky` is
//! Windows-only; on other platforms it builds to a stub that points at
//! `squeaky`.

#![deny(unsafe_code)]

#[cfg(windows)]
mod config;
#[cfg(windows)]
mod driver;
#[cfg(windows)]
mod stats;
#[cfg(windows)]
#[allow(unsafe_code)]
mod winhttp;
#[cfg(windows)]
mod workload;

#[cfg(windows)]
fn main() -> std::process::ExitCode {
    use std::path::Path;

    let path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "talky.json".to_string());

    let config = match config::Config::load(Path::new(&path)) {
        Ok(config) => config,
        Err(e) => {
            eprintln!("talky: {e}");
            return std::process::ExitCode::FAILURE;
        }
    };

    match driver::run(&config) {
        Ok(report) => {
            print!("{}", report.render());
            std::process::ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("talky: {e}");
            std::process::ExitCode::FAILURE
        }
    }
}

#[cfg(not(windows))]
fn main() -> std::process::ExitCode {
    eprintln!(
        "talky uses the Microsoft-native WinHTTP API and runs on Windows only.\n\
         Use the Rust-native `squeaky` client on other platforms."
    );
    std::process::ExitCode::FAILURE
}
