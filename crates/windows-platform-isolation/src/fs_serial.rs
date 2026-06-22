// Copyright (c) Microsoft Corporation.

//! Read side of the shared C++ PIL filesystem artifact (D5/D15): a safe
//! deserializer that turns the `<Platform><Filesystem>` XML emitted by the C++
//! PIL `filesystem::save_xml` into an immutable base [`FileTree`]. It is the
//! filesystem analogue of [`load_registry_hive`](crate::load_registry_hive).
//!
//! Pure safe-half code (D13): parsing goes through `roxmltree` (a read-only,
//! `#![forbid(unsafe_code)]`-compatible DOM), so the whole loader contains no
//! `unsafe`.
//!
//! Format (mirroring the C++ writer): each `<Root kind="N" text="…">` element
//! carries the root directory's own metadata as attributes and nests
//! `<Directory>` / `<File>` children. A directory or file element may carry
//! `deleted="true"` (a tombstone) or `mirrored="true"` (a name-only
//! placeholder). The node kind is implied by the element name and is *not*
//! serialized; file byte content is never stored (metadata only, D14).
//!
//! Folding rules (mirroring the registry loader's D19): a sealed snapshot is
//! collapsed into the immutable base — tombstones are skipped, and a mirrored
//! placeholder becomes an empty directory so its observed name still
//! enumerates.

use crate::file_path::FilePath;
use crate::fs_error::{FilesystemError, FilesystemResult};
use crate::fs_tree::{FileMetadata, FileTree};
use crate::{OrdinalCasing, Utf16};

/// The directory separator written between path components when rebuilding a
/// [`FilePath`] from nested element names.
const SEPARATOR: u16 = b'\\' as u16;

/// Load a base [`FileTree`] from a C++ PIL filesystem artifact.
///
/// `xml` is the serialized document; the loader accepts being handed either the
/// `<Platform>` root or a `<Filesystem>` element directly. Each `<Root>` becomes
/// a top-level directory named with its `text` (e.g. `C:`), under which the
/// nested directories and files resolve as ordinary path components. An absent
/// `<Filesystem>` yields an empty tree (not an error).
///
/// Wrap the result in `OverlayFileTree::new(casing, tree)` to read or modify it.
///
/// # Errors
///
/// Returns [`FilesystemError::MalformedArtifact`] if the XML is not well-formed,
/// a required attribute is missing, or a metadata attribute cannot be parsed.
pub fn load_filesystem<C: OrdinalCasing>(casing: &C, xml: &str) -> FilesystemResult<FileTree> {
    let doc = roxmltree::Document::parse(xml)
        .map_err(|e| FilesystemError::MalformedArtifact(format!("XML parse error: {e}")))?;

    let root = doc.root_element();
    let filesystem = if root.has_tag_name("Filesystem") {
        Some(root)
    } else {
        root.children()
            .find(|n| n.is_element() && n.has_tag_name("Filesystem"))
    };

    let mut tree = FileTree::new();
    let Some(filesystem) = filesystem else {
        return Ok(tree);
    };

    for root_node in filesystem
        .children()
        .filter(|n| n.is_element() && n.has_tag_name("Root"))
    {
        let text = required_attr(&root_node, "text")?;
        let prefix: Vec<u16> = Utf16::from_utf8(text).as_units().to_vec();
        let metadata = read_metadata(&root_node)?;
        tree.insert_dir(casing, &FilePath::from_units(prefix.clone()), metadata);
        load_dir_children(casing, &mut tree, &prefix, &root_node)?;
    }

    Ok(tree)
}

/// Load the `<Directory>` / `<File>` children of `elem`, whose own path is
/// `prefix` (the units of a [`FilePath`] up to and including this directory).
fn load_dir_children<C: OrdinalCasing>(
    casing: &C,
    tree: &mut FileTree,
    prefix: &[u16],
    elem: &roxmltree::Node<'_, '_>,
) -> FilesystemResult<()> {
    for child in elem.children().filter(roxmltree::Node::is_element) {
        if child.has_tag_name("Directory") {
            // A deleted subtree is absent in the sealed base; skip it.
            if attr_is_true(&child, "deleted") {
                continue;
            }
            let name = required_attr(&child, "name")?;
            let path = join(prefix, name);
            let metadata = read_metadata(&child)?;
            tree.insert_dir(casing, &FilePath::from_units(path.clone()), metadata);

            // A mirrored placeholder enumerates by name but has no captured
            // children, so it folds to an empty directory.
            if attr_is_true(&child, "mirrored") {
                continue;
            }
            load_dir_children(casing, tree, &path, &child)?;
        } else if child.has_tag_name("File") {
            // A deleted file is absent in the sealed base; skip it.
            if attr_is_true(&child, "deleted") {
                continue;
            }
            let name = required_attr(&child, "name")?;
            let path = join(prefix, name);
            let metadata = read_metadata(&child)?;
            tree.insert_file(casing, &FilePath::from_units(path), metadata);
        }
        // Unknown elements are ignored for forward compatibility.
    }
    Ok(())
}

/// Append `name` to `prefix` with a separator, producing the units of the
/// child's full [`FilePath`].
fn join(prefix: &[u16], name: &str) -> Vec<u16> {
    let name_units = Utf16::from_utf8(name);
    let mut out = Vec::with_capacity(prefix.len() + 1 + name_units.as_units().len());
    out.extend_from_slice(prefix);
    out.push(SEPARATOR);
    out.extend_from_slice(name_units.as_units());
    out
}

/// Reconstruct a node's [`FileMetadata`] from a persisted element. The kind is
/// implied by the element name, so it is not part of the stored metadata here.
/// Every attribute defaults to `0` when absent, matching the C++ reader.
fn read_metadata(elem: &roxmltree::Node<'_, '_>) -> FilesystemResult<FileMetadata> {
    Ok(FileMetadata {
        size: parse_attr::<u64>(elem, "size")?,
        creation_time: parse_attr::<i64>(elem, "creation_time")?,
        last_write_time: parse_attr::<i64>(elem, "last_write_time")?,
        last_access_time: parse_attr::<i64>(elem, "last_access_time")?,
        attributes: parse_attr::<u32>(elem, "attributes")?,
    })
}

/// Parse a numeric attribute, defaulting to `0` if absent and erroring if
/// present but unparseable.
fn parse_attr<T>(elem: &roxmltree::Node<'_, '_>, name: &str) -> FilesystemResult<T>
where
    T: core::str::FromStr + Default,
{
    match elem.attribute(name) {
        None => Ok(T::default()),
        Some(raw) => raw.parse::<T>().map_err(|_| {
            FilesystemError::MalformedArtifact(format!(
                "<{}> attribute {name:?} is not a valid integer: {raw:?}",
                elem.tag_name().name()
            ))
        }),
    }
}

/// Read a required attribute, erroring with a useful message if absent.
fn required_attr<'a>(
    node: &roxmltree::Node<'a, '_>,
    name: &str,
) -> FilesystemResult<&'a str> {
    node.attribute(name).ok_or_else(|| {
        FilesystemError::MalformedArtifact(format!(
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
    use crate::fs_tree::{NodeKind, OverlayFileTree};
    use windows_text::AsciiOrdinalCasing;

    fn p(s: &str) -> FilePath {
        FilePath::from_utf8(s)
    }

    const SMALL: &str = r#"<?xml version="1.0" encoding="utf-8"?>
<Platform>
  <Filesystem>
    <Root kind="2" text="C:" size="0" attributes="16">
      <Directory name="Windows" attributes="16">
        <File name="notepad.exe" size="4096" attributes="32"/>
      </Directory>
    </Root>
  </Filesystem>
</Platform>
"#;

    #[test]
    fn loads_nested_dir_and_file() {
        let casing = AsciiOrdinalCasing;
        let tree = load_filesystem(&casing, SMALL).expect("artifact should parse");
        let overlay = OverlayFileTree::new(casing, tree);
        assert!(overlay.dir_exists(&p("C:\\Windows")));
        assert!(overlay.file_exists(&p("C:\\Windows\\notepad.exe")));
        let md = overlay.file_metadata(&p("C:\\Windows\\notepad.exe")).unwrap();
        assert_eq!(md.size, 4096);
        assert_eq!(md.attributes, 32);
    }

    #[test]
    fn absent_filesystem_yields_empty_tree() {
        let casing = AsciiOrdinalCasing;
        let tree = load_filesystem(&casing, "<Platform></Platform>").expect("parses");
        let overlay = OverlayFileTree::new(casing, tree);
        assert!(!overlay.dir_exists(&p("C:\\Windows")));
    }

    #[test]
    fn malformed_xml_is_reported() {
        let casing = AsciiOrdinalCasing;
        let err = load_filesystem(&casing, "<Platform").unwrap_err();
        assert!(matches!(err, FilesystemError::MalformedArtifact(_)));
    }

    #[test]
    fn missing_root_text_is_reported() {
        let casing = AsciiOrdinalCasing;
        let xml = r#"<Platform><Filesystem><Root kind="2"/></Filesystem></Platform>"#;
        let err = load_filesystem(&casing, xml).unwrap_err();
        assert!(matches!(err, FilesystemError::MalformedArtifact(_)));
    }

    #[test]
    fn unparseable_metadata_is_reported() {
        let casing = AsciiOrdinalCasing;
        let xml = r#"<Platform><Filesystem><Root kind="2" text="C:">
            <File name="x" size="not-a-number"/>
        </Root></Filesystem></Platform>"#;
        let err = load_filesystem(&casing, xml).unwrap_err();
        assert!(matches!(err, FilesystemError::MalformedArtifact(_)));
    }

    #[test]
    fn deleted_entries_fold_away() {
        let casing = AsciiOrdinalCasing;
        let xml = r#"<Platform><Filesystem><Root kind="2" text="C:">
            <Directory name="Keep"/>
            <Directory name="Gone" deleted="true"/>
            <File name="stale.tmp" deleted="true"/>
        </Root></Filesystem></Platform>"#;
        let tree = load_filesystem(&casing, xml).expect("parses");
        let overlay = OverlayFileTree::new(casing, tree);
        assert!(overlay.dir_exists(&p("C:\\Keep")));
        assert!(!overlay.dir_exists(&p("C:\\Gone")));
        assert!(!overlay.file_exists(&p("C:\\stale.tmp")));
    }

    #[test]
    fn mirrored_placeholder_is_empty_directory() {
        let casing = AsciiOrdinalCasing;
        let xml = r#"<Platform><Filesystem><Root kind="2" text="C:">
            <Directory name="Mir" mirrored="true">
                <File name="ignored.txt" size="9"/>
            </Directory>
        </Root></Filesystem></Platform>"#;
        let tree = load_filesystem(&casing, xml).expect("parses");
        let overlay = OverlayFileTree::new(casing, tree);
        assert!(overlay.dir_exists(&p("C:\\Mir")));
        assert!(overlay.read_dir(&p("C:\\Mir")).unwrap().is_empty());
    }

    #[test]
    fn read_dir_is_ordinal_ordered() {
        let casing = AsciiOrdinalCasing;
        let xml = r#"<Platform><Filesystem><Root kind="2" text="C:">
            <Directory name="Windows"/>
            <Directory name="Users"/>
            <Directory name="ProgramData"/>
            <File name="autoexec.bat" size="1"/>
        </Root></Filesystem></Platform>"#;
        let tree = load_filesystem(&casing, xml).expect("parses");
        let overlay = OverlayFileTree::new(casing, tree);
        let entries = overlay.read_dir(&p("C:")).unwrap();
        let names: Vec<String> = entries.iter().map(|e| e.name.to_utf8().unwrap()).collect();
        // AsciiOrdinalCasing folds case: autoexec.bat(A), ProgramData(P),
        // Users(U), Windows(W).
        assert_eq!(
            names,
            vec![
                "autoexec.bat".to_string(),
                "ProgramData".to_string(),
                "Users".to_string(),
                "Windows".to_string()
            ]
        );
        assert_eq!(entries[0].kind, NodeKind::File);
    }
}
