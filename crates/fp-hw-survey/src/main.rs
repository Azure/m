// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! `fp-hw-survey` — capture native floating-point hardware behavior across many
//! machines and architectures, then merge captures to find where the hardware
//! actually disagrees.
//!
//! Subcommands:
//!   capture   Run the corpus on this machine, write an NDJSON capture file.
//!   merge     Combine capture files and emit only the divergent rows.
//!   selftest  Run known-answer checks for this machine's oracle.
//!   info      Print host identity and supported-op count.

mod arch;
mod capture;
mod corpus;
mod host;
mod jsonio;
mod merge;
mod mode;
mod normflags;
mod ops;
mod selftest;

use std::process::ExitCode;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() {
        usage();
        return ExitCode::from(2);
    }
    match args[0].as_str() {
        "capture" => cmd_capture(&args[1..]),
        "merge" => cmd_merge(&args[1..]),
        "selftest" => cmd_selftest(),
        "info" => cmd_info(),
        "-h" | "--help" | "help" => {
            usage();
            ExitCode::SUCCESS
        }
        other => {
            eprintln!("unknown subcommand: {other}");
            usage();
            ExitCode::from(2)
        }
    }
}

fn usage() {
    eprintln!(
        "fp-hw-survey {}\n\
         \n\
         USAGE:\n\
         \x20 fp-hw-survey capture --label <name> [--out <file>] [--pairs <N>] \\\n\
         \x20                      [--budget-mb <N>] [--ops <a,b,...>]\n\
         \x20 fp-hw-survey merge --out <file> <capture.ndjson>...\n\
         \x20 fp-hw-survey selftest\n\
         \x20 fp-hw-survey info\n\
         \n\
         capture options:\n\
         \x20 --label      Required. Human name for this machine (e.g. \"m2-macbook\").\n\
         \x20 --out        Output file (default: capture-<label>.ndjson).\n\
         \x20 --pairs      Random operand draws per op (default 2000).\n\
         \x20 --budget-mb  Hard output-size cap in MB (default 150).\n\
         \x20 --ops        Comma-separated op labels to restrict capture to.",
        env!("CARGO_PKG_VERSION")
    );
}

/// Pull the value following `flag` from `args`, or `None` if absent.
fn flag_value<'a>(args: &'a [String], flag: &str) -> Option<&'a str> {
    args.iter()
        .position(|a| a == flag)
        .and_then(|i| args.get(i + 1))
        .map(|s| s.as_str())
}

fn cmd_capture(args: &[String]) -> ExitCode {
    let Some(label) = flag_value(args, "--label") else {
        eprintln!("error: --label is required");
        return ExitCode::from(2);
    };
    let out = flag_value(args, "--out")
        .map(|s| s.to_string())
        .unwrap_or_else(|| format!("capture-{}.ndjson", sanitize(label)));
    let pairs = flag_value(args, "--pairs")
        .and_then(|s| s.parse::<usize>().ok())
        .unwrap_or(2000);
    let budget_mb = flag_value(args, "--budget-mb")
        .and_then(|s| s.parse::<u64>().ok())
        .unwrap_or(150);
    let only_ops: Vec<String> = flag_value(args, "--ops")
        .map(|s| {
            s.split(',')
                .map(|x| x.trim().to_string())
                .filter(|x| !x.is_empty())
                .collect()
        })
        .unwrap_or_default();

    // Self-test gate: never emit data from an oracle that fails known answers.
    match selftest::run() {
        Ok(n) => eprintln!("selftest passed ({n} checks) on arch {}", arch::arch_tag()),
        Err(fails) => {
            eprintln!("SELF-TEST FAILED — refusing to capture:");
            for f in &fails {
                eprintln!("  {f}");
            }
            return ExitCode::FAILURE;
        }
    }

    let cfg = capture::Config {
        label: label.to_string(),
        out: out.clone(),
        pairs,
        budget_bytes: budget_mb.saturating_mul(1024 * 1024),
        only_ops,
    };

    match capture::run(&cfg) {
        Ok((ops_n, rows)) => {
            eprintln!("captured {ops_n} ops, {rows} rows -> {out}");
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("capture error: {e}");
            ExitCode::FAILURE
        }
    }
}

fn cmd_merge(args: &[String]) -> ExitCode {
    let Some(out) = flag_value(args, "--out") else {
        eprintln!("error: merge requires --out <file>");
        return ExitCode::from(2);
    };
    let inputs: Vec<String> = args
        .iter()
        .filter(|a| !a.starts_with("--") && a.as_str() != out)
        .cloned()
        .collect();
    if inputs.len() < 2 {
        eprintln!("error: merge needs at least two capture files");
        return ExitCode::from(2);
    }
    match merge::run(out, &inputs) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("merge error: {e}");
            ExitCode::FAILURE
        }
    }
}

fn cmd_selftest() -> ExitCode {
    match selftest::run() {
        Ok(n) => {
            println!("selftest passed: {n} checks on arch {}", arch::arch_tag());
            ExitCode::SUCCESS
        }
        Err(fails) => {
            println!("selftest FAILED:");
            for f in &fails {
                println!("  {f}");
            }
            ExitCode::FAILURE
        }
    }
}

fn cmd_info() -> ExitCode {
    let supported = ops::catalogue()
        .iter()
        .filter(|o| arch::supports(o.label))
        .count();
    println!("arch:      {}", host::arch_name());
    println!("arch_tag:  {}", arch::arch_tag());
    println!("os:        {}", host::os_name());
    println!("cpu:       {}", host::cpu_brand());
    println!("features:  {}", host::features().join(", "));
    println!("catalogue: {} ops", ops::catalogue().len());
    println!("supported: {supported} ops on this host");
    ExitCode::SUCCESS
}

/// Make a label safe for use in a default filename.
fn sanitize(s: &str) -> String {
    s.chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect()
}
