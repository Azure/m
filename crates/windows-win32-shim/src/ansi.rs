// Copyright (c) Microsoft Corporation.

//! `CP_ACP` boundary transcoding for the ANSI (`*A`) entry points (SHIM-D15).
//!
//! The `*A` exports differ from their `*W` peers only at the string boundary:
//! they accept / return `CP_ACP` (ANSI) bytes where the `*W` forms use UTF-16,
//! then delegate to the **same** safe `reg_ops` / `fs_ops` core. This module
//! owns the transcoding (Design Autonomy): the conversion is *specified* here as
//! "go through the `windows-text` `CP_ACP` code page" and `windows-text` is the
//! chosen implementation because its `MultiByteToWideChar` / `WideCharToMultiByte`
//! wrappers convert the whole slice by length — so embedded and trailing NULs
//! (which `REG_MULTI_SZ` data relies on) survive a round-trip.
//!
//! Everything here is pointer-free and `unsafe`-free; the raw-pointer marshaling
//! that feeds these helpers stays quarantined in the ABI modules (SHIM-D2).

use windows_platform_isolation::Utf16;
use windows_sys::Win32::System::Registry::{REG_EXPAND_SZ, REG_LINK, REG_MULTI_SZ, REG_SZ};
use windows_text::CodePage;

/// The byte width of one UTF-16 code unit (registry string DATA is UTF-16 LE).
const UTF16_UNIT_BYTES: usize = 2;

/// Decode `CP_ACP` bytes (no embedded NUL — a NUL-terminated `LPCSTR` body) into
/// UTF-16.
///
/// A decode failure (which `CP_ACP` does not normally produce for a byte input)
/// yields the empty string, so the downstream op fails with the same not-found /
/// invalid shape the `W` form would see for an empty argument (owned behavior).
#[must_use]
pub fn ansi_to_utf16(bytes: &[u8]) -> Utf16 {
    Utf16::from_code_page(CodePage::ANSI, bytes).unwrap_or_else(|_| Utf16::from_units(Vec::new()))
}

/// Encode UTF-16 `units` into `CP_ACP` bytes. A failure yields empty bytes
/// (owned behavior, mirroring [`ansi_to_utf16`]).
#[must_use]
pub fn utf16_to_ansi(units: &[u16]) -> Vec<u8> {
    Utf16::from_units(units.to_vec())
        .to_code_page(CodePage::ANSI)
        .unwrap_or_default()
}

/// Encode UTF-16 `units` into a fixed `CHAR` buffer, truncating to leave room
/// for a terminating NUL and zero-filling the remainder. This is the
/// `WIN32_FIND_DATAA` `cFileName` / `cAlternateFileName` shape.
pub fn fill_ansi_fixed(units: &[u16], buf: &mut [u8]) {
    buf.fill(0);
    if buf.is_empty() {
        return;
    }
    let ansi = utf16_to_ansi(units);
    let n = core::cmp::min(ansi.len(), buf.len() - 1);
    buf[..n].copy_from_slice(&ansi[..n]);
}

/// Whether a `REG_*` type carries textual DATA whose bytes must be transcoded
/// between `CP_ACP` and the UTF-16 stored form at the `A`/`W` boundary.
///
/// The textual types are `REG_SZ`, `REG_EXPAND_SZ`, `REG_LINK`, and
/// `REG_MULTI_SZ` (matching the C++ `mwin32` D6 set). Changing this set is a
/// breaking change.
#[must_use]
pub fn is_string_type(win32_type: u32) -> bool {
    win32_type == REG_SZ
        || win32_type == REG_EXPAND_SZ
        || win32_type == REG_LINK
        || win32_type == REG_MULTI_SZ
}

/// Reinterpret UTF-16 LE `bytes` (the registry stored form) as code units. A
/// trailing odd byte is dropped (owned tolerance; the stored form is always an
/// even number of bytes).
fn bytes_to_units(bytes: &[u8]) -> Vec<u16> {
    bytes
        .chunks_exact(UTF16_UNIT_BYTES)
        .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
        .collect()
}

/// Serialize UTF-16 `units` into their UTF-16 LE byte form (the registry stored
/// form).
fn units_to_bytes(units: &[u16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(units.len() * UTF16_UNIT_BYTES);
    for &u in units {
        out.extend_from_slice(&u.to_le_bytes());
    }
    out
}

/// Convert registry value DATA from the UTF-16 stored form to `CP_ACP` bytes for
/// an `A` caller. For textual types the whole buffer is converted in one call
/// (embedded / trailing NULs preserved, so `REG_MULTI_SZ` is handled uniformly);
/// non-textual types pass through unchanged.
#[must_use]
pub fn data_wide_to_ansi(win32_type: u32, bytes: &[u8]) -> Vec<u8> {
    if is_string_type(win32_type) {
        utf16_to_ansi(&bytes_to_units(bytes))
    } else {
        bytes.to_vec()
    }
}

/// Convert registry value DATA from an `A` caller's `CP_ACP` bytes to the UTF-16
/// stored form. The textual / passthrough split mirrors [`data_wide_to_ansi`].
#[must_use]
pub fn data_ansi_to_wide(win32_type: u32, bytes: &[u8]) -> Vec<u8> {
    if is_string_type(win32_type) {
        units_to_bytes(ansi_to_utf16(bytes).as_units())
    } else {
        bytes.to_vec()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use windows_sys::Win32::System::Registry::{REG_BINARY, REG_DWORD};

    fn ascii_units(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    #[test]
    fn ansi_round_trip_ascii() {
        let units = ascii_units("Hello");
        let ansi = utf16_to_ansi(&units);
        assert_eq!(ansi, b"Hello");
        assert_eq!(ansi_to_utf16(&ansi).as_units(), units.as_slice());
    }

    #[test]
    fn ansi_to_utf16_empty_is_empty() {
        assert!(ansi_to_utf16(&[]).as_units().is_empty());
        assert!(utf16_to_ansi(&[]).is_empty());
    }

    #[test]
    fn fill_ansi_fixed_truncates_and_nul_terminates() {
        let mut buf = [0xAAu8; 4];
        fill_ansi_fixed(&ascii_units("abcdef"), &mut buf);
        // Room for 3 chars + NUL.
        assert_eq!(&buf, b"abc\0");
    }

    #[test]
    fn fill_ansi_fixed_fits_with_trailing_zeros() {
        let mut buf = [0xAAu8; 8];
        fill_ansi_fixed(&ascii_units("ab"), &mut buf);
        assert_eq!(&buf, b"ab\0\0\0\0\0\0");
    }

    #[test]
    fn fill_ansi_fixed_empty_buffer_is_noop() {
        let mut buf: [u8; 0] = [];
        fill_ansi_fixed(&ascii_units("x"), &mut buf);
        assert!(buf.is_empty());
    }

    #[test]
    fn string_type_predicate() {
        assert!(is_string_type(REG_SZ));
        assert!(is_string_type(REG_EXPAND_SZ));
        assert!(is_string_type(REG_LINK));
        assert!(is_string_type(REG_MULTI_SZ));
        assert!(!is_string_type(REG_BINARY));
        assert!(!is_string_type(REG_DWORD));
    }

    #[test]
    fn data_round_trip_reg_sz() {
        // Stored form: UTF-16 LE "Hi" + terminating NUL unit.
        let stored = units_to_bytes(&[b'H' as u16, b'i' as u16, 0]);
        let ansi = data_wide_to_ansi(REG_SZ, &stored);
        assert_eq!(ansi, b"Hi\0");
        assert_eq!(data_ansi_to_wide(REG_SZ, &ansi), stored);
    }

    #[test]
    fn data_round_trip_reg_multi_sz_preserves_embedded_nuls() {
        // "a\0b\0\0" as UTF-16 LE: two strings then the list terminator.
        let units = [b'a' as u16, 0, b'b' as u16, 0, 0];
        let stored = units_to_bytes(&units);
        let ansi = data_wide_to_ansi(REG_MULTI_SZ, &stored);
        assert_eq!(ansi, b"a\0b\0\0");
        assert_eq!(data_ansi_to_wide(REG_MULTI_SZ, &ansi), stored);
    }

    #[test]
    fn data_non_string_types_pass_through() {
        let raw = [0x01u8, 0x02, 0x03, 0xFF];
        assert_eq!(data_wide_to_ansi(REG_BINARY, &raw), raw);
        assert_eq!(data_ansi_to_wide(REG_BINARY, &raw), raw);
        let dword = 0x1234_5678u32.to_le_bytes();
        assert_eq!(data_wide_to_ansi(REG_DWORD, &dword), dword);
        assert_eq!(data_ansi_to_wide(REG_DWORD, &dword), dword);
    }

    #[test]
    fn bytes_to_units_drops_trailing_odd_byte() {
        assert_eq!(bytes_to_units(&[0x41, 0x00, 0x42]), vec![0x0041]);
    }
}
