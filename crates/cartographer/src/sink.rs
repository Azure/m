// Copyright (c) Microsoft Corporation.

//! The output sink abstraction (AJ-C1).
//!
//! Per the repository's "one output site" rule, every byte cartographer emits —
//! diagnostics and generated spec text alike — flows through a single
//! [`OutputSink`]. The concrete destination (standard output, a file, or an
//! in-memory buffer for tests) is thereby separable from the code that produces
//! the content, and the tool never calls `println!`/`print!` from more than one
//! place.

use std::io::{self, Write};

/// A line-oriented sink for cartographer's textual output.
///
/// Implementations append a newline after each line. The single standard-output
/// implementation is [`StdoutSink`]; [`BufferSink`] captures output in memory.
pub trait OutputSink {
    /// Write one line of output (a trailing newline is appended).
    fn write_line(&mut self, line: &str);
}

/// The one [`OutputSink`] that writes to standard output — the sole site in the
/// tool that touches `stdout`.
pub struct StdoutSink {
    out: io::Stdout,
}

impl StdoutSink {
    /// Create a sink over the process's standard output.
    #[must_use]
    pub fn new() -> Self {
        Self { out: io::stdout() }
    }
}

impl Default for StdoutSink {
    fn default() -> Self {
        Self::new()
    }
}

impl OutputSink for StdoutSink {
    fn write_line(&mut self, line: &str) {
        // Best-effort: a closed pipe (e.g. `cartographer … | head`) must not panic.
        let _ = writeln!(self.out, "{line}");
    }
}

/// An [`OutputSink`] that collects emitted lines in memory, for tests and for
/// capturing output to compare or redirect.
#[derive(Debug, Default)]
pub struct BufferSink {
    lines: Vec<String>,
}

impl BufferSink {
    /// A new, empty buffer sink.
    #[must_use]
    pub fn new() -> Self {
        Self { lines: Vec::new() }
    }

    /// The captured lines, in order.
    #[must_use]
    pub fn lines(&self) -> &[String] {
        &self.lines
    }

    /// The captured output as a single newline-joined string.
    #[must_use]
    pub fn into_text(self) -> String {
        self.lines.join("\n")
    }
}

impl OutputSink for BufferSink {
    fn write_line(&mut self, line: &str) {
        self.lines.push(line.to_string());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn buffer_sink_collects_lines_in_order() {
        let mut sink = BufferSink::new();
        sink.write_line("first");
        sink.write_line("second");
        sink.write_line("third");
        assert_eq!(sink.lines(), ["first", "second", "third"]);
    }

    #[test]
    fn buffer_sink_joins_into_text() {
        let mut sink = BufferSink::new();
        sink.write_line("a");
        sink.write_line("b");
        assert_eq!(sink.into_text(), "a\nb");
    }

    #[test]
    fn empty_buffer_sink_is_empty() {
        let sink = BufferSink::new();
        assert!(sink.lines().is_empty());
        assert_eq!(sink.into_text(), "");
    }

    #[test]
    fn output_sink_is_usable_through_a_trait_object() {
        let mut sink = BufferSink::new();
        {
            let dyn_sink: &mut dyn OutputSink = &mut sink;
            dyn_sink.write_line("via dyn");
        }
        assert_eq!(sink.lines(), ["via dyn"]);
    }
}
