// Copyright (c) Microsoft Corporation.

//! The Win32 `(REG_* type, raw bytes)` ↔ [`ValueData`] codec (SHIM-D11).
//!
//! The Win32 registry value ABI is byte-oriented: a value is a `REG_*` type tag
//! plus an opaque byte buffer with an explicit length. The
//! `windows-platform-isolation` facade is instead structured: a [`ValueData`]
//! enum with six typed variants. This module owns the translation between the
//! two representations (Design Autonomy — the mapping is specified here, not
//! inherited).
//!
//! The six Win32 types this crate round-trips are `REG_SZ`, `REG_EXPAND_SZ`,
//! `REG_MULTI_SZ`, `REG_DWORD`, `REG_QWORD`, and `REG_BINARY` — exactly the six
//! [`ValueData`] variants. Round-trip is **byte-faithful** for these types
//! within the documented constraints below: the byte length a caller wrote is
//! reproduced exactly on read-back (so `REG_SZ` including its terminating NUL
//! round-trips to the same `cbData`).
//!
//! Constraints (owned behavior):
//! - String types (`REG_SZ` / `REG_EXPAND_SZ` / `REG_MULTI_SZ`) require an
//!   even byte length (whole UTF-16 code units); an odd length is rejected with
//!   `ERROR_INVALID_DATA`.
//! - `REG_DWORD` requires exactly 4 bytes and `REG_QWORD` exactly 8; other
//!   lengths are rejected with `ERROR_INVALID_DATA`.
//! - Any `REG_*` type outside the six above is decoded as `REG_BINARY`
//!   (bytes preserved, but the type tag degrades to `REG_BINARY` on read-back).
//!   Changing this set is a breaking change.

use windows_platform_isolation::{Utf16, ValueData};
use windows_sys::Win32::Foundation::ERROR_INVALID_DATA;
use windows_sys::Win32::System::Registry::{
    REG_BINARY, REG_DWORD, REG_EXPAND_SZ, REG_MULTI_SZ, REG_QWORD, REG_SZ,
};

use crate::error_map::Lstatus;

/// The byte width of one UTF-16 code unit.
const UTF16_UNIT_BYTES: usize = 2;
/// The byte width of a `REG_DWORD`.
const DWORD_BYTES: usize = 4;
/// The byte width of a `REG_QWORD`.
const QWORD_BYTES: usize = 8;

/// Encode a [`ValueData`] into its Win32 `(type, bytes)` representation.
///
/// The returned type is one of the six supported `REG_*` tags and the bytes are
/// the exact buffer a `RegQueryValueExW` caller would receive.
#[must_use]
pub fn encode(value: &ValueData) -> (u32, Vec<u8>) {
    match value {
        ValueData::String(s) => (REG_SZ, units_to_bytes(s.as_units())),
        ValueData::ExpandString(s) => (REG_EXPAND_SZ, units_to_bytes(s.as_units())),
        ValueData::MultiString(list) => (REG_MULTI_SZ, multi_sz_to_bytes(list)),
        ValueData::Dword(n) => (REG_DWORD, n.to_le_bytes().to_vec()),
        ValueData::Qword(n) => (REG_QWORD, n.to_le_bytes().to_vec()),
        ValueData::Binary(b) => (REG_BINARY, b.clone()),
        // `ValueData` is `#[non_exhaustive]`; a future variant has no defined
        // byte encoding yet, so preserve nothing rather than emit wrong bytes.
        _ => (REG_BINARY, Vec::new()),
    }
}

/// Decode a Win32 `(type, bytes)` pair into a [`ValueData`].
///
/// # Errors
///
/// Returns `ERROR_INVALID_DATA` when the byte length is inconsistent with the
/// declared type (odd-length string, or a `REG_DWORD`/`REG_QWORD` whose length
/// is not 4/8).
pub fn decode(win32_type: u32, bytes: &[u8]) -> Result<ValueData, Lstatus> {
    let invalid = ERROR_INVALID_DATA as Lstatus;
    match win32_type {
        REG_SZ => Ok(ValueData::String(Utf16::from_units(bytes_to_units(bytes)?))),
        REG_EXPAND_SZ => Ok(ValueData::ExpandString(Utf16::from_units(
            bytes_to_units(bytes)?,
        ))),
        REG_MULTI_SZ => Ok(ValueData::MultiString(bytes_to_multi_sz(bytes)?)),
        REG_DWORD => {
            let arr: [u8; DWORD_BYTES] = bytes.try_into().map_err(|_| invalid)?;
            Ok(ValueData::Dword(u32::from_le_bytes(arr)))
        }
        REG_QWORD => {
            let arr: [u8; QWORD_BYTES] = bytes.try_into().map_err(|_| invalid)?;
            Ok(ValueData::Qword(u64::from_le_bytes(arr)))
        }
        // REG_BINARY and any unsupported type: preserve the bytes verbatim.
        _ => Ok(ValueData::Binary(bytes.to_vec())),
    }
}

/// Flatten UTF-16 code units to little-endian bytes.
fn units_to_bytes(units: &[u16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(units.len() * UTF16_UNIT_BYTES);
    for &u in units {
        out.extend_from_slice(&u.to_le_bytes());
    }
    out
}

/// Parse little-endian bytes back into UTF-16 code units.
///
/// # Errors
///
/// Returns `ERROR_INVALID_DATA` if the byte length is odd.
fn bytes_to_units(bytes: &[u8]) -> Result<Vec<u16>, Lstatus> {
    if !bytes.len().is_multiple_of(UTF16_UNIT_BYTES) {
        return Err(ERROR_INVALID_DATA as Lstatus);
    }
    Ok(bytes
        .chunks_exact(UTF16_UNIT_BYTES)
        .map(|c| u16::from_le_bytes([c[0], c[1]]))
        .collect())
}

/// Encode a `REG_MULTI_SZ` list: each string followed by a NUL unit, then a
/// final NUL unit terminating the list.
fn multi_sz_to_bytes(list: &[Utf16]) -> Vec<u8> {
    let mut units: Vec<u16> = Vec::new();
    for s in list {
        units.extend_from_slice(s.as_units());
        units.push(0);
    }
    units.push(0);
    units_to_bytes(&units)
}

/// Decode a `REG_MULTI_SZ` byte buffer into its component strings, stopping at
/// the first empty string (the list terminator).
///
/// # Errors
///
/// Returns `ERROR_INVALID_DATA` if the byte length is odd.
fn bytes_to_multi_sz(bytes: &[u8]) -> Result<Vec<Utf16>, Lstatus> {
    let units = bytes_to_units(bytes)?;
    Ok(units
        .split(|&u| u == 0)
        .take_while(|component| !component.is_empty())
        .map(|component| Utf16::from_units(component.to_vec()))
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    fn round_trip(value: ValueData) {
        let (t, bytes) = encode(&value);
        let decoded = decode(t, &bytes).expect("decode of just-encoded value must succeed");
        assert_eq!(decoded, value, "round-trip changed the value");
    }

    #[test]
    fn dword_round_trips_and_is_little_endian() {
        let (t, bytes) = encode(&ValueData::Dword(0xDEAD_BEEF));
        assert_eq!(t, REG_DWORD);
        assert_eq!(bytes, vec![0xEF, 0xBE, 0xAD, 0xDE]);
        round_trip(ValueData::Dword(0xDEAD_BEEF));
        round_trip(ValueData::Dword(0));
        round_trip(ValueData::Dword(u32::MAX));
    }

    #[test]
    fn qword_round_trips_and_is_little_endian() {
        let (t, bytes) = encode(&ValueData::Qword(1));
        assert_eq!(t, REG_QWORD);
        assert_eq!(bytes, vec![1, 0, 0, 0, 0, 0, 0, 0]);
        round_trip(ValueData::Qword(1 << 40));
        round_trip(ValueData::Qword(u64::MAX));
    }

    #[test]
    fn string_round_trips_with_embedded_nul_byte_count() {
        // "hi" plus a terminating NUL unit => 3 units => 6 bytes, reproduced.
        let value = ValueData::String(Utf16::from_units(vec![
            0x68, // 'h'
            0x69, // 'i'
            0x00, // trailing NUL preserved as a unit
        ]));
        let (t, bytes) = encode(&value);
        assert_eq!(t, REG_SZ);
        assert_eq!(bytes.len(), 6);
        round_trip(value);
    }

    #[test]
    fn plain_string_round_trips() {
        round_trip(ValueData::String(w("hello world")));
        round_trip(ValueData::ExpandString(w("%PATH%")));
        round_trip(ValueData::String(w("")));
    }

    #[test]
    fn multi_sz_round_trips_two_strings() {
        // ["a","bb"] encodes to a \0 b b \0 \0 (6 units, 12 bytes).
        let value = ValueData::MultiString(vec![w("a"), w("bb")]);
        let (t, bytes) = encode(&value);
        assert_eq!(t, REG_MULTI_SZ);
        assert_eq!(bytes.len(), 12);
        let units = bytes_to_units(&bytes).unwrap();
        assert_eq!(units, vec![0x61, 0x00, 0x62, 0x62, 0x00, 0x00]);
        round_trip(value);
    }

    #[test]
    fn multi_sz_empty_list_round_trips() {
        round_trip(ValueData::MultiString(vec![]));
    }

    #[test]
    fn binary_round_trips_verbatim() {
        let (t, bytes) = encode(&ValueData::Binary(vec![0x00, 0x11, 0x22, 0xFE, 0xFF]));
        assert_eq!(t, REG_BINARY);
        assert_eq!(bytes, vec![0x00, 0x11, 0x22, 0xFE, 0xFF]);
        round_trip(ValueData::Binary(vec![0xDE, 0xAD, 0xBE, 0xEF]));
        round_trip(ValueData::Binary(vec![]));
    }

    #[test]
    fn odd_length_string_is_invalid_data() {
        assert_eq!(decode(REG_SZ, &[0x41]), Err(ERROR_INVALID_DATA as Lstatus));
        assert_eq!(
            decode(REG_EXPAND_SZ, &[0x41, 0x00, 0x42]),
            Err(ERROR_INVALID_DATA as Lstatus)
        );
        assert_eq!(
            decode(REG_MULTI_SZ, &[0x41]),
            Err(ERROR_INVALID_DATA as Lstatus)
        );
    }

    #[test]
    fn wrong_length_integers_are_invalid_data() {
        assert_eq!(
            decode(REG_DWORD, &[1, 2, 3]),
            Err(ERROR_INVALID_DATA as Lstatus)
        );
        assert_eq!(
            decode(REG_DWORD, &[1, 2, 3, 4, 5]),
            Err(ERROR_INVALID_DATA as Lstatus)
        );
        assert_eq!(decode(REG_QWORD, &[0; 4]), Err(ERROR_INVALID_DATA as Lstatus));
    }

    #[test]
    fn unsupported_type_decodes_as_binary() {
        // REG_NONE (0) and other tags preserve their bytes as binary.
        let decoded = decode(0, &[1, 2, 3]).unwrap();
        assert_eq!(decoded, ValueData::Binary(vec![1, 2, 3]));
    }

    #[test]
    fn dword_byte_layout_matches_manual_query_buffer() {
        // A DWORD written then read as 4 LE bytes recovers the value.
        let (_, bytes) = encode(&ValueData::Dword(0x1234_5678));
        assert_eq!(bytes, vec![0x78, 0x56, 0x34, 0x12]);
    }
}
