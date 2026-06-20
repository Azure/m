// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Phase 2: align capture files row-by-row and emit only the rows where the
//! machines actually disagree. Because every machine generates an identical
//! deterministic corpus, rows align on the logical key
//! `(op, a, b, c, mode, flush)`. A key whose `(res, flags)` pair is not unanimous
//! across the machines that produced it is a divergence — the small, valuable
//! artifact this whole tool exists to find.

use std::collections::BTreeMap;
use std::fs::File;
use std::io::{BufRead, BufReader, BufWriter, Write};

use crate::jsonio::{self, ObjectWriter};
use crate::normflags;

#[derive(Clone)]
struct Entry {
    machine: String,
    arch: String,
    res: u64,
    flags: u32,
}

struct RowKey {
    op: String,
    a: u64,
    b: u64,
    c: u64,
    mode: String,
    flush: bool,
}

fn key_string(k: &RowKey) -> String {
    format!(
        "{}|{}|{}|{}|{}|{}",
        k.op, k.a, k.b, k.c, k.mode, k.flush as u8
    )
}

/// Load one capture file, pushing its rows into `map`. Returns the machine label.
fn load(path: &str, map: &mut BTreeMap<String, Vec<Entry>>) -> std::io::Result<String> {
    let f = File::open(path)?;
    let r = BufReader::new(f);
    let mut machine = String::new();
    let mut arch = String::new();
    for line in r.lines() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }
        let Some(obj) = jsonio::parse_object(&line) else {
            continue;
        };
        if let Some(kind) = obj.get("kind").and_then(|v| v.as_str()) {
            if kind == "header" {
                machine = obj
                    .get("label")
                    .and_then(|v| v.as_str())
                    .unwrap_or("?")
                    .to_string();
                arch = obj
                    .get("arch_tag")
                    .or_else(|| obj.get("arch"))
                    .and_then(|v| v.as_str())
                    .unwrap_or("?")
                    .to_string();
                continue;
            }
        }
        let (Some(op), Some(a), Some(b), Some(c), Some(mode), Some(flush), Some(res), Some(flags)) = (
            obj.get("op").and_then(|v| v.as_str()),
            obj.get("a").and_then(|v| v.as_u64()),
            obj.get("b").and_then(|v| v.as_u64()),
            obj.get("c").and_then(|v| v.as_u64()),
            obj.get("mode").and_then(|v| v.as_str()),
            obj.get("flush").and_then(|v| v.as_bool()),
            obj.get("res").and_then(|v| v.as_u64()),
            obj.get("flags").and_then(|v| v.as_u64()),
        ) else {
            continue;
        };
        let k = RowKey {
            op: op.to_string(),
            a,
            b,
            c,
            mode: mode.to_string(),
            flush,
        };
        map.entry(key_string(&k)).or_default().push(Entry {
            machine: machine.clone(),
            arch: arch.clone(),
            res,
            flags: flags as u32,
        });
    }
    if machine.is_empty() {
        machine = path.to_string();
    }
    Ok(machine)
}

/// Run the merge. Writes a divergence NDJSON file and prints a summary.
pub fn run(out_path: &str, inputs: &[String]) -> std::io::Result<()> {
    let mut map: BTreeMap<String, Vec<Entry>> = BTreeMap::new();
    let mut machines = Vec::new();
    for path in inputs {
        let m = load(path, &mut map)?;
        eprintln!("loaded {path} (machine: {m})");
        machines.push(m);
    }

    let out = File::create(out_path)?;
    let mut w = BufWriter::new(out);

    // Header.
    let mut h = ObjectWriter::new();
    h.str_field("kind", "merge-header")
        .u64_field("machines", machines.len() as u64)
        .str_array_field("machine_labels", &machines)
        .str_field("tool_version", env!("CARGO_PKG_VERSION"));
    writeln!(w, "{}", h.finish())?;

    let mut divergences: u64 = 0;
    let mut per_op: BTreeMap<String, u64> = BTreeMap::new();
    let mut total_keys: u64 = 0;

    for (key, entries) in &map {
        total_keys += 1;
        // Only meaningful when at least two machines produced this key.
        if entries.len() < 2 {
            continue;
        }
        let first = (entries[0].res, entries[0].flags);
        let unanimous = entries.iter().all(|e| (e.res, e.flags) == first);
        if unanimous {
            continue;
        }
        divergences += 1;

        // key = "op|a|b|c|mode|flush"
        let parts: Vec<&str> = key.split('|').collect();
        let op = parts[0];
        *per_op.entry(op.to_string()).or_default() += 1;

        let mut row = ObjectWriter::new();
        row.str_field("op", op)
            .u64_field("a", parts[1].parse().unwrap_or(0))
            .u64_field("b", parts[2].parse().unwrap_or(0))
            .u64_field("c", parts[3].parse().unwrap_or(0))
            .str_field("mode", parts[4])
            .bool_field("flush", parts[5] == "1");
        // Per-machine results as a flat parallel encoding.
        let machines_str: Vec<String> = entries.iter().map(|e| e.machine.clone()).collect();
        let arches_str: Vec<String> = entries.iter().map(|e| e.arch.clone()).collect();
        let res_str: Vec<String> = entries.iter().map(|e| e.res.to_string()).collect();
        let flags_str: Vec<String> = entries.iter().map(|e| normflags::render(e.flags)).collect();
        row.str_array_field("machines", &machines_str)
            .str_array_field("arches", &arches_str)
            .str_array_field("res", &res_str)
            .str_array_field("flags", &flags_str);
        writeln!(w, "{}", row.finish())?;
    }

    w.flush()?;

    eprintln!("---- merge summary ----");
    eprintln!("machines:     {}", machines.len());
    eprintln!("aligned keys: {total_keys}");
    eprintln!("divergences:  {divergences}");
    if !per_op.is_empty() {
        eprintln!("by op:");
        for (op, n) in &per_op {
            eprintln!("  {op:<14} {n}");
        }
    }
    eprintln!("written to:   {out_path}");
    Ok(())
}
