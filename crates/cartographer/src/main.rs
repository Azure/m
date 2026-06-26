// Copyright (c) Microsoft Corporation.

//! `cartographer` command-line entry point. Parses arguments, runs the tool with
//! all output routed through the single [`StdoutSink`], and exits with the tool's
//! status code.

use std::process::ExitCode;

use cartographer::{OutputSink, StdoutSink, cli};

fn main() -> ExitCode {
    let argv: Vec<String> = std::env::args().collect();
    let mut sink = StdoutSink::new();
    match cli::parse_args(&argv) {
        Ok(args) => {
            let code = cli::run(&args, &mut sink);
            ExitCode::from(u8::try_from(code).unwrap_or(1))
        }
        Err(message) => {
            sink.write_line(&format!("error: {message}"));
            sink.write_line(cli::usage());
            ExitCode::from(64) // EX_USAGE
        }
    }
}
