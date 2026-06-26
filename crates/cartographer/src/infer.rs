// Copyright (c) Microsoft Corporation.

//! Inferring path templates from observed concrete paths (AJ-E1).
//!
//! Synthesis needs to fold many concrete observed paths (`/custom/cat`,
//! `/custom/dog`, …) onto OpenAPI path templates (`/custom/{id}`). Two mechanisms
//! cooperate:
//!
//! 1. **Spec-driven (preferred).** A concrete path that already matches a known
//!    template (from the existing specs) is assigned to it, preserving the human's
//!    parameter names. This is why, once someone renames a synthesized
//!    `/custom/{id}` to `/custom/{word}`, later runs keep `{word}`.
//! 2. **Heuristic (for novel paths).** Unmatched paths are clustered with a
//!    conservative rule: a segment collapses to a `{id}` parameter only when it is
//!    **not** a top-level resource (there is a literal segment before it) and its
//!    parent has **two or more** distinct *leaf* children. So `/custom/cat` and
//!    `/custom/dog` become `/custom/{id}`, while sibling top-level resources like
//!    `/healthz` and `/metrics` stay literal.
//!
//! The heuristic deliberately only collapses a trailing data segment; multi-
//! parameter templates are left to spec-driven matching. The synthesized
//! parameter name is a generic placeholder a human is expected to refine.

use std::collections::BTreeMap;

use crate::path::{PathMatch, PathTemplate, best_match};

/// The synthesized placeholder name for an inferred path parameter.
const INFERRED_PARAM: &str = "id";

/// A set of path templates: the existing spec templates plus any inferred from
/// observed paths.
#[derive(Clone, Debug, Default)]
pub struct TemplateSet {
    templates: Vec<PathTemplate>,
}

impl TemplateSet {
    /// Infer templates from observed concrete paths, preferring `existing`
    /// templates (a concrete path matching one of them produces no new template).
    #[must_use]
    pub fn infer(observed: &[String], existing: &[PathTemplate]) -> TemplateSet {
        let mut templates = existing.to_vec();

        // Cluster only the paths the existing templates do not already cover.
        let mut trie = Trie::default();
        for path in observed {
            if best_match(existing, path).is_none() {
                trie.insert(&split_segments(path));
            }
        }

        let mut inferred = Vec::new();
        trie.emit("", 0, &mut inferred);
        for raw in inferred {
            if !templates.iter().any(|t| t.raw() == raw) {
                templates.push(PathTemplate::parse(&raw));
            }
        }

        TemplateSet { templates }
    }

    /// All templates (existing + inferred).
    #[must_use]
    pub fn templates(&self) -> &[PathTemplate] {
        &self.templates
    }

    /// Assign a concrete path to its most specific template, capturing parameters.
    #[must_use]
    pub fn assign(&self, path: &str) -> Option<(&PathTemplate, PathMatch)> {
        best_match(&self.templates, path)
    }
}

/// Split a path into its segments, dropping the empty segment before the leading
/// `/` (so `/custom/cat` → `["custom", "cat"]`).
fn split_segments(path: &str) -> Vec<&str> {
    path.split('/').skip(1).collect()
}

/// A prefix tree of path segments used to cluster observed paths.
#[derive(Default)]
struct Trie {
    children: BTreeMap<String, Trie>,
    terminal: bool,
}

impl Trie {
    fn insert(&mut self, segments: &[&str]) {
        match segments.split_first() {
            None => self.terminal = true,
            Some((head, rest)) => self
                .children
                .entry((*head).to_string())
                .or_default()
                .insert(rest),
        }
    }

    /// Emit the templates rooted at this node. `prefix` is the path built so far
    /// (e.g. `/custom`); `depth` is this node's depth (root is 0).
    fn emit(&self, prefix: &str, depth: usize, out: &mut Vec<String>) {
        if self.terminal {
            out.push(if prefix.is_empty() {
                "/".to_string()
            } else {
                prefix.to_string()
            });
        }

        if self.should_collapse(depth) {
            // The collapsed children are leaf endpoints: emit one parameterized
            // template and do not recurse.
            out.push(format!("{prefix}/{{{INFERRED_PARAM}}}"));
            return;
        }

        for (segment, child) in &self.children {
            child.emit(&format!("{prefix}/{segment}"), depth + 1, out);
        }
    }

    /// Whether this node's children should collapse into a single `{id}`: the
    /// node is below the top level, has at least two children, and every child is
    /// a leaf (a pure endpoint with no further structure).
    fn should_collapse(&self, depth: usize) -> bool {
        depth >= 1
            && self.children.len() >= 2
            && self
                .children
                .values()
                .all(|child| child.children.is_empty())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn raws(set: &TemplateSet) -> Vec<String> {
        let mut raws: Vec<String> = set.templates().iter().map(|t| t.raw().to_string()).collect();
        raws.sort();
        raws
    }

    fn paths(items: &[&str]) -> Vec<String> {
        items.iter().map(|s| (*s).to_string()).collect()
    }

    #[test]
    fn sibling_data_segments_collapse_to_a_parameter() {
        let set = TemplateSet::infer(&paths(&["/custom/cat", "/custom/dog"]), &[]);
        assert_eq!(raws(&set), vec!["/custom/{id}".to_string()]);
        // And a concrete path now assigns to it, capturing the value.
        let (template, matched) = set.assign("/custom/cat").expect("assign");
        assert_eq!(template.raw(), "/custom/{id}");
        assert_eq!(matched.params, vec![("id".to_string(), "cat".to_string())]);
    }

    #[test]
    fn top_level_resources_stay_literal() {
        let set = TemplateSet::infer(&paths(&["/healthz", "/metrics"]), &[]);
        assert_eq!(raws(&set), vec!["/healthz".to_string(), "/metrics".to_string()]);
    }

    #[test]
    fn enumerate_and_item_paths_coexist() {
        // /custom is an endpoint; /custom/{id} is the item template.
        let set = TemplateSet::infer(&paths(&["/custom", "/custom/cat", "/custom/dog"]), &[]);
        assert_eq!(
            raws(&set),
            vec!["/custom".to_string(), "/custom/{id}".to_string()]
        );
    }

    #[test]
    fn nested_resource_items_collapse() {
        let set = TemplateSet::infer(&paths(&["/users/alice", "/users/bob"]), &[]);
        assert_eq!(raws(&set), vec!["/users/{id}".to_string()]);
    }

    #[test]
    fn existing_template_is_preferred_over_inference() {
        let existing = vec![PathTemplate::parse("/custom/{word}")];
        let set = TemplateSet::infer(&paths(&["/custom/cat", "/custom/dog"]), &existing);
        // No new /custom/{id}; the spec template handles the concrete paths.
        assert_eq!(raws(&set), vec!["/custom/{word}".to_string()]);
        let (template, matched) = set.assign("/custom/cat").expect("assign");
        assert_eq!(template.raw(), "/custom/{word}");
        assert_eq!(matched.params, vec![("word".to_string(), "cat".to_string())]);
    }

    #[test]
    fn a_single_observation_stays_literal() {
        // One value is not enough evidence to parameterize.
        let set = TemplateSet::infer(&paths(&["/custom/cat"]), &[]);
        assert_eq!(raws(&set), vec!["/custom/cat".to_string()]);
    }

    #[test]
    fn healthz_alone_is_literal() {
        let set = TemplateSet::infer(&paths(&["/healthz"]), &[]);
        assert_eq!(raws(&set), vec!["/healthz".to_string()]);
        assert!(set.assign("/healthz").is_some());
    }

    #[test]
    fn mixed_resources_infer_independently() {
        let set = TemplateSet::infer(
            &paths(&[
                "/healthz",
                "/custom",
                "/custom/cat",
                "/custom/dog",
                "/custom/mouse",
            ]),
            &[],
        );
        assert_eq!(
            raws(&set),
            vec![
                "/custom".to_string(),
                "/custom/{id}".to_string(),
                "/healthz".to_string(),
            ]
        );
    }
}
