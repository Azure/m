// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Portable, architecture-neutral FP control state.
//!
//! Each architecture maps these logical modes onto its native control register
//! (`FPCR` on AArch64, `MXCSR` on x86-64). The *logical* mode — not the raw
//! control bits — is what a capture record stores, so that a row produced on an
//! arm64 box and a row produced on an x64 box for "round-to-nearest, no flush"
//! line up under the same key during merge.
//!
//! `Default NaN` (DN) is deliberately left disabled and is not part of the
//! logical mode: forcing default-NaN propagation would *hide* exactly the
//! cross-vendor NaN-propagation differences this survey exists to find.

/// IEEE rounding direction.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum Round {
    /// Round to nearest, ties to even.
    Ne,
    /// Round toward +infinity.
    Up,
    /// Round toward -infinity.
    Down,
    /// Round toward zero (truncate).
    Zero,
}

impl Round {
    /// Stable two-character key used in capture records and merge keys.
    pub fn key(self) -> &'static str {
        match self {
            Round::Ne => "RN",
            Round::Up => "RP",
            Round::Down => "RM",
            Round::Zero => "RZ",
        }
    }

    /// Parse a key produced by [`Round::key`].
    #[cfg_attr(not(test), allow(dead_code))]
    pub fn parse(s: &str) -> Option<Round> {
        match s {
            "RN" => Some(Round::Ne),
            "RP" => Some(Round::Up),
            "RM" => Some(Round::Down),
            "RZ" => Some(Round::Zero),
            _ => None,
        }
    }
}

/// A complete logical control state: rounding direction plus flush-to-zero.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct Mode {
    pub round: Round,
    /// Flush subnormals to zero. On AArch64 this drives `FZ` (and `FZ16` for
    /// half-precision ops); on x86-64 it drives `FTZ` and `DAZ`.
    pub flush: bool,
}

impl Mode {
    /// Stable key, e.g. `"RN.f0"` or `"RZ.f1"`.
    #[cfg_attr(not(test), allow(dead_code))]
    pub fn key(self) -> String {
        format!("{}.f{}", self.round.key(), self.flush as u8)
    }

    /// Parse a key produced by [`Mode::key`].
    #[cfg_attr(not(test), allow(dead_code))]
    pub fn parse(s: &str) -> Option<Mode> {
        let (r, f) = s.split_once(".f")?;
        let round = Round::parse(r)?;
        let flush = match f {
            "0" => false,
            "1" => true,
            _ => return None,
        };
        Some(Mode { round, flush })
    }
}

/// The fixed sweep of logical modes every capture exercises: all four rounding
/// directions crossed with flush off/on (8 modes).
pub fn all_modes() -> Vec<Mode> {
    let mut v = Vec::with_capacity(8);
    for &round in &[Round::Ne, Round::Up, Round::Down, Round::Zero] {
        for &flush in &[false, true] {
            v.push(Mode { round, flush });
        }
    }
    v
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_key_roundtrips() {
        for r in [Round::Ne, Round::Up, Round::Down, Round::Zero] {
            assert_eq!(Round::parse(r.key()), Some(r));
        }
    }

    #[test]
    fn mode_key_roundtrips() {
        for m in all_modes() {
            assert_eq!(Mode::parse(&m.key()), Some(m));
        }
    }

    #[test]
    fn all_modes_is_eight_distinct() {
        let v = all_modes();
        assert_eq!(v.len(), 8);
        let mut keys: Vec<String> = v.iter().map(|m| m.key()).collect();
        keys.sort();
        keys.dedup();
        assert_eq!(keys.len(), 8);
    }

    #[test]
    fn bad_keys_rejected() {
        assert_eq!(Round::parse("XX"), None);
        assert_eq!(Mode::parse("RN"), None);
        assert_eq!(Mode::parse("RN.f2"), None);
        assert_eq!(Mode::parse("ZZ.f0"), None);
    }
}
