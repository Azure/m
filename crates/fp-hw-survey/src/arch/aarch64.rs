// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT
#![cfg(target_arch = "aarch64")]

//! AArch64 hardware oracle: executes each catalogued FP operation as the real
//! scalar instruction inside an inline-asm block, under a caller-chosen `FPCR`
//! state, and reads back the cumulative `FPSR` exception flags.
//!
//! `FPSR`'s low cumulative bits already use the normalized flag layout
//! (`IOC..IXC` at bits 0..4, `IDC` at bit 7), so no flag translation is needed
//! here — only masking.

use crate::mode::{Mode, Round};
use crate::normflags;

/// Build an `FPCR` value for the logical mode. `RMode` occupies bits [23:22];
/// flush sets both `FZ` (bit 24, single/double) and `FZ16` (bit 19, half) so a
/// single value works for every format. `DN`/`AHP` stay 0 — we want to *observe*
/// NaN propagation, not normalize it away.
fn fpcr_for(m: Mode) -> u64 {
    let rmode: u64 = match m.round {
        Round::Ne => 0,
        Round::Up => 1,
        Round::Down => 2,
        Round::Zero => 3,
    };
    let mut v = rmode << 22;
    if m.flush {
        v |= 1 << 24; // FZ
        v |= 1 << 19; // FZ16
    }
    v
}

macro_rules! abin {
    ($(#[$a:meta])* $name:ident, $insn:literal, $r:literal, $m:literal) => {
        $(#[$a])*
        #[inline(never)]
        unsafe fn $name(a: u64, b: u64, fpcr: u64) -> (u64, u64) {
            let res: u64;
            let fpsr: u64;
            core::arch::asm!(
                "mrs {s}, fpcr",
                "msr fpcr, {c}",
                "isb",
                "msr fpsr, xzr",
                concat!("fmov ", $r, "0, {a:", $m, "}"),
                concat!("fmov ", $r, "1, {b:", $m, "}"),
                concat!($insn, " ", $r, "0, ", $r, "0, ", $r, "1"),
                concat!("fmov {res:", $m, "}, ", $r, "0"),
                "mrs {fpsr}, fpsr",
                "msr fpcr, {s}",
                "isb",
                s = out(reg) _,
                c = in(reg) fpcr,
                a = in(reg) a,
                b = in(reg) b,
                res = out(reg) res,
                fpsr = out(reg) fpsr,
                out("v0") _,
                out("v1") _,
            );
            (res, fpsr)
        }
    };
}

macro_rules! aun {
    ($(#[$a:meta])* $name:ident, $insn:literal, $r:literal, $m:literal) => {
        $(#[$a])*
        #[inline(never)]
        unsafe fn $name(a: u64, fpcr: u64) -> (u64, u64) {
            let res: u64;
            let fpsr: u64;
            core::arch::asm!(
                "mrs {s}, fpcr",
                "msr fpcr, {c}",
                "isb",
                "msr fpsr, xzr",
                concat!("fmov ", $r, "0, {a:", $m, "}"),
                concat!($insn, " ", $r, "0, ", $r, "0"),
                concat!("fmov {res:", $m, "}, ", $r, "0"),
                "mrs {fpsr}, fpsr",
                "msr fpcr, {s}",
                "isb",
                s = out(reg) _,
                c = in(reg) fpcr,
                a = in(reg) a,
                res = out(reg) res,
                fpsr = out(reg) fpsr,
                out("v0") _,
            );
            (res, fpsr)
        }
    };
}

macro_rules! afma {
    ($name:ident, $insn:literal, $r:literal, $m:literal) => {
        #[inline(never)]
        unsafe fn $name(a: u64, b: u64, c: u64, fpcr: u64) -> (u64, u64) {
            // Compute a*b + c. `fmadd Sd, Sn, Sm, Sa` = Sn*Sm + Sa, so map
            // Sn=a, Sm=b, Sa=c.
            let res: u64;
            let fpsr: u64;
            core::arch::asm!(
                "mrs {s}, fpcr",
                "msr fpcr, {f}",
                "isb",
                "msr fpsr, xzr",
                concat!("fmov ", $r, "1, {a:", $m, "}"),
                concat!("fmov ", $r, "2, {b:", $m, "}"),
                concat!("fmov ", $r, "3, {c:", $m, "}"),
                concat!($insn, " ", $r, "0, ", $r, "1, ", $r, "2, ", $r, "3"),
                concat!("fmov {res:", $m, "}, ", $r, "0"),
                "mrs {fpsr}, fpsr",
                "msr fpcr, {s}",
                "isb",
                s = out(reg) _,
                f = in(reg) fpcr,
                a = in(reg) a,
                b = in(reg) b,
                c = in(reg) c,
                res = out(reg) res,
                fpsr = out(reg) fpsr,
                out("v0") _,
                out("v1") _,
                out("v2") _,
                out("v3") _,
            );
            (res, fpsr)
        }
    };
}

macro_rules! acvtff {
    ($(#[$a:meta])* $name:ident, $sr:literal, $sm:literal, $dr:literal, $dm:literal) => {
        $(#[$a])*
        #[inline(never)]
        unsafe fn $name(a: u64, fpcr: u64) -> (u64, u64) {
            let res: u64;
            let fpsr: u64;
            core::arch::asm!(
                "mrs {s}, fpcr",
                "msr fpcr, {c}",
                "isb",
                "msr fpsr, xzr",
                concat!("fmov ", $sr, "0, {a:", $sm, "}"),
                concat!("fcvt ", $dr, "0, ", $sr, "0"),
                concat!("fmov {res:", $dm, "}, ", $dr, "0"),
                "mrs {fpsr}, fpsr",
                "msr fpcr, {s}",
                "isb",
                s = out(reg) _,
                c = in(reg) fpcr,
                a = in(reg) a,
                res = out(reg) res,
                fpsr = out(reg) fpsr,
                out("v0") _,
            );
            (res, fpsr)
        }
    };
}

macro_rules! acvtf2i {
    ($name:ident, $insn:literal, $im:literal, $fr:literal, $fm:literal) => {
        #[inline(never)]
        unsafe fn $name(a: u64, fpcr: u64) -> (u64, u64) {
            let res: u64;
            let fpsr: u64;
            core::arch::asm!(
                "mrs {s}, fpcr",
                "msr fpcr, {c}",
                "isb",
                "msr fpsr, xzr",
                concat!("fmov ", $fr, "0, {a:", $fm, "}"),
                concat!($insn, " {res:", $im, "}, ", $fr, "0"),
                "mrs {fpsr}, fpsr",
                "msr fpcr, {s}",
                "isb",
                s = out(reg) _,
                c = in(reg) fpcr,
                a = in(reg) a,
                res = out(reg) res,
                fpsr = out(reg) fpsr,
                out("v0") _,
            );
            (res, fpsr)
        }
    };
}

macro_rules! acvti2f {
    ($name:ident, $insn:literal, $fr:literal, $fm:literal, $im:literal) => {
        #[inline(never)]
        unsafe fn $name(a: u64, fpcr: u64) -> (u64, u64) {
            let res: u64;
            let fpsr: u64;
            core::arch::asm!(
                "mrs {s}, fpcr",
                "msr fpcr, {c}",
                "isb",
                "msr fpsr, xzr",
                concat!($insn, " ", $fr, "0, {a:", $im, "}"),
                concat!("fmov {res:", $fm, "}, ", $fr, "0"),
                "mrs {fpsr}, fpsr",
                "msr fpcr, {s}",
                "isb",
                s = out(reg) _,
                c = in(reg) fpcr,
                a = in(reg) a,
                res = out(reg) res,
                fpsr = out(reg) fpsr,
                out("v0") _,
            );
            (res, fpsr)
        }
    };
}

// ── Two-source single ────────────────────────────────────────────────────
abin!(fadd_s, "fadd", "s", "w");
abin!(fsub_s, "fsub", "s", "w");
abin!(fmul_s, "fmul", "s", "w");
abin!(fdiv_s, "fdiv", "s", "w");
abin!(fmax_s, "fmax", "s", "w");
abin!(fmin_s, "fmin", "s", "w");
abin!(fmaxnm_s, "fmaxnm", "s", "w");
abin!(fminnm_s, "fminnm", "s", "w");
abin!(fmulx_s, "fmulx", "s", "w");
abin!(fabd_s, "fabd", "s", "w");
abin!(frecps_s, "frecps", "s", "w");
abin!(frsqrts_s, "frsqrts", "s", "w");

// ── Two-source double ────────────────────────────────────────────────────
abin!(fadd_d, "fadd", "d", "x");
abin!(fsub_d, "fsub", "d", "x");
abin!(fmul_d, "fmul", "d", "x");
abin!(fdiv_d, "fdiv", "d", "x");
abin!(fmax_d, "fmax", "d", "x");
abin!(fmin_d, "fmin", "d", "x");
abin!(fmaxnm_d, "fmaxnm", "d", "x");
abin!(fminnm_d, "fminnm", "d", "x");
abin!(fmulx_d, "fmulx", "d", "x");
abin!(fabd_d, "fabd", "d", "x");
abin!(frecps_d, "frecps", "d", "x");
abin!(frsqrts_d, "frsqrts", "d", "x");

// ── Two-source half (FEAT_FP16) ──────────────────────────────────────────
abin!(
    #[target_feature(enable = "fp16")]
    fadd_h,
    "fadd",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fsub_h,
    "fsub",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fmul_h,
    "fmul",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fdiv_h,
    "fdiv",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fmax_h,
    "fmax",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fmin_h,
    "fmin",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fmaxnm_h,
    "fmaxnm",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fminnm_h,
    "fminnm",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fmulx_h,
    "fmulx",
    "h",
    "w"
);
abin!(
    #[target_feature(enable = "fp16")]
    fabd_h,
    "fabd",
    "h",
    "w"
);

// ── One-source single/double ─────────────────────────────────────────────
aun!(fsqrt_s, "fsqrt", "s", "w");
aun!(frecpe_s, "frecpe", "s", "w");
aun!(frsqrte_s, "frsqrte", "s", "w");
aun!(frecpx_s, "frecpx", "s", "w");
aun!(frintn_s, "frintn", "s", "w");
aun!(frinta_s, "frinta", "s", "w");
aun!(frintp_s, "frintp", "s", "w");
aun!(frintm_s, "frintm", "s", "w");
aun!(frintz_s, "frintz", "s", "w");
aun!(fsqrt_d, "fsqrt", "d", "x");
aun!(frecpe_d, "frecpe", "d", "x");
aun!(frsqrte_d, "frsqrte", "d", "x");
aun!(frecpx_d, "frecpx", "d", "x");
aun!(frintn_d, "frintn", "d", "x");
aun!(frinta_d, "frinta", "d", "x");
aun!(frintp_d, "frintp", "d", "x");
aun!(frintm_d, "frintm", "d", "x");
aun!(frintz_d, "frintz", "d", "x");
aun!(
    #[target_feature(enable = "fp16")]
    fsqrt_h,
    "fsqrt",
    "h",
    "w"
);

// ── Fused multiply-add ───────────────────────────────────────────────────
afma!(fmadd_s, "fmadd", "s", "w");
afma!(fmadd_d, "fmadd", "d", "x");

// ── Float→float convert ──────────────────────────────────────────────────
acvtff!(fcvt_s2d, "s", "w", "d", "x");
acvtff!(fcvt_d2s, "d", "x", "s", "w");
acvtff!(
    #[target_feature(enable = "fp16")]
    fcvt_s2h,
    "s",
    "w",
    "h",
    "w"
);
acvtff!(
    #[target_feature(enable = "fp16")]
    fcvt_d2h,
    "d",
    "x",
    "h",
    "w"
);
acvtff!(
    #[target_feature(enable = "fp16")]
    fcvt_h2s,
    "h",
    "w",
    "s",
    "w"
);
acvtff!(
    #[target_feature(enable = "fp16")]
    fcvt_h2d,
    "h",
    "w",
    "d",
    "x"
);

// ── Float→int (round toward zero) ────────────────────────────────────────
acvtf2i!(fcvtzs_s_w, "fcvtzs", "w", "s", "w");
acvtf2i!(fcvtzs_s_x, "fcvtzs", "x", "s", "w");
acvtf2i!(fcvtzs_d_w, "fcvtzs", "w", "d", "x");
acvtf2i!(fcvtzs_d_x, "fcvtzs", "x", "d", "x");
acvtf2i!(fcvtzu_s_w, "fcvtzu", "w", "s", "w");
acvtf2i!(fcvtzu_s_x, "fcvtzu", "x", "s", "w");
acvtf2i!(fcvtzu_d_w, "fcvtzu", "w", "d", "x");
acvtf2i!(fcvtzu_d_x, "fcvtzu", "x", "d", "x");

// ── Int→float ────────────────────────────────────────────────────────────
acvti2f!(scvtf_w_s, "scvtf", "s", "w", "w");
acvti2f!(scvtf_x_s, "scvtf", "s", "w", "x");
acvti2f!(scvtf_w_d, "scvtf", "d", "x", "w");
acvti2f!(scvtf_x_d, "scvtf", "d", "x", "x");
acvti2f!(ucvtf_w_s, "ucvtf", "s", "w", "w");
acvti2f!(ucvtf_x_s, "ucvtf", "s", "w", "x");
acvti2f!(ucvtf_w_d, "ucvtf", "d", "x", "w");
acvti2f!(ucvtf_x_d, "ucvtf", "d", "x", "x");

/// 32-bit result mask for `.w` integer destinations.
const W: u64 = 0xFFFF_FFFF;

pub fn arch_tag() -> &'static str {
    "aarch64"
}

fn has_fp16() -> bool {
    crate::host::aarch64_fp16()
}

/// Whether this host can execute the op. All AArch64 ops are supported except
/// half-precision ones on hosts lacking `FEAT_FP16`.
pub fn supports(label: &str) -> bool {
    if label.ends_with(".h")
        || label == "fcvt.s2h"
        || label == "fcvt.d2h"
        || label == "fcvt.h2s"
        || label == "fcvt.h2d"
    {
        return has_fp16();
    }
    crate::ops::catalogue().iter().any(|o| o.label == label)
}

/// Execute `label` with the given operand bits and logical mode, returning
/// `(result_bits, normalized_flags)`. Returns `None` if unsupported on this
/// host (e.g. half op without `FEAT_FP16`).
pub fn eval(label: &str, a: u64, b: u64, c: u64, mode: Mode) -> Option<(u64, u32)> {
    let f = fpcr_for(mode);
    let half =
        label.ends_with(".h") || matches!(label, "fcvt.s2h" | "fcvt.d2h" | "fcvt.h2s" | "fcvt.h2d");
    if half && !has_fp16() {
        return None;
    }
    let (res, fpsr): (u64, u64) = unsafe {
        match label {
            // two-source single
            "fadd.s" => fadd_s(a, b, f),
            "fsub.s" => fsub_s(a, b, f),
            "fmul.s" => fmul_s(a, b, f),
            "fdiv.s" => fdiv_s(a, b, f),
            "fmax.s" => fmax_s(a, b, f),
            "fmin.s" => fmin_s(a, b, f),
            "fmaxnm.s" => fmaxnm_s(a, b, f),
            "fminnm.s" => fminnm_s(a, b, f),
            "fmulx.s" => fmulx_s(a, b, f),
            "fabd.s" => fabd_s(a, b, f),
            "frecps.s" => frecps_s(a, b, f),
            "frsqrts.s" => frsqrts_s(a, b, f),
            // two-source double
            "fadd.d" => fadd_d(a, b, f),
            "fsub.d" => fsub_d(a, b, f),
            "fmul.d" => fmul_d(a, b, f),
            "fdiv.d" => fdiv_d(a, b, f),
            "fmax.d" => fmax_d(a, b, f),
            "fmin.d" => fmin_d(a, b, f),
            "fmaxnm.d" => fmaxnm_d(a, b, f),
            "fminnm.d" => fminnm_d(a, b, f),
            "fmulx.d" => fmulx_d(a, b, f),
            "fabd.d" => fabd_d(a, b, f),
            "frecps.d" => frecps_d(a, b, f),
            "frsqrts.d" => frsqrts_d(a, b, f),
            // two-source half
            "fadd.h" => fadd_h(a, b, f),
            "fsub.h" => fsub_h(a, b, f),
            "fmul.h" => fmul_h(a, b, f),
            "fdiv.h" => fdiv_h(a, b, f),
            "fmax.h" => fmax_h(a, b, f),
            "fmin.h" => fmin_h(a, b, f),
            "fmaxnm.h" => fmaxnm_h(a, b, f),
            "fminnm.h" => fminnm_h(a, b, f),
            "fmulx.h" => fmulx_h(a, b, f),
            "fabd.h" => fabd_h(a, b, f),
            // one-source
            "fsqrt.s" => fsqrt_s(a, f),
            "frecpe.s" => frecpe_s(a, f),
            "frsqrte.s" => frsqrte_s(a, f),
            "frecpx.s" => frecpx_s(a, f),
            "frintn.s" => frintn_s(a, f),
            "frinta.s" => frinta_s(a, f),
            "frintp.s" => frintp_s(a, f),
            "frintm.s" => frintm_s(a, f),
            "frintz.s" => frintz_s(a, f),
            "fsqrt.d" => fsqrt_d(a, f),
            "frecpe.d" => frecpe_d(a, f),
            "frsqrte.d" => frsqrte_d(a, f),
            "frecpx.d" => frecpx_d(a, f),
            "frintn.d" => frintn_d(a, f),
            "frinta.d" => frinta_d(a, f),
            "frintp.d" => frintp_d(a, f),
            "frintm.d" => frintm_d(a, f),
            "frintz.d" => frintz_d(a, f),
            "fsqrt.h" => fsqrt_h(a, f),
            // fma
            "fmadd.s" => fmadd_s(a, b, c, f),
            "fmadd.d" => fmadd_d(a, b, c, f),
            // float→float
            "fcvt.s2d" => fcvt_s2d(a, f),
            "fcvt.d2s" => fcvt_d2s(a, f),
            "fcvt.s2h" => fcvt_s2h(a, f),
            "fcvt.d2h" => fcvt_d2h(a, f),
            "fcvt.h2s" => fcvt_h2s(a, f),
            "fcvt.h2d" => fcvt_h2d(a, f),
            // float→int
            "fcvtzs.s.w" => fcvtzs_s_w(a, f),
            "fcvtzs.s.x" => fcvtzs_s_x(a, f),
            "fcvtzs.d.w" => fcvtzs_d_w(a, f),
            "fcvtzs.d.x" => fcvtzs_d_x(a, f),
            "fcvtzu.s.w" => fcvtzu_s_w(a, f),
            "fcvtzu.s.x" => fcvtzu_s_x(a, f),
            "fcvtzu.d.w" => fcvtzu_d_w(a, f),
            "fcvtzu.d.x" => fcvtzu_d_x(a, f),
            // int→float
            "scvtf.w.s" => scvtf_w_s(a, f),
            "scvtf.x.s" => scvtf_x_s(a, f),
            "scvtf.w.d" => scvtf_w_d(a, f),
            "scvtf.x.d" => scvtf_x_d(a, f),
            "ucvtf.w.s" => ucvtf_w_s(a, f),
            "ucvtf.x.s" => ucvtf_x_s(a, f),
            "ucvtf.w.d" => ucvtf_w_d(a, f),
            "ucvtf.x.d" => ucvtf_x_d(a, f),
            _ => return None,
        }
    };
    // `.w` integer destinations: compare only the low 32 bits.
    let res = if label.ends_with(".w") { res & W } else { res };
    Some((res, (fpsr as u32) & normflags::MASK))
}
