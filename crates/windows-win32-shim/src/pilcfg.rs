// Copyright (c) Microsoft Corporation.

//! The `.pilcfg` JSON sidecar (SHIM-D5 / MW4).
//!
//! A `<host-executable>.pilcfg` file selects how the process-wide session
//! ([`crate::session`]) composes its isolation stack. The schema and semantics
//! mirror the C++ `mwin32` sidecar (`pilcfg.h` / `pilcfg.cpp`) so the two shims
//! consume interchangeable artifacts (SHIM-D1): the recognized members are
//! `buffer_updates`, `record_modifications`, `redirections`, `persisted_state`,
//! `capture_snapshot`, `diagnostic_log`, and `fault_script`; the `webcore`
//! block is ignored here. The all-default value is **passthrough** — calls flow
//! straight through to the live OS registry.
//!
//! Parsing is **strict** (a malformed document or a recognized member of the
//! wrong type is an error), but *loading* is **tolerant**: an absent,
//! unreadable, or malformed sidecar yields the default passthrough
//! configuration and never fails the host (mwin32 D5). The JSON shape we accept
//! and the `%VAR%` expansion we apply are specified here and owned by us (Design
//! Autonomy); `tinyjson` is merely the parser chosen to satisfy that spec.

use std::fmt;
use std::path::PathBuf;

use tinyjson::JsonValue;

/// JSON member names recognized in a `.pilcfg` file. Changing, adding, or
/// removing any of these is a breaking change to the shared sidecar format
/// (SHIM-D5).
mod member {
    pub const BUFFER_UPDATES: &str = "buffer_updates";
    pub const RECORD_MODIFICATIONS: &str = "record_modifications";
    pub const REDIRECTIONS: &str = "redirections";
    pub const FROM: &str = "from";
    pub const TO: &str = "to";
    pub const PERSISTED_STATE: &str = "persisted_state";
    pub const CAPTURE_SNAPSHOT: &str = "capture_snapshot";
    pub const DIAGNOSTIC_LOG: &str = "diagnostic_log";
    pub const FAULT_SCRIPT: &str = "fault_script";
}

/// The parsed contents of a `.pilcfg` sidecar. Each field maps onto a layer the
/// session can compose; the all-default value is passthrough.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Pilcfg {
    /// Interpose a write-buffering layer: registry mutations are captured in
    /// memory and not written through to the live registry.
    pub buffer_updates: bool,

    /// Interpose a layer that records every registry modification.
    pub record_modifications: bool,

    /// Registry path redirections, each mapping a public path prefix (`.0`) to
    /// the private path (`.1`) it is rewritten to. Empty by default. The keys
    /// are logical namespace identifiers and are taken literally (no `%VAR%`
    /// expansion).
    pub redirections: Vec<(String, String)>,

    /// Path to a persisted registry-state XML artifact. When non-empty the
    /// session runs entirely against the loaded snapshot and never touches the
    /// live registry; the buffer / redirection settings are then ignored.
    pub persisted_state: String,

    /// Path to which the session writes a snapshot of its registry state at
    /// teardown. Empty by default (no capture).
    pub capture_snapshot: String,

    /// Path to which the session writes its diagnostic modification log at
    /// teardown. Empty by default (no log).
    pub diagnostic_log: String,

    /// Path to a fault-script artifact layered on top of the base stack. Empty
    /// by default (no fault injection).
    pub fault_script: String,
}

/// A `.pilcfg` parse failure. The JSON text is not valid JSON, is not a JSON
/// object, or a recognized member is present with the wrong shape.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PilcfgError(String);

impl PilcfgError {
    fn new(message: impl Into<String>) -> Self {
        Self(message.into())
    }

    /// The human-readable failure description.
    #[must_use]
    pub fn message(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for PilcfgError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "pilcfg: {}", self.0)
    }
}

impl std::error::Error for PilcfgError {}

/// Parse the JSON text of a `.pilcfg` file into a [`Pilcfg`].
///
/// The accepted schema is a JSON object with optional boolean members
/// `buffer_updates` and `record_modifications`, an optional `redirections`
/// array of `{ "from": <string>, "to": <string> }` objects, and optional string
/// members `persisted_state`, `capture_snapshot`, `diagnostic_log`, and
/// `fault_script`. Absent members keep their default; unknown members (and the
/// `webcore` block) are ignored. The four path-bearing string members undergo
/// `%VAR%` expansion ([`expand_environment_path`]); redirection keys do not.
///
/// # Errors
///
/// Returns [`PilcfgError`] if the text is not valid JSON, the root is not a JSON
/// object, a recognized member has the wrong type, or a `redirections` element
/// is not an object carrying string `from` and `to` members.
pub fn parse_pilcfg(json_text: &str) -> Result<Pilcfg, PilcfgError> {
    let parsed: JsonValue = json_text
        .parse()
        .map_err(|e| PilcfgError::new(format!("invalid JSON: {e}")))?;

    let JsonValue::Object(object) = parsed else {
        return Err(PilcfgError::new("root must be a JSON object"));
    };

    Ok(Pilcfg {
        buffer_updates: read_bool(&object, member::BUFFER_UPDATES)?,
        record_modifications: read_bool(&object, member::RECORD_MODIFICATIONS)?,
        redirections: read_redirections(&object)?,
        persisted_state: read_path(&object, member::PERSISTED_STATE)?,
        capture_snapshot: read_path(&object, member::CAPTURE_SNAPSHOT)?,
        diagnostic_log: read_path(&object, member::DIAGNOSTIC_LOG)?,
        fault_script: read_path(&object, member::FAULT_SCRIPT)?,
    })
}

/// A parsed JSON object (the `tinyjson` representation of `{ … }`).
type Object = std::collections::HashMap<String, JsonValue>;

/// Read an optional boolean member: absent yields `false`, present-but-not-bool
/// is an error.
fn read_bool(object: &Object, name: &str) -> Result<bool, PilcfgError> {
    match object.get(name) {
        None => Ok(false),
        Some(JsonValue::Boolean(value)) => Ok(*value),
        Some(_) => Err(PilcfgError::new(format!("'{name}' must be a boolean"))),
    }
}

/// Read an optional path-bearing string member: absent yields an empty string,
/// present-but-not-string is an error. The value undergoes `%VAR%` expansion.
fn read_path(object: &Object, name: &str) -> Result<String, PilcfgError> {
    match object.get(name) {
        None => Ok(String::new()),
        Some(JsonValue::String(value)) => Ok(expand_environment_path(value)),
        Some(_) => Err(PilcfgError::new(format!("'{name}' must be a string"))),
    }
}

/// Read a required string member of a `redirections` element.
fn read_required_string(object: &Object, name: &str) -> Result<String, PilcfgError> {
    match object.get(name) {
        Some(JsonValue::String(value)) => Ok(value.clone()),
        Some(_) => Err(PilcfgError::new(format!(
            "each 'redirections' element's '{name}' must be a string"
        ))),
        None => Err(PilcfgError::new(
            "each 'redirections' element must have 'from' and 'to' members",
        )),
    }
}

/// Read the optional `redirections` array: absent yields an empty vector,
/// present-but-not-array (or an element that is not a `{from,to}` object) is an
/// error. Order is preserved. Redirection strings are taken literally.
fn read_redirections(object: &Object) -> Result<Vec<(String, String)>, PilcfgError> {
    let Some(value) = object.get(member::REDIRECTIONS) else {
        return Ok(Vec::new());
    };
    let JsonValue::Array(elements) = value else {
        return Err(PilcfgError::new("'redirections' must be an array"));
    };

    let mut redirections = Vec::with_capacity(elements.len());
    for element in elements {
        let JsonValue::Object(entry) = element else {
            return Err(PilcfgError::new(
                "each 'redirections' element must be an object",
            ));
        };
        let from = read_required_string(entry, member::FROM)?;
        let to = read_required_string(entry, member::TO)?;
        redirections.push((from, to));
    }
    Ok(redirections)
}

/// Expand Windows `%VAR%` environment-variable references in a host-path value.
///
/// Specified behavior (owned by us): a `%NAME%` token is replaced by the value
/// of environment variable `NAME`; a token naming an undefined variable is left
/// verbatim (including its surrounding `%`), and a string with no complete
/// `%…%` token is returned unchanged. Only host-path members are expanded;
/// logical namespace identifiers (redirection keys) are taken literally, so a
/// legitimate `%` in a key is never disturbed. This lets a checked-in `.pilcfg`
/// resolve to per-machine locations such as `%TEMP%\\snapshot.xml`.
#[must_use]
pub fn expand_environment_path(value: &str) -> String {
    if !value.contains('%') {
        return value.to_owned();
    }

    let mut out = String::with_capacity(value.len());
    let mut rest = value;
    while let Some(open) = rest.find('%') {
        out.push_str(&rest[..open]);
        let after_open = &rest[open + 1..];
        match after_open.find('%') {
            Some(close) => {
                let name = &after_open[..close];
                match std::env::var(name) {
                    Ok(expanded) if !name.is_empty() => out.push_str(&expanded),
                    // Undefined (or empty) token: emit it verbatim, including
                    // both delimiters, and continue scanning after the closer.
                    _ => {
                        out.push('%');
                        out.push_str(name);
                        out.push('%');
                    }
                }
                rest = &after_open[close + 1..];
            }
            None => {
                // A trailing unmatched '%': emit the remainder verbatim.
                out.push('%');
                out.push_str(after_open);
                return out;
            }
        }
    }
    out.push_str(rest);
    out
}

/// Locate, read, and parse the `<host-executable>.pilcfg` sidecar.
///
/// Resolves the running process executable via [`std::env::current_exe`] (safe)
/// and appends `.pilcfg`. Any failure — the path cannot be determined, the file
/// is absent or unreadable, the bytes are not UTF-8, or the JSON is malformed —
/// yields the default passthrough [`Pilcfg`] rather than an error, so a missing
/// or broken sidecar never breaks the host process (mwin32 D5).
#[must_use]
pub fn load_pilcfg() -> Pilcfg {
    sidecar_path()
        .and_then(|path| std::fs::read_to_string(path).ok())
        .and_then(|text| parse_pilcfg(&text).ok())
        .unwrap_or_default()
}

/// The full path of the host executable's `.pilcfg` sidecar, or `None` if the
/// executable path cannot be determined.
fn sidecar_path() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    let file_name = exe.file_name()?;
    let mut sidecar = file_name.to_owned();
    sidecar.push(".pilcfg");
    Some(exe.with_file_name(sidecar))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_object_is_passthrough() {
        let cfg = parse_pilcfg("{}").expect("empty object parses");
        assert_eq!(cfg, Pilcfg::default());
        assert!(!cfg.buffer_updates);
        assert!(!cfg.record_modifications);
    }

    #[test]
    fn buffer_updates_true() {
        let cfg = parse_pilcfg(r#"{ "buffer_updates": true }"#).unwrap();
        assert!(cfg.buffer_updates);
        assert!(!cfg.record_modifications);
    }

    #[test]
    fn record_modifications_true() {
        let cfg = parse_pilcfg(r#"{ "record_modifications": true }"#).unwrap();
        assert!(!cfg.buffer_updates);
        assert!(cfg.record_modifications);
    }

    #[test]
    fn both_true_and_explicitly_false() {
        let both = parse_pilcfg(r#"{ "buffer_updates": true, "record_modifications": true }"#)
            .unwrap();
        assert!(both.buffer_updates && both.record_modifications);

        let neither =
            parse_pilcfg(r#"{ "buffer_updates": false, "record_modifications": false }"#).unwrap();
        assert!(!neither.buffer_updates && !neither.record_modifications);
    }

    #[test]
    fn unknown_members_and_nested_objects_ignored() {
        let cfg = parse_pilcfg(
            r#"{ "buffer_updates": true, "future_option": 42, "note": "hi", "nested": { "x": 1 } }"#,
        )
        .unwrap();
        assert!(cfg.buffer_updates);
        assert!(!cfg.record_modifications);
    }

    #[test]
    fn whitespace_and_member_order_tolerated() {
        let cfg =
            parse_pilcfg("\n\t {\r\n  \"record_modifications\" :\ttrue\n}\n").unwrap();
        assert!(cfg.record_modifications);

        let ordered =
            parse_pilcfg(r#"{ "record_modifications": true, "buffer_updates": true }"#).unwrap();
        assert!(ordered.buffer_updates && ordered.record_modifications);
    }

    #[test]
    fn invalid_json_is_an_error() {
        assert!(parse_pilcfg("{ not json").is_err());
        assert!(parse_pilcfg("").is_err());
        assert!(parse_pilcfg(r#"{ "buffer_updates": true,, }"#).is_err());
    }

    #[test]
    fn non_object_root_is_an_error() {
        for text in ["[]", "true", "42", r#""a string""#, "null"] {
            assert!(parse_pilcfg(text).is_err(), "{text} must be rejected");
        }
    }

    #[test]
    fn non_boolean_recognized_member_is_an_error() {
        for text in [
            r#"{ "buffer_updates": "yes" }"#,
            r#"{ "buffer_updates": 1 }"#,
            r#"{ "record_modifications": null }"#,
            r#"{ "record_modifications": [] }"#,
        ] {
            assert!(parse_pilcfg(text).is_err(), "{text} must be rejected");
        }
    }

    #[test]
    fn redirections_default_empty_and_parse_in_order() {
        assert!(parse_pilcfg(r#"{ "buffer_updates": true }"#)
            .unwrap()
            .redirections
            .is_empty());
        assert!(parse_pilcfg(r#"{ "redirections": [] }"#)
            .unwrap()
            .redirections
            .is_empty());

        let cfg = parse_pilcfg(
            r#"{ "redirections": [
                { "from": "HKLM\\A", "to": "HKCU\\X" },
                { "from": "HKLM\\B", "to": "HKCU\\Y" },
                { "from": "HKLM\\C", "to": "HKCU\\Z" }
            ] }"#,
        )
        .unwrap();
        assert_eq!(cfg.redirections.len(), 3);
        assert_eq!(cfg.redirections[0], ("HKLM\\A".to_owned(), "HKCU\\X".to_owned()));
        assert_eq!(cfg.redirections[2].1, "HKCU\\Z");
    }

    #[test]
    fn redirections_combine_with_flags_and_preserve_unicode() {
        let cfg = parse_pilcfg(
            r#"{ "buffer_updates": true,
                 "redirections": [ { "from": "HKCU\\\u041a\u043b\u044e\u0447", "to": "HKCU\\Schl\u00fcssel" } ] }"#,
        )
        .unwrap();
        assert!(cfg.buffer_updates);
        assert_eq!(cfg.redirections.len(), 1);
        assert_eq!(cfg.redirections[0].0, "HKCU\\\u{041a}\u{043b}\u{044e}\u{0447}");
        assert_eq!(cfg.redirections[0].1, "HKCU\\Schl\u{00fc}ssel");
    }

    #[test]
    fn malformed_redirections_are_errors() {
        for text in [
            r#"{ "redirections": {} }"#,
            r#"{ "redirections": "nope" }"#,
            r#"{ "redirections": [ "HKLM\\A" ] }"#,
            r#"{ "redirections": [ 42 ] }"#,
            r#"{ "redirections": [ { "from": "HKLM\\A" } ] }"#,
            r#"{ "redirections": [ { "to": "HKCU\\X" } ] }"#,
            r#"{ "redirections": [ {} ] }"#,
            r#"{ "redirections": [ { "from": 1, "to": "HKCU\\X" } ] }"#,
            r#"{ "redirections": [ { "from": "HKLM\\A", "to": true } ] }"#,
        ] {
            assert!(parse_pilcfg(text).is_err(), "{text} must be rejected");
        }
    }

    #[test]
    fn path_members_default_empty_and_parse() {
        let cfg = parse_pilcfg(r#"{ "buffer_updates": true }"#).unwrap();
        assert!(cfg.persisted_state.is_empty());
        assert!(cfg.capture_snapshot.is_empty());
        assert!(cfg.diagnostic_log.is_empty());
        assert!(cfg.fault_script.is_empty());

        let cfg = parse_pilcfg(
            r#"{ "persisted_state": "C:\\snapshots\\reg.xml",
                 "capture_snapshot": "C:\\out\\snap.xml",
                 "diagnostic_log": "C:\\out\\log.txt",
                 "fault_script": "C:\\faults\\script.xml" }"#,
        )
        .unwrap();
        assert_eq!(cfg.persisted_state, "C:\\snapshots\\reg.xml");
        assert_eq!(cfg.capture_snapshot, "C:\\out\\snap.xml");
        assert_eq!(cfg.diagnostic_log, "C:\\out\\log.txt");
        assert_eq!(cfg.fault_script, "C:\\faults\\script.xml");
    }

    #[test]
    fn path_members_preserve_unicode() {
        let cfg = parse_pilcfg(
            r#"{ "persisted_state": "C:\\\u041a\u043b\u044e\u0447\\reg.xml" }"#,
        )
        .unwrap();
        assert_eq!(cfg.persisted_state, "C:\\\u{041a}\u{043b}\u{044e}\u{0447}\\reg.xml");
    }

    #[test]
    fn non_string_path_member_is_an_error() {
        for text in [
            r#"{ "persisted_state": 7 }"#,
            r#"{ "persisted_state": true }"#,
            r#"{ "persisted_state": [] }"#,
            r#"{ "persisted_state": null }"#,
            r#"{ "fault_script": 7 }"#,
            r#"{ "fault_script": [] }"#,
        ] {
            assert!(parse_pilcfg(text).is_err(), "{text} must be rejected");
        }
    }

    #[test]
    fn env_expansion_leaves_plain_and_undefined_tokens_alone() {
        assert_eq!(expand_environment_path(""), "");
        assert_eq!(
            expand_environment_path("C:\\no\\tokens\\here.xml"),
            "C:\\no\\tokens\\here.xml"
        );
        // An undefined variable token is left verbatim, delimiters and all.
        assert_eq!(
            expand_environment_path("%PILCFG_DEFINITELY_UNSET_VAR%\\x.xml"),
            "%PILCFG_DEFINITELY_UNSET_VAR%\\x.xml"
        );
        // A trailing unmatched '%' is emitted verbatim.
        assert_eq!(expand_environment_path("50%"), "50%");
    }

    #[test]
    fn env_expansion_substitutes_a_defined_variable() {
        // `SystemRoot` is always defined on Windows; read it (never mutate the
        // environment) so the test stays reproducible.
        let system_root = std::env::var("SystemRoot").expect("SystemRoot is set on Windows");
        assert_eq!(
            expand_environment_path("%SystemRoot%\\snap.xml"),
            format!("{system_root}\\snap.xml")
        );
    }

    #[test]
    fn env_expansion_applies_through_the_parser_for_paths_only() {
        let system_root = std::env::var("SystemRoot").expect("SystemRoot is set on Windows");
        let json = r#"{ "persisted_state": "%SystemRoot%\\reg.xml",
                  "redirections": [ { "from": "%SystemRoot%", "to": "HKCU\\X" } ] }"#;
        let cfg = parse_pilcfg(json).unwrap();
        // The path member is expanded …
        assert_eq!(cfg.persisted_state, format!("{system_root}\\reg.xml"));
        // … but the redirection key is taken literally.
        assert_eq!(cfg.redirections[0].0, "%SystemRoot%");
    }

    #[test]
    fn load_pilcfg_with_no_sidecar_is_passthrough() {
        // The test harness executable has no adjacent `.pilcfg`, so the tolerant
        // loader must yield the default passthrough configuration.
        assert_eq!(load_pilcfg(), Pilcfg::default());
    }
}
