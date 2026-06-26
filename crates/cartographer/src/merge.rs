// Copyright (c) Microsoft Corporation.

//! Merging synthesized operations into an existing spec (AJ-E3).
//!
//! [`merge`] folds a synthesized [`Document`] into a base one. The default is
//! **additive and prose-preserving**: human-authored operation ids, summaries,
//! descriptions, tags, and existing schemas are kept untouched; only newly
//! observed surface is added — new paths, new operations, new response statuses,
//! and new parameters. With `overwrite` set, observed structure (parameters,
//! request body, responses) replaces the base's for operations that were
//! re-observed, while still preserving the operation-level prose.
//!
//! Deep schema *value* merging (widening a human schema to admit a newly observed
//! field) is intentionally out of scope for now; the default merge widens the API
//! surface by addition, not by rewriting existing schemas.

use crate::model::{Document, Operation, Parameter, PathItem};

/// Merge `synth` into `base`, returning the combined document.
///
/// Default (`overwrite == false`): additive and prose-preserving. With
/// `overwrite == true`: re-observed operations have their structure replaced
/// (prose preserved).
#[must_use]
pub fn merge(mut base: Document, synth: &Document, overwrite: bool) -> Document {
    for (path, synth_item) in &synth.paths {
        match base.paths.get_mut(path) {
            None => {
                base.paths.insert(path.clone(), synth_item.clone());
            }
            Some(base_item) => merge_path_item(base_item, synth_item, overwrite),
        }
    }
    base
}

fn merge_path_item(base: &mut PathItem, synth: &PathItem, overwrite: bool) {
    add_missing_params(&mut base.parameters, &synth.parameters);
    for (method, synth_op) in synth.operations() {
        let Some(slot) = base.operation_slot_mut(method) else {
            continue;
        };
        match slot {
            None => *slot = Some(synth_op.clone()),
            Some(base_op) => {
                if overwrite {
                    overwrite_operation(base_op, synth_op);
                } else {
                    extend_operation(base_op, synth_op);
                }
            }
        }
    }
}

/// Additive merge: keep the base operation's prose and existing schemas; add only
/// newly observed parameters, response statuses, and a request body if missing.
fn extend_operation(base: &mut Operation, synth: &Operation) {
    add_missing_params(&mut base.parameters, &synth.parameters);
    for (status, response) in &synth.responses {
        base.responses
            .entry(status.clone())
            .or_insert_with(|| response.clone());
    }
    if base.request_body.is_none() {
        base.request_body = synth.request_body.clone();
    }
}

/// Overwrite merge: keep the base operation's prose (id/summary/description/tags)
/// but replace its observed structure (parameters, request body, responses).
fn overwrite_operation(base: &mut Operation, synth: &Operation) {
    base.parameters = synth.parameters.clone();
    base.request_body = synth.request_body.clone();
    base.responses = synth.responses.clone();
}

/// Append parameters from `incoming` that the base lacks, keyed by `(name, in)`.
fn add_missing_params(base: &mut Vec<Parameter>, incoming: &[Parameter]) {
    for parameter in incoming {
        let present = base
            .iter()
            .any(|existing| existing.name == parameter.name && existing.location == parameter.location);
        if !present {
            base.push(parameter.clone());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{
        Content, Document, MediaType, Operation, Parameter, ParameterIn, PathItem, Response,
        Responses, Schema, SchemaType, SimpleType,
    };

    fn object(fields: &[(&str, SimpleType)]) -> Schema {
        let mut schema = Schema {
            schema_type: Some(SchemaType::Single(SimpleType::Object)),
            ..Schema::default()
        };
        for (name, ty) in fields {
            schema
                .properties
                .insert((*name).to_string(), Schema::of_type(*ty));
        }
        schema
    }

    fn json_response(description: &str, schema: Schema) -> Response {
        let mut content = Content::new();
        content.insert("application/json".to_string(), MediaType { schema: Some(schema) });
        Response {
            description: description.to_string(),
            content,
            ..Response::default()
        }
    }

    fn responses(pairs: Vec<(&str, Response)>) -> Responses {
        let mut map = Responses::new();
        for (status, response) in pairs {
            map.insert(status.to_string(), response);
        }
        map
    }

    /// A base spec with a human-authored `GET /custom/{word}`.
    fn base() -> Document {
        let operation = Operation {
            operation_id: Some("getCustomWord".to_string()),
            summary: Some("Check a custom word".to_string()),
            responses: responses(vec![(
                "200",
                json_response("the membership", object(&[("word", SimpleType::String)])),
            )]),
            ..Operation::default()
        };
        let item = PathItem {
            get: Some(operation),
            ..PathItem::default()
        };
        let mut document = Document::new("merriam", "1.0.0");
        document.paths.insert("/custom/{word}".to_string(), item);
        document
    }

    /// A synthesized spec that re-observes the GET (with a different schema) plus a
    /// new 404, and adds a brand-new POST.
    fn synthesized() -> Document {
        let get = Operation {
            responses: responses(vec![
                (
                    "200",
                    json_response("OK", object(&[("word", SimpleType::String), ("exists", SimpleType::Boolean)])),
                ),
                ("404", json_response("Not Found", object(&[("error", SimpleType::String)]))),
            ]),
            ..Operation::default()
        };
        let post = Operation {
            responses: responses(vec![(
                "200",
                json_response("OK", object(&[("added", SimpleType::Boolean)])),
            )]),
            ..Operation::default()
        };
        let item = PathItem {
            get: Some(get),
            post: Some(post),
            ..PathItem::default()
        };
        let mut document = Document::new("synthesized", "0.1.0");
        document.paths.insert("/custom/{word}".to_string(), item);
        document
    }

    #[test]
    fn merging_into_an_empty_base_is_fresh_synthesis() {
        let empty = Document::new("base", "1.0.0");
        let merged = merge(empty, &synthesized(), false);
        assert!(merged.paths.contains_key("/custom/{word}"));
        assert!(merged.paths["/custom/{word}"].operation("POST").is_some());
    }

    #[test]
    fn default_merge_preserves_prose_and_existing_schema_and_adds_new_surface() {
        let merged = merge(base(), &synthesized(), false);
        let get = merged.paths["/custom/{word}"].operation("GET").unwrap();
        // Human prose preserved.
        assert_eq!(get.operation_id.as_deref(), Some("getCustomWord"));
        assert_eq!(get.summary.as_deref(), Some("Check a custom word"));
        // The base 200 schema is untouched (no `exists`).
        let schema = get.responses["200"].content["application/json"]
            .schema
            .as_ref()
            .unwrap();
        assert!(schema.properties.contains_key("word"));
        assert!(!schema.properties.contains_key("exists"));
        // The newly observed 404 was added.
        assert!(get.responses.contains_key("404"));
        // The brand-new POST operation was added.
        assert!(merged.paths["/custom/{word}"].operation("POST").is_some());
    }

    #[test]
    fn overwrite_merge_replaces_structure_but_keeps_prose() {
        let merged = merge(base(), &synthesized(), true);
        let get = merged.paths["/custom/{word}"].operation("GET").unwrap();
        // Prose preserved.
        assert_eq!(get.operation_id.as_deref(), Some("getCustomWord"));
        // The 200 schema is now the observed one (includes `exists`).
        let schema = get.responses["200"].content["application/json"]
            .schema
            .as_ref()
            .unwrap();
        assert!(schema.properties.contains_key("exists"));
        assert!(get.responses.contains_key("404"));
    }

    #[test]
    fn a_new_path_is_added_wholesale() {
        let mut synth = Document::new("s", "1");
        synth.paths.insert(
            "/healthz".to_string(),
            PathItem {
                get: Some(Operation {
                    responses: responses(vec![("200", json_response("OK", object(&[("status", SimpleType::String)])))]),
                    ..Operation::default()
                }),
                ..PathItem::default()
            },
        );
        let merged = merge(base(), &synth, false);
        assert!(merged.paths.contains_key("/custom/{word}")); // base kept
        assert!(merged.paths.contains_key("/healthz")); // new added
    }

    #[test]
    fn missing_parameters_are_added_but_existing_kept() {
        let mut base = base();
        base.paths.get_mut("/custom/{word}").unwrap().parameters.push(Parameter {
            name: "word".to_string(),
            location: ParameterIn::Path,
            required: true,
            description: Some("the word".to_string()),
            schema: Some(Schema::of_type(SimpleType::String)),
        });
        let mut synth = synthesized();
        let item = synth.paths.get_mut("/custom/{word}").unwrap();
        item.parameters.push(Parameter {
            name: "word".to_string(), // same param — must not duplicate
            location: ParameterIn::Path,
            required: true,
            description: None,
            schema: Some(Schema::of_type(SimpleType::String)),
        });
        item.parameters.push(Parameter {
            name: "trace".to_string(), // new param — added
            location: ParameterIn::Header,
            required: false,
            description: None,
            schema: Some(Schema::of_type(SimpleType::String)),
        });
        let merged = merge(base, &synth, false);
        let params = &merged.paths["/custom/{word}"].parameters;
        // word kept once (with its human description), trace added.
        assert_eq!(params.iter().filter(|p| p.name == "word").count(), 1);
        let word = params.iter().find(|p| p.name == "word").unwrap();
        assert_eq!(word.description.as_deref(), Some("the word"));
        assert!(params.iter().any(|p| p.name == "trace"));
    }
}
