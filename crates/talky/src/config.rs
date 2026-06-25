// Copyright (c) Microsoft Corporation.

//! Configuration model for the `talky` stress client.
//!
//! All execution parameters come from a JSON file (see [`Config::load`]). The
//! schema is intentionally shared in shape with the `squeaky` client so one
//! config file can drive either. Endpoints may be IPv4 or IPv6, and the
//! [`Config::ip_version`] filter selects which families to drive.

use std::fs;
use std::net::{Ipv4Addr, Ipv6Addr};
use std::path::Path;

use serde::Deserialize;

/// The full set of execution parameters.
#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Config {
    /// Base endpoint URLs, e.g. `http://127.0.0.1:8080` or `http://[::1]:8080`.
    pub endpoints: Vec<String>,
    /// Which IP families to drive (filters [`endpoints`](Config::endpoints)).
    #[serde(default)]
    pub ip_version: IpVersion,
    /// Number of concurrent workers.
    #[serde(default = "default_workers")]
    pub workers: u32,
    /// Run for at most this many seconds (`0` = no time bound).
    #[serde(default)]
    pub duration_secs: u64,
    /// Stop after this many total requests (`0` = no count bound).
    #[serde(default)]
    pub max_requests: u64,
    /// Per-request timeout in milliseconds.
    #[serde(default = "default_timeout_ms")]
    pub request_timeout_ms: u32,
    /// The `X-Wordy-User` identity to send.
    #[serde(default = "default_user")]
    pub user: String,
    /// Seed for the deterministic workload PRNG.
    #[serde(default = "default_seed")]
    pub seed: u64,
    /// The weighted mix of routes to exercise.
    #[serde(default = "default_routes")]
    pub routes: Vec<RouteWeight>,
}

/// Which IP families to drive.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum IpVersion {
    /// IPv4 literals and hostnames.
    V4,
    /// IPv6 literals and hostnames.
    V6,
    /// Every endpoint, regardless of family.
    #[default]
    Both,
}

/// A route and its selection weight.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
pub struct RouteWeight {
    /// The route to exercise.
    pub name: Route,
    /// Relative selection weight (`0` disables the route).
    pub weight: u32,
}

/// The `wordy` routes `talky` can drive.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Route {
    /// `GET /healthz`.
    Health,
    /// `POST /spellcheck`.
    Spellcheck,
    /// `POST /anagram`.
    Anagram,
    /// `GET /shared?pattern=`.
    SharedEnum,
    /// `POST /custom/{word}`.
    CustomAdd,
    /// `GET /custom/{word}`.
    CustomExists,
    /// `DELETE /custom/{word}`.
    CustomRemove,
    /// `GET /custom`.
    CustomEnum,
}

fn default_workers() -> u32 {
    8
}
fn default_timeout_ms() -> u32 {
    5_000
}
fn default_user() -> String {
    "talky".to_string()
}
fn default_seed() -> u64 {
    1
}
fn default_routes() -> Vec<RouteWeight> {
    use Route::*;
    [
        (Health, 1),
        (Spellcheck, 4),
        (Anagram, 2),
        (SharedEnum, 2),
        (CustomAdd, 1),
        (CustomExists, 1),
        (CustomRemove, 1),
        (CustomEnum, 1),
    ]
    .into_iter()
    .map(|(name, weight)| RouteWeight { name, weight })
    .collect()
}

impl Config {
    /// Load and validate a config from a JSON file.
    pub fn load(path: &Path) -> Result<Self, String> {
        let text = fs::read_to_string(path)
            .map_err(|e| format!("cannot read config {}: {e}", path.display()))?;
        let config: Config =
            serde_json::from_str(&text).map_err(|e| format!("invalid config JSON: {e}"))?;
        config.validate()?;
        Ok(config)
    }

    /// Validate the configuration, returning a human-readable error on failure.
    pub fn validate(&self) -> Result<(), String> {
        if self.workers == 0 {
            return Err("workers must be at least 1".to_string());
        }
        if self.duration_secs == 0 && self.max_requests == 0 {
            return Err("set duration_secs and/or max_requests (both are 0)".to_string());
        }
        if self.total_weight() == 0 {
            return Err("routes must include at least one positive weight".to_string());
        }
        // Surfaces parse + family-filter errors early.
        if self.select_endpoints()?.is_empty() {
            return Err(format!(
                "no endpoints match ip_version {:?}",
                self.ip_version
            ));
        }
        Ok(())
    }

    /// The sum of all route weights.
    pub fn total_weight(&self) -> u32 {
        self.routes.iter().map(|r| r.weight).sum()
    }

    /// Parse the endpoints and keep those matching [`ip_version`](Config::ip_version).
    pub fn select_endpoints(&self) -> Result<Vec<Endpoint>, String> {
        let mut selected = Vec::new();
        for raw in &self.endpoints {
            let endpoint = Endpoint::parse(raw)?;
            if endpoint.matches(self.ip_version) {
                selected.push(endpoint);
            }
        }
        Ok(selected)
    }
}

/// The transport scheme of an endpoint.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Scheme {
    /// Plain HTTP.
    Http,
    /// HTTP over TLS.
    Https,
}

/// The address family of an endpoint host.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Family {
    /// An IPv4 literal.
    V4,
    /// An IPv6 literal.
    V6,
    /// A hostname (family resolved at connect time).
    Name,
}

/// A parsed base endpoint.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Endpoint {
    /// The transport scheme.
    pub scheme: Scheme,
    /// The host: an IPv4/IPv6 literal (no brackets) or a hostname.
    pub host: String,
    /// The TCP port.
    pub port: u16,
    /// The host's address family.
    pub family: Family,
}

impl Endpoint {
    /// Parse a `scheme://host[:port]` endpoint. IPv6 literals are bracketed
    /// (`http://[::1]:8080`); any path component is ignored (the route supplies
    /// the path). The default port is 80 for `http`, 443 for `https`.
    pub fn parse(raw: &str) -> Result<Self, String> {
        let (scheme, rest) = match raw.split_once("://") {
            Some(("http", rest)) => (Scheme::Http, rest),
            Some(("https", rest)) => (Scheme::Https, rest),
            Some((other, _)) => return Err(format!("unsupported scheme {other:?} in {raw:?}")),
            None => return Err(format!("missing scheme in endpoint {raw:?}")),
        };

        // Strip any path / query so only the authority remains.
        let authority = rest
            .find(['/', '?', '#'])
            .map_or(rest, |end| &rest[..end]);
        if authority.is_empty() {
            return Err(format!("missing host in endpoint {raw:?}"));
        }

        let default_port = match scheme {
            Scheme::Http => 80,
            Scheme::Https => 443,
        };

        let (host, port) = if let Some(after_bracket) = authority.strip_prefix('[') {
            // IPv6 literal: [addr] or [addr]:port.
            let close = after_bracket
                .find(']')
                .ok_or_else(|| format!("unterminated IPv6 literal in {raw:?}"))?;
            let host = &after_bracket[..close];
            let tail = &after_bracket[close + 1..];
            let port = parse_port(tail, default_port, raw)?;
            (host.to_string(), port)
        } else {
            match authority.rsplit_once(':') {
                Some((host, port)) => (host.to_string(), parse_explicit_port(port, raw)?),
                None => (authority.to_string(), default_port),
            }
        };

        if host.is_empty() {
            return Err(format!("empty host in endpoint {raw:?}"));
        }
        let family = classify_host(&host);
        Ok(Endpoint {
            scheme,
            host,
            port,
            family,
        })
    }

    /// Whether this endpoint should be driven under the given filter.
    pub fn matches(&self, want: IpVersion) -> bool {
        match (want, self.family) {
            (IpVersion::Both, _) => true,
            // A hostname can resolve to either family, so it is always eligible.
            (_, Family::Name) => true,
            (IpVersion::V4, Family::V4) => true,
            (IpVersion::V6, Family::V6) => true,
            _ => false,
        }
    }

    /// Whether the scheme is `https`.
    pub fn is_secure(&self) -> bool {
        self.scheme == Scheme::Https
    }
}

/// Classify a host string as an IPv4 literal, IPv6 literal, or hostname.
fn classify_host(host: &str) -> Family {
    if host.parse::<Ipv4Addr>().is_ok() {
        Family::V4
    } else if host.parse::<Ipv6Addr>().is_ok() {
        Family::V6
    } else {
        Family::Name
    }
}

/// Parse a `:port` or empty tail (after an IPv6 literal), defaulting if empty.
fn parse_port(tail: &str, default: u16, raw: &str) -> Result<u16, String> {
    match tail.strip_prefix(':') {
        Some(port) => parse_explicit_port(port, raw),
        None if tail.is_empty() => Ok(default),
        None => Err(format!("expected ':port' after IPv6 literal in {raw:?}")),
    }
}

/// Parse an explicit port number.
fn parse_explicit_port(port: &str, raw: &str) -> Result<u16, String> {
    port.parse::<u16>()
        .map_err(|_| format!("invalid port {port:?} in {raw:?}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_ipv4_endpoint_with_port() {
        let e = Endpoint::parse("http://127.0.0.1:8080").unwrap();
        assert_eq!(e.scheme, Scheme::Http);
        assert_eq!(e.host, "127.0.0.1");
        assert_eq!(e.port, 8080);
        assert_eq!(e.family, Family::V4);
        assert!(!e.is_secure());
    }

    #[test]
    fn parses_ipv6_endpoint_with_port() {
        let e = Endpoint::parse("http://[::1]:8080").unwrap();
        assert_eq!(e.host, "::1");
        assert_eq!(e.port, 8080);
        assert_eq!(e.family, Family::V6);
    }

    #[test]
    fn defaults_ports_by_scheme() {
        assert_eq!(Endpoint::parse("http://host").unwrap().port, 80);
        assert_eq!(Endpoint::parse("https://host").unwrap().port, 443);
        assert!(Endpoint::parse("https://host").unwrap().is_secure());
    }

    #[test]
    fn classifies_hostnames() {
        assert_eq!(Endpoint::parse("http://localhost:9").unwrap().family, Family::Name);
    }

    #[test]
    fn strips_path_and_query() {
        let e = Endpoint::parse("http://10.0.0.1:80/ignored?x=1").unwrap();
        assert_eq!(e.host, "10.0.0.1");
        assert_eq!(e.port, 80);
    }

    #[test]
    fn rejects_bad_endpoints() {
        assert!(Endpoint::parse("ftp://host").is_err());
        assert!(Endpoint::parse("host:80").is_err());
        assert!(Endpoint::parse("http://[::1").is_err());
        assert!(Endpoint::parse("http://host:notaport").is_err());
        assert!(Endpoint::parse("http://").is_err());
    }

    #[test]
    fn ip_version_filter() {
        let v4 = Endpoint::parse("http://127.0.0.1:1").unwrap();
        let v6 = Endpoint::parse("http://[::1]:1").unwrap();
        let name = Endpoint::parse("http://host:1").unwrap();
        assert!(v4.matches(IpVersion::V4));
        assert!(!v4.matches(IpVersion::V6));
        assert!(v6.matches(IpVersion::V6));
        assert!(!v6.matches(IpVersion::V4));
        // Hostnames and `both` are always eligible.
        assert!(name.matches(IpVersion::V4));
        assert!(name.matches(IpVersion::V6));
        assert!(v4.matches(IpVersion::Both));
        assert!(v6.matches(IpVersion::Both));
    }

    #[test]
    fn select_endpoints_applies_filter() {
        let config: Config = serde_json::from_str(
            r#"{
                "endpoints": ["http://127.0.0.1:8080", "http://[::1]:8080"],
                "ip_version": "v6",
                "duration_secs": 1
            }"#,
        )
        .unwrap();
        let selected = config.select_endpoints().unwrap();
        assert_eq!(selected.len(), 1);
        assert_eq!(selected[0].family, Family::V6);
    }

    #[test]
    fn validate_requires_a_bound() {
        let config: Config = serde_json::from_str(
            r#"{ "endpoints": ["http://127.0.0.1:8080"] }"#,
        )
        .unwrap();
        // No duration and no max_requests.
        assert!(config.validate().is_err());
    }

    #[test]
    fn validate_accepts_a_complete_config() {
        let config: Config = serde_json::from_str(
            r#"{
                "endpoints": ["http://127.0.0.1:8080"],
                "duration_secs": 5,
                "workers": 4
            }"#,
        )
        .unwrap();
        assert!(config.validate().is_ok());
        assert!(config.total_weight() > 0);
    }

    #[test]
    fn rejects_unknown_fields() {
        let err = serde_json::from_str::<Config>(
            r#"{ "endpoints": [], "bogus": 1 }"#,
        );
        assert!(err.is_err());
    }
}
