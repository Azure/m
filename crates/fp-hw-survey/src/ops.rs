// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! The operation catalogue: every FP operation the survey knows how to drive,
//! described in an architecture-neutral way. Each architecture's oracle decides
//! which of these it can actually execute (see `arch::supports`).

/// Floating-point storage format.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum Fmt {
    Half,
    Single,
    Double,
}

/// Operand layout / computation shape for an operation. This tells `capture`
/// where to draw operands from and how many there are.
///
/// The struct-variant fields are a *complete* descriptor of each conversion's
/// shape (source/destination format, signedness, integer width). Operand
/// generation reads `from`/`width`; the remaining fields document the op for
/// readers and keep the catalogue self-describing even though runtime dispatch
/// is keyed on the op label string rather than on `Kind`.
#[derive(Copy, Clone, Debug)]
#[allow(dead_code)]
pub enum Kind {
    /// `a OP b`, both of the given float format.
    Bin(Fmt),
    /// `OP a`, one operand of the given float format.
    Un(Fmt),
    /// `a*b + c`, all of the given float format (single-rounded fused MAC).
    Fma(Fmt),
    /// Float→float conversion of one operand from `from` to `to`.
    CvtF2F { from: Fmt, to: Fmt },
    /// Float→integer conversion (round toward zero) of one float operand.
    CvtF2I { from: Fmt, signed: bool, width: u8 },
    /// Integer→float conversion of one integer operand of the given bit width.
    CvtI2F { to: Fmt, signed: bool, width: u8 },
}

/// One catalogued operation: a stable label plus its operand shape.
#[derive(Copy, Clone, Debug)]
pub struct OpSpec {
    pub label: &'static str,
    pub kind: Kind,
}

/// The full catalogue (the union across architectures). Labels match the scheme
/// used by the `rook` golden file so a merged divergence set drops straight in.
pub fn catalogue() -> Vec<OpSpec> {
    use Fmt::*;
    use Kind::*;
    let mut v: Vec<OpSpec> = Vec::new();
    macro_rules! op {
        ($l:literal, $k:expr) => {
            v.push(OpSpec {
                label: $l,
                kind: $k,
            })
        };
    }

    // ── Two-source arithmetic ────────────────────────────────────────────
    for &(suf, fmt) in &[("s", Single), ("d", Double), ("h", Half)] {
        for base in [
            "fadd", "fsub", "fmul", "fdiv", "fmax", "fmin", "fmaxnm", "fminnm", "fmulx", "fabd",
        ] {
            let label: &'static str = Box::leak(format!("{base}.{suf}").into_boxed_str());
            v.push(OpSpec {
                label,
                kind: Bin(fmt),
            });
        }
    }
    // frecps / frsqrts are single/double only.
    for &(suf, fmt) in &[("s", Single), ("d", Double)] {
        for base in ["frecps", "frsqrts"] {
            let label: &'static str = Box::leak(format!("{base}.{suf}").into_boxed_str());
            v.push(OpSpec {
                label,
                kind: Bin(fmt),
            });
        }
    }

    // ── One-source ───────────────────────────────────────────────────────
    for &(suf, fmt) in &[("s", Single), ("d", Double), ("h", Half)] {
        let label: &'static str = Box::leak(format!("fsqrt.{suf}").into_boxed_str());
        v.push(OpSpec {
            label,
            kind: Un(fmt),
        });
    }
    for &(suf, fmt) in &[("s", Single), ("d", Double)] {
        for base in [
            "frecpe", "frsqrte", "frecpx", "frintn", "frinta", "frintp", "frintm", "frintz",
        ] {
            let label: &'static str = Box::leak(format!("{base}.{suf}").into_boxed_str());
            v.push(OpSpec {
                label,
                kind: Un(fmt),
            });
        }
    }

    // ── Fused multiply-add ───────────────────────────────────────────────
    op!("fmadd.s", Fma(Single));
    op!("fmadd.d", Fma(Double));

    // ── Float→float convert ──────────────────────────────────────────────
    op!(
        "fcvt.s2d",
        CvtF2F {
            from: Single,
            to: Double
        }
    );
    op!(
        "fcvt.d2s",
        CvtF2F {
            from: Double,
            to: Single
        }
    );
    op!(
        "fcvt.s2h",
        CvtF2F {
            from: Single,
            to: Half
        }
    );
    op!(
        "fcvt.d2h",
        CvtF2F {
            from: Double,
            to: Half
        }
    );
    op!(
        "fcvt.h2s",
        CvtF2F {
            from: Half,
            to: Single
        }
    );
    op!(
        "fcvt.h2d",
        CvtF2F {
            from: Half,
            to: Double
        }
    );

    // ── Float→int, round toward zero ─────────────────────────────────────
    for (&from, fs) in [Single, Double].iter().zip(["s", "d"]) {
        for (signed, ss) in [(true, "fcvtzs"), (false, "fcvtzu")] {
            for &(width, ws) in &[(32u8, "w"), (64u8, "x")] {
                let label: &'static str = Box::leak(format!("{ss}.{fs}.{ws}").into_boxed_str());
                v.push(OpSpec {
                    label,
                    kind: CvtF2I {
                        from,
                        signed,
                        width,
                    },
                });
            }
        }
    }

    // ── Int→float ────────────────────────────────────────────────────────
    for (&to, fs) in [Single, Double].iter().zip(["s", "d"]) {
        for (signed, ss) in [(true, "scvtf"), (false, "ucvtf")] {
            for &(width, ws) in &[(32u8, "w"), (64u8, "x")] {
                let label: &'static str = Box::leak(format!("{ss}.{ws}.{fs}").into_boxed_str());
                v.push(OpSpec {
                    label,
                    kind: CvtI2F { to, signed, width },
                });
            }
        }
    }

    v
}
