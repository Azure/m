// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT
#![cfg(target_arch = "x86_64")]

//! x86-64 SSE hardware oracle. Executes each supported catalogued op as the
//! real scalar SSE/SSE2 (and, for FMA, FMA3) instruction under a caller-chosen
//! `MXCSR` state, reading back the cumulative exception flags and translating
//! them into the normalized layout.
//!
//! **This module cannot be exercised on the author's development host (which is
//! aarch64).** Its correctness on real x64 hardware is gated at runtime by the
//! `selftest` known-answer checks, which `capture` runs and which abort the run
//! on any mismatch.

use crate::mode::{Mode, Round};
use crate::normflags;

/// Build an `MXCSR` value for the logical mode. All six exception masks are set
/// (bits 7..12 = `0x1F80`) so nothing traps. **Rounding-control encoding differs
/// from AArch64**: RC bits [14:13] are 00=nearest, 01=down(−inf), 10=up(+inf),
/// 11=zero. Flush sets `FTZ` (bit 15) and `DAZ` (bit 6).
fn mxcsr_for(m: Mode) -> u32 {
    let rc: u32 = match m.round {
        Round::Ne => 0b00,
        Round::Down => 0b01,
        Round::Up => 0b10,
        Round::Zero => 0b11,
    };
    let mut v: u32 = 0x1F80 | (rc << 13);
    if m.flush {
        v |= 1 << 15; // FTZ
        v |= 1 << 6; // DAZ
    }
    v
}

/// Translate the six low `MXCSR` exception-status bits into the normalized
/// layout. x86 status bit order is IE,DE,ZE,OE,UE,PE.
fn translate(mxcsr: u32) -> u32 {
    let s = mxcsr & 0x3F;
    let mut n = 0u32;
    if s & (1 << 0) != 0 {
        n |= normflags::IOC; // IE  → invalid
    }
    if s & (1 << 1) != 0 {
        n |= normflags::IDC; // DE  → input denormal
    }
    if s & (1 << 2) != 0 {
        n |= normflags::DZC; // ZE  → divide by zero
    }
    if s & (1 << 3) != 0 {
        n |= normflags::OFC; // OE  → overflow
    }
    if s & (1 << 4) != 0 {
        n |= normflags::UFC; // UE  → underflow
    }
    if s & (1 << 5) != 0 {
        n |= normflags::IXC; // PE  → inexact
    }
    n
}

macro_rules! xbin {
    ($name:ident, $op:literal, $ld:literal, $rm:literal) => {
        #[inline(never)]
        unsafe fn $name(a: u64, b: u64, mxcsr: u32) -> (u64, u32) {
            let ctrl = mxcsr;
            let mut outv: u32 = 0;
            let res: u64;
            core::arch::asm!(
                "ldmxcsr [{c}]",
                concat!($ld, " xmm0, {a:", $rm, "}"),
                concat!($ld, " xmm1, {b:", $rm, "}"),
                concat!($op, " xmm0, xmm1"),
                concat!($ld, " {res:", $rm, "}, xmm0"),
                "stmxcsr [{o}]",
                c = in(reg) &ctrl,
                o = in(reg) &mut outv,
                a = in(reg) a,
                b = in(reg) b,
                res = out(reg) res,
                out("xmm0") _,
                out("xmm1") _,
            );
            (res, translate(outv))
        }
    };
}

macro_rules! xun {
    ($name:ident, $op:literal, $ld:literal, $rm:literal) => {
        #[inline(never)]
        unsafe fn $name(a: u64, mxcsr: u32) -> (u64, u32) {
            let ctrl = mxcsr;
            let mut outv: u32 = 0;
            let res: u64;
            core::arch::asm!(
                "ldmxcsr [{c}]",
                concat!($ld, " xmm0, {a:", $rm, "}"),
                concat!($op, " xmm0, xmm0"),
                concat!($ld, " {res:", $rm, "}, xmm0"),
                "stmxcsr [{o}]",
                c = in(reg) &ctrl,
                o = in(reg) &mut outv,
                a = in(reg) a,
                res = out(reg) res,
                out("xmm0") _,
            );
            (res, translate(outv))
        }
    };
}

macro_rules! xfma {
    ($name:ident, $op:literal, $ld:literal, $rm:literal) => {
        #[target_feature(enable = "avx,fma")]
        #[inline(never)]
        unsafe fn $name(a: u64, b: u64, c: u64, mxcsr: u32) -> (u64, u32) {
            // vfmadd213ss xmm0, xmm1, xmm2  =>  xmm0 = xmm1*xmm0 + xmm2.
            // Load xmm0=a, xmm1=b, xmm2=c  =>  b*a + c = a*b + c.
            let ctrl = mxcsr;
            let mut outv: u32 = 0;
            let res: u64;
            core::arch::asm!(
                "ldmxcsr [{c}]",
                concat!($ld, " xmm0, {a:", $rm, "}"),
                concat!($ld, " xmm1, {b:", $rm, "}"),
                concat!($ld, " xmm2, {cc:", $rm, "}"),
                concat!($op, " xmm0, xmm1, xmm2"),
                concat!($ld, " {res:", $rm, "}, xmm0"),
                "stmxcsr [{o}]",
                c = in(reg) &ctrl,
                o = in(reg) &mut outv,
                a = in(reg) a,
                b = in(reg) b,
                cc = in(reg) c,
                res = out(reg) res,
                out("xmm0") _,
                out("xmm1") _,
                out("xmm2") _,
            );
            (res, translate(outv))
        }
    };
}

macro_rules! xcvtff {
    ($name:ident, $op:literal, $ldi:literal, $rmi:literal, $ldo:literal, $rmo:literal) => {
        #[inline(never)]
        unsafe fn $name(a: u64, mxcsr: u32) -> (u64, u32) {
            let ctrl = mxcsr;
            let mut outv: u32 = 0;
            let res: u64;
            core::arch::asm!(
                "ldmxcsr [{c}]",
                concat!($ldi, " xmm0, {a:", $rmi, "}"),
                concat!($op, " xmm0, xmm0"),
                concat!($ldo, " {res:", $rmo, "}, xmm0"),
                "stmxcsr [{o}]",
                c = in(reg) &ctrl,
                o = in(reg) &mut outv,
                a = in(reg) a,
                res = out(reg) res,
                out("xmm0") _,
            );
            (res, translate(outv))
        }
    };
}

macro_rules! xcvtf2i {
    ($name:ident, $op:literal, $ldi:literal, $rmi:literal, $rmo:literal) => {
        #[inline(never)]
        unsafe fn $name(a: u64, mxcsr: u32) -> (u64, u32) {
            let ctrl = mxcsr;
            let mut outv: u32 = 0;
            let res: u64;
            core::arch::asm!(
                "ldmxcsr [{c}]",
                concat!($ldi, " xmm0, {a:", $rmi, "}"),
                concat!($op, " {res:", $rmo, "}, xmm0"),
                "stmxcsr [{o}]",
                c = in(reg) &ctrl,
                o = in(reg) &mut outv,
                a = in(reg) a,
                res = out(reg) res,
                out("xmm0") _,
            );
            (res, translate(outv))
        }
    };
}

macro_rules! xcvti2f {
    ($name:ident, $op:literal, $rmi:literal, $ldo:literal, $rmo:literal) => {
        #[inline(never)]
        unsafe fn $name(a: u64, mxcsr: u32) -> (u64, u32) {
            let ctrl = mxcsr;
            let mut outv: u32 = 0;
            let res: u64;
            core::arch::asm!(
                "ldmxcsr [{c}]",
                concat!($op, " xmm0, {a:", $rmi, "}"),
                concat!($ldo, " {res:", $rmo, "}, xmm0"),
                "stmxcsr [{o}]",
                c = in(reg) &ctrl,
                o = in(reg) &mut outv,
                a = in(reg) a,
                res = out(reg) res,
                out("xmm0") _,
            );
            (res, translate(outv))
        }
    };
}

// ── Arithmetic single ─────────────────────────────────────────────────────
xbin!(addss, "addss", "movd", "e");
xbin!(subss, "subss", "movd", "e");
xbin!(mulss, "mulss", "movd", "e");
xbin!(divss, "divss", "movd", "e");
xbin!(maxss, "maxss", "movd", "e");
xbin!(minss, "minss", "movd", "e");
// ── Arithmetic double ─────────────────────────────────────────────────────
xbin!(addsd, "addsd", "movq", "r");
xbin!(subsd, "subsd", "movq", "r");
xbin!(mulsd, "mulsd", "movq", "r");
xbin!(divsd, "divsd", "movq", "r");
xbin!(maxsd, "maxsd", "movq", "r");
xbin!(minsd, "minsd", "movq", "r");
// ── Square root ───────────────────────────────────────────────────────────
xun!(sqrtss, "sqrtss", "movd", "e");
xun!(sqrtsd, "sqrtsd", "movq", "r");
// ── FMA (FMA3) ────────────────────────────────────────────────────────────
xfma!(fmadd_ss, "vfmadd213ss", "movd", "e");
xfma!(fmadd_sd, "vfmadd213sd", "movq", "r");
// ── Float→float ───────────────────────────────────────────────────────────
xcvtff!(cvtss2sd, "cvtss2sd", "movd", "e", "movq", "r");
xcvtff!(cvtsd2ss, "cvtsd2ss", "movq", "r", "movd", "e");
// ── Float→int (truncating; signed only) ───────────────────────────────────
xcvtf2i!(cvttss2si_w, "cvttss2si", "movd", "e", "e");
xcvtf2i!(cvttss2si_x, "cvttss2si", "movd", "e", "r");
xcvtf2i!(cvttsd2si_w, "cvttsd2si", "movq", "r", "e");
xcvtf2i!(cvttsd2si_x, "cvttsd2si", "movq", "r", "r");
// ── Int→float (signed only) ───────────────────────────────────────────────
xcvti2f!(cvtsi2ss_w, "cvtsi2ss", "e", "movd", "e");
xcvti2f!(cvtsi2ss_x, "cvtsi2ss", "r", "movd", "e");
xcvti2f!(cvtsi2sd_w, "cvtsi2sd", "e", "movq", "r");
xcvti2f!(cvtsi2sd_x, "cvtsi2sd", "r", "movq", "r");

const W: u64 = 0xFFFF_FFFF;

pub fn arch_tag() -> &'static str {
    "x86_64"
}

fn has_fma() -> bool {
    std::arch::is_x86_feature_detected!("fma") && std::arch::is_x86_feature_detected!("avx")
}

/// The ops x86-64 can faithfully execute as a single scalar instruction. Ops
/// with no scalar SSE counterpart (`fmaxnm`, `fmulx`, the estimate family, the
/// directed-rounding float→int forms, unsigned conversions, all half ops) are
/// deliberately absent and yield `None`.
pub fn supports(label: &str) -> bool {
    match label {
        "fadd.s" | "fsub.s" | "fmul.s" | "fdiv.s" | "fmax.s" | "fmin.s" | "fadd.d" | "fsub.d"
        | "fmul.d" | "fdiv.d" | "fmax.d" | "fmin.d" | "fsqrt.s" | "fsqrt.d" | "fcvt.s2d"
        | "fcvt.d2s" | "fcvtzs.s.w" | "fcvtzs.s.x" | "fcvtzs.d.w" | "fcvtzs.d.x" | "scvtf.w.s"
        | "scvtf.x.s" | "scvtf.w.d" | "scvtf.x.d" => true,
        "fmadd.s" | "fmadd.d" => has_fma(),
        _ => false,
    }
}

pub fn eval(label: &str, a: u64, b: u64, c: u64, mode: Mode) -> Option<(u64, u32)> {
    let m = mxcsr_for(mode);
    let (res, flags) = unsafe {
        match label {
            "fadd.s" => addss(a, b, m),
            "fsub.s" => subss(a, b, m),
            "fmul.s" => mulss(a, b, m),
            "fdiv.s" => divss(a, b, m),
            "fmax.s" => maxss(a, b, m),
            "fmin.s" => minss(a, b, m),
            "fadd.d" => addsd(a, b, m),
            "fsub.d" => subsd(a, b, m),
            "fmul.d" => mulsd(a, b, m),
            "fdiv.d" => divsd(a, b, m),
            "fmax.d" => maxsd(a, b, m),
            "fmin.d" => minsd(a, b, m),
            "fsqrt.s" => sqrtss(a, m),
            "fsqrt.d" => sqrtsd(a, m),
            "fmadd.s" => {
                if !has_fma() {
                    return None;
                }
                fmadd_ss(a, b, c, m)
            }
            "fmadd.d" => {
                if !has_fma() {
                    return None;
                }
                fmadd_sd(a, b, c, m)
            }
            "fcvt.s2d" => cvtss2sd(a, m),
            "fcvt.d2s" => cvtsd2ss(a, m),
            "fcvtzs.s.w" => cvttss2si_w(a, m),
            "fcvtzs.s.x" => cvttss2si_x(a, m),
            "fcvtzs.d.w" => cvttsd2si_w(a, m),
            "fcvtzs.d.x" => cvttsd2si_x(a, m),
            "scvtf.w.s" => cvtsi2ss_w(a, m),
            "scvtf.x.s" => cvtsi2ss_x(a, m),
            "scvtf.w.d" => cvtsi2sd_w(a, m),
            "scvtf.x.d" => cvtsi2sd_x(a, m),
            _ => return None,
        }
    };
    let res = if label.ends_with(".w") { res & W } else { res };
    Some((res, flags & normflags::MASK))
}
