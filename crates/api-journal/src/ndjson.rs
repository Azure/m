// Copyright (c) Microsoft Corporation.

//! Tolerant NDJSON read/write helpers (AJ-A4).
//!
//! Our specified on-disk format is newline-delimited JSON: exactly one compact
//! [`JournalRecord`] per line. [`write_record`] realizes that format;
//! [`read_records`] consumes it.
//!
//! Reading is deliberately *tolerant* because journals are produced on many machines and
//! gathered by concatenation, copy, and transfer that may introduce blank lines, comments,
//! a truncated trailing line, or the occasional corrupted line. None of those abort the
//! read: blank and comment (`#` / `//`) lines are skipped, malformed lines are counted and
//! skipped, and every well-formed record is returned. We use `serde_json` to satisfy this
//! format because its behavior matches our specification — the format is ours, not "whatever
//! serde does."

use std::io::{self, BufRead, Write};

use crate::record::JournalRecord;

/// Counts of lines that did not yield a record while reading a journal.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ReadStats {
    /// Lines that failed to parse as a [`JournalRecord`] (malformed JSON or wrong shape, or
    /// a line that was not valid UTF-8).
    pub malformed: usize,
    /// Blank lines and comment lines (`#` / `//`) skipped — these are not errors.
    pub skipped_blank: usize,
}

/// Append one record to `writer` as a single NDJSON line (compact JSON + `\n`).
///
/// The caller supplies the destination; for the shim this is an append-mode file handle so
/// records accumulate across the process's lifetime.
///
/// # Errors
/// Returns any I/O error from serializing or writing.
pub fn write_record<W: Write>(writer: &mut W, record: &JournalRecord) -> io::Result<()> {
    serde_json::to_writer(&mut *writer, record)?;
    writer.write_all(b"\n")
}

/// Read every well-formed record from a journal, tolerating blank, comment, and malformed
/// lines.
///
/// Returns the records in file order plus [`ReadStats`] describing what was skipped. Never
/// fails on malformed content; an I/O error from the underlying reader surfaces as a
/// malformed-line count rather than aborting the whole read.
pub fn read_records<R: BufRead>(reader: R) -> (Vec<JournalRecord>, ReadStats) {
    let mut records = Vec::new();
    let mut stats = ReadStats::default();
    for line in reader.lines() {
        let line = match line {
            Ok(line) => line,
            // e.g. a line that is not valid UTF-8: count and continue.
            Err(_) => {
                stats.malformed += 1;
                continue;
            }
        };
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed.starts_with('#') || trimmed.starts_with("//") {
            stats.skipped_blank += 1;
            continue;
        }
        match serde_json::from_str::<JournalRecord>(trimmed) {
            Ok(record) => records.push(record),
            Err(_) => stats.malformed += 1,
        }
    }
    (records, stats)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::record::{HeaderField, Seam};
    use crate::shape::BodyShape;

    fn record(seq: u64, path: &str) -> JournalRecord {
        JournalRecord {
            seam: Seam::Egress,
            method: "GET".into(),
            path: path.into(),
            status: 200,
            response_headers: vec![HeaderField {
                name: "Content-Type".into(),
                value: Some("application/json".into()),
            }],
            response_body: BodyShape::String,
            timestamp_ms: 1,
            session_id: 99,
            seq,
            ..Default::default()
        }
    }

    fn render(records: &[JournalRecord]) -> Vec<u8> {
        let mut buf = Vec::new();
        for r in records {
            write_record(&mut buf, r).expect("write");
        }
        buf
    }

    #[test]
    fn single_record_round_trips() {
        let original = record(1, "/healthz");
        let bytes = render(std::slice::from_ref(&original));
        let (back, stats) = read_records(bytes.as_slice());
        assert_eq!(back, vec![original]);
        assert_eq!(stats, ReadStats::default());
    }

    #[test]
    fn many_records_preserve_order() {
        let originals: Vec<_> = (0..300).map(|i| record(i, "/custom/word")).collect();
        let bytes = render(&originals);
        let (back, stats) = read_records(bytes.as_slice());
        assert_eq!(back, originals);
        assert_eq!(stats, ReadStats::default());
    }

    #[test]
    fn one_line_per_record_no_embedded_newlines() {
        let originals = vec![record(1, "/a"), record(2, "/b"), record(3, "/c")];
        let bytes = render(&originals);
        let text = String::from_utf8(bytes).expect("utf8");
        assert_eq!(text.matches('\n').count(), 3);
        assert_eq!(text.lines().count(), 3);
    }

    #[test]
    fn blank_lines_are_skipped_not_fatal() {
        let mut bytes = render(&[record(1, "/a")]);
        bytes.extend_from_slice(b"\n   \n");
        bytes.extend(render(&[record(2, "/b")]));
        let (back, stats) = read_records(bytes.as_slice());
        assert_eq!(back.len(), 2);
        assert_eq!(stats.malformed, 0);
        assert_eq!(stats.skipped_blank, 2);
    }

    #[test]
    fn comment_lines_are_skipped() {
        let mut bytes = b"# a header comment\n// another comment\n".to_vec();
        bytes.extend(render(&[record(1, "/a")]));
        let (back, stats) = read_records(bytes.as_slice());
        assert_eq!(back.len(), 1);
        assert_eq!(stats.skipped_blank, 2);
        assert_eq!(stats.malformed, 0);
    }

    #[test]
    fn malformed_line_is_counted_and_others_survive() {
        let mut bytes = render(&[record(1, "/a")]);
        bytes.extend_from_slice(b"{not valid json\n");
        bytes.extend(render(&[record(2, "/b")]));
        let (back, stats) = read_records(bytes.as_slice());
        assert_eq!(back.len(), 2);
        assert_eq!(stats.malformed, 1);
        assert_eq!(stats.skipped_blank, 0);
    }

    #[test]
    fn truncated_trailing_line_without_newline_parses() {
        let mut bytes = render(&[record(1, "/a")]);
        // Append a second record's JSON with no trailing newline.
        serde_json::to_writer(&mut bytes, &record(2, "/b")).expect("write");
        let (back, stats) = read_records(bytes.as_slice());
        assert_eq!(back.len(), 2);
        assert_eq!(stats.malformed, 0);
    }

    #[test]
    fn empty_input_yields_nothing() {
        let (back, stats) = read_records(b"".as_slice());
        assert!(back.is_empty());
        assert_eq!(stats, ReadStats::default());
    }
}
