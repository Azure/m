// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT
#![cfg(not(any(target_arch = "aarch64", target_arch = "x86_64")))]

//! Fallback oracle for architectures this tool has no hand-written asm for.
//! It supports nothing; `capture` on such a host produces only a header line.

use crate::mode::Mode;

pub fn arch_tag() -> &'static str {
    "unsupported"
}

pub fn supports(_label: &str) -> bool {
    false
}

pub fn eval(_label: &str, _a: u64, _b: u64, _c: u64, _mode: Mode) -> Option<(u64, u32)> {
    None
}
