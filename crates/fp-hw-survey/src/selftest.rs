// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Known-answer self-test. Because the x86-64 asm cannot be exercised on the
//! author's aarch64 development host, `capture` runs this gate first and aborts
//! if any check fails — preventing a machine from emitting untrustworthy data
//! from broken inline asm.

use crate::arch;
use crate::mode::{Mode, Round};

struct Case {
    label: &'static str,
    a: u64,
    b: u64,
    c: u64,
    expect: u64,
    desc: &'static str,
}

fn rn() -> Mode {
    Mode {
        round: Round::Ne,
        flush: false,
    }
}

fn cases() -> Vec<Case> {
    // Bit patterns: f32 1.0=0x3f800000, 2.0=0x40000000, 3.0=0x40400000,
    // 4.0=0x40800000, 6.0=0x40c00000, 2.5=0x40200000.
    // f64 1.0=0x3ff0..., 2.0=0x4000..., 4.0=0x4010..., 6.0=0x4018...
    vec![
        Case {
            label: "fadd.s",
            a: 0x3f80_0000,
            b: 0x3f80_0000,
            c: 0,
            expect: 0x4000_0000,
            desc: "1.0 + 1.0 == 2.0 (f32)",
        },
        Case {
            label: "fmul.s",
            a: 0x4000_0000,
            b: 0x4040_0000,
            c: 0,
            expect: 0x40c0_0000,
            desc: "2.0 * 3.0 == 6.0 (f32)",
        },
        Case {
            label: "fsub.s",
            a: 0x4040_0000,
            b: 0x3f80_0000,
            c: 0,
            expect: 0x4000_0000,
            desc: "3.0 - 1.0 == 2.0 (f32)",
        },
        Case {
            label: "fdiv.s",
            a: 0x40c0_0000,
            b: 0x4040_0000,
            c: 0,
            expect: 0x4000_0000,
            desc: "6.0 / 3.0 == 2.0 (f32)",
        },
        Case {
            label: "fsqrt.s",
            a: 0x4080_0000,
            b: 0,
            c: 0,
            expect: 0x4000_0000,
            desc: "sqrt(4.0) == 2.0 (f32)",
        },
        Case {
            label: "fadd.d",
            a: 0x3ff0_0000_0000_0000,
            b: 0x3ff0_0000_0000_0000,
            c: 0,
            expect: 0x4000_0000_0000_0000,
            desc: "1.0 + 1.0 == 2.0 (f64)",
        },
        Case {
            label: "fsqrt.d",
            a: 0x4010_0000_0000_0000,
            b: 0,
            c: 0,
            expect: 0x4000_0000_0000_0000,
            desc: "sqrt(4.0) == 2.0 (f64)",
        },
        Case {
            label: "fcvt.s2d",
            a: 0x3f80_0000,
            b: 0,
            c: 0,
            expect: 0x3ff0_0000_0000_0000,
            desc: "(f64)1.0f == 1.0 (f32->f64)",
        },
        Case {
            label: "fcvt.d2s",
            a: 0x4000_0000_0000_0000,
            b: 0,
            c: 0,
            expect: 0x4000_0000,
            desc: "(f32)2.0 == 2.0 (f64->f32)",
        },
        Case {
            label: "fcvtzs.s.w",
            a: 0x4020_0000,
            b: 0,
            c: 0,
            expect: 2,
            desc: "(i32)2.5f == 2 (truncate)",
        },
        Case {
            label: "scvtf.w.s",
            a: 3,
            b: 0,
            c: 0,
            expect: 0x4040_0000,
            desc: "(f32)3 == 3.0",
        },
    ]
}

/// Run all known-answer checks for the current host. Returns `Ok(n)` with the
/// number of checks that ran, or `Err(failures)` listing every mismatch.
pub fn run() -> Result<usize, Vec<String>> {
    let mut ran = 0usize;
    let mut fails = Vec::new();
    for case in cases() {
        if !arch::supports(case.label) {
            continue; // op not available on this arch; not a failure
        }
        if let Some((res, _flags)) = arch::eval(case.label, case.a, case.b, case.c, rn()) {
            ran += 1;
            if res != case.expect {
                fails.push(format!(
                    "FAIL {}: {} -> got 0x{:x}, expected 0x{:x}",
                    case.label, case.desc, res, case.expect
                ));
            }
        }
        // `None` => unsupported at runtime; skip silently.
    }
    if fails.is_empty() {
        Ok(ran)
    } else {
        Err(fails)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_answers_hold_on_this_host() {
        // Exercises the real hardware oracle for the test architecture.
        match run() {
            Ok(n) => assert!(n > 0, "no self-test cases ran on this arch"),
            Err(fails) => panic!("self-test failed: {fails:?}"),
        }
    }
}
