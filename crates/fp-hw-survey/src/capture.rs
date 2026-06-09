// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Phase 1: drive every supported catalogued op over a deterministic operand ×
//! mode corpus on the local hardware and stream native `(result, flags)` rows
//! to an NDJSON file.

use std::fs::File;
use std::io::{BufWriter, Write};

use crate::arch;
use crate::corpus;
use crate::host;
use crate::jsonio::ObjectWriter;
use crate::mode;
use crate::ops::{self, Fmt, Kind};

/// Capture configuration assembled from CLI args.
pub struct Config {
    pub label: String,
    pub out: String,
    /// Number of random operand draws per op (on top of the curated edge cases).
    pub pairs: usize,
    /// Hard cap on output size in bytes; capture stops once exceeded.
    pub budget_bytes: u64,
    /// If non-empty, only ops whose label is in this list are captured.
    pub only_ops: Vec<String>,
}

fn float_edges(fmt: Fmt) -> Vec<u64> {
    match fmt {
        Fmt::Single => corpus::SINGLE.iter().map(|&x| x as u64).collect(),
        Fmt::Double => corpus::DOUBLE.to_vec(),
        Fmt::Half => corpus::HALF.iter().map(|&x| x as u64).collect(),
    }
}

fn float_fill(fmt: Fmt, stream: u64, n: usize) -> Vec<u64> {
    match fmt {
        Fmt::Single => corpus::fill_u32(stream, n)
            .into_iter()
            .map(|x| x as u64)
            .collect(),
        Fmt::Double => corpus::fill_u64(stream, n),
        Fmt::Half => corpus::fill_u16(stream, n)
            .into_iter()
            .map(|x| x as u64)
            .collect(),
    }
}

fn int_edges(width: u8) -> Vec<u64> {
    if width == 32 {
        corpus::INTS.iter().map(|&x| x & 0xFFFF_FFFF).collect()
    } else {
        corpus::INTS.to_vec()
    }
}

fn int_fill(width: u8, stream: u64, n: usize) -> Vec<u64> {
    if width == 32 {
        corpus::fill_u32(stream, n)
            .into_iter()
            .map(|x| x as u64)
            .collect()
    } else {
        corpus::fill_u64(stream, n)
    }
}

/// Build the `(a, b, c)` operand triples for one op. `c` is `0` for non-FMA ops.
fn operands(spec: &ops::OpSpec, op_idx: usize, pairs: usize) -> Vec<(u64, u64, u64)> {
    let s0 = (op_idx as u64) * 8;
    let s1 = s0 + 1;
    let s2 = s0 + 2;
    let mut out = Vec::new();
    match spec.kind {
        Kind::Bin(fmt) => {
            let e = float_edges(fmt);
            for &a in &e {
                for &b in &e {
                    out.push((a, b, 0));
                }
            }
            let fa = float_fill(fmt, s0, pairs);
            let fb = float_fill(fmt, s1, pairs);
            for i in 0..pairs {
                out.push((fa[i], fb[i], 0));
            }
        }
        Kind::Un(fmt) => {
            for &a in &float_edges(fmt) {
                out.push((a, 0, 0));
            }
            for a in float_fill(fmt, s0, pairs) {
                out.push((a, 0, 0));
            }
        }
        Kind::Fma(fmt) => {
            let e = float_edges(fmt);
            // Curated coverage: edge a × edge b with c in {0.0, 1.0, -1.0}.
            let cs: [u64; 3] = match fmt {
                Fmt::Single => [0x0000_0000, 0x3f80_0000, 0xbf80_0000],
                Fmt::Double => [
                    0x0000_0000_0000_0000,
                    0x3ff0_0000_0000_0000,
                    0xbff0_0000_0000_0000,
                ],
                Fmt::Half => [0x0000, 0x3c00, 0xbc00],
            };
            for &a in &e {
                for &b in &e {
                    for &cc in &cs {
                        out.push((a, b, cc));
                    }
                }
            }
            let fa = float_fill(fmt, s0, pairs);
            let fb = float_fill(fmt, s1, pairs);
            let fc = float_fill(fmt, s2, pairs);
            for i in 0..pairs {
                out.push((fa[i], fb[i], fc[i]));
            }
        }
        Kind::CvtF2F { from, .. } => {
            for &a in &float_edges(from) {
                out.push((a, 0, 0));
            }
            for a in float_fill(from, s0, pairs) {
                out.push((a, 0, 0));
            }
        }
        Kind::CvtF2I { from, .. } => {
            for &a in &float_edges(from) {
                out.push((a, 0, 0));
            }
            for a in float_fill(from, s0, pairs) {
                out.push((a, 0, 0));
            }
        }
        Kind::CvtI2F { width, .. } => {
            for &a in &int_edges(width) {
                out.push((a, 0, 0));
            }
            for a in int_fill(width, s0, pairs) {
                out.push((a, 0, 0));
            }
        }
    }
    out
}

/// Run a capture. Returns `(ops_captured, rows_written)`.
pub fn run(cfg: &Config) -> std::io::Result<(usize, u64)> {
    let file = File::create(&cfg.out)?;
    let mut w = BufWriter::new(file);

    // Header line.
    let captured_unix = host::now_unix();
    let mut h = ObjectWriter::new();
    h.str_field("kind", "header")
        .u64_field("schema", 1)
        .str_field("arch", host::arch_name())
        .str_field("arch_tag", arch::arch_tag())
        .str_field("os", host::os_name())
        .str_field("cpu", &host::cpu_brand())
        .str_field("label", &cfg.label)
        .str_array_field("features", &host::features())
        .u64_field("captured_unix", captured_unix)
        .str_field("captured_utc", &host::unix_to_iso_utc(captured_unix))
        .str_field("tool_version", env!("CARGO_PKG_VERSION"));
    let header = h.finish();
    writeln!(w, "{header}")?;
    let mut bytes: u64 = header.len() as u64 + 1;

    let modes = mode::all_modes();
    let catalogue = ops::catalogue();
    let mut ops_captured = 0usize;
    let mut rows: u64 = 0;

    'outer: for (op_idx, spec) in catalogue.iter().enumerate() {
        if !cfg.only_ops.is_empty() && !cfg.only_ops.iter().any(|o| o == spec.label) {
            continue;
        }
        if !arch::supports(spec.label) {
            continue;
        }
        let triples = operands(spec, op_idx, cfg.pairs);
        let mut any = false;
        for &(a, b, c) in &triples {
            for &m in &modes {
                let Some((res, flags)) = arch::eval(spec.label, a, b, c, m) else {
                    continue;
                };
                any = true;
                let mut row = ObjectWriter::new();
                row.str_field("op", spec.label)
                    .u64_field("a", a)
                    .u64_field("b", b)
                    .u64_field("c", c)
                    .str_field("mode", m.round.key())
                    .bool_field("flush", m.flush)
                    .u64_field("res", res)
                    .u64_field("flags", flags as u64);
                let line = row.finish();
                writeln!(w, "{line}")?;
                bytes += line.len() as u64 + 1;
                rows += 1;
                if bytes >= cfg.budget_bytes {
                    eprintln!(
                        "budget of {} bytes reached after {} rows; stopping early",
                        cfg.budget_bytes, rows
                    );
                    if any {
                        ops_captured += 1;
                    }
                    break 'outer;
                }
            }
        }
        if any {
            ops_captured += 1;
        }
    }

    w.flush()?;
    Ok((ops_captured, rows))
}
