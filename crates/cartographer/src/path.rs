// Copyright (c) Microsoft Corporation.

//! Matching observed concrete paths against OpenAPI path templates (AJ-D1).
//!
//! An OpenAPI path key like `/custom/{word}` is a template whose `{…}` segments
//! are parameters. Validation matches an observed concrete path (e.g.
//! `/custom/cat`) against the spec's templates to find the operation it
//! exercised; synthesis (AJ-E) reuses the same matching to fold concrete paths
//! onto known templates. When several templates match, the **most specific** one
//! — the fewest parameters, ties broken by template text — wins, so a literal
//! `/custom/special` is preferred over `/custom/{word}`.

/// One segment of a path template: a literal, or a `{name}` parameter.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Segment {
    /// A fixed path segment that must match exactly.
    Literal(String),
    /// A `{name}` parameter that matches any single non-empty segment.
    Param(String),
}

/// A parsed OpenAPI path template.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PathTemplate {
    raw: String,
    segments: Vec<Segment>,
}

/// The result of matching a concrete path against a template: the captured
/// parameter values, in template order.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PathMatch {
    /// `(parameter name, concrete value)` pairs.
    pub params: Vec<(String, String)>,
}

impl PathTemplate {
    /// Parse a path template (e.g. `/custom/{word}`).
    #[must_use]
    pub fn parse(template: &str) -> PathTemplate {
        let segments = template
            .split('/')
            .map(|segment| {
                if segment.len() >= 2 && segment.starts_with('{') && segment.ends_with('}') {
                    Segment::Param(segment[1..segment.len() - 1].to_string())
                } else {
                    Segment::Literal(segment.to_string())
                }
            })
            .collect();
        PathTemplate {
            raw: template.to_string(),
            segments,
        }
    }

    /// The original template text.
    #[must_use]
    pub fn raw(&self) -> &str {
        &self.raw
    }

    /// The number of `{…}` parameters in the template.
    #[must_use]
    pub fn param_count(&self) -> usize {
        self.segments
            .iter()
            .filter(|s| matches!(s, Segment::Param(_)))
            .count()
    }

    /// The parameter names, in template order.
    #[must_use]
    pub fn param_names(&self) -> Vec<&str> {
        self.segments
            .iter()
            .filter_map(|segment| match segment {
                Segment::Param(name) => Some(name.as_str()),
                Segment::Literal(_) => None,
            })
            .collect()
    }

    /// Match a concrete path, capturing parameter values, or `None` if it does
    /// not match.
    #[must_use]
    pub fn matches(&self, concrete: &str) -> Option<PathMatch> {
        let concrete_segments: Vec<&str> = concrete.split('/').collect();
        if concrete_segments.len() != self.segments.len() {
            return None;
        }
        let mut params = Vec::new();
        for (template_segment, concrete_segment) in self.segments.iter().zip(&concrete_segments) {
            match template_segment {
                Segment::Literal(literal) => {
                    if literal != concrete_segment {
                        return None;
                    }
                }
                Segment::Param(name) => {
                    // A parameter matches any single non-empty segment.
                    if concrete_segment.is_empty() {
                        return None;
                    }
                    params.push((name.clone(), (*concrete_segment).to_string()));
                }
            }
        }
        Some(PathMatch { params })
    }
}

/// Find the most specific template that matches `concrete`: fewest parameters
/// first, ties broken by template text for determinism.
#[must_use]
pub fn best_match<'a>(
    templates: &'a [PathTemplate],
    concrete: &str,
) -> Option<(&'a PathTemplate, PathMatch)> {
    let mut best: Option<(&PathTemplate, PathMatch)> = None;
    for template in templates {
        let Some(matched) = template.matches(concrete) else {
            continue;
        };
        let is_better = match &best {
            None => true,
            Some((current, _)) => {
                (template.param_count(), template.raw())
                    < (current.param_count(), current.raw())
            }
        };
        if is_better {
            best = Some((template, matched));
        }
    }
    best
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn template_matches_a_concrete_path_capturing_params() {
        let template = PathTemplate::parse("/custom/{word}");
        let matched = template.matches("/custom/cat").expect("match");
        assert_eq!(matched.params, vec![("word".to_string(), "cat".to_string())]);
    }

    #[test]
    fn template_rejects_wrong_segment_count_or_literal() {
        let template = PathTemplate::parse("/custom/{word}");
        assert!(template.matches("/custom").is_none());
        assert!(template.matches("/custom/cat/extra").is_none());
        assert!(template.matches("/other/cat").is_none());
    }

    #[test]
    fn literal_template_matches_only_itself() {
        let template = PathTemplate::parse("/healthz");
        assert!(template.matches("/healthz").is_some());
        assert!(template.matches("/healthy").is_none());
        assert_eq!(template.param_count(), 0);
    }

    #[test]
    fn multiple_parameters_are_captured_in_order() {
        let template = PathTemplate::parse("/u/{user}/d/{doc}");
        let matched = template.matches("/u/alice/d/42").expect("match");
        assert_eq!(
            matched.params,
            vec![
                ("user".to_string(), "alice".to_string()),
                ("doc".to_string(), "42".to_string()),
            ]
        );
    }

    #[test]
    fn parameter_does_not_match_empty_segment() {
        let template = PathTemplate::parse("/custom/{word}");
        assert!(template.matches("/custom/").is_none());
    }

    #[test]
    fn trailing_slash_is_a_distinct_path() {
        let template = PathTemplate::parse("/custom");
        assert!(template.matches("/custom/").is_none());
    }

    #[test]
    fn best_match_prefers_the_literal_over_the_parameterized() {
        let templates = vec![
            PathTemplate::parse("/custom/{word}"),
            PathTemplate::parse("/custom/special"),
        ];
        // The literal wins for the literal path.
        let (template, _) = best_match(&templates, "/custom/special").expect("match");
        assert_eq!(template.raw(), "/custom/special");
        // The parameterized template handles other values.
        let (template, matched) = best_match(&templates, "/custom/cat").expect("match");
        assert_eq!(template.raw(), "/custom/{word}");
        assert_eq!(matched.params, vec![("word".to_string(), "cat".to_string())]);
    }

    #[test]
    fn best_match_is_deterministic_for_equal_specificity() {
        // Two equally specific templates that both match: the lexically smaller
        // template text wins.
        let templates = vec![
            PathTemplate::parse("/{b}/x"),
            PathTemplate::parse("/{a}/x"),
        ];
        let (template, _) = best_match(&templates, "/zzz/x").expect("match");
        assert_eq!(template.raw(), "/{a}/x");
    }

    #[test]
    fn best_match_returns_none_when_nothing_matches() {
        let templates = vec![PathTemplate::parse("/healthz")];
        assert!(best_match(&templates, "/custom/cat").is_none());
    }
}
