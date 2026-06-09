// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Normalized FP exception-flag layout, shared across architectures.
//!
//! AArch64 `FPSR` and x86-64 `MXCSR` expose the same six IEEE sticky exception
//! flags but in different bit positions. To make captures from different
//! architectures directly comparable, every capture record stores flags in
//! this single normalized layout (which happens to match the AArch64 `FPSR`
//! cumulative-flag positions). Each architecture's oracle translates its native
//! flag word into this layout before recording.

/// Invalid Operation.
pub const IOC: u32 = 1 << 0;
/// Divide by Zero.
pub const DZC: u32 = 1 << 1;
/// Overflow.
pub const OFC: u32 = 1 << 2;
/// Underflow.
pub const UFC: u32 = 1 << 3;
/// Inexact.
pub const IXC: u32 = 1 << 4;
/// Input Denormal.
pub const IDC: u32 = 1 << 7;

/// Mask of all six normalized exception bits.
pub const MASK: u32 = IOC | DZC | OFC | UFC | IXC | IDC;

/// Render a normalized flag word as a short stable string, e.g. `"IOC|IXC"` or
/// `"-"` when no flags are set. Used only for human-facing summaries; the
/// machine record stores the raw `u32`.
pub fn render(flags: u32) -> String {
    let mut parts: Vec<&str> = Vec::new();
    if flags & IOC != 0 {
        parts.push("IOC");
    }
    if flags & DZC != 0 {
        parts.push("DZC");
    }
    if flags & OFC != 0 {
        parts.push("OFC");
    }
    if flags & UFC != 0 {
        parts.push("UFC");
    }
    if flags & IXC != 0 {
        parts.push("IXC");
    }
    if flags & IDC != 0 {
        parts.push("IDC");
    }
    if parts.is_empty() {
        "-".to_string()
    } else {
        parts.join("|")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn render_none() {
        assert_eq!(render(0), "-");
    }

    #[test]
    fn render_single_and_multi() {
        assert_eq!(render(IOC), "IOC");
        assert_eq!(render(IDC), "IDC");
        assert_eq!(render(IOC | IXC), "IOC|IXC");
        assert_eq!(render(DZC | OFC | UFC), "DZC|OFC|UFC");
        assert_eq!(render(MASK), "IOC|DZC|OFC|UFC|IXC|IDC");
    }

    #[test]
    fn mask_covers_all_named_bits() {
        assert_eq!(MASK, IOC | DZC | OFC | UFC | IXC | IDC);
    }
}
