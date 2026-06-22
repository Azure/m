// Copyright (c) Microsoft Corporation.

//! Registry key paths.
//!
//! A [`KeyPath`] is a sequence of UTF-16 name components (D7). Parsing accepts
//! either separator (`\` or `/`) and ignores empty components, so leading,
//! trailing, and doubled separators are tolerated. Comparison of individual
//! components for lookup is the tree's job, via the ordinal-casing seam (D6);
//! `KeyPath`'s own `Eq`/`Hash` are exact (case-sensitive) over components.

use crate::Utf16;

/// A registry key path: an ordered list of name components rooted at a
/// session-vended root (D11), e.g. `Software\App\Settings`.
#[derive(Clone, PartialEq, Eq, Hash, Debug, Default)]
pub struct KeyPath {
    components: Vec<Utf16>,
}

impl KeyPath {
    /// The empty (root-relative) path.
    #[must_use]
    pub fn root() -> Self {
        Self::default()
    }

    /// Parse a path from UTF-8, splitting on `\` or `/` and dropping empty
    /// components.
    #[must_use]
    pub fn parse(path: &str) -> Self {
        let components = path
            .split(['\\', '/'])
            .filter(|s| !s.is_empty())
            .map(Utf16::from_utf8)
            .collect();
        Self { components }
    }

    /// The path's name components, root-first.
    #[must_use]
    pub fn components(&self) -> &[Utf16] {
        &self.components
    }

    /// Whether this is the root (no components).
    #[must_use]
    pub fn is_root(&self) -> bool {
        self.components.is_empty()
    }

    /// Number of components.
    #[must_use]
    pub fn len(&self) -> usize {
        self.components.len()
    }

    /// Whether the path has no components (an alias for [`is_root`](Self::is_root)).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.components.is_empty()
    }

    /// Append a component in place.
    pub fn push(&mut self, name: Utf16) {
        self.components.push(name);
    }

    /// Return a new path with `name` appended.
    #[must_use]
    pub fn child(&self, name: Utf16) -> Self {
        let mut next = self.clone();
        next.push(name);
        next
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn root_is_empty() {
        let p = KeyPath::root();
        assert!(p.is_root());
        assert_eq!(p.len(), 0);
        assert!(p.components().is_empty());
    }

    #[test]
    fn parse_splits_on_either_separator() {
        let p = KeyPath::parse("Software\\App/Settings");
        assert_eq!(p.len(), 3);
        assert_eq!(p.components()[0], Utf16::from_utf8("Software"));
        assert_eq!(p.components()[1], Utf16::from_utf8("App"));
        assert_eq!(p.components()[2], Utf16::from_utf8("Settings"));
    }

    #[test]
    fn parse_tolerates_leading_trailing_doubled_separators() {
        let p = KeyPath::parse("\\\\Software\\\\App\\");
        assert_eq!(p.len(), 2);
        assert_eq!(p.components()[0], Utf16::from_utf8("Software"));
        assert_eq!(p.components()[1], Utf16::from_utf8("App"));
    }

    #[test]
    fn parse_empty_is_root() {
        assert!(KeyPath::parse("").is_root());
        assert!(KeyPath::parse("\\\\").is_root());
    }

    #[test]
    fn child_and_push_append() {
        let base = KeyPath::parse("Software");
        let child = base.child(Utf16::from_utf8("App"));
        assert_eq!(child.len(), 2);
        // Original is unchanged.
        assert_eq!(base.len(), 1);

        let mut p = KeyPath::root();
        p.push(Utf16::from_utf8("A"));
        p.push(Utf16::from_utf8("B"));
        assert_eq!(p.len(), 2);
    }
}
