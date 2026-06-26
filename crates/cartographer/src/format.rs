// Copyright (c) Microsoft Corporation.

//! Reading and writing OpenAPI specs as JSON or YAML (AJ-C3).
//!
//! cartographer reads existing specs in either format and default-writes YAML.
//! The format boundary is owned here: a [`Document`] is parsed from JSON
//! (`serde_json`) or YAML (`serde_yaml_ng`) and serialized back to either. Loading
//! is *tolerant* — a directory of specs is walked and each unparseable file yields
//! a [`LoadError`] rather than aborting the load, so one malformed spec never
//! hides the rest.

use std::path::{Path, PathBuf};

use crate::environment::Environment;
use crate::model::Document;

/// The serialized form of an OpenAPI document.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SpecFormat {
    /// JSON (`.json`).
    Json,
    /// YAML (`.yaml` / `.yml`). The default written format.
    #[default]
    Yaml,
}

impl SpecFormat {
    /// The format implied by a path's extension, or `None` if unrecognized.
    #[must_use]
    pub fn from_path(path: &Path) -> Option<SpecFormat> {
        match path.extension()?.to_str()?.to_ascii_lowercase().as_str() {
            "json" => Some(SpecFormat::Json),
            "yaml" | "yml" => Some(SpecFormat::Yaml),
            _ => None,
        }
    }
}

/// A spec successfully loaded from a source path.
#[derive(Clone, Debug, PartialEq)]
pub struct LoadedSpec {
    /// The source path the document was read from.
    pub path: PathBuf,
    /// The format it was read as.
    pub format: SpecFormat,
    /// The parsed document.
    pub document: Document,
}

/// A spec source that could not be read or parsed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LoadError {
    /// The offending path.
    pub path: PathBuf,
    /// A human-readable description of the failure.
    pub message: String,
}

/// The result of loading a path: the specs that parsed, and the sources that did
/// not.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct LoadOutcome {
    /// Successfully loaded specs, in sorted path order.
    pub specs: Vec<LoadedSpec>,
    /// Sources that failed to read or parse.
    pub errors: Vec<LoadError>,
}

/// Parse a document from text in a known format.
///
/// # Errors
/// Returns a human-readable message if the text is not a valid document in
/// `format`.
pub fn parse_document(text: &str, format: SpecFormat) -> Result<Document, String> {
    match format {
        SpecFormat::Json => serde_json::from_str(text).map_err(|e| e.to_string()),
        SpecFormat::Yaml => serde_yaml_ng::from_str(text).map_err(|e| e.to_string()),
    }
}

/// Parse a document of unknown format, trying JSON first then YAML.
///
/// # Errors
/// Returns a combined message if the text parses as neither.
pub fn parse_auto(text: &str) -> Result<(Document, SpecFormat), String> {
    match serde_json::from_str::<Document>(text) {
        Ok(document) => Ok((document, SpecFormat::Json)),
        Err(json_err) => match serde_yaml_ng::from_str::<Document>(text) {
            Ok(document) => Ok((document, SpecFormat::Yaml)),
            Err(yaml_err) => Err(format!(
                "not valid JSON ({json_err}) or YAML ({yaml_err})"
            )),
        },
    }
}

/// Serialize a document to text in the given format.
///
/// JSON is pretty-printed for human review; YAML is block style.
///
/// # Errors
/// Returns a message if serialization fails (it should not for a well-formed
/// document).
pub fn serialize_document(document: &Document, format: SpecFormat) -> Result<String, String> {
    match format {
        SpecFormat::Json => serde_json::to_string_pretty(document).map_err(|e| e.to_string()),
        SpecFormat::Yaml => serde_yaml_ng::to_string(document).map_err(|e| e.to_string()),
    }
}

/// Parse an environment descriptor (D-CART-4) from text in a known format.
///
/// # Errors
/// Returns a human-readable message if the text is not a valid descriptor in
/// `format`.
pub fn parse_environment(text: &str, format: SpecFormat) -> Result<Environment, String> {
    match format {
        SpecFormat::Json => serde_json::from_str(text).map_err(|e| e.to_string()),
        SpecFormat::Yaml => serde_yaml_ng::from_str(text).map_err(|e| e.to_string()),
    }
}

/// Serialize an environment descriptor (D-CART-4) to text in the given format.
///
/// JSON is pretty-printed for human review; YAML is block style. The format
/// boundary is shared with [`serialize_document`]; the descriptor shape is owned
/// in [`crate::environment`].
///
/// # Errors
/// Returns a message if serialization fails (it should not for a well-formed
/// descriptor).
pub fn serialize_environment(
    environment: &Environment,
    format: SpecFormat,
) -> Result<String, String> {
    match format {
        SpecFormat::Json => serde_json::to_string_pretty(environment).map_err(|e| e.to_string()),
        SpecFormat::Yaml => serde_yaml_ng::to_string(environment).map_err(|e| e.to_string()),
    }
}

/// Load every spec reachable from `path`.
///
/// If `path` is a file it is loaded directly (its format inferred from the
/// extension, falling back to content sniffing). If `path` is a directory, every
/// top-level `.json` / `.yaml` / `.yml` file is loaded in sorted order. Tolerant:
/// unreadable or unparseable sources are recorded in [`LoadOutcome::errors`].
#[must_use]
pub fn load_path(path: &Path) -> LoadOutcome {
    let mut outcome = LoadOutcome::default();
    if path.is_dir() {
        let read_dir = match std::fs::read_dir(path) {
            Ok(read_dir) => read_dir,
            Err(error) => {
                outcome.errors.push(LoadError {
                    path: path.to_path_buf(),
                    message: format!("cannot read directory: {error}"),
                });
                return outcome;
            }
        };
        let mut entries: Vec<PathBuf> = read_dir
            .filter_map(|entry| entry.ok().map(|entry| entry.path()))
            .filter(|candidate| candidate.is_file() && SpecFormat::from_path(candidate).is_some())
            .collect();
        entries.sort();
        for entry in entries {
            load_one(&entry, &mut outcome);
        }
    } else {
        load_one(path, &mut outcome);
    }
    outcome
}

/// Load a single file into `outcome`.
fn load_one(path: &Path, outcome: &mut LoadOutcome) {
    let text = match std::fs::read_to_string(path) {
        Ok(text) => text,
        Err(error) => {
            outcome.errors.push(LoadError {
                path: path.to_path_buf(),
                message: format!("cannot read file: {error}"),
            });
            return;
        }
    };
    let parsed = match SpecFormat::from_path(path) {
        Some(format) => parse_document(&text, format).map(|document| (document, format)),
        None => parse_auto(&text),
    };
    match parsed {
        Ok((document, format)) => outcome.specs.push(LoadedSpec {
            path: path.to_path_buf(),
            format,
            document,
        }),
        Err(message) => outcome.errors.push(LoadError {
            path: path.to_path_buf(),
            message,
        }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::Document;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn sample() -> Document {
        Document::new("merriam", "1.0.0")
    }

    fn temp_dir(tag: &str) -> PathBuf {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let dir = std::env::temp_dir().join(format!(
            "cartographer-{tag}-{}-{nanos}",
            std::process::id()
        ));
        std::fs::create_dir_all(&dir).expect("create temp dir");
        dir
    }

    #[test]
    fn json_round_trips() {
        let doc = sample();
        let text = serialize_document(&doc, SpecFormat::Json).expect("serialize json");
        let back = parse_document(&text, SpecFormat::Json).expect("parse json");
        assert_eq!(doc, back);
    }

    #[test]
    fn yaml_round_trips() {
        let doc = sample();
        let text = serialize_document(&doc, SpecFormat::Yaml).expect("serialize yaml");
        let back = parse_document(&text, SpecFormat::Yaml).expect("parse yaml");
        assert_eq!(doc, back);
    }

    #[test]
    fn parse_auto_detects_json_then_yaml() {
        let doc = sample();
        let json = serialize_document(&doc, SpecFormat::Json).expect("json");
        let (from_json, fmt_json) = parse_auto(&json).expect("auto json");
        assert_eq!(fmt_json, SpecFormat::Json);
        assert_eq!(from_json, doc);

        let yaml = serialize_document(&doc, SpecFormat::Yaml).expect("yaml");
        let (from_yaml, fmt_yaml) = parse_auto(&yaml).expect("auto yaml");
        assert_eq!(fmt_yaml, SpecFormat::Yaml);
        assert_eq!(from_yaml, doc);
    }

    #[test]
    fn format_from_extension() {
        assert_eq!(SpecFormat::from_path(Path::new("a.json")), Some(SpecFormat::Json));
        assert_eq!(SpecFormat::from_path(Path::new("a.yaml")), Some(SpecFormat::Yaml));
        assert_eq!(SpecFormat::from_path(Path::new("a.yml")), Some(SpecFormat::Yaml));
        assert_eq!(SpecFormat::from_path(Path::new("a.txt")), None);
        assert_eq!(SpecFormat::from_path(Path::new("a.JSON")), Some(SpecFormat::Json));
    }

    #[test]
    fn load_single_file() {
        let dir = temp_dir("single");
        let path = dir.join("spec.json");
        std::fs::write(&path, serialize_document(&sample(), SpecFormat::Json).unwrap()).unwrap();
        let outcome = load_path(&path);
        std::fs::remove_dir_all(&dir).ok();
        assert!(outcome.errors.is_empty());
        assert_eq!(outcome.specs.len(), 1);
        assert_eq!(outcome.specs[0].format, SpecFormat::Json);
        assert_eq!(outcome.specs[0].document, sample());
    }

    #[test]
    fn load_directory_mixed_formats_tolerates_malformed() {
        let dir = temp_dir("dir");
        std::fs::write(
            dir.join("a.json"),
            serialize_document(&Document::new("a", "1"), SpecFormat::Json).unwrap(),
        )
        .unwrap();
        std::fs::write(
            dir.join("b.yaml"),
            serialize_document(&Document::new("b", "2"), SpecFormat::Yaml).unwrap(),
        )
        .unwrap();
        // A malformed spec with a recognized extension is recorded as an error.
        std::fs::write(dir.join("broken.json"), "{ not valid").unwrap();
        // A non-spec extension is ignored entirely.
        std::fs::write(dir.join("notes.txt"), "ignore me").unwrap();

        let outcome = load_path(&dir);
        std::fs::remove_dir_all(&dir).ok();

        assert_eq!(outcome.specs.len(), 2, "a.json + b.yaml");
        // Sorted order: a.json before b.yaml (broken.json errored).
        assert_eq!(outcome.specs[0].document.info.title, "a");
        assert_eq!(outcome.specs[1].document.info.title, "b");
        assert_eq!(outcome.errors.len(), 1);
        assert!(outcome.errors[0].path.ends_with("broken.json"));
    }

    #[test]
    fn unparseable_file_is_an_error_not_a_panic() {
        let dir = temp_dir("bad");
        let path = dir.join("spec.yaml");
        std::fs::write(&path, ":\n  - not a document").unwrap();
        let outcome = load_path(&path);
        std::fs::remove_dir_all(&dir).ok();
        assert!(outcome.specs.is_empty());
        assert_eq!(outcome.errors.len(), 1);
    }
}
