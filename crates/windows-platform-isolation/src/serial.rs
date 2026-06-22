// Copyright (c) Microsoft Corporation.

//! Read side of the shared C++ PIL registry artifact (D5/D15): a safe
//! deserializer that turns the pugixml `<Platform><Registry>` XML documented in
//! D18 into an immutable base [`Hive`], following the mapping decisions in D19.
//! The matching write side ([`save_registry_hive`], D21) re-serializes a hive
//! back to the same format, so `load`→`save`→`load` is a fixed point.
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
use crate::tree::{Hive, OverlayTree, ValueData};
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

/// Serialize a base [`Hive`] back to a C++ PIL registry artifact (D21) — the
/// inverse of [`load_registry_hive`].
///
/// Each first-level subkey is a canonical predefined-hive name (D19) emitted as
/// a `<Key>` under `<Registry>`; nested keys recurse, and values are written as
/// `<Value name type data/>` with lowercase-hex, little-endian `data` produced
/// by [`encode_value`]. Output ordering is the registry's ordinal sort (D8), so
/// it is deterministic and `load`→`save`→`load` is a fixed point.
///
/// Key and value names are assumed to be well-formed text, as the shared
/// artifact format requires; any ill-formed UTF-16 is rendered with the Unicode
/// replacement character to match the format's text-only name model.
#[must_use]
pub fn save_registry_hive<C: OrdinalCasing>(casing: C, hive: &Hive) -> String {
    let tree = OverlayTree::new(casing, hive.clone());
    let root = KeyPath::root();

    let mut out = String::new();
    out.push_str("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    out.push_str("<Platform>\n");
    out.push_str("  <Registry>\n");
    for name in tree.enum_subkeys(&root).unwrap_or_default() {
        write_key(&tree, &mut out, &root.child(name.clone()), &name, 2);
    }
    out.push_str("  </Registry>\n");
    out.push_str("</Platform>\n");
    out
}

/// Emit the `<Key>` at `path` (named `name`) with its values and subkeys,
/// indented `depth` levels of two spaces, recursing into subkeys.
fn write_key<C: OrdinalCasing>(
    tree: &OverlayTree<C>,
    out: &mut String,
    path: &KeyPath,
    name: &Utf16,
    depth: usize,
) {
    use core::fmt::Write as _;

    let pad = "  ".repeat(depth);
    let values = tree.enum_values(path).unwrap_or_default();
    let subkeys = tree.enum_subkeys(path).unwrap_or_default();

    if values.is_empty() && subkeys.is_empty() {
        let _ = writeln!(out, "{pad}<Key name=\"{}\"/>", xml_escape(name));
        return;
    }

    let _ = writeln!(out, "{pad}<Key name=\"{}\">", xml_escape(name));
    let child_pad = "  ".repeat(depth + 1);
    for (vname, data) in &values {
        let (type_code, bytes) = encode_value(data);
        let _ = writeln!(
            out,
            "{child_pad}<Value name=\"{}\" type=\"{type_code}\" data=\"{}\"/>",
            xml_escape(vname),
            bytes_to_hex(&bytes),
        );
    }
    for sub in &subkeys {
        write_key(tree, out, &path.child(sub.clone()), sub, depth + 1);
    }
    let _ = writeln!(out, "{pad}</Key>");
}

/// Lowercase-hex encode bytes (the inverse of [`hex_to_bytes`]).
fn bytes_to_hex(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut s = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        s.push(char::from(HEX[usize::from(b >> 4)]));
        s.push(char::from(HEX[usize::from(b & 0x0f)]));
    }
    s
}

/// Escape a name for use as an XML attribute value (the five predefined
/// entities relevant to a double-quoted attribute).
fn xml_escape(name: &Utf16) -> String {
    let text = String::from_utf16_lossy(name.as_units());
    let mut out = String::with_capacity(text.len());
    for c in text.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            _ => out.push(c),
        }
    }
    out
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
///
/// Shared by both the XML loader and the live registry provider (D20): the live
/// read path hands the OS `REG_*` type code and raw bytes straight through here.
pub(crate) fn decode_value(type_code: u32, bytes: Vec<u8>) -> Result<ValueData> {
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

/// Encode [`ValueData`] into its raw `(REG_* type code, value bytes)` form — the
/// exact inverse of [`decode_value`] (D20/D21). Shared by the live write path
/// and the XML save side so a captured value round-trips back to the same bytes.
///
/// String types carry their UTF-16 NUL terminator; multi-strings are
/// NUL-separated and double-NUL-terminated, matching what the live registry and
/// the C++ artifact format expect.
pub(crate) fn encode_value(data: &ValueData) -> (u32, Vec<u8>) {
    match data {
        ValueData::String(s) => (reg_value_type::STRING, encode_string(s)),
        ValueData::ExpandString(s) => (reg_value_type::EXPAND_STRING, encode_string(s)),
        ValueData::MultiString(list) => (reg_value_type::MULTI_STRING, encode_multi_string(list)),
        ValueData::Dword(n) => (reg_value_type::UINT32, n.to_le_bytes().to_vec()),
        ValueData::Qword(n) => (reg_value_type::UINT64, n.to_le_bytes().to_vec()),
        ValueData::Binary(bytes) => (reg_value_type::BINARY, bytes.clone()),
    }
}

/// Encode UTF-16 code units as little-endian bytes with a single trailing NUL
/// terminator (the inverse of [`decode_string`]).
fn encode_string(s: &Utf16) -> Vec<u8> {
    let mut units = s.as_units().to_vec();
    units.push(0);
    le_bytes(&units)
}

/// Encode a string list as NUL-separated, double-NUL-terminated UTF-16LE bytes
/// (the inverse of [`decode_multi_string`]). An empty list encodes to a lone
/// NUL terminator.
fn encode_multi_string(list: &[Utf16]) -> Vec<u8> {
    let mut units: Vec<u16> = Vec::new();
    for s in list {
        units.extend_from_slice(s.as_units());
        units.push(0);
    }
    // Terminating empty string (also the whole encoding when the list is empty).
    units.push(0);
    le_bytes(&units)
}

/// Serialize UTF-16 code units to little-endian bytes (the inverse of
/// [`le_units`]).
fn le_bytes(units: &[u16]) -> Vec<u8> {
    let mut out = Vec::with_capacity(units.len() * 2);
    for &u in units {
        out.extend_from_slice(&u.to_le_bytes());
    }
    out
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

    // --- Write side (M5-4) -------------------------------------------------

    fn save(hive: &Hive) -> String {
        save_registry_hive(casing(), hive)
    }

    /// Build a hive with one key under HKCU carrying the given named values.
    fn hive_with(values: &[(&str, ValueData)]) -> Hive {
        let mut hive = Hive::new();
        let path = KeyPath::parse("HKEY_CURRENT_USER\\T");
        hive.insert_key(&casing(), &path);
        for (name, data) in values {
            hive.insert_value(&casing(), &path, u(name), data.clone());
        }
        hive
    }

    #[test]
    fn save_then_load_is_a_fixed_point() {
        let hive1 = load(SAMPLE);
        let xml1 = save(&hive1);
        let hive2 = load(&xml1);
        let xml2 = save(&hive2);
        assert_eq!(xml1, xml2, "load∘save must be idempotent");
    }

    #[test]
    fn fixture_artifact_round_trips_idempotently() {
        let fixture = include_str!("../testdata/registry_artifact.xml");
        let xml1 = save(&load(fixture));
        let xml2 = save(&load(&xml1));
        assert_eq!(xml1, xml2, "fixture load∘save must be idempotent");
    }

    #[test]
    fn saved_document_preserves_values_and_folds_tombstones() {
        let reloaded = load(&save(&load(SAMPLE)));
        let t = tree(reloaded);
        let software = KeyPath::parse("HKEY_LOCAL_MACHINE\\Software");
        assert_eq!(
            t.get_value(&software, &u("Name")).unwrap(),
            ValueData::String(u("base"))
        );
        assert_eq!(
            t.get_value(&software, &u("Count")).unwrap(),
            ValueData::Dword(0x18)
        );
        // The tombstoned value/key from the source did not reappear.
        assert_eq!(
            t.get_value(&software, &u("old")),
            Err(RegistryError::ValueNotFound)
        );
        assert!(!t.key_exists(&software.child(u("Gone"))));
        // Default (empty-name) value survives the round trip.
        assert_eq!(
            t.get_value(&software.child(u("App")), &u("")).unwrap(),
            ValueData::String(u("hi"))
        );
    }

    #[test]
    fn every_value_type_round_trips_through_save() {
        let original = hive_with(&[
            ("s", ValueData::String(u("hello"))),
            ("e", ValueData::ExpandString(u("%PATH%"))),
            ("m", ValueData::MultiString(vec![u("one"), u("two")])),
            ("empty", ValueData::MultiString(Vec::new())),
            ("d", ValueData::Dword(0xDEAD_BEEF)),
            ("q", ValueData::Qword(0x0123_4567_89AB_CDEF)),
            ("b", ValueData::Binary(vec![0, 1, 2, 254, 255])),
        ]);
        let t = tree(load(&save(&original)));
        let path = KeyPath::parse("HKEY_CURRENT_USER\\T");
        assert_eq!(t.get_value(&path, &u("s")).unwrap(), ValueData::String(u("hello")));
        assert_eq!(
            t.get_value(&path, &u("e")).unwrap(),
            ValueData::ExpandString(u("%PATH%"))
        );
        assert_eq!(
            t.get_value(&path, &u("m")).unwrap(),
            ValueData::MultiString(vec![u("one"), u("two")])
        );
        assert_eq!(
            t.get_value(&path, &u("empty")).unwrap(),
            ValueData::MultiString(Vec::new())
        );
        assert_eq!(t.get_value(&path, &u("d")).unwrap(), ValueData::Dword(0xDEAD_BEEF));
        assert_eq!(
            t.get_value(&path, &u("q")).unwrap(),
            ValueData::Qword(0x0123_4567_89AB_CDEF)
        );
        assert_eq!(
            t.get_value(&path, &u("b")).unwrap(),
            ValueData::Binary(vec![0, 1, 2, 254, 255])
        );
    }

    #[test]
    fn empty_key_serializes_self_closing() {
        let mut hive = Hive::new();
        hive.insert_key(&casing(), &KeyPath::parse("HKEY_CURRENT_USER\\Empty"));
        let xml = save(&hive);
        assert!(xml.contains("<Key name=\"Empty\"/>"), "got:\n{xml}");
    }

    #[test]
    fn special_characters_in_names_are_escaped_and_round_trip() {
        let original = hive_with(&[("a&b<c>\"d", ValueData::Dword(7))]);
        let xml = save(&original);
        assert!(xml.contains("a&amp;b&lt;c&gt;&quot;d"), "got:\n{xml}");
        let t = tree(load(&xml));
        assert_eq!(
            t.get_value(&KeyPath::parse("HKEY_CURRENT_USER\\T"), &u("a&b<c>\"d"))
                .unwrap(),
            ValueData::Dword(7)
        );
    }

    #[test]
    fn document_has_xml_declaration_and_root() {
        let xml = save(&load(SAMPLE));
        assert!(xml.starts_with("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Platform>"));
        assert!(xml.trim_end().ends_with("</Platform>"));
    }
}
