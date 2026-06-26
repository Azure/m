// Copyright (c) Microsoft Corporation.

//! The OpenAPI 3.1 document model (AJ-C2).
//!
//! A deliberately small subset of the OpenAPI Specification — enough to describe
//! the REST surfaces this project serves (paths, operations, parameters, request
//! bodies, responses) and the JSON-Schema-2020-12 schemas their bodies take. The
//! shape and its JSON/YAML mapping are owned here (Design Autonomy); `serde`
//! merely realizes them.
//!
//! 3.1 specifics honored: nullability is a **type array** (`["string", "null"]`),
//! not a `nullable` flag; composition uses `anyOf`; field renames match the
//! specification (`operationId`, `requestBody`, `$ref`). Unknown fields in an
//! input document are ignored, so a spec carrying constructs outside this subset
//! still loads (its extra fields are simply not modeled).

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

/// An ordered map of path templates to their [`PathItem`]. Sorted by key for
/// deterministic, reproducible output.
pub type Paths = BTreeMap<String, PathItem>;

/// An ordered map of response status keys (`"200"`, `"404"`, `"default"`, …) to
/// their [`Response`].
pub type Responses = BTreeMap<String, Response>;

/// A media-type map (`"application/json"` → [`MediaType`]).
pub type Content = BTreeMap<String, MediaType>;

/// The root OpenAPI document.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Document {
    /// The OpenAPI version string (e.g. `"3.1.0"`).
    pub openapi: String,
    /// API metadata.
    pub info: Info,
    /// Connectivity targets. Omitted when empty.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub servers: Vec<Server>,
    /// The available paths and operations. Omitted when empty.
    #[serde(default, skip_serializing_if = "Paths::is_empty")]
    pub paths: Paths,
    /// Reusable components. Omitted when absent.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub components: Option<Components>,
}

impl Document {
    /// A new OpenAPI 3.1.0 document with the given title and version and no paths.
    #[must_use]
    pub fn new(title: impl Into<String>, version: impl Into<String>) -> Self {
        Self {
            openapi: "3.1.0".to_string(),
            info: Info {
                title: title.into(),
                version: version.into(),
                description: None,
            },
            servers: Vec::new(),
            paths: Paths::new(),
            components: None,
        }
    }
}

impl Default for Document {
    fn default() -> Self {
        Self::new(String::new(), "0.0.0")
    }
}

/// API metadata (`info`). `title` and `version` are required by the spec.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Info {
    /// The API title.
    pub title: String,
    /// The API version (distinct from the OpenAPI version).
    pub version: String,
    /// An optional description.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
}

/// A server / connectivity target.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Server {
    /// The server URL.
    pub url: String,
    /// An optional description.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
}

/// The operations available on a single path template.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct PathItem {
    /// An optional summary applying to all operations on this path.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
    /// An optional description applying to all operations on this path.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    /// The `GET` operation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub get: Option<Operation>,
    /// The `PUT` operation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub put: Option<Operation>,
    /// The `POST` operation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub post: Option<Operation>,
    /// The `DELETE` operation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub delete: Option<Operation>,
    /// The `OPTIONS` operation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub options: Option<Operation>,
    /// The `HEAD` operation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub head: Option<Operation>,
    /// The `PATCH` operation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub patch: Option<Operation>,
    /// The `TRACE` operation.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trace: Option<Operation>,
    /// Parameters common to every operation on this path.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub parameters: Vec<Parameter>,
}

impl PathItem {
    /// The operation for an HTTP method (case-insensitive), if present.
    #[must_use]
    pub fn operation(&self, method: &str) -> Option<&Operation> {
        match method.to_ascii_uppercase().as_str() {
            "GET" => self.get.as_ref(),
            "PUT" => self.put.as_ref(),
            "POST" => self.post.as_ref(),
            "DELETE" => self.delete.as_ref(),
            "OPTIONS" => self.options.as_ref(),
            "HEAD" => self.head.as_ref(),
            "PATCH" => self.patch.as_ref(),
            "TRACE" => self.trace.as_ref(),
            _ => None,
        }
    }

    /// A mutable handle to the operation slot for an HTTP method (case-insensitive),
    /// or `None` if the method is not a modeled HTTP verb. Used by synthesis to
    /// insert or update an operation.
    pub fn operation_slot_mut(&mut self, method: &str) -> Option<&mut Option<Operation>> {
        Some(match method.to_ascii_uppercase().as_str() {
            "GET" => &mut self.get,
            "PUT" => &mut self.put,
            "POST" => &mut self.post,
            "DELETE" => &mut self.delete,
            "OPTIONS" => &mut self.options,
            "HEAD" => &mut self.head,
            "PATCH" => &mut self.patch,
            "TRACE" => &mut self.trace,
            _ => return None,
        })
    }

    /// Iterate the present operations as `(method, operation)` pairs in a fixed
    /// method order.
    pub fn operations(&self) -> impl Iterator<Item = (&'static str, &Operation)> {
        [
            ("GET", &self.get),
            ("PUT", &self.put),
            ("POST", &self.post),
            ("DELETE", &self.delete),
            ("OPTIONS", &self.options),
            ("HEAD", &self.head),
            ("PATCH", &self.patch),
            ("TRACE", &self.trace),
        ]
        .into_iter()
        .filter_map(|(method, slot)| slot.as_ref().map(|op| (method, op)))
    }
}

/// A single API operation.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Operation {
    /// A unique operation identifier.
    #[serde(default, rename = "operationId", skip_serializing_if = "Option::is_none")]
    pub operation_id: Option<String>,
    /// A short summary.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
    /// A longer description.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    /// Grouping tags.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub tags: Vec<String>,
    /// Operation-specific parameters.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub parameters: Vec<Parameter>,
    /// The request body, if any.
    #[serde(default, rename = "requestBody", skip_serializing_if = "Option::is_none")]
    pub request_body: Option<RequestBody>,
    /// The responses by status. Always present (the spec requires at least one).
    pub responses: Responses,
}

/// The location of a parameter.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum ParameterIn {
    /// A templated path segment.
    Path,
    /// A query-string parameter.
    Query,
    /// A request header.
    Header,
    /// A cookie.
    Cookie,
}

/// A single operation or path parameter.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Parameter {
    /// The parameter name.
    pub name: String,
    /// Where the parameter appears.
    #[serde(rename = "in")]
    pub location: ParameterIn,
    /// Whether the parameter is required (always `true` for path parameters).
    #[serde(default, skip_serializing_if = "is_false")]
    pub required: bool,
    /// An optional description.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    /// The parameter's value schema.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub schema: Option<Schema>,
}

/// A request body.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct RequestBody {
    /// An optional description.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    /// Whether the body is required.
    #[serde(default, skip_serializing_if = "is_false")]
    pub required: bool,
    /// The body content keyed by media type.
    pub content: Content,
}

/// A single media type's schema.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct MediaType {
    /// The schema of the content.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub schema: Option<Schema>,
    /// A literal example of the content, synthesized from a journal captured under
    /// `bodies: full`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub example: Option<serde_json::Value>,
}

/// A single response.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Response {
    /// A human description (required by the spec).
    pub description: String,
    /// Response headers by name.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub headers: BTreeMap<String, ResponseHeader>,
    /// The response content keyed by media type.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub content: Content,
}

/// A response header definition.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct ResponseHeader {
    /// An optional description.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    /// The header's value schema.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub schema: Option<Schema>,
}

/// The reusable components object. Only schemas are modeled today.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Components {
    /// Reusable named schemas.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub schemas: BTreeMap<String, Schema>,
}

/// A single JSON-Schema (2020-12 dialect) type token.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum SimpleType {
    /// `null`.
    Null,
    /// `boolean`.
    Boolean,
    /// `object`.
    Object,
    /// `array`.
    Array,
    /// `number`.
    Number,
    /// `string`.
    String,
    /// `integer`.
    Integer,
}

/// A schema `type`: either a single type, or (for nullability and unions of
/// primitive kinds) an array of types per JSON Schema 2020-12.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(untagged)]
pub enum SchemaType {
    /// A single type token, e.g. `"string"`.
    Single(SimpleType),
    /// A type array, e.g. `["string", "null"]`.
    Multiple(Vec<SimpleType>),
}

/// A JSON-Schema-2020-12 schema (the subset cartographer emits and reads).
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Schema {
    /// The schema type (single token or type array for nullability).
    #[serde(default, rename = "type", skip_serializing_if = "Option::is_none")]
    pub schema_type: Option<SchemaType>,
    /// An optional format annotation (e.g. `int64`, `date-time`).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub format: Option<String>,
    /// Object properties (for `type: object`).
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub properties: BTreeMap<String, Schema>,
    /// Required property names.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub required: Vec<String>,
    /// The element schema (for `type: array`).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub items: Option<Box<Schema>>,
    /// `anyOf` alternatives (for union shapes).
    #[serde(default, rename = "anyOf", skip_serializing_if = "Vec::is_empty")]
    pub any_of: Vec<Schema>,
    /// A reference to another schema (`$ref`).
    #[serde(default, rename = "$ref", skip_serializing_if = "Option::is_none")]
    pub reference: Option<String>,
    /// An optional description.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
}

impl Schema {
    /// A schema with a single type token.
    #[must_use]
    pub fn of_type(simple: SimpleType) -> Self {
        Self {
            schema_type: Some(SchemaType::Single(simple)),
            ..Self::default()
        }
    }
}

/// serde `skip_serializing_if` helper for boolean fields that default to `false`.
fn is_false(value: &bool) -> bool {
    !*value
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Build a representative document exercising most of the model.
    fn sample_document() -> Document {
        let mut word_item = PathItem::default();
        word_item.parameters.push(Parameter {
            name: "word".to_string(),
            location: ParameterIn::Path,
            required: true,
            description: None,
            schema: Some(Schema::of_type(SimpleType::String)),
        });
        let mut responses = Responses::new();
        let mut content = Content::new();
        let mut props = BTreeMap::new();
        props.insert("word".to_string(), Schema::of_type(SimpleType::String));
        props.insert("exists".to_string(), Schema::of_type(SimpleType::Boolean));
        content.insert(
            "application/json".to_string(),
            MediaType {
                schema: Some(Schema {
                    schema_type: Some(SchemaType::Single(SimpleType::Object)),
                    properties: props,
                    required: vec!["word".to_string(), "exists".to_string()],
                    ..Schema::default()
                }),
                example: None,
            },
        );
        responses.insert(
            "200".to_string(),
            Response {
                description: "the membership result".to_string(),
                content,
                ..Response::default()
            },
        );
        word_item.get = Some(Operation {
            operation_id: Some("getCustomWord".to_string()),
            summary: Some("Check a custom word".to_string()),
            responses,
            ..Operation::default()
        });

        let mut doc = Document::new("merriam", "1.0.0");
        doc.paths.insert("/custom/{word}".to_string(), word_item);
        doc
    }

    #[test]
    fn document_round_trips_through_json() {
        let doc = sample_document();
        let json = serde_json::to_string_pretty(&doc).expect("serialize");
        let back: Document = serde_json::from_str(&json).expect("deserialize");
        assert_eq!(doc, back);
    }

    #[test]
    fn field_renames_match_the_specification() {
        let doc = sample_document();
        let json = serde_json::to_string(&doc).expect("serialize");
        assert!(json.contains("\"operationId\":\"getCustomWord\""), "{json}");
        // `in` rename and parameter required flag.
        assert!(json.contains("\"in\":\"path\""), "{json}");
        assert!(json.contains("\"required\":true"), "{json}");
        // The schema `type` token uses the JSON Schema spelling.
        assert!(json.contains("\"type\":\"object\""), "{json}");
    }

    #[test]
    fn nullable_is_a_type_array() {
        let schema = Schema {
            schema_type: Some(SchemaType::Multiple(vec![
                SimpleType::String,
                SimpleType::Null,
            ])),
            ..Schema::default()
        };
        let json = serde_json::to_string(&schema).expect("serialize");
        assert_eq!(json, r#"{"type":["string","null"]}"#);
        let back: Schema = serde_json::from_str(&json).expect("deserialize");
        assert_eq!(schema, back);
    }

    #[test]
    fn any_of_and_ref_use_spec_keys() {
        let schema = Schema {
            any_of: vec![
                Schema::of_type(SimpleType::Integer),
                Schema::of_type(SimpleType::String),
            ],
            ..Schema::default()
        };
        let json = serde_json::to_string(&schema).expect("serialize");
        assert!(json.contains("\"anyOf\""), "{json}");

        let reference = Schema {
            reference: Some("#/components/schemas/Word".to_string()),
            ..Schema::default()
        };
        let json = serde_json::to_string(&reference).expect("serialize");
        assert_eq!(json, r##"{"$ref":"#/components/schemas/Word"}"##);
    }

    #[test]
    fn empty_fields_are_omitted() {
        let doc = Document::new("empty", "0.1.0");
        let json = serde_json::to_string(&doc).expect("serialize");
        // No servers, paths, or components when empty.
        assert!(!json.contains("servers"), "{json}");
        assert!(!json.contains("paths"), "{json}");
        assert!(!json.contains("components"), "{json}");
        assert!(json.contains("\"openapi\":\"3.1.0\""), "{json}");
        assert!(json.contains("\"title\":\"empty\""), "{json}");
    }

    #[test]
    fn single_type_deserializes_from_a_string() {
        let schema: Schema = serde_json::from_str(r#"{"type":"integer"}"#).expect("deserialize");
        assert_eq!(
            schema.schema_type,
            Some(SchemaType::Single(SimpleType::Integer))
        );
    }

    #[test]
    fn unknown_fields_are_ignored() {
        // A document carrying constructs outside the modeled subset still loads.
        let json = r#"{
            "openapi": "3.1.0",
            "info": { "title": "x", "version": "1", "termsOfService": "http://x" },
            "jsonSchemaDialect": "https://json-schema.org/draft/2020-12/schema",
            "webhooks": {},
            "paths": {}
        }"#;
        let doc: Document = serde_json::from_str(json).expect("tolerant deserialize");
        assert_eq!(doc.info.title, "x");
        assert!(doc.paths.is_empty());
    }

    #[test]
    fn path_item_operation_lookup_is_case_insensitive() {
        let doc = sample_document();
        let item = &doc.paths["/custom/{word}"];
        assert!(item.operation("get").is_some());
        assert!(item.operation("GET").is_some());
        assert!(item.operation("post").is_none());
        let methods: Vec<_> = item.operations().map(|(m, _)| m).collect();
        assert_eq!(methods, ["GET"]);
    }

    #[test]
    fn media_type_example_round_trips_and_is_omitted_when_absent() {
        let media = MediaType {
            schema: Some(Schema::of_type(SimpleType::Object)),
            example: Some(serde_json::json!({"a": 1})),
        };
        let json = serde_json::to_string(&media).unwrap();
        assert!(json.contains("\"example\""), "{json}");
        let back: MediaType = serde_json::from_str(&json).unwrap();
        assert_eq!(media, back);

        let bare = MediaType {
            schema: Some(Schema::of_type(SimpleType::String)),
            example: None,
        };
        let json = serde_json::to_string(&bare).unwrap();
        assert!(!json.contains("example"), "{json}");
    }
}
