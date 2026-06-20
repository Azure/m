// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Deterministic operand corpora.
//!
//! Every machine must generate **identical** operand sequences so that captures
//! align row-for-row during merge. All sets are therefore fixed consts plus a
//! seeded SplitMix64 fill — never wall-clock or thread-randomized.

/// Curated single-precision edge bit patterns: signed zeros/infinities, quiet
/// and signalling NaNs, the subnormal/normal boundaries, powers near the
/// integer-precision limit, rounding boundaries, and a few ordinary values.
pub const SINGLE: &[u32] = &[
    0x0000_0000, // +0
    0x8000_0000, // -0
    0x0000_0001, // smallest +subnormal
    0x8000_0001, // smallest -subnormal
    0x007f_ffff, // largest +subnormal
    0x0080_0000, // smallest +normal
    0x3f80_0000, // 1.0
    0xbf80_0000, // -1.0
    0x4000_0000, // 2.0
    0x3f00_0000, // 0.5
    0x4049_0fdb, // pi
    0x3eaa_aaab, // 1/3 rounded
    0x4b80_0000, // 2^24 (integer-precision limit)
    0x4b7f_ffff, // 2^24 - 1
    0x7f7f_ffff, // largest +normal
    0xff7f_ffff, // largest -normal
    0x7f80_0000, // +inf
    0xff80_0000, // -inf
    0x7fc0_0000, // +qNaN
    0xffc0_0000, // -qNaN
    0x7f80_0001, // +sNaN
    0x7fbf_ffff, // +sNaN (max payload)
    0x3f7f_ffff, // just below 1.0
    0x3f80_0001, // just above 1.0
    0x4170_0000, // 15.0
    0xc170_0000, // -15.0
    0x0080_0001, // smallest normal + 1 ulp
    0x7e80_0000, // large finite (~8.5e37)
    0x0040_0000, // mid subnormal
    0x4f00_0000, // 2^31 (overflow boundary for i32)
    0x5f00_0000, // 2^63 (overflow boundary for i64)
    0xcf00_0000, // -2^31
];

/// Curated double-precision edge bit patterns.
pub const DOUBLE: &[u64] = &[
    0x0000_0000_0000_0000, // +0
    0x8000_0000_0000_0000, // -0
    0x0000_0000_0000_0001, // smallest +subnormal
    0x8000_0000_0000_0001, // smallest -subnormal
    0x000f_ffff_ffff_ffff, // largest +subnormal
    0x0010_0000_0000_0000, // smallest +normal
    0x3ff0_0000_0000_0000, // 1.0
    0xbff0_0000_0000_0000, // -1.0
    0x4000_0000_0000_0000, // 2.0
    0x3fe0_0000_0000_0000, // 0.5
    0x4009_21fb_5444_2d18, // pi
    0x3fd5_5555_5555_5555, // 1/3 rounded
    0x4330_0000_0000_0000, // 2^52 (integer-precision limit)
    0x432f_ffff_ffff_ffff, // 2^52 - 1
    0x7fef_ffff_ffff_ffff, // largest +normal
    0xffef_ffff_ffff_ffff, // largest -normal
    0x7ff0_0000_0000_0000, // +inf
    0xfff0_0000_0000_0000, // -inf
    0x7ff8_0000_0000_0000, // +qNaN
    0xfff8_0000_0000_0000, // -qNaN
    0x7ff0_0000_0000_0001, // +sNaN
    0x7ff7_ffff_ffff_ffff, // +sNaN (max payload)
    0x3fef_ffff_ffff_ffff, // just below 1.0
    0x3ff0_0000_0000_0001, // just above 1.0
    0x41df_ffff_ffc0_0000, // 2^31 - 1 region
    0x43e0_0000_0000_0000, // 2^63 (overflow boundary for i64)
    0xc3e0_0000_0000_0000, // -2^63
    0x4079_0000_0000_0000, // 400.0
    0x0008_0000_0000_0000, // mid subnormal
    0x7fe0_0000_0000_0000, // large finite
    0x000f_ffff_ffff_fffe, // near subnormal top
];

/// Curated half-precision (IEEE binary16) edge bit patterns.
pub const HALF: &[u16] = &[
    0x0000, // +0
    0x8000, // -0
    0x0001, // smallest +subnormal
    0x8001, // smallest -subnormal
    0x03ff, // largest +subnormal
    0x0400, // smallest +normal
    0x3c00, // 1.0
    0xbc00, // -1.0
    0x4000, // 2.0
    0x3800, // 0.5
    0x3555, // 1/3 rounded
    0x6400, // 1024 = 2^10 (integer-precision limit)
    0x63ff, // 1023
    0x7bff, // largest +normal (65504)
    0xfbff, // largest -normal
    0x7c00, // +inf
    0xfc00, // -inf
    0x7e00, // +qNaN
    0xfe00, // -qNaN
    0x7c01, // +sNaN
    0x7dff, // +sNaN (max payload)
    0x3bff, // just below 1.0
    0x3c01, // just above 1.0
    0x4900, // 10.0
    0x0200, // mid subnormal
];

/// Curated integer operands (used for both 32- and 64-bit int→float, with the
/// stored operand width fixing the value): zeros, ±1, powers of two, the
/// signed/unsigned extremes, and the precision-limit boundaries.
pub const INTS: &[u64] = &[
    0,
    1,
    0xffff_ffff_ffff_ffff, // -1 / u64::MAX
    2,
    0x7fff_ffff,           // i32::MAX
    0x8000_0000,           // i32::MIN as u32 / 2^31
    0xffff_ffff,           // u32::MAX / -1 as u32
    0x0100_0000,           // 2^24
    0x0100_0001,           // 2^24 + 1 (f32 rounding boundary)
    0x0020_0000_0000_0000, // 2^53
    0x0020_0000_0000_0001, // 2^53 + 1 (f64 rounding boundary)
    0x7fff_ffff_ffff_ffff, // i64::MAX
    0x8000_0000_0000_0000, // i64::MIN / 2^63
    1000,
    0x0000_0000_dead_beef,
    0x0000_0001_0000_0000, // 2^32
    0xffff_ffff_0000_0000,
    42,
];

/// A seeded SplitMix64 generator producing reproducible 64-bit words.
pub struct SplitMix64 {
    state: u64,
}

impl SplitMix64 {
    pub fn new(seed: u64) -> Self {
        SplitMix64 { state: seed }
    }

    pub fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
        z ^ (z >> 31)
    }
}

/// Fixed base seed so every machine's random fill is identical. The XORed
/// stream id keeps independent draws (e.g. operand `a` vs operand `b`) from
/// being correlated.
pub const FILL_SEED: u64 = 0x4650_4857_5355_5256; // "FPHWSURV"

/// Generate `n` reproducible 32-bit fill words for stream `stream`.
pub fn fill_u32(stream: u64, n: usize) -> Vec<u32> {
    let mut r = SplitMix64::new(FILL_SEED ^ stream.wrapping_mul(0x1000_0001));
    (0..n).map(|_| r.next_u64() as u32).collect()
}

/// Generate `n` reproducible 64-bit fill words for stream `stream`.
pub fn fill_u64(stream: u64, n: usize) -> Vec<u64> {
    let mut r = SplitMix64::new(FILL_SEED ^ stream.wrapping_mul(0x1000_0001));
    (0..n).map(|_| r.next_u64()).collect()
}

/// Generate `n` reproducible 16-bit fill words for stream `stream`.
pub fn fill_u16(stream: u64, n: usize) -> Vec<u16> {
    let mut r = SplitMix64::new(FILL_SEED ^ stream.wrapping_mul(0x1000_0001));
    (0..n).map(|_| r.next_u64() as u16).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splitmix_is_deterministic() {
        let a: Vec<u64> = (0..8)
            .scan(SplitMix64::new(42), |s, _| Some(s.next_u64()))
            .collect();
        let b: Vec<u64> = (0..8)
            .scan(SplitMix64::new(42), |s, _| Some(s.next_u64()))
            .collect();
        assert_eq!(a, b);
    }

    #[test]
    fn fills_are_reproducible_and_sized() {
        assert_eq!(fill_u32(0, 16), fill_u32(0, 16));
        assert_eq!(fill_u64(3, 16), fill_u64(3, 16));
        assert_eq!(fill_u16(7, 16).len(), 16);
    }

    #[test]
    fn streams_are_decorrelated() {
        assert_ne!(fill_u64(0, 8), fill_u64(1, 8));
    }

    #[test]
    fn edge_tables_nonempty_and_distinct() {
        assert!(SINGLE.len() >= 24);
        assert!(DOUBLE.len() >= 24);
        assert!(HALF.len() >= 20);
        let mut s = SINGLE.to_vec();
        s.sort_unstable();
        s.dedup();
        assert_eq!(s.len(), SINGLE.len(), "SINGLE has duplicate entries");
    }
}
