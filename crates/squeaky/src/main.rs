// Copyright (c) Microsoft Corporation.

//! `squeaky` — a Rust-native (Tokio + `reqwest`) stress client for `wordy`.
//!
//! `squeaky` drives configurable load against a running `wordy` service using
//! the native Rust async web stack — the counterpart to the WinHTTP-based
//! `talky` client. All parameters come from a JSON config file (default
//! `squeaky.json`, or the path given as the first argument); IPv4 and IPv6
//! endpoints are both supported. Being pure Rust, `squeaky` is cross-platform.

#![deny(unsafe_code)]

mod config;
mod driver;
mod stats;
mod workload;

use std::path::Path;
use std::process::ExitCode;

#[tokio::main]
async fn main() -> ExitCode {
    let path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "squeaky.json".to_string());

    let config = match config::Config::load(Path::new(&path)) {
        Ok(config) => config,
        Err(e) => {
            eprintln!("squeaky: {e}");
            return ExitCode::FAILURE;
        }
    };

    match driver::run(&config).await {
        Ok(report) => {
            print!("{}", report.render());
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("squeaky: {e}");
            ExitCode::FAILURE
        }
    }
}
