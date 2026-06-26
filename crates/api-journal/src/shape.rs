// Copyright (c) Microsoft Corporation.

//! The shapes-only body model (AJ-A2).
//!
//! A captured request or response body is reduced to a [`BodyShape`]: a JSON *schema
//! skeleton* describing structure (field names, JSON types, nesting, array element shape)
//! with **no literal scalar values**. This is the default capture mode (see
//! `DESIGN-NOTES.md`, D-AJ-2): it lets the offline `cartographer` tool infer OpenAPI
//! schemas without exporting user data.
//!
//! Shapes are *mergeable*: [`BodyShape::merge`] folds many observed bodies into one shape
//! that describes them all (numeric widening, optional object fields, and `anyOf`-style
//! [`Union`](BodyShape::Union) alternatives for genuinely divergent samples). Merge is
//! commutative and idempotent, so the order in which journal records are processed does not
//! affect the synthesized schema.

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

/// The structural shape of a JSON body, with no literal scalar values retained.
///
/// Changing the serialized form of any variant is a breaking change to the on-disk journal
/// format shared with `cartographer`.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum BodyShape {
    /// No body was present (zero bytes). The default shape.
    #[default]
    Empty,
    /// The body was not captured structurally (capture mode `None`, or an as-yet-unobserved
    /// element). Yields to any concrete shape on merge.
    Unknown,
    /// A non-JSON body (or unparseable JSON) whose structure we do not model.
    Opaque,
    /// JSON `null`.
    Null,
    /// JSON `true`/`false`.
    Bool,
    /// A JSON number with no fractional part observed (a hint; integers are a subset of
    /// numbers and widen to [`Number`](BodyShape::Number) on merge).
    Integer,
    /// A JSON number with a fractional part.
    Number,
    /// A JSON string.
    String,
    /// A JSON array; the boxed shape is the merge of every observed element shape.
    Array(Box<BodyShape>),
    /// A JSON object; keys map to a [`Field`] (shape + whether it was always present).
    Object(BTreeMap<String, Field>),
    /// Divergent alternatives (e.g. a value seen as a string in one sample and a number in
    /// another). Models JSON Schema `anyOf`. Members are kept distinct by family.
    Union(Vec<BodyShape>),
}

/// One object field: its shape and whether it was present in every merged sample.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Field {
    /// The shape of the field's value.
    pub shape: BodyShape,
    /// True if the field appeared in all samples merged into this shape so far; false once
    /// any sample omitted it (i.e. the field is optional).
    pub required: bool,
}

impl BodyShape {
    /// Derive a shape from raw body bytes and an optional content type.
    ///
    /// JSON bodies (content type contains `json`, or no content type is given) are parsed
    /// and reduced to a structural shape. Empty bodies become [`Empty`](BodyShape::Empty);
    /// non-JSON or unparseable bodies become [`Opaque`](BodyShape::Opaque).
    pub fn derive(bytes: &[u8], content_type: Option<&str>) -> BodyShape {
        if bytes.is_empty() {
            return BodyShape::Empty;
        }
        let looks_jsonish = match content_type {
            Some(ct) => ct.to_ascii_lowercase().contains("json"),
            None => true,
        };
        if !looks_jsonish {
            return BodyShape::Opaque;
        }
        match serde_json::from_slice::<serde_json::Value>(bytes) {
            Ok(value) => shape_of(&value),
            Err(_) => BodyShape::Opaque,
        }
    }

    /// Fold two shapes into one that describes both.
    ///
    /// Commutative and idempotent. Numeric kinds widen (`Integer` + `Number` → `Number`);
    /// objects union their keys (a key absent from either side becomes optional); arrays
    /// merge element-wise; divergent kinds become a [`Union`](BodyShape::Union).
    #[must_use]
    pub fn merge(self, other: BodyShape) -> BodyShape {
        use BodyShape::{Array, Empty, Integer, Number, Object, Union, Unknown};
        match (self, other) {
            (a, b) if a == b => a,
            // Unknown / Empty yield to a concrete shape.
            (Unknown, b) | (b, Unknown) => b,
            (Empty, b) | (b, Empty) => b,
            // Numeric widening.
            (Integer, Number) | (Number, Integer) => Number,
            // Structural merges.
            (Array(a), Array(b)) => Array(Box::new((*a).merge(*b))),
            (Object(a), Object(b)) => Object(merge_objects(a, b)),
            // Union folding.
            (Union(xs), Union(ys)) => {
                let mut acc = xs;
                for y in ys {
                    acc = union_push(acc, y);
                }
                normalize_union(acc)
            }
            (Union(xs), y) => normalize_union(union_push(xs, y)),
            (x, Union(ys)) => normalize_union(union_push(ys, x)),
            // Two incompatible concrete shapes become a union.
            (x, y) => normalize_union(vec![x, y]),
        }
    }
}

/// Reduce a parsed JSON value to its structural shape.
fn shape_of(value: &serde_json::Value) -> BodyShape {
    use serde_json::Value;
    match value {
        Value::Null => BodyShape::Null,
        Value::Bool(_) => BodyShape::Bool,
        Value::Number(n) => {
            if n.is_i64() || n.is_u64() {
                BodyShape::Integer
            } else {
                BodyShape::Number
            }
        }
        Value::String(_) => BodyShape::String,
        Value::Array(items) => {
            let mut elem = BodyShape::Unknown;
            for item in items {
                elem = elem.merge(shape_of(item));
            }
            BodyShape::Array(Box::new(elem))
        }
        Value::Object(map) => {
            let mut fields = BTreeMap::new();
            for (key, val) in map {
                fields.insert(
                    key.clone(),
                    Field {
                        shape: shape_of(val),
                        required: true,
                    },
                );
            }
            BodyShape::Object(fields)
        }
    }
}

/// Merge two object field maps. A key present on only one side becomes optional; a shared
/// key merges its shapes and stays required only if required on both sides.
fn merge_objects(
    mut a: BTreeMap<String, Field>,
    b: BTreeMap<String, Field>,
) -> BTreeMap<String, Field> {
    // Any key in `a` that `b` lacks is now optional.
    for (key, field) in a.iter_mut() {
        if !b.contains_key(key) {
            field.required = false;
        }
    }
    for (key, fb) in b {
        match a.remove(&key) {
            Some(fa) => {
                a.insert(
                    key,
                    Field {
                        shape: fa.shape.merge(fb.shape),
                        required: fa.required && fb.required,
                    },
                );
            }
            // Key only in `b`: present there but absent from `a`'s samples, so optional.
            None => {
                a.insert(
                    key,
                    Field {
                        shape: fb.shape,
                        required: false,
                    },
                );
            }
        }
    }
    a
}

/// The merge-compatibility family of a shape. Two shapes in the same family combine into a
/// single shape; shapes in different families coexist as distinct union members.
fn family(shape: &BodyShape) -> u8 {
    match shape {
        BodyShape::Null => 0,
        BodyShape::Bool => 1,
        BodyShape::Integer | BodyShape::Number => 2,
        BodyShape::String => 3,
        BodyShape::Array(_) => 4,
        BodyShape::Object(_) => 5,
        BodyShape::Empty => 6,
        BodyShape::Opaque => 7,
        BodyShape::Unknown => 8,
        BodyShape::Union(_) => 9,
    }
}

/// Fold `shape` into a union member set, merging it with an existing same-family member if
/// one exists, otherwise appending it. Invariant: at most one member per family.
fn union_push(mut members: Vec<BodyShape>, shape: BodyShape) -> Vec<BodyShape> {
    let fam = family(&shape);
    if let Some(slot) = members.iter_mut().find(|m| family(m) == fam) {
        let existing = std::mem::replace(slot, BodyShape::Unknown);
        *slot = existing.merge(shape);
    } else {
        members.push(shape);
    }
    members
}

/// Collapse a single-member set to that member, otherwise sort members by family for a
/// deterministic, reproducible union ordering.
fn normalize_union(mut members: Vec<BodyShape>) -> BodyShape {
    if members.len() == 1 {
        return members.pop().expect("len checked");
    }
    members.sort_by_key(family);
    BodyShape::Union(members)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn obj(fields: &[(&str, BodyShape, bool)]) -> BodyShape {
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
    fn empty_bytes_is_empty() {
        assert_eq!(BodyShape::derive(b"", Some("application/json")), BodyShape::Empty);
    }

    #[test]
    fn json_null_bool_string() {
        assert_eq!(BodyShape::derive(b"null", None), BodyShape::Null);
        assert_eq!(BodyShape::derive(b"true", None), BodyShape::Bool);
        assert_eq!(BodyShape::derive(b"\"hi\"", None), BodyShape::String);
    }

    #[test]
    fn integer_vs_number() {
        assert_eq!(BodyShape::derive(b"42", None), BodyShape::Integer);
        assert_eq!(BodyShape::derive(b"-7", None), BodyShape::Integer);
        assert_eq!(BodyShape::derive(b"3.14", None), BodyShape::Number);
    }

    #[test]
    fn homogeneous_array() {
        assert_eq!(
            BodyShape::derive(b"[1,2,3]", None),
            BodyShape::Array(Box::new(BodyShape::Integer))
        );
    }

    #[test]
    fn mixed_array_becomes_union_element() {
        let shape = BodyShape::derive(b"[1,\"a\"]", None);
        assert_eq!(
            shape,
            BodyShape::Array(Box::new(BodyShape::Union(vec![
                BodyShape::Integer,
                BodyShape::String,
            ])))
        );
    }

    #[test]
    fn object_fields_required_on_single_sample() {
        let shape = BodyShape::derive(br#"{"a":1,"b":"x"}"#, Some("application/json"));
        assert_eq!(
            shape,
            obj(&[
                ("a", BodyShape::Integer, true),
                ("b", BodyShape::String, true),
            ])
        );
    }

    #[test]
    fn nested_object_and_array() {
        let shape = BodyShape::derive(br#"{"a":{"b":[true]}}"#, None);
        assert_eq!(
            shape,
            obj(&[(
                "a",
                obj(&[("b", BodyShape::Array(Box::new(BodyShape::Bool)), true)]),
                true,
            )])
        );
    }

    #[test]
    fn non_json_content_type_is_opaque() {
        assert_eq!(
            BodyShape::derive(b"<html></html>", Some("text/html")),
            BodyShape::Opaque
        );
    }

    #[test]
    fn malformed_json_is_opaque() {
        assert_eq!(
            BodyShape::derive(b"{not json", Some("application/json")),
            BodyShape::Opaque
        );
    }

    #[test]
    fn merge_is_idempotent() {
        let shape = obj(&[("a", BodyShape::Integer, true)]);
        assert_eq!(shape.clone().merge(shape.clone()), shape);
    }

    #[test]
    fn merge_widens_integer_to_number() {
        assert_eq!(BodyShape::Integer.merge(BodyShape::Number), BodyShape::Number);
        assert_eq!(BodyShape::Number.merge(BodyShape::Integer), BodyShape::Number);
    }

    #[test]
    fn merge_makes_missing_field_optional() {
        let with = obj(&[("a", BodyShape::Integer, true), ("b", BodyShape::String, true)]);
        let without = obj(&[("a", BodyShape::Integer, true)]);
        let merged = with.merge(without);
        assert_eq!(
            merged,
            obj(&[
                ("a", BodyShape::Integer, true),
                ("b", BodyShape::String, false),
            ])
        );
    }

    #[test]
    fn merge_divergent_scalars_is_union() {
        assert_eq!(
            BodyShape::String.merge(BodyShape::Integer),
            BodyShape::Union(vec![BodyShape::Integer, BodyShape::String])
        );
    }

    #[test]
    fn merge_null_with_string_is_nullable_union() {
        assert_eq!(
            BodyShape::Null.merge(BodyShape::String),
            BodyShape::Union(vec![BodyShape::Null, BodyShape::String])
        );
    }

    #[test]
    fn merge_empty_array_then_typed_array() {
        let empty = BodyShape::derive(b"[]", None);
        assert_eq!(empty, BodyShape::Array(Box::new(BodyShape::Unknown)));
        let typed = BodyShape::derive(b"[1]", None);
        assert_eq!(
            empty.merge(typed),
            BodyShape::Array(Box::new(BodyShape::Integer))
        );
    }

    #[test]
    fn merge_unknown_yields_to_concrete() {
        assert_eq!(BodyShape::Unknown.merge(BodyShape::Bool), BodyShape::Bool);
        assert_eq!(BodyShape::Bool.merge(BodyShape::Unknown), BodyShape::Bool);
    }

    #[test]
    fn merge_is_commutative_for_objects() {
        let a = obj(&[("x", BodyShape::Integer, true)]);
        let b = obj(&[("y", BodyShape::String, true)]);
        assert_eq!(a.clone().merge(b.clone()), b.merge(a));
    }

    #[test]
    fn union_keeps_one_member_per_family() {
        // Three integers and a string across nested merges collapse to {Integer, String}.
        let shape = BodyShape::derive(b"[1,2,\"a\",3]", None);
        assert_eq!(
            shape,
            BodyShape::Array(Box::new(BodyShape::Union(vec![
                BodyShape::Integer,
                BodyShape::String,
            ])))
        );
    }
}
