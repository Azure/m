// Copyright (c) Microsoft Corporation.

//! `cartographer` command-line entry point.
//!
//! The full argument surface (`--spec`, `--journal`, `--out`, `--format`,
//! `--report`, `--update`, `--strict`) lands in AJ-E4. For now this establishes
//! the single standard-output site via [`StdoutSink`] so all later output routes
//! through the one sink.

use cartographer::{OutputSink, StdoutSink};

fn main() {
    let mut sink = StdoutSink::new();
    sink.write_line("cartographer: no command yet (the CLI arrives in AJ-E4)");
}
