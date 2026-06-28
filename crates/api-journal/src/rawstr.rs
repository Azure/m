// Copyright (c) Microsoft Corporation.

//! Encoding-tagged captured text (UT-A1, SHIM-D26).
//!
//! Text captured from the platform is carried **verbatim** in its native encoding
//! plus a one-byte tag of what that encoding is — never transcoded by the producer.
//! Win32 wide APIs (WinHTTP egress, file, registry) yield UTF-16; the HTTP server
//! stack (http.sys / IIS) yields raw narrow octets because HTTP is a byte protocol.
//! Only the final consumer (`cartographer`) decodes. This avoids imposing a
//! transcoding burden in the observed service and preserves ill-formed UTF-16
//! losslessly.
//!
//! Serialized form is uniform and tagged: `{ "enc": "u16"|"raw", "b64": "…" }`. The
//! bytes are base64 (transport packaging, byte-preserving — not a transcode); we do
//! not sniff for UTF-8 validity, because inspecting purely for journal readability
//! has no value to the producer.

use base64::engine::general_purpose::STANDARD as BASE64;
use serde::{Deserialize, Serialize};

/// The native encoding of captured text. Changing a serde rename value is a
/// breaking on-disk format change.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub enum TextEnc {
    /// UTF-16 little-endian code units — Win32 wide APIs (WinHTTP, file, registry).
    #[serde(rename = "u16")]
    Utf16Le,
    /// Raw narrow octets — HTTP server stack (http.sys / IIS); exact charset unknown.
    #[default]
    #[serde(rename = "raw")]
    Bytes,
}

/// Captured text held verbatim as raw bytes plus its native [`TextEnc`] tag.
///
/// Construct from whatever the platform handed us; decode only when a human/analysis
/// view is needed via [`to_string_lossy`](RawStr::to_string_lossy).
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct RawStr {
    enc: TextEnc,
    #[serde(rename = "b64", with = "b64bytes")]
    bytes: Vec<u8>,
}

impl RawStr {
    /// Wrap UTF-16 code units verbatim (little-endian), preserving ill-formed
    /// sequences (e.g. unpaired surrogates) losslessly.
    #[must_use]
    pub fn from_utf16_units(units: &[u16]) -> Self {
        let mut bytes = Vec::with_capacity(units.len() * 2);
        for u in units {
            bytes.extend_from_slice(&u.to_le_bytes());
        }
        Self { enc: TextEnc::Utf16Le, bytes }
    }

    /// Wrap raw narrow octets verbatim (exact charset unknown).
    #[must_use]
    pub fn from_bytes(bytes: &[u8]) -> Self {
        Self { enc: TextEnc::Bytes, bytes: bytes.to_vec() }
    }

    /// Wrap UTF-8 text as raw octets (its bytes are carried verbatim under the
    /// `Bytes` tag).
    #[must_use]
    pub fn from_utf8(s: &str) -> Self {
        Self::from_bytes(s.as_bytes())
    }

    /// The native encoding tag.
    #[must_use]
    pub fn encoding(&self) -> TextEnc {
        self.enc
    }

    /// The raw, untranscoded bytes.
    #[must_use]
    pub fn raw_bytes(&self) -> &[u8] {
        &self.bytes
    }

    /// Whether there are no bytes (used for `skip_serializing_if`).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.bytes.is_empty()
    }

    /// Decode to a UTF-8 `String`, lossily — the only place a transcode happens, in
    /// the reader. Ill-formed sequences become U+FFFD; never panics.
    #[must_use]
    pub fn to_string_lossy(&self) -> String {
        match self.enc {
            TextEnc::Utf16Le => {
                let units: Vec<u16> = self
                    .bytes
                    .chunks_exact(2)
                    .map(|c| u16::from_le_bytes([c[0], c[1]]))
                    .collect();
                String::from_utf16_lossy(&units)
            }
            TextEnc::Bytes => String::from_utf8_lossy(&self.bytes).into_owned(),
        }
    }
}

/// serde adapter: bytes <-> base64 string. Always present (uniform tagged form).
mod b64bytes {
    use super::BASE64;
    use base64::Engine as _;
    use serde::{Deserialize, Deserializer, Serializer};

    pub(super) fn serialize<S: Serializer>(bytes: &[u8], serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(&BASE64.encode(bytes))
    }

    pub(super) fn deserialize<'de, D: Deserializer<'de>>(d: D) -> Result<Vec<u8>, D::Error> {
        let text = String::deserialize(d)?;
        BASE64.decode(text.as_bytes()).map_err(serde::de::Error::custom)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn round_trip(r: &RawStr) -> RawStr {
        let json = serde_json::to_string(r).expect("serialize");
        serde_json::from_str(&json).expect("parse")
    }

    #[test]
    fn utf16_round_trips_and_decodes() {
        let r = RawStr::from_utf16_units(&[0x0048, 0x0069]); // "Hi"
        assert_eq!(round_trip(&r), r);
        assert_eq!(r.encoding(), TextEnc::Utf16Le);
        assert_eq!(r.to_string_lossy(), "Hi");
    }

    #[test]
    fn bytes_round_trip_and_decode() {
        let r = RawStr::from_utf8("GET");
        assert_eq!(round_trip(&r), r);
        assert_eq!(r.encoding(), TextEnc::Bytes);
        assert_eq!(r.to_string_lossy(), "GET");
    }

    #[test]
    fn ill_formed_utf16_is_preserved_verbatim() {
        // A lone high surrogate is not well-formed UTF-16, but must round-trip byte-
        // for-byte and decode lossily (U+FFFD) rather than panic or be dropped.
        let r = RawStr::from_utf16_units(&[0xD800, 0x0041]);
        let back = round_trip(&r);
        assert_eq!(back, r);
        assert_eq!(back.raw_bytes(), &[0x00, 0xD8, 0x41, 0x00]);
        assert!(back.to_string_lossy().ends_with('A'));
    }

    #[test]
    fn serialized_form_is_tagged_base64() {
        let json = serde_json::to_string(&RawStr::from_utf8("ab")).unwrap();
        assert!(json.contains("\"enc\":\"raw\""), "expected raw tag in {json}");
        assert!(json.contains("\"b64\":\"YWI=\""), "expected base64 body in {json}");
        let json16 = serde_json::to_string(&RawStr::from_utf16_units(&[0x41])).unwrap();
        assert!(json16.contains("\"enc\":\"u16\""), "expected u16 tag in {json16}");
    }

    #[test]
    fn default_is_empty_raw() {
        let r = RawStr::default();
        assert_eq!(r.encoding(), TextEnc::Bytes);
        assert!(r.is_empty());
        assert_eq!(round_trip(&r), r);
    }

    #[test]
    fn non_utf8_narrow_bytes_survive() {
        let r = RawStr::from_bytes(&[0x00, 0xFF, 0x80, 0x7F]);
        assert_eq!(round_trip(&r).raw_bytes(), &[0x00, 0xFF, 0x80, 0x7F]);
    }
}
