// Copyright (c) Microsoft Corporation.

//! Read side of the shared C++ PIL registry artifact (D5/D15): a safe
//! deserializer that turns the pugixml `<Platform><Registry>` XML documented in
//! D18 into an immutable base [`Hive`], following the mapping decisions in D19.
//!
//! Pure safe-half code (D13): parsing goes through `roxmltree` (a read-only,
//! `#![forbid(unsafe_code)]`-compatible DOM), so the whole loader contains no
//! `unsafe`.
//!
//! Folding rules (D19): a sealed snapshot is collapsed into the immutable base —
//! tombstones (`deleted="true"`) are skipped, mirrored placeholders
//! (`mirrored="true"`) become empty keys so the observed name still enumerates,
//! `last_write_time` is ignored, and `type`/`data` decode to [`ValueData`] with
//! a lossless `Binary` fallback for types the Rust model has no variant for.

use crate::error::{RegistryError, Result};
use crate::path::KeyPath;
use crate::tree::{Hive, ValueData};
use crate::{OrdinalCasing, Utf16};

/// `reg_value_type` numbering shared with the C++ PIL (`registry_base_types.h`,
/// the Win32 `REG_*` values). Changing any of these is a breaking change to the
/// shared artifact format (D5/D18).
mod reg_value_type {
    pub const STRING: u32 = 1; // REG_SZ
    pub const EXPAND_STRING: u32 = 2; // REG_EXPAND_SZ
    pub const BINARY: u32 = 3; // REG_BINARY
    pub const UINT32: u32 = 4; // REG_DWORD
    pub const MULTI_STRING: u32 = 7; // REG_MULTI_SZ
    pub const UINT64: u32 = 11; // REG_QWORD
}

/// Load a base [`Hive`] from a C++ PIL registry artifact (D18).
///
/// `xml` is the serialized document; the loader accepts being handed either the
/// `<Platform>` root or a `<Registry>` element directly. Each `<Key>` under
/// `<Registry>` becomes a first-level subkey of the returned hive, named with
/// the canonical full hive name (D19), so paths vended by a [`Session`] resolve.
/// An absent `<Registry>` yields an empty hive (not an error).
///
/// Wrap the result in `OverlayTree::new(casing, hive)` to read or modify it.
///
/// # Errors
///
/// Returns [`RegistryError::MalformedArtifact`] if the XML is not well-formed,
/// a predefined-hive name is unrecognized, a required attribute is missing, or
/// a value's `type`/`data` cannot be decoded.
///
/// [`Session`]: crate::Session
pub fn load_registry_hive<C: OrdinalCasing>(casing: &C, xml: &str) -> Result<Hive> {
    let doc = roxmltree::Document::parse(xml)
        .map_err(|e| RegistryError::MalformedArtifact(format!("XML parse error: {e}")))?;

    let root = doc.root_element();
    let registry = if root.has_tag_name("Registry") {
        Some(root)
    } else {
        root.children().find(|n| n.is_element() && n.has_tag_name("Registry"))
    };

    let mut hive = Hive::new();
    let Some(registry) = registry else {
        return Ok(hive);
    };

    for hive_key in registry.children().filter(|n| n.is_element() && n.has_tag_name("Key")) {
        let raw_name = required_attr(&hive_key, "name")?;
        let canonical = normalize_hive_name(raw_name)?;
        let path = KeyPath::root().child(Utf16::from_utf8(canonical));
        load_key(casing, &mut hive, &path, &hive_key)?;
    }

    Ok(hive)
}

/// Insert `key_elem` at `path` and recurse into its children. The caller has
/// already established that this key is present (not a tombstone).
fn load_key<C: OrdinalCasing>(
    casing: &C,
    hive: &mut Hive,
    path: &KeyPath,
    key_elem: &roxmltree::Node<'_, '_>,
) -> Result<()> {
    // Guarantee the key itself appears even if it is empty or mirrored.
    hive.insert_key(casing, path);

    // A mirrored placeholder enumerates by name but has no captured contents.
    if attr_is_true(key_elem, "mirrored") {
        return Ok(());
    }

    for child in key_elem.children().filter(roxmltree::Node::is_element) {
        if child.has_tag_name("Value") {
            load_value(casing, hive, path, &child)?;
        } else if child.has_tag_name("Key") {
            // A deleted subkey is absent in the sealed base; skip it.
            if attr_is_true(&child, "deleted") {
                continue;
            }
            let name = required_attr(&child, "name")?;
            let child_path = path.child(Utf16::from_utf8(name));
            load_key(casing, hive, &child_path, &child)?;
        }
        // Unknown elements are ignored for forward compatibility.
    }

    Ok(())
}

/// Decode a `<Value>` element and insert it at `path`, unless it is a tombstone.
fn load_value<C: OrdinalCasing>(
    casing: &C,
    hive: &mut Hive,
    path: &KeyPath,
    value_elem: &roxmltree::Node<'_, '_>,
) -> Result<()> {
    let name = required_attr(value_elem, "name")?;

    // A deleted value is absent in the sealed base; skip it.
    if attr_is_true(value_elem, "deleted") {
        return Ok(());
    }

    let type_str = required_attr(value_elem, "type")?;
    let type_code: u32 = type_str
        .parse()
        .map_err(|_| RegistryError::MalformedArtifact(format!("invalid value type: {type_str:?}")))?;
    let data_hex = required_attr(value_elem, "data")?;
    let bytes = hex_to_bytes(data_hex)?;
    let data = decode_value(type_code, bytes)?;

    hive.insert_value(casing, path, Utf16::from_utf8(name), data);
    Ok(())
}

/// Map any accepted predefined-hive spelling (abbreviation or long form, ordinal
/// case-insensitive) to the canonical full name used by `WellKnownRoot`
/// canonical names (D19).
fn normalize_hive_name(name: &str) -> Result<&'static str> {
    let upper = name.to_ascii_uppercase();
    let canonical = match upper.as_str() {
        "HKCR" | "HKEY_CLASSES_ROOT" => "HKEY_CLASSES_ROOT",
        "HKCU" | "HKEY_CURRENT_USER" => "HKEY_CURRENT_USER",
        "HKLM" | "HKEY_LOCAL_MACHINE" => "HKEY_LOCAL_MACHINE",
        "HKCC" | "HKEY_CURRENT_CONFIG" => "HKEY_CURRENT_CONFIG",
        "HKEY_USERS" => "HKEY_USERS",
        "HKEY_CURRENT_USER_LOCAL_SETTINGS" => "HKEY_CURRENT_USER_LOCAL_SETTINGS",
        "HKEY_PERFORMANCE_DATA" => "HKEY_PERFORMANCE_DATA",
        "HKEY_PERFORMANCE_TEXT" => "HKEY_PERFORMANCE_TEXT",
        "HKEY_PERFORMANCE_NLSTEXT" => "HKEY_PERFORMANCE_NLSTEXT",
        _ => {
            return Err(RegistryError::MalformedArtifact(format!(
                "unknown predefined hive name: {name:?}"
            )));
        }
    };
    Ok(canonical)
}

/// Decode raw value bytes into [`ValueData`] per the `reg_value_type` (D19).
/// Types the Rust model has no variant for (`none`, `dword_be`, `link`, or any
/// unrecognized code) fall back to `Binary`, preserving the bytes losslessly.
fn decode_value(type_code: u32, bytes: Vec<u8>) -> Result<ValueData> {
    let data = match type_code {
        reg_value_type::STRING => ValueData::String(decode_string(&bytes)?),
        reg_value_type::EXPAND_STRING => ValueData::ExpandString(decode_string(&bytes)?),
        reg_value_type::MULTI_STRING => ValueData::MultiString(decode_multi_string(&bytes)?),
        reg_value_type::UINT32 => {
            let arr: [u8; 4] = bytes.as_slice().try_into().map_err(|_| {
                RegistryError::MalformedArtifact(format!(
                    "REG_DWORD requires 4 bytes, found {}",
                    bytes.len()
                ))
            })?;
            ValueData::Dword(u32::from_le_bytes(arr))
        }
        reg_value_type::UINT64 => {
            let arr: [u8; 8] = bytes.as_slice().try_into().map_err(|_| {
                RegistryError::MalformedArtifact(format!(
                    "REG_QWORD requires 8 bytes, found {}",
                    bytes.len()
                ))
            })?;
            ValueData::Qword(u64::from_le_bytes(arr))
        }
        reg_value_type::BINARY => ValueData::Binary(bytes),
        // Every other (none / dword_be / link / unknown) code.
        _ => ValueData::Binary(bytes),
    };
    Ok(data)
}

/// Decode UTF-16LE bytes into code units, dropping a single trailing NUL
/// terminator if present (D19). Ill-formed UTF-16 is preserved losslessly (D9).
fn decode_string(bytes: &[u8]) -> Result<Utf16> {
    let mut units = le_units(bytes)?;
    if units.last() == Some(&0) {
        units.pop();
    }
    Ok(Utf16::from_units(units))
}

/// Decode a NUL-separated, double-NUL-terminated UTF-16LE list (D19).
fn decode_multi_string(bytes: &[u8]) -> Result<Vec<Utf16>> {
    let units = le_units(bytes)?;
    let mut segments: Vec<Vec<u16>> = Vec::new();
    let mut current: Vec<u16> = Vec::new();
    for &u in &units {
        if u == 0 {
            segments.push(std::mem::take(&mut current));
        } else {
            current.push(u);
        }
    }
    if !current.is_empty() {
        segments.push(current);
    }
    // Drop the trailing empty segment(s) produced by the double-NUL terminator.
    while segments.last().is_some_and(Vec::is_empty) {
        segments.pop();
    }
    Ok(segments.into_iter().map(Utf16::from_units).collect())
}

/// Reinterpret a byte slice as little-endian UTF-16 code units. The length must
/// be even (a whole number of `u16`).
fn le_units(bytes: &[u8]) -> Result<Vec<u16>> {
    if !bytes.len().is_multiple_of(2) {
        return Err(RegistryError::MalformedArtifact(format!(
            "UTF-16 data has odd byte length {}",
            bytes.len()
        )));
    }
    Ok(bytes
        .chunks_exact(2)
        .map(|c| u16::from_le_bytes([c[0], c[1]]))
        .collect())
}

/// Decode a lowercase/uppercase hex string into bytes (the inverse of the C++
/// `bytes_to_hex`). An empty string decodes to no bytes.
fn hex_to_bytes(hex: &str) -> Result<Vec<u8>> {
    let chars = hex.as_bytes();
    if !chars.len().is_multiple_of(2) {
        return Err(RegistryError::MalformedArtifact(format!(
            "hex data has odd length {}",
            chars.len()
        )));
    }
    let mut out = Vec::with_capacity(chars.len() / 2);
    for pair in chars.chunks_exact(2) {
        let hi = hex_nibble(pair[0])?;
        let lo = hex_nibble(pair[1])?;
        out.push((hi << 4) | lo);
    }
    Ok(out)
}

fn hex_nibble(c: u8) -> Result<u8> {
    match c {
        b'0'..=b'9' => Ok(c - b'0'),
        b'a'..=b'f' => Ok(c - b'a' + 10),
        b'A'..=b'F' => Ok(c - b'A' + 10),
        _ => Err(RegistryError::MalformedArtifact(format!(
            "invalid hex digit: {:?}",
            char::from(c)
        ))),
    }
}

/// Read a required attribute, erroring with a useful message if absent.
fn required_attr<'a>(node: &roxmltree::Node<'a, '_>, name: &str) -> Result<&'a str> {
    node.attribute(name).ok_or_else(|| {
        RegistryError::MalformedArtifact(format!(
            "<{}> missing required attribute {name:?}",
            node.tag_name().name()
        ))
    })
}

/// Whether `node` has `attr="true"`.
fn attr_is_true(node: &roxmltree::Node<'_, '_>, attr: &str) -> bool {
    node.attribute(attr) == Some("true")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tree::OverlayTree;
    use windows_text::AsciiOrdinalCasing;

    fn casing() -> AsciiOrdinalCasing {
        AsciiOrdinalCasing
    }

    fn load(xml: &str) -> Hive {
        load_registry_hive(&casing(), xml).expect("artifact should parse")
    }

    fn tree(hive: Hive) -> OverlayTree<AsciiOrdinalCasing> {
        OverlayTree::new(casing(), hive)
    }

    fn u(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    const SAMPLE: &str = r#"<Platform>
  <Registry>
    <Key name="HKLM">
      <Key name="Software" last_write_time="133600000000000000">
        <Value name="Name" type="1" data="62006100730065000000"/>
        <Value name="Count" type="4" data="18000000"/>
        <Value name="old" deleted="true"/>
        <Key name="App"><Value name="" type="1" data="680069000000"/></Key>
        <Key name="Observed" mirrored="true"/>
        <Key name="Gone" deleted="true"/>
      </Key>
    </Key>
  </Registry>
</Platform>"#;

    #[test]
    fn hive_name_is_normalized_to_canonical() {
        let t = tree(load(SAMPLE));
        // "HKLM" must resolve under the canonical full name a Session vends.
        assert!(t.key_exists(&KeyPath::parse("HKEY_LOCAL_MACHINE")));
        assert!(t.key_exists(&KeyPath::parse("HKEY_LOCAL_MACHINE\\Software")));
        // The abbreviation itself is not a key name.
        assert!(!t.key_exists(&KeyPath::parse("HKLM")));
    }

    #[test]
    fn string_value_decodes_and_strips_terminator() {
        let t = tree(load(SAMPLE));
        let got = t
            .get_value(&KeyPath::parse("HKEY_LOCAL_MACHINE\\Software"), &u("Name"))
            .expect("value present");
        assert_eq!(got, ValueData::String(u("base")));
    }

    #[test]
    fn dword_value_decodes_little_endian() {
        let t = tree(load(SAMPLE));
        let got = t
            .get_value(&KeyPath::parse("HKEY_LOCAL_MACHINE\\Software"), &u("Count"))
            .expect("value present");
        assert_eq!(got, ValueData::Dword(0x18));
    }

    #[test]
    fn default_value_uses_empty_name() {
        let t = tree(load(SAMPLE));
        let got = t
            .get_value(&KeyPath::parse("HKEY_LOCAL_MACHINE\\Software\\App"), &u(""))
            .expect("default value present");
        assert_eq!(got, ValueData::String(u("hi")));
    }

    #[test]
    fn tombstoned_value_is_absent() {
        let t = tree(load(SAMPLE));
        let got = t.get_value(&KeyPath::parse("HKEY_LOCAL_MACHINE\\Software"), &u("old"));
        assert_eq!(got, Err(RegistryError::ValueNotFound));
    }

    #[test]
    fn tombstoned_key_is_absent() {
        let t = tree(load(SAMPLE));
        assert!(!t.key_exists(&KeyPath::parse("HKEY_LOCAL_MACHINE\\Software\\Gone")));
    }

    #[test]
    fn mirrored_key_is_present_but_empty() {
        let t = tree(load(SAMPLE));
        let observed = KeyPath::parse("HKEY_LOCAL_MACHINE\\Software\\Observed");
        assert!(t.key_exists(&observed));
        assert!(t.enum_subkeys(&observed).expect("enumerable").is_empty());
    }

    #[test]
    fn subkeys_enumerate_in_ordinal_order() {
        let t = tree(load(SAMPLE));
        let names = t
            .enum_subkeys(&KeyPath::parse("HKEY_LOCAL_MACHINE\\Software"))
            .expect("enumerable");
        // App, Observed present; Gone (deleted) absent; ordinal order.
        assert_eq!(names, vec![u("App"), u("Observed")]);
    }

    #[test]
    fn registry_root_element_is_accepted_directly() {
        let xml = r#"<Registry><Key name="HKCU"><Key name="Env"/></Key></Registry>"#;
        let t = tree(load(xml));
        assert!(t.key_exists(&KeyPath::parse("HKEY_CURRENT_USER\\Env")));
    }

    #[test]
    fn absent_registry_yields_empty_hive() {
        let t = tree(load("<Platform></Platform>"));
        assert!(!t.key_exists(&KeyPath::parse("HKEY_LOCAL_MACHINE")));
    }

    #[test]
    fn multi_string_splits_on_nul_and_drops_terminator() {
        // "ab" NUL "c" NUL NUL  -> ["ab", "c"]
        let xml = r#"<Registry><Key name="HKLM">
            <Value name="M" type="7" data="610062000000630000000000"/>
        </Key></Registry>"#;
        let t = tree(load(xml));
        let got = t
            .get_value(&KeyPath::parse("HKEY_LOCAL_MACHINE"), &u("M"))
            .expect("value present");
        assert_eq!(got, ValueData::MultiString(vec![u("ab"), u("c")]));
    }

    #[test]
    fn qword_value_decodes_little_endian() {
        let xml = r#"<Registry><Key name="HKLM">
            <Value name="Q" type="11" data="0100000000000000"/>
        </Key></Registry>"#;
        let t = tree(load(xml));
        let got = t
            .get_value(&KeyPath::parse("HKEY_LOCAL_MACHINE"), &u("Q"))
            .expect("value present");
        assert_eq!(got, ValueData::Qword(1));
    }

    #[test]
    fn binary_value_is_raw_bytes() {
        let xml = r#"<Registry><Key name="HKLM">
            <Value name="B" type="3" data="deadbeef"/>
        </Key></Registry>"#;
        let t = tree(load(xml));
        let got = t
            .get_value(&KeyPath::parse("HKEY_LOCAL_MACHINE"), &u("B"))
            .expect("value present");
        assert_eq!(got, ValueData::Binary(vec![0xde, 0xad, 0xbe, 0xef]));
    }

    #[test]
    fn unsupported_type_falls_back_to_binary() {
        // type 6 == REG_LINK, no Rust variant -> Binary.
        let xml = r#"<Registry><Key name="HKLM">
            <Value name="L" type="6" data="0102"/>
        </Key></Registry>"#;
        let t = tree(load(xml));
        let got = t
            .get_value(&KeyPath::parse("HKEY_LOCAL_MACHINE"), &u("L"))
            .expect("value present");
        assert_eq!(got, ValueData::Binary(vec![0x01, 0x02]));
    }

    #[test]
    fn unknown_hive_name_is_malformed() {
        let err = load_registry_hive(&casing(), r#"<Registry><Key name="HKBOGUS"/></Registry>"#)
            .unwrap_err();
        assert!(matches!(err, RegistryError::MalformedArtifact(_)));
    }

    #[test]
    fn malformed_xml_is_reported() {
        let err = load_registry_hive(&casing(), "<Registry><Key>").unwrap_err();
        assert!(matches!(err, RegistryError::MalformedArtifact(_)));
    }

    #[test]
    fn odd_length_hex_is_malformed() {
        let xml = r#"<Registry><Key name="HKLM"><Value name="X" type="3" data="abc"/></Key></Registry>"#;
        let err = load_registry_hive(&casing(), xml).unwrap_err();
        assert!(matches!(err, RegistryError::MalformedArtifact(_)));
    }

    #[test]
    fn dword_with_wrong_length_is_malformed() {
        let xml = r#"<Registry><Key name="HKLM"><Value name="X" type="4" data="0102"/></Key></Registry>"#;
        let err = load_registry_hive(&casing(), xml).unwrap_err();
        assert!(matches!(err, RegistryError::MalformedArtifact(_)));
    }
}
