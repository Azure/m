// Copyright (c) Microsoft Corporation.

//! The command-line surface (AJ-E4).
//!
//! ```text
//! cartographer --spec <path>... --journal <path>... [--out <dir>]
//!              [--format yaml|json] [--report text|ndjson]
//!              [--update] [--overwrite] [--strict]
//! ```
//!
//! Default: load the specs and journals, validate the observed traffic against
//! the specs, and report diagnostics. With `--update`, also synthesize and merge
//! an updated spec into `--out`. Every byte is written through the supplied
//! [`OutputSink`] (the "one output site" rule). Exit codes: `0` success; `2`
//! under `--strict` when an error-severity diagnostic was found; `1` on an
//! operational problem (unreadable input, missing `--out` for `--update`, or a
//! write failure).
//!
//! Argument parsing is hand-rolled to avoid a dependency; the accepted grammar is
//! owned here.

use std::fs::File;
use std::io::BufReader;
use std::path::{Path, PathBuf};

use api_journal::{JournalRecord, read_records};

use crate::diagnostics::{ReportFormat, Severity, render};
use crate::format::{SpecFormat, load_path, serialize_document};
use crate::merge::merge;
use crate::model::Document;
use crate::path::PathTemplate;
use crate::sink::OutputSink;
use crate::synth::synthesize;
use crate::validate::{SpecIndex, validate_stream};

/// Parsed command-line arguments.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Args {
    /// Spec sources (files or directories).
    pub specs: Vec<PathBuf>,
    /// Journal sources (NDJSON files or directories).
    pub journals: Vec<PathBuf>,
    /// Output directory for `--update`.
    pub out: Option<PathBuf>,
    /// The written spec format (default YAML).
    pub format: SpecFormat,
    /// The diagnostic report format (default text).
    pub report: ReportFormat,
    /// Synthesize and write an updated spec.
    pub update: bool,
    /// In `--update`, replace re-observed structure rather than only adding.
    pub overwrite: bool,
    /// Fail (exit 2) when an error-severity diagnostic is found.
    pub strict: bool,
    /// Print usage and exit.
    pub help: bool,
}

/// The usage text.
#[must_use]
pub fn usage() -> &'static str {
    "cartographer --spec <path>... --journal <path>... [--out <dir>] \
[--format yaml|json] [--report text|ndjson] [--update] [--overwrite] [--strict]"
}

/// Parse arguments (excluding the program name is *not* assumed — `argv[0]` is
/// skipped here).
///
/// # Errors
/// Returns a message for an unknown flag, a missing value, or an invalid choice.
pub fn parse_args(argv: &[String]) -> Result<Args, String> {
    let mut args = Args::default();
    let mut index = 1;
    while index < argv.len() {
        let flag = argv[index].as_str();
        match flag {
            "--spec" => args.specs.push(take_value(argv, &mut index, flag)?.into()),
            "--journal" => args.journals.push(take_value(argv, &mut index, flag)?.into()),
            "--out" => args.out = Some(take_value(argv, &mut index, flag)?.into()),
            "--format" => {
                args.format = match take_value(argv, &mut index, flag)?.as_str() {
                    "yaml" => SpecFormat::Yaml,
                    "json" => SpecFormat::Json,
                    other => return Err(format!("invalid --format '{other}' (expected yaml|json)")),
                }
            }
            "--report" => {
                args.report = match take_value(argv, &mut index, flag)?.as_str() {
                    "text" => ReportFormat::Text,
                    "ndjson" => ReportFormat::Ndjson,
                    other => {
                        return Err(format!("invalid --report '{other}' (expected text|ndjson)"))
                    }
                }
            }
            "--update" => args.update = true,
            "--overwrite" => args.overwrite = true,
            "--strict" => args.strict = true,
            "-h" | "--help" => args.help = true,
            other => return Err(format!("unknown argument: {other}")),
        }
        index += 1;
    }
    Ok(args)
}

/// Consume the value following a flag.
fn take_value(argv: &[String], index: &mut usize, flag: &str) -> Result<String, String> {
    *index += 1;
    argv.get(*index)
        .cloned()
        .ok_or_else(|| format!("{flag} requires a value"))
}

/// Execute the tool, writing all output through `sink`, and return the exit code.
pub fn run(args: &Args, sink: &mut dyn OutputSink) -> i32 {
    if args.help {
        sink.write_line(usage());
        return 0;
    }

    let mut io_problem = false;

    // Load specs.
    let mut documents: Vec<Document> = Vec::new();
    for spec in &args.specs {
        let outcome = load_path(spec);
        for loaded in outcome.specs {
            documents.push(loaded.document);
        }
        for error in outcome.errors {
            sink.write_line(&format!(
                "error[spec_load] {}: {}",
                error.path.display(),
                error.message
            ));
            io_problem = true;
        }
    }

    // Read journals.
    let mut records: Vec<JournalRecord> = Vec::new();
    for journal in &args.journals {
        match read_journal(journal) {
            Ok((mut read, malformed)) => {
                records.append(&mut read);
                if malformed > 0 {
                    sink.write_line(&format!(
                        "warning[journal] {}: {malformed} malformed line(s) skipped",
                        journal.display()
                    ));
                }
            }
            Err(message) => {
                sink.write_line(&format!("error[journal] {}: {message}", journal.display()));
                io_problem = true;
            }
        }
    }

    // Validate and report.
    let index = SpecIndex::from_documents(&documents);
    let diagnostics = validate_stream(&index, &records);
    render(&diagnostics, args.report, sink);
    let had_error = diagnostics.iter().any(|d| d.severity == Severity::Error);

    // Update.
    if args.update {
        match &args.out {
            None => {
                sink.write_line("error[update] --update requires --out <dir>");
                io_problem = true;
            }
            Some(out) => {
                if write_updated_spec(out, args, &documents, &records, sink).is_err() {
                    io_problem = true;
                }
            }
        }
    }

    if io_problem {
        return 1;
    }
    if args.strict && had_error {
        return 2;
    }
    0
}

/// Synthesize an updated spec, merge it into the loaded specs, and write it.
fn write_updated_spec(
    out: &Path,
    args: &Args,
    documents: &[Document],
    records: &[JournalRecord],
    sink: &mut dyn OutputSink,
) -> Result<(), ()> {
    // Use the first loaded spec as the base (preserving its info), combining any
    // others into it; an empty project starts from a fresh document.
    let mut base = documents
        .first()
        .cloned()
        .unwrap_or_else(|| Document::new("openapi", "0.1.0"));
    for document in documents.iter().skip(1) {
        base = merge(base, document, true);
    }

    let existing: Vec<PathTemplate> = base.paths.keys().map(|p| PathTemplate::parse(p)).collect();
    let synthesized = synthesize(records, &existing);
    let merged = merge(base, &synthesized, args.overwrite);

    let text = match serialize_document(&merged, args.format) {
        Ok(text) => text,
        Err(message) => {
            sink.write_line(&format!("error[update] serialization failed: {message}"));
            return Err(());
        }
    };
    let filename = format!("openapi.{}", format_extension(args.format));
    if let Err(error) = std::fs::create_dir_all(out) {
        sink.write_line(&format!("error[update] cannot create {}: {error}", out.display()));
        return Err(());
    }
    let path = out.join(&filename);
    if let Err(error) = std::fs::write(&path, text) {
        sink.write_line(&format!("error[update] cannot write {}: {error}", path.display()));
        return Err(());
    }
    sink.write_line(&format!("wrote {}", path.display()));
    Ok(())
}

fn format_extension(format: SpecFormat) -> &'static str {
    match format {
        SpecFormat::Json => "json",
        SpecFormat::Yaml => "yaml",
    }
}

/// Read journal records from a file, or from every `.ndjson` file in a directory.
fn read_journal(path: &Path) -> Result<(Vec<JournalRecord>, usize), String> {
    if path.is_dir() {
        let read_dir = std::fs::read_dir(path).map_err(|e| e.to_string())?;
        let mut files: Vec<PathBuf> = read_dir
            .filter_map(|entry| entry.ok().map(|entry| entry.path()))
            .filter(|candidate| {
                candidate.is_file()
                    && candidate
                        .extension()
                        .and_then(|ext| ext.to_str())
                        .is_some_and(|ext| ext.eq_ignore_ascii_case("ndjson"))
            })
            .collect();
        files.sort();
        let mut records = Vec::new();
        let mut malformed = 0;
        for file in files {
            let handle = File::open(&file).map_err(|e| e.to_string())?;
            let (mut read, stats) = read_records(BufReader::new(handle));
            records.append(&mut read);
            malformed += stats.malformed;
        }
        Ok((records, malformed))
    } else {
        let handle = File::open(path).map_err(|e| e.to_string())?;
        let (records, stats) = read_records(BufReader::new(handle));
        Ok((records, stats.malformed))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sink::BufferSink;

    fn argv(items: &[&str]) -> Vec<String> {
        std::iter::once("cartographer")
            .chain(items.iter().copied())
            .map(|s| s.to_string())
            .collect()
    }

    #[test]
    fn parses_all_options() {
        let args = parse_args(&argv(&[
            "--spec", "a.yaml", "--spec", "b.json", "--journal", "j.ndjson", "--out", "out",
            "--format", "json", "--report", "ndjson", "--update", "--overwrite", "--strict",
        ]))
        .expect("parse");
        assert_eq!(args.specs.len(), 2);
        assert_eq!(args.journals.len(), 1);
        assert_eq!(args.out, Some(PathBuf::from("out")));
        assert_eq!(args.format, SpecFormat::Json);
        assert_eq!(args.report, ReportFormat::Ndjson);
        assert!(args.update && args.overwrite && args.strict);
    }

    #[test]
    fn defaults_are_yaml_text_and_no_flags() {
        let args = parse_args(&argv(&["--journal", "j.ndjson"])).expect("parse");
        assert_eq!(args.format, SpecFormat::Yaml);
        assert_eq!(args.report, ReportFormat::Text);
        assert!(!args.update && !args.strict && !args.overwrite);
    }

    #[test]
    fn rejects_unknown_flag_and_bad_choice_and_missing_value() {
        assert!(parse_args(&argv(&["--nope"])).is_err());
        assert!(parse_args(&argv(&["--format", "toml"])).is_err());
        assert!(parse_args(&argv(&["--spec"])).is_err());
    }

    #[test]
    fn help_flag_is_recognized() {
        let args = parse_args(&argv(&["--help"])).expect("parse");
        assert!(args.help);
        let mut sink = BufferSink::new();
        assert_eq!(run(&args, &mut sink), 0);
        assert_eq!(sink.lines().len(), 1);
    }

    fn temp_dir() -> PathBuf {
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let dir = std::env::temp_dir().join(format!("cartographer-cli-{}-{nanos}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    /// Write a minimal spec and a journal with one undocumented-path record.
    fn fixture(dir: &Path) -> (PathBuf, PathBuf) {
        use api_journal::{JournalRecord, Seam, write_record};
        let spec_path = dir.join("spec.yaml");
        let mut doc = Document::new("svc", "1.0.0");
        doc.paths.insert("/healthz".to_string(), Default::default());
        std::fs::write(&spec_path, serialize_document(&doc, SpecFormat::Yaml).unwrap()).unwrap();

        let journal_path = dir.join("j.ndjson");
        let record = JournalRecord {
            seam: Seam::Inbound,
            method: "GET".into(),
            path: "/admin".into(), // not in the spec -> undocumented path
            status: 200,
            ..Default::default()
        };
        let mut buffer = Vec::new();
        write_record(&mut buffer, &record).unwrap();
        std::fs::write(&journal_path, buffer).unwrap();
        (spec_path, journal_path)
    }

    #[test]
    fn run_reports_diagnostics_and_strict_sets_exit_code() {
        let dir = temp_dir();
        let (spec, journal) = fixture(&dir);

        // Non-strict: reports the finding, exit 0.
        let args = parse_args(&argv(&[
            "--spec",
            spec.to_str().unwrap(),
            "--journal",
            journal.to_str().unwrap(),
        ]))
        .unwrap();
        let mut sink = BufferSink::new();
        assert_eq!(run(&args, &mut sink), 0);
        assert!(sink.lines().iter().any(|l| l.contains("undocumented_path")));

        // Strict: same finding now fails with exit 2.
        let strict = parse_args(&argv(&[
            "--spec",
            spec.to_str().unwrap(),
            "--journal",
            journal.to_str().unwrap(),
            "--strict",
        ]))
        .unwrap();
        let mut sink = BufferSink::new();
        assert_eq!(run(&strict, &mut sink), 2);

        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn run_update_writes_a_spec_file() {
        let dir = temp_dir();
        let (spec, journal) = fixture(&dir);
        let out = dir.join("out");

        let args = parse_args(&argv(&[
            "--spec",
            spec.to_str().unwrap(),
            "--journal",
            journal.to_str().unwrap(),
            "--out",
            out.to_str().unwrap(),
            "--update",
        ]))
        .unwrap();
        let mut sink = BufferSink::new();
        assert_eq!(run(&args, &mut sink), 0);
        let written = out.join("openapi.yaml");
        assert!(written.is_file(), "spec written");
        // The synthesized spec now documents the previously-undocumented /admin.
        let text = std::fs::read_to_string(&written).unwrap();
        assert!(text.contains("/admin"), "{text}");

        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn run_update_without_out_is_an_operational_error() {
        let dir = temp_dir();
        let (spec, journal) = fixture(&dir);
        let args = parse_args(&argv(&[
            "--spec",
            spec.to_str().unwrap(),
            "--journal",
            journal.to_str().unwrap(),
            "--update",
        ]))
        .unwrap();
        let mut sink = BufferSink::new();
        assert_eq!(run(&args, &mut sink), 1);
        assert!(sink.lines().iter().any(|l| l.contains("requires --out")));
        std::fs::remove_dir_all(&dir).ok();
    }
}
