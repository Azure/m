// Copyright (c) Microsoft Corporation.

//! NDJSON-driven Win32→`m` alias **object** emitter (MW5, SHIM-D4).
//!
//! Where [`crate::alias_gen`] emits C++ *source* (compiled by `cl.exe`), this
//! module writes the alias **COFF object bytes directly** via the `object`
//! crate. Producing the alias artifact therefore needs **no C++ compiler and no
//! MSVC tool at all** — only the client's own linker consumes the result. The
//! module has no `unsafe` and no platform dependency; it is pure manifest →
//! object-bytes translation, runnable from a build script or a test on any host
//! (the emitted object targets x64 regardless of the host).
//!
//! # Manifest
//!
//! The input is the checked-in NDJSON manifest [`ALIAS_MANIFEST`]
//! (`windows_win32_shim_aliases.ndjson`). One JSON object per active alias:
//!
//! ```text
//! {"win32":"RegOpenKeyExW","shim":"mRegOpenKeyExW"}
//! ```
//!
//! * `win32` (required): the genuine Win32 name being redirected.
//! * `shim` (optional): the target export; defaults to `"m"` + `win32`. When
//!   present it must equal `"m"` + `win32` (the mechanical strip-the-`m` rule),
//!   so the field is a readable cross-check, not an independent degree of freedom.
//! * `alias` (optional, default `true`): set `false` for a name that is exported
//!   but deliberately **not** auto-redirected (e.g. `CloseHandle`). Such records
//!   are parsed and validated but emit no alias slot.
//!
//! A blank line, or a line whose first non-blank characters are `#` or `//`, is a
//! comment / section line and is ignored. A not-yet-implemented name is carried
//! as a commented-out record so it enables by uncommenting.
//!
//! # Emitted object
//!
//! For each aliased record the object carries, in a `.data` section:
//!
//! * an **undefined external** symbol `m<Name>` (resolved at client link time
//!   from the shim's import library);
//! * a **defined public** 8-byte slot `__imp_<Name>` initialized — via an
//!   `IMAGE_REL_AMD64_ADDR64` relocation — to the address of `m<Name>`. This is
//!   the decisive redirect: a genuine `<windows.h>` `dllimport` call compiles to
//!   `call [__imp_<Name>]`, which now lands on the shim.
//!
//! and, in a `.drectve` linker-directive section, a `/alternatename:<Name>=m<Name>`
//! for every aliased record (a best-effort fallback for a plain, non-`dllimport`
//! reference). This mirrors the C++ `generate_mwin32_alias.cmake` contract.
//!
//! The slot type is intentionally signature-free: the IAT slot is pointer-sized
//! and the client casts through its own declared signature at the call site.

use std::collections::BTreeSet;

use object::write::{Object, Relocation, Section, Symbol, SymbolSection};
use object::{
    Architecture, BinaryFormat, Endianness, RelocationEncoding, RelocationFlags, RelocationKind,
    SectionFlags, SectionKind, SymbolFlags, SymbolKind, SymbolScope,
};
use tinyjson::JsonValue;

/// The NDJSON alias manifest, embedded at compile time. The single, versioned
/// source of truth for the set of Win32 names this object redirects.
pub const ALIAS_MANIFEST: &str = include_str!("../windows_win32_shim_aliases.ndjson");

/// Size in bytes of an x64 `__imp_` IAT slot (a pointer).
const PTR_SIZE: usize = 8;
/// Alignment of an `__imp_` slot (pointer-aligned).
const PTR_ALIGN: u64 = 8;
/// Width, in bits, of the `IMAGE_REL_AMD64_ADDR64` relocation that initializes a
/// slot to `&m<Name>`.
const ADDR64_BITS: u8 = 64;

// COFF section characteristics for a `.drectve` linker-directive section: it
// carries linker info (`LNK_INFO`), is removed from the final image
// (`LNK_REMOVE`), and is byte-aligned (`ALIGN_1BYTES`). Changing these is a
// breaking change to the emitted directive section.
const IMAGE_SCN_LNK_INFO: u32 = 0x0000_0200;
const IMAGE_SCN_LNK_REMOVE: u32 = 0x0000_0800;
const IMAGE_SCN_ALIGN_1BYTES: u32 = 0x0010_0000;

/// Error produced while parsing the manifest or emitting the object.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AliasObjError {
    /// A non-comment line was not a parseable JSON object of the expected shape.
    /// Carries the 1-based line number and a human-readable reason.
    Json { line: usize, message: String },
    /// A record had no `win32` field. Carries the 1-based line number.
    MissingWin32 { line: usize },
    /// A `win32` / `shim` name was malformed (empty, non-identifier, or a `shim`
    /// that is not `"m"` + `win32`). Carries the line number and offending name.
    InvalidName { line: usize, name: String },
    /// The `object` writer failed to encode the COFF object. Carries the message.
    Emit(String),
}

impl std::fmt::Display for AliasObjError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            AliasObjError::Json { line, message } => {
                write!(f, "alias manifest line {line}: {message}")
            }
            AliasObjError::MissingWin32 { line } => {
                write!(f, "alias manifest line {line}: record has no \"win32\" field")
            }
            AliasObjError::InvalidName { line, name } => {
                write!(f, "alias manifest line {line}: invalid name '{name}'")
            }
            AliasObjError::Emit(message) => write!(f, "alias object emission failed: {message}"),
        }
    }
}

impl std::error::Error for AliasObjError {}

/// One parsed manifest record.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AliasRecord {
    /// Genuine Win32 name being redirected (e.g. `RegOpenKeyExW`).
    pub win32: String,
    /// Shim export the genuine name is redirected to (e.g. `mRegOpenKeyExW`);
    /// always `"m"` + [`Self::win32`].
    pub shim: String,
    /// Whether this record is auto-redirected. `false` marks an exported but
    /// opt-out name (no alias slot emitted).
    pub alias: bool,
}

/// True for a legal genuine Win32 export name: a non-empty ASCII identifier
/// whose first character is a letter or underscore (the latter covers the
/// dusty-deck `_lopen` / `_lread` primitives) and whose remaining characters are
/// ASCII alphanumeric or underscore.
fn is_valid_win32(name: &str) -> bool {
    let mut chars = name.chars();
    match chars.next() {
        Some(c) if c.is_ascii_alphabetic() || c == '_' => {}
        _ => return false,
    }
    chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

/// Parse the NDJSON manifest into records, in file order. Comment / blank lines
/// are skipped; every other line must be a JSON object carrying a valid `win32`
/// (and optional `shim` / `alias`).
pub fn parse_manifest(ndjson: &str) -> Result<Vec<AliasRecord>, AliasObjError> {
    let mut records = Vec::new();
    for (idx, raw) in ndjson.lines().enumerate() {
        let line = idx + 1;
        let trimmed = raw.trim();
        if trimmed.is_empty() || trimmed.starts_with('#') || trimmed.starts_with("//") {
            continue;
        }
        let value: JsonValue = trimmed.parse().map_err(|e| AliasObjError::Json {
            line,
            message: format!("{e}"),
        })?;
        let JsonValue::Object(obj) = &value else {
            return Err(AliasObjError::Json {
                line,
                message: "expected a JSON object".to_string(),
            });
        };
        let win32 = match obj.get("win32") {
            Some(JsonValue::String(s)) => s.clone(),
            Some(_) => {
                return Err(AliasObjError::Json {
                    line,
                    message: "\"win32\" must be a string".to_string(),
                });
            }
            None => return Err(AliasObjError::MissingWin32 { line }),
        };
        if !is_valid_win32(&win32) {
            return Err(AliasObjError::InvalidName { line, name: win32 });
        }
        let expected_shim = format!("m{win32}");
        let shim = match obj.get("shim") {
            Some(JsonValue::String(s)) => s.clone(),
            Some(_) => {
                return Err(AliasObjError::Json {
                    line,
                    message: "\"shim\" must be a string".to_string(),
                });
            }
            None => expected_shim.clone(),
        };
        if shim != expected_shim {
            return Err(AliasObjError::InvalidName { line, name: shim });
        }
        let alias = match obj.get("alias") {
            Some(JsonValue::Boolean(b)) => *b,
            Some(_) => {
                return Err(AliasObjError::Json {
                    line,
                    message: "\"alias\" must be a boolean".to_string(),
                });
            }
            None => true,
        };
        records.push(AliasRecord { win32, shim, alias });
    }
    Ok(records)
}

/// The records that receive an emitted alias slot: `alias == true`, deduped by
/// `win32` (first occurrence wins), preserving file order.
fn aliased_records(records: &[AliasRecord]) -> Vec<&AliasRecord> {
    let mut seen = BTreeSet::new();
    records
        .iter()
        .filter(|r| r.alias)
        .filter(|r| seen.insert(r.win32.as_str()))
        .collect()
}

/// The set of genuine Win32 names that the manifest redirects (aliased only).
/// Useful for parity checks against other manifests.
pub fn aliased_win32_names(ndjson: &str) -> Result<BTreeSet<String>, AliasObjError> {
    let records = parse_manifest(ndjson)?;
    Ok(aliased_records(&records)
        .into_iter()
        .map(|r| r.win32.clone())
        .collect())
}

/// Emit the Win32→`m` alias COFF object (x64) for the given NDJSON manifest.
/// Returns the encoded object-file bytes ready to be written to a `.obj`.
pub fn generate_alias_object(ndjson: &str) -> Result<Vec<u8>, AliasObjError> {
    let records = parse_manifest(ndjson)?;
    let aliased = aliased_records(&records);

    let mut obj = Object::new(BinaryFormat::Coff, Architecture::X86_64, Endianness::Little);
    let data = obj.add_section(Vec::new(), b".data".to_vec(), SectionKind::Data);

    let mut directives = String::new();
    for record in &aliased {
        // Undefined external reference to the shim function `m<Name>`.
        let target = obj.add_symbol(Symbol {
            name: record.shim.clone().into_bytes(),
            value: 0,
            size: 0,
            kind: SymbolKind::Text,
            scope: SymbolScope::Unknown,
            weak: false,
            section: SymbolSection::Undefined,
            flags: SymbolFlags::None,
        });

        // The pointer-sized `__imp_<Name>` IAT slot, initialized by a relocation
        // to `&m<Name>`.
        let offset = obj.append_section_data(data, &[0u8; PTR_SIZE], PTR_ALIGN);
        obj.add_symbol(Symbol {
            name: format!("__imp_{}", record.win32).into_bytes(),
            value: offset,
            size: PTR_SIZE as u64,
            kind: SymbolKind::Data,
            scope: SymbolScope::Linkage,
            weak: false,
            section: SymbolSection::Section(data),
            flags: SymbolFlags::None,
        });
        obj.add_relocation(
            data,
            Relocation {
                offset,
                symbol: target,
                addend: 0,
                flags: RelocationFlags::Generic {
                    kind: RelocationKind::Absolute,
                    encoding: RelocationEncoding::Generic,
                    size: ADDR64_BITS,
                },
            },
        )
        .map_err(|e| AliasObjError::Emit(format!("{e}")))?;

        if !directives.is_empty() {
            directives.push(' ');
        }
        directives.push_str(&format!("/alternatename:{}={}", record.win32, record.shim));
    }

    // The `.drectve` linker-directive section carrying the /alternatename
    // fallbacks. Only emitted when there is at least one aliased record.
    if !directives.is_empty() {
        let drectve = obj.add_section(Vec::new(), b".drectve".to_vec(), SectionKind::Linker);
        obj.append_section_data(drectve, directives.as_bytes(), 1);
        let section: &mut Section = obj.section_mut(drectve);
        section.flags = SectionFlags::Coff {
            characteristics: IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE | IMAGE_SCN_ALIGN_1BYTES,
        };
    }

    obj.write().map_err(|e| AliasObjError::Emit(format!("{e}")))
}

#[cfg(test)]
mod tests {
    use super::*;
    use object::read::{Object as _, ObjectSection as _, ObjectSymbol as _};

    const SYNTHETIC: &str = "\
# a comment
// another comment

{\"win32\":\"RegOpenKeyExW\",\"shim\":\"mRegOpenKeyExW\"}
{\"win32\":\"CreateFileW\"}
{\"win32\":\"CloseHandle\",\"shim\":\"mCloseHandle\",\"alias\":false}
";

    #[test]
    fn parse_skips_comments_and_blanks() {
        let records = parse_manifest(SYNTHETIC).unwrap();
        assert_eq!(records.len(), 3);
        assert_eq!(records[0].win32, "RegOpenKeyExW");
    }

    #[test]
    fn parse_defaults_shim_to_m_plus_win32() {
        let records = parse_manifest(SYNTHETIC).unwrap();
        let create = records.iter().find(|r| r.win32 == "CreateFileW").unwrap();
        assert_eq!(create.shim, "mCreateFileW");
        assert!(create.alias);
    }

    #[test]
    fn parse_reads_alias_false() {
        let records = parse_manifest(SYNTHETIC).unwrap();
        let close = records.iter().find(|r| r.win32 == "CloseHandle").unwrap();
        assert!(!close.alias);
    }

    #[test]
    fn parse_errors_on_missing_win32() {
        let err = parse_manifest("{\"shim\":\"mFoo\"}").unwrap_err();
        assert!(matches!(err, AliasObjError::MissingWin32 { line: 1 }));
    }

    #[test]
    fn parse_errors_on_non_object_line() {
        let err = parse_manifest("[1,2,3]").unwrap_err();
        assert!(matches!(err, AliasObjError::Json { line: 1, .. }));
    }

    #[test]
    fn parse_errors_on_shim_mismatch() {
        let err = parse_manifest("{\"win32\":\"Foo\",\"shim\":\"mBar\"}").unwrap_err();
        assert!(matches!(err, AliasObjError::InvalidName { line: 1, name } if name == "mBar"));
    }

    #[test]
    fn parse_errors_on_malformed_win32() {
        let err = parse_manifest("{\"win32\":\"1Foo\"}").unwrap_err();
        assert!(matches!(err, AliasObjError::InvalidName { line: 1, name } if name == "1Foo"));
    }

    #[test]
    fn parse_accepts_dusty_deck_underscore_name() {
        let records = parse_manifest("{\"win32\":\"_lopen\",\"shim\":\"m_lopen\"}").unwrap();
        assert_eq!(records[0].win32, "_lopen");
        assert_eq!(records[0].shim, "m_lopen");
    }

    #[test]
    fn aliased_excludes_alias_false_and_dedupes() {
        let dupes = "\
{\"win32\":\"Foo\"}
{\"win32\":\"Foo\"}
{\"win32\":\"Bar\",\"alias\":false}
";
        let records = parse_manifest(dupes).unwrap();
        let aliased = aliased_records(&records);
        assert_eq!(aliased.len(), 1);
        assert_eq!(aliased[0].win32, "Foo");
    }

    #[test]
    fn generated_object_parses_as_coff() {
        let bytes = generate_alias_object(SYNTHETIC).unwrap();
        let file = object::File::parse(bytes.as_slice()).unwrap();
        assert_eq!(file.format(), object::BinaryFormat::Coff);
        assert_eq!(file.architecture(), object::Architecture::X86_64);
    }

    #[test]
    fn generated_object_has_imp_slot_and_undefined_target_per_alias() {
        let bytes = generate_alias_object(SYNTHETIC).unwrap();
        let file = object::File::parse(bytes.as_slice()).unwrap();

        let names: Vec<String> = file
            .symbols()
            .filter_map(|s| s.name().ok().map(String::from))
            .collect();
        // RegOpenKeyExW + CreateFileW are aliased (CloseHandle is alias:false).
        assert!(names.iter().any(|n| n == "__imp_RegOpenKeyExW"));
        assert!(names.iter().any(|n| n == "__imp_CreateFileW"));
        assert!(!names.iter().any(|n| n == "__imp_CloseHandle"));
        assert!(names.iter().any(|n| n == "mRegOpenKeyExW"));
        assert!(names.iter().any(|n| n == "mCreateFileW"));
    }

    #[test]
    fn generated_object_relocations_match_aliased_count() {
        let bytes = generate_alias_object(SYNTHETIC).unwrap();
        let file = object::File::parse(bytes.as_slice()).unwrap();
        let reloc_count: usize = file.sections().map(|s| s.relocations().count()).sum();
        // Two aliased records (RegOpenKeyExW, CreateFileW); CloseHandle opts out.
        assert_eq!(reloc_count, 2);
    }

    #[test]
    fn generated_object_drectve_carries_alternatename() {
        let bytes = generate_alias_object(SYNTHETIC).unwrap();
        let file = object::File::parse(bytes.as_slice()).unwrap();
        let drectve = file
            .sections()
            .find(|s| s.name() == Ok(".drectve"))
            .expect(".drectve section present");
        let text = String::from_utf8(drectve.data().unwrap().to_vec()).unwrap();
        assert!(text.contains("/alternatename:RegOpenKeyExW=mRegOpenKeyExW"));
        assert!(text.contains("/alternatename:CreateFileW=mCreateFileW"));
        assert!(!text.contains("CloseHandle"));
    }

    #[test]
    fn real_manifest_aliases_ninety_nine_names() {
        let names = aliased_win32_names(ALIAS_MANIFEST).unwrap();
        assert_eq!(names.len(), 99);
        assert!(names.contains("RegOpenKeyExW"));
        assert!(names.contains("FindFirstFileExW"));
        // The loader family is aliased (non-opt-in).
        assert!(names.contains("LoadLibraryW"));
        assert!(names.contains("GetProcAddress"));
        // The COM family is aliased (non-opt-in).
        assert!(names.contains("CoCreateInstance"));
        assert!(names.contains("CoGetClassObject"));
        // The web-host activation entry is aliased (non-opt-in, MW11).
        assert!(names.contains("RegisterModule"));
        // CloseHandle is exported but opts out of auto-redirect.
        assert!(!names.contains("CloseHandle"));
    }

    #[test]
    fn real_manifest_emits_a_parseable_object_with_ninety_nine_slots() {
        let bytes = generate_alias_object(ALIAS_MANIFEST).unwrap();
        let file = object::File::parse(bytes.as_slice()).unwrap();
        let imp_count = file
            .symbols()
            .filter_map(|s| s.name().ok())
            .filter(|n| n.starts_with("__imp_"))
            .count();
        assert_eq!(imp_count, 99);
    }

    /// Drift guard: the NDJSON aliased set must equal the `.def` aliased set
    /// (the two manifests describe the same logical alias roster; the COFF path
    /// reads the NDJSON, the C++-text path reads the `.def`).
    #[test]
    fn ndjson_aliased_set_matches_def_aliased_set() {
        let ndjson_set = aliased_win32_names(ALIAS_MANIFEST).unwrap();
        let def = crate::alias_gen::parse_manifest(crate::alias_gen::EXPORT_DEF).unwrap();
        let def_set: BTreeSet<String> = def
            .aliased
            .iter()
            .map(|shim| crate::alias_gen::win32_name(shim).to_string())
            .collect();
        assert_eq!(ndjson_set, def_set);
    }
}
