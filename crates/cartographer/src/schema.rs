// Copyright (c) Microsoft Corporation.

//! Rendering [`api_journal`] body shapes to OpenAPI schemas (AJ-C4).
//!
//! A captured body is journaled as a [`BodyShape`] (a shapes-only JSON skeleton).
//! Synthesis turns that into an OpenAPI [`Schema`] in the JSON-Schema-2020-12
//! dialect: scalar kinds become `type` tokens, objects become `properties` +
//! `required`, arrays become `items`, and unions become either a nullable
//! `type` array (the common `null`-plus-one case) or an `anyOf`.

use api_journal::BodyShape;

use crate::model::{Schema, SchemaType, SimpleType};

/// Render a body shape to an OpenAPI [`Schema`], or `None` when the shape carries
/// no schema worth emitting ([`BodyShape::Empty`] — no body — or
/// [`BodyShape::Unknown`] — not captured).
#[must_use]
pub fn render_schema(shape: &BodyShape) -> Option<Schema> {
    match shape {
        BodyShape::Empty | BodyShape::Unknown => None,
        BodyShape::Opaque => Some(Schema {
            schema_type: Some(SchemaType::Single(SimpleType::String)),
            description: Some("opaque (non-JSON) body".to_string()),
            ..Schema::default()
        }),
        BodyShape::Null => Some(Schema::of_type(SimpleType::Null)),
        BodyShape::Bool => Some(Schema::of_type(SimpleType::Boolean)),
        BodyShape::Integer => Some(Schema::of_type(SimpleType::Integer)),
        BodyShape::Number => Some(Schema::of_type(SimpleType::Number)),
        BodyShape::String => Some(Schema::of_type(SimpleType::String)),
        BodyShape::Array(element) => Some(Schema {
            schema_type: Some(SchemaType::Single(SimpleType::Array)),
            items: render_schema(element).map(Box::new),
            ..Schema::default()
        }),
        BodyShape::Object(fields) => {
            let mut schema = Schema {
                schema_type: Some(SchemaType::Single(SimpleType::Object)),
                ..Schema::default()
            };
            for (name, field) in fields {
                schema
                    .properties
                    .insert(name.clone(), render_or_empty(&field.shape));
                if field.required {
                    schema.required.push(name.clone());
                }
            }
            Some(schema)
        }
        BodyShape::Union(members) => Some(render_union(members)),
    }
}

/// Render a shape, substituting a permissive empty schema (`{}`) when it has no
/// schema of its own — used for object properties and union members, where the
/// slot must exist even if its value shape was never captured.
fn render_or_empty(shape: &BodyShape) -> Schema {
    render_schema(shape).unwrap_or_default()
}

/// Render a union: a single non-null alternative plus `null` becomes a nullable
/// schema (a `type` array); anything richer becomes an `anyOf`.
fn render_union(members: &[BodyShape]) -> Schema {
    let has_null = members.iter().any(|m| matches!(m, BodyShape::Null));
    let non_null: Vec<&BodyShape> = members
        .iter()
        .filter(|m| !matches!(m, BodyShape::Null))
        .collect();

    if non_null.len() == 1 {
        let mut schema = render_or_empty(non_null[0]);
        if has_null {
            make_nullable(&mut schema);
        }
        return schema;
    }

    let mut any_of: Vec<Schema> = non_null.iter().map(|m| render_or_empty(m)).collect();
    if has_null {
        any_of.push(Schema::of_type(SimpleType::Null));
    }
    Schema {
        any_of,
        ..Schema::default()
    }
}

/// Make a schema accept `null` in addition to its existing form: extend a `type`
/// token into a `["…", "null"]` array, or, when there is no plain type (a `$ref`
/// or `anyOf`), express nullability as `anyOf: [original, {type: null}]`.
fn make_nullable(schema: &mut Schema) {
    match &mut schema.schema_type {
        Some(SchemaType::Single(single)) => {
            let single = *single;
            schema.schema_type = Some(SchemaType::Multiple(vec![single, SimpleType::Null]));
        }
        Some(SchemaType::Multiple(types)) => {
            if !types.contains(&SimpleType::Null) {
                types.push(SimpleType::Null);
            }
        }
        None => {
            let original = std::mem::take(schema);
            schema.any_of = vec![original, Schema::of_type(SimpleType::Null)];
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use api_journal::Field;
    use std::collections::BTreeMap;

    fn object(fields: &[(&str, BodyShape, bool)]) -> BodyShape {
        let mut map = BTreeMap::new();
        for (name, shape, required) in fields {
            map.insert(
                (*name).to_string(),
                Field {
                    shape: shape.clone(),
                    required: *required,
                },
            );
        }
        BodyShape::Object(map)
    }

    #[test]
    fn empty_and_unknown_have_no_schema() {
        assert_eq!(render_schema(&BodyShape::Empty), None);
        assert_eq!(render_schema(&BodyShape::Unknown), None);
    }

    #[test]
    fn scalars_map_to_type_tokens() {
        assert_eq!(
            render_schema(&BodyShape::Bool),
            Some(Schema::of_type(SimpleType::Boolean))
        );
        assert_eq!(
            render_schema(&BodyShape::Integer),
            Some(Schema::of_type(SimpleType::Integer))
        );
        assert_eq!(
            render_schema(&BodyShape::Number),
            Some(Schema::of_type(SimpleType::Number))
        );
        assert_eq!(
            render_schema(&BodyShape::String),
            Some(Schema::of_type(SimpleType::String))
        );
        assert_eq!(
            render_schema(&BodyShape::Null),
            Some(Schema::of_type(SimpleType::Null))
        );
    }

    #[test]
    fn opaque_renders_as_a_described_string() {
        let schema = render_schema(&BodyShape::Opaque).expect("schema");
        assert_eq!(schema.schema_type, Some(SchemaType::Single(SimpleType::String)));
        assert!(schema.description.is_some());
    }

    #[test]
    fn typed_array_renders_items() {
        let schema = render_schema(&BodyShape::Array(Box::new(BodyShape::Integer))).expect("schema");
        assert_eq!(schema.schema_type, Some(SchemaType::Single(SimpleType::Array)));
        assert_eq!(
            schema.items.as_deref(),
            Some(&Schema::of_type(SimpleType::Integer))
        );
    }

    #[test]
    fn untyped_array_has_no_items() {
        let schema = render_schema(&BodyShape::Array(Box::new(BodyShape::Unknown))).expect("schema");
        assert_eq!(schema.schema_type, Some(SchemaType::Single(SimpleType::Array)));
        assert_eq!(schema.items, None);
    }

    #[test]
    fn object_renders_properties_and_required() {
        let shape = object(&[
            ("word", BodyShape::String, true),
            ("count", BodyShape::Integer, false),
        ]);
        let schema = render_schema(&shape).expect("schema");
        assert_eq!(schema.schema_type, Some(SchemaType::Single(SimpleType::Object)));
        assert_eq!(
            schema.properties.get("word"),
            Some(&Schema::of_type(SimpleType::String))
        );
        assert_eq!(
            schema.properties.get("count"),
            Some(&Schema::of_type(SimpleType::Integer))
        );
        // Only the required field is listed; ordering is deterministic (sorted).
        assert_eq!(schema.required, vec!["word".to_string()]);
    }

    #[test]
    fn divergent_union_renders_any_of() {
        let shape = BodyShape::Union(vec![BodyShape::Integer, BodyShape::String]);
        let schema = render_schema(&shape).expect("schema");
        assert_eq!(schema.schema_type, None);
        assert_eq!(
            schema.any_of,
            vec![
                Schema::of_type(SimpleType::Integer),
                Schema::of_type(SimpleType::String),
            ]
        );
    }

    #[test]
    fn nullable_scalar_renders_a_type_array() {
        let shape = BodyShape::Union(vec![BodyShape::Null, BodyShape::String]);
        let schema = render_schema(&shape).expect("schema");
        assert_eq!(
            schema.schema_type,
            Some(SchemaType::Multiple(vec![SimpleType::String, SimpleType::Null]))
        );
        assert!(schema.any_of.is_empty());
    }

    #[test]
    fn nullable_object_keeps_properties_and_adds_null() {
        let inner = object(&[("word", BodyShape::String, true)]);
        let shape = BodyShape::Union(vec![BodyShape::Null, inner]);
        let schema = render_schema(&shape).expect("schema");
        assert_eq!(
            schema.schema_type,
            Some(SchemaType::Multiple(vec![SimpleType::Object, SimpleType::Null]))
        );
        assert!(schema.properties.contains_key("word"));
        assert_eq!(schema.required, vec!["word".to_string()]);
    }

    #[test]
    fn nested_object_and_array_render_recursively() {
        let shape = object(&[(
            "matches",
            BodyShape::Array(Box::new(BodyShape::String)),
            true,
        )]);
        let schema = render_schema(&shape).expect("schema");
        let matches = schema.properties.get("matches").expect("matches");
        assert_eq!(matches.schema_type, Some(SchemaType::Single(SimpleType::Array)));
        assert_eq!(
            matches.items.as_deref(),
            Some(&Schema::of_type(SimpleType::String))
        );
    }
}
