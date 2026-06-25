// Copyright (c) Microsoft Corporation.

//! Workload generation for `talky`: a deterministic PRNG, weighted route
//! selection, and per-route request synthesis. Pure and host-agnostic, so the
//! request shapes are unit-testable without a network.

use serde_json::json;

use crate::config::{Route, RouteWeight};

/// A small, fast, deterministic xorshift PRNG. Reproducible from a seed so a
/// given config produces a repeatable workload.
#[derive(Debug, Clone)]
pub struct Rng {
    state: u64,
}

impl Rng {
    /// Seed the PRNG. A zero seed is nudged to a non-zero state.
    pub fn new(seed: u64) -> Self {
        Rng {
            state: seed ^ 0x9E37_79B9_7F4A_7C15 | 1,
        }
    }

    /// Next pseudo-random `u64` (xorshift64*).
    pub fn next_u64(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.state = x;
        x.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }

    /// A value in `0..n` (`n` must be non-zero).
    pub fn below(&mut self, n: u32) -> u32 {
        (self.next_u64() % u64::from(n)) as u32
    }
}

/// The HTTP method of a synthesized request.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Method {
    /// `GET`.
    Get,
    /// `POST`.
    Post,
    /// `DELETE`.
    Delete,
}

/// A synthesized request: everything except the target endpoint.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LogicalRequest {
    /// The route this request exercises.
    pub route: Route,
    /// The HTTP method.
    pub method: Method,
    /// The origin-relative path (including any query string).
    pub path: String,
    /// The request body, if any (always JSON when present).
    pub body: Option<String>,
}

/// The weighted route mix and the parameters request synthesis needs.
#[derive(Debug, Clone)]
pub struct Workload {
    /// Routes with non-zero weight, paired with their cumulative weight bound.
    cumulative: Vec<(Route, u32)>,
    total_weight: u32,
}

impl Workload {
    /// Build a workload from the configured route weights. Zero-weight routes
    /// are dropped.
    pub fn new(routes: &[RouteWeight]) -> Self {
        let mut cumulative = Vec::new();
        let mut running = 0u32;
        for rw in routes {
            if rw.weight == 0 {
                continue;
            }
            running += rw.weight;
            cumulative.push((rw.name, running));
        }
        Workload {
            cumulative,
            total_weight: running,
        }
    }

    /// Choose a route according to the weights.
    pub fn choose_route(&self, rng: &mut Rng) -> Route {
        let pick = rng.below(self.total_weight);
        for (route, bound) in &self.cumulative {
            if pick < *bound {
                return *route;
            }
        }
        // Unreachable while total_weight > 0; default defensively.
        self.cumulative
            .last()
            .map_or(Route::Health, |(route, _)| *route)
    }

    /// Synthesize the next request: choose a route, then build it.
    pub fn next_request(&self, rng: &mut Rng) -> LogicalRequest {
        let route = self.choose_route(rng);
        build_request(route, rng)
    }
}

/// Build a request for a specific route.
fn build_request(route: Route, rng: &mut Rng) -> LogicalRequest {
    match route {
        Route::Health => LogicalRequest {
            route,
            method: Method::Get,
            path: "/healthz".to_string(),
            body: None,
        },
        Route::Spellcheck => {
            let count = 1 + rng.below(5);
            let words: Vec<String> = (0..count).map(|_| random_word(rng, 3, 8)).collect();
            LogicalRequest {
                route,
                method: Method::Post,
                path: "/spellcheck".to_string(),
                body: Some(json!({ "words": words }).to_string()),
            }
        }
        Route::Anagram => {
            let template = random_template(rng);
            let tray = random_word(rng, 1, 5);
            let wildcards = rng.below(2);
            LogicalRequest {
                route,
                method: Method::Post,
                path: "/anagram".to_string(),
                body: Some(
                    json!({ "template": template, "tray": tray, "wildcards": wildcards })
                        .to_string(),
                ),
            }
        }
        Route::SharedEnum => {
            let pattern = random_template(rng);
            LogicalRequest {
                route,
                method: Method::Get,
                path: format!("/shared?pattern={}", percent_encode(&pattern)),
                body: None,
            }
        }
        Route::CustomAdd => LogicalRequest {
            route,
            method: Method::Post,
            path: format!("/custom/{}", percent_encode(&random_word(rng, 3, 8))),
            body: None,
        },
        Route::CustomExists => LogicalRequest {
            route,
            method: Method::Get,
            path: format!("/custom/{}", percent_encode(&random_word(rng, 3, 8))),
            body: None,
        },
        Route::CustomRemove => LogicalRequest {
            route,
            method: Method::Delete,
            path: format!("/custom/{}", percent_encode(&random_word(rng, 3, 8))),
            body: None,
        },
        Route::CustomEnum => LogicalRequest {
            route,
            method: Method::Get,
            path: "/custom".to_string(),
            body: None,
        },
    }
}

/// A random lowercase word of length `min..=max`.
fn random_word(rng: &mut Rng, min: u32, max: u32) -> String {
    let len = min + rng.below(max - min + 1);
    (0..len)
        .map(|_| (b'a' + rng.below(26) as u8) as char)
        .collect()
}

/// A random anagram / regex template: fixed letters with some `.` blanks.
fn random_template(rng: &mut Rng) -> String {
    let len = 3 + rng.below(3);
    (0..len)
        .map(|_| {
            if rng.below(3) == 0 {
                '.'
            } else {
                (b'a' + rng.below(26) as u8) as char
            }
        })
        .collect()
}

/// Percent-encode characters that are unsafe in a URL path/query component.
/// Only `[A-Za-z0-9._~-]` pass through; everything else is `%XX`.
fn percent_encode(value: &str) -> String {
    let mut out = String::with_capacity(value.len());
    for &byte in value.as_bytes() {
        if byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'~' | b'-') {
            out.push(byte as char);
        } else {
            out.push('%');
            out.push(hex_digit(byte >> 4));
            out.push(hex_digit(byte & 0x0f));
        }
    }
    out
}

/// Upper-case hex digit for a `0..16` nibble.
fn hex_digit(nibble: u8) -> char {
    match nibble {
        0..=9 => (b'0' + nibble) as char,
        _ => (b'A' + (nibble - 10)) as char,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn weights() -> Vec<RouteWeight> {
        [
            (Route::Health, 1),
            (Route::Spellcheck, 1),
            (Route::Anagram, 1),
            (Route::SharedEnum, 1),
            (Route::CustomAdd, 1),
            (Route::CustomExists, 1),
            (Route::CustomRemove, 1),
            (Route::CustomEnum, 1),
        ]
        .into_iter()
        .map(|(name, weight)| RouteWeight { name, weight })
        .collect()
    }

    #[test]
    fn rng_is_deterministic_for_a_seed() {
        let mut a = Rng::new(42);
        let mut b = Rng::new(42);
        for _ in 0..100 {
            assert_eq!(a.next_u64(), b.next_u64());
        }
    }

    #[test]
    fn rng_below_is_in_range() {
        let mut rng = Rng::new(7);
        for _ in 0..1000 {
            assert!(rng.below(10) < 10);
        }
    }

    #[test]
    fn zero_weight_routes_are_dropped() {
        let routes = vec![
            RouteWeight { name: Route::Health, weight: 0 },
            RouteWeight { name: Route::Anagram, weight: 5 },
        ];
        let w = Workload::new(&routes);
        let mut rng = Rng::new(1);
        for _ in 0..50 {
            assert_eq!(w.choose_route(&mut rng), Route::Anagram);
        }
    }

    #[test]
    fn choose_route_covers_all_weighted_routes() {
        let w = Workload::new(&weights());
        let mut rng = Rng::new(123);
        let mut seen = std::collections::HashSet::new();
        for _ in 0..2000 {
            seen.insert(w.choose_route(&mut rng));
        }
        assert_eq!(seen.len(), 8, "every route should be selectable");
    }

    #[test]
    fn health_request_is_well_formed() {
        let r = build_request(Route::Health, &mut Rng::new(1));
        assert_eq!(r.method, Method::Get);
        assert_eq!(r.path, "/healthz");
        assert!(r.body.is_none());
    }

    #[test]
    fn spellcheck_request_has_json_words() {
        let r = build_request(Route::Spellcheck, &mut Rng::new(2));
        assert_eq!(r.method, Method::Post);
        assert_eq!(r.path, "/spellcheck");
        let body = r.body.unwrap();
        let v: serde_json::Value = serde_json::from_str(&body).unwrap();
        assert!(v["words"].as_array().unwrap().iter().all(|w| w.is_string()));
    }

    #[test]
    fn anagram_request_has_template_and_tray() {
        let r = build_request(Route::Anagram, &mut Rng::new(3));
        assert_eq!(r.method, Method::Post);
        let v: serde_json::Value = serde_json::from_str(&r.body.unwrap()).unwrap();
        assert!(v["template"].is_string());
        assert!(v["tray"].is_string());
        assert!(v["wildcards"].is_number());
    }

    #[test]
    fn shared_enum_request_encodes_pattern() {
        let r = build_request(Route::SharedEnum, &mut Rng::new(4));
        assert_eq!(r.method, Method::Get);
        assert!(r.path.starts_with("/shared?pattern="));
        assert!(r.body.is_none());
    }

    #[test]
    fn custom_routes_have_word_path() {
        for (route, method) in [
            (Route::CustomAdd, Method::Post),
            (Route::CustomExists, Method::Get),
            (Route::CustomRemove, Method::Delete),
        ] {
            let r = build_request(route, &mut Rng::new(5));
            assert_eq!(r.method, method);
            assert!(r.path.starts_with("/custom/"));
            assert!(r.path.len() > "/custom/".len());
        }
    }

    #[test]
    fn percent_encode_escapes_unsafe_bytes() {
        assert_eq!(percent_encode("a.b-c"), "a.b-c");
        assert_eq!(percent_encode("a b"), "a%20b");
        assert_eq!(percent_encode("/?#"), "%2F%3F%23");
    }
}
