// Copyright (c) Microsoft Corporation.

//! The diagnostic model and its rendering (AJ-D2).
//!
//! Validation (AJ-D3) compares observed traffic against the existing specs and
//! emits [`Diagnostic`]s for every deviation. A diagnostic carries a
//! [`Severity`], a machine-readable [`DiagnosticCode`], a [`Location`] (path,
//! optional method and status), a human message, and an observation `count`
//! (set by aggregation, AJ-D4).
//!
//! Diagnostics render through the single [`OutputSink`](crate::sink::OutputSink)
//! in two modes: human-readable `text`, and one-JSON-object-per-line `ndjson` for
//! machine consumption.

use serde::{Deserialize, Serialize};

use crate::sink::OutputSink;

/// How serious a diagnostic is.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Severity {
    /// A definite contract violation.
    #[default]
    Error,
    /// A likely problem that does not necessarily break the contract.
    Warning,
    /// Informational only.
    Info,
}

impl Severity {
    /// The lowercase token (`"error"` / `"warning"` / `"info"`).
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Severity::Error => "error",
            Severity::Warning => "warning",
            Severity::Info => "info",
        }
    }
}

/// A machine-readable diagnostic category.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DiagnosticCode {
    /// An observed path has no matching template in any spec.
    UndocumentedPath,
    /// An observed method is not declared on the matched path.
    UndocumentedOperation,
    /// An observed response status is not declared on the operation.
    UndeclaredStatus,
    /// An observed query parameter is not declared on the operation.
    UndeclaredParameter,
    /// An observed (non-standard) request header is not declared.
    UndeclaredHeader,
    /// The observed request body does not conform to the declared schema.
    RequestSchemaMismatch,
    /// The observed response body does not conform to the declared schema.
    ResponseSchemaMismatch,
    /// An observed value's type does not match the declared type.
    TypeMismatch,
}

impl DiagnosticCode {
    /// The snake_case token for this code (matches the serialized form).
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            DiagnosticCode::UndocumentedPath => "undocumented_path",
            DiagnosticCode::UndocumentedOperation => "undocumented_operation",
            DiagnosticCode::UndeclaredStatus => "undeclared_status",
            DiagnosticCode::UndeclaredParameter => "undeclared_parameter",
            DiagnosticCode::UndeclaredHeader => "undeclared_header",
            DiagnosticCode::RequestSchemaMismatch => "request_schema_mismatch",
            DiagnosticCode::ResponseSchemaMismatch => "response_schema_mismatch",
            DiagnosticCode::TypeMismatch => "type_mismatch",
        }
    }
}

/// Where a diagnostic applies.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Location {
    /// The path (a template when matched, the observed concrete path otherwise).
    pub path: String,
    /// The HTTP method, if the diagnostic is operation-specific.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub method: Option<String>,
    /// The response status, if the diagnostic is response-specific.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub status: Option<u16>,
}

impl Location {
    /// A path-only location.
    #[must_use]
    pub fn path(path: impl Into<String>) -> Self {
        Self {
            path: path.into(),
            method: None,
            status: None,
        }
    }

    /// A path + method location.
    #[must_use]
    pub fn operation(path: impl Into<String>, method: impl Into<String>) -> Self {
        Self {
            path: path.into(),
            method: Some(method.into()),
            status: None,
        }
    }

    /// A path + method + status location.
    #[must_use]
    pub fn response(path: impl Into<String>, method: impl Into<String>, status: u16) -> Self {
        Self {
            path: path.into(),
            method: Some(method.into()),
            status: Some(status),
        }
    }

    /// A compact `METHOD /path -> STATUS` rendering (omitting absent parts).
    #[must_use]
    pub fn to_text(&self) -> String {
        let mut out = String::new();
        if let Some(method) = &self.method {
            out.push_str(method);
            out.push(' ');
        }
        out.push_str(&self.path);
        if let Some(status) = self.status {
            out.push_str(" -> ");
            out.push_str(&status.to_string());
        }
        out
    }
}

/// One validation finding.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Diagnostic {
    /// The severity.
    pub severity: Severity,
    /// The machine-readable category.
    pub code: DiagnosticCode,
    /// Where it applies.
    pub location: Location,
    /// A human-readable explanation.
    pub message: String,
    /// How many observations produced this finding (set by aggregation).
    pub count: usize,
}

impl Diagnostic {
    /// A new diagnostic with an observation count of 1.
    #[must_use]
    pub fn new(
        severity: Severity,
        code: DiagnosticCode,
        location: Location,
        message: impl Into<String>,
    ) -> Self {
        Self {
            severity,
            code,
            location,
            message: message.into(),
            count: 1,
        }
    }

    /// The single-line text rendering, e.g.
    /// `error[undeclared_status] GET /custom/{word} -> 418: status not declared (x3)`.
    #[must_use]
    pub fn to_text(&self) -> String {
        let mut line = format!(
            "{}[{}] {}: {}",
            self.severity.as_str(),
            self.code.as_str(),
            self.location.to_text(),
            self.message
        );
        if self.count > 1 {
            line.push_str(&format!(" (x{})", self.count));
        }
        line
    }
}

/// The report rendering mode.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ReportFormat {
    /// Human-readable single lines.
    #[default]
    Text,
    /// One JSON object per line.
    Ndjson,
}

/// Render diagnostics through `sink` in the chosen format.
pub fn render(diagnostics: &[Diagnostic], format: ReportFormat, sink: &mut dyn OutputSink) {
    for diagnostic in diagnostics {
        let line = match format {
            ReportFormat::Text => diagnostic.to_text(),
            ReportFormat::Ndjson => {
                serde_json::to_string(diagnostic).unwrap_or_else(|_| "{}".to_string())
            }
        };
        sink.write_line(&line);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sink::BufferSink;

    fn sample() -> Diagnostic {
        Diagnostic::new(
            Severity::Error,
            DiagnosticCode::UndeclaredStatus,
            Location::response("/custom/{word}", "GET", 418),
            "status not declared",
        )
    }

    #[test]
    fn text_rendering_includes_severity_code_location_and_message() {
        let line = sample().to_text();
        assert_eq!(
            line,
            "error[undeclared_status] GET /custom/{word} -> 418: status not declared"
        );
    }

    #[test]
    fn text_rendering_appends_count_when_above_one() {
        let mut diagnostic = sample();
        diagnostic.count = 3;
        assert!(diagnostic.to_text().ends_with("(x3)"));
    }

    #[test]
    fn path_only_location_omits_method_and_status() {
        let diagnostic = Diagnostic::new(
            Severity::Error,
            DiagnosticCode::UndocumentedPath,
            Location::path("/custom/cat"),
            "no matching path",
        );
        assert_eq!(
            diagnostic.to_text(),
            "error[undocumented_path] /custom/cat: no matching path"
        );
    }

    #[test]
    fn ndjson_rendering_is_valid_json_per_line() {
        let diagnostics = vec![sample(), sample()];
        let mut sink = BufferSink::new();
        render(&diagnostics, ReportFormat::Ndjson, &mut sink);
        assert_eq!(sink.lines().len(), 2);
        for line in sink.lines() {
            let value: serde_json::Value = serde_json::from_str(line).expect("valid json");
            assert_eq!(value["code"], "undeclared_status");
            assert_eq!(value["severity"], "error");
            assert_eq!(value["location"]["status"], 418);
        }
    }

    #[test]
    fn text_render_writes_one_line_per_diagnostic() {
        let diagnostics = vec![sample(), sample(), sample()];
        let mut sink = BufferSink::new();
        render(&diagnostics, ReportFormat::Text, &mut sink);
        assert_eq!(sink.lines().len(), 3);
    }

    #[test]
    fn codes_and_severities_have_stable_tokens() {
        assert_eq!(DiagnosticCode::UndocumentedPath.as_str(), "undocumented_path");
        assert_eq!(
            DiagnosticCode::ResponseSchemaMismatch.as_str(),
            "response_schema_mismatch"
        );
        assert_eq!(Severity::Warning.as_str(), "warning");
        // The serialized form matches `as_str`.
        assert_eq!(
            serde_json::to_string(&DiagnosticCode::TypeMismatch).unwrap(),
            "\"type_mismatch\""
        );
    }
}
