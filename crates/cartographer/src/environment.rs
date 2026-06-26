// Copyright (c) Microsoft Corporation.

//! The observed-environment descriptor model (D-CART-4, EM-A1).
//!
//! Where the OpenAPI [`Document`](crate::model::Document) describes the *messages*
//! on a channel, the environment descriptor describes the *participants and
//! channels* around them — the layer OpenAPI omits — so a later phase can drive
//! automated replay and fault injection.
//!
//! Three layers (D-CART-4):
//!
//! - [`Actor`] — a concrete observed participant. Carries the binding evidence
//!   (scheme/host/port + observed counts). The "who we actually saw."
//! - [`Role`] — an abstract part an actor plays; **the substitution unit**. One
//!   actor plays one or more roles; a role is recast independently.
//! - [`Channel`] — a directed role→role edge carrying a [`ContractRef`] to the
//!   OpenAPI document cartographer already synthesizes.
//!
//! Bindings are *subordinate* to roles: a concrete `(scheme, host, port)` is
//! evidence that a participant played a role, never the unit of description. The
//! role id is the stable substitution handle.
//!
//! The shape and its JSON/YAML mapping are owned here (Design Autonomy); `serde`
//! merely realizes them. The `info` block reuses the OpenAPI [`Info`] object.

use std::collections::BTreeMap;

use serde::{Deserialize, Serialize};

use crate::model::Info;

/// The descriptor format version this crate emits.
pub const VERSION: &str = "0.1.0";

/// An ordered map of actor id → [`Actor`]. Sorted by key for deterministic output.
pub type Actors = BTreeMap<String, Actor>;

/// An ordered map of role id → [`Role`]. Sorted by key for deterministic output.
pub type Roles = BTreeMap<String, Role>;

/// An ordered map of channel id → [`Channel`]. Sorted by key for deterministic output.
pub type Channels = BTreeMap<String, Channel>;

/// The root observed-environment descriptor.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Environment {
    /// The descriptor format version string (e.g. `"0.1.0"`).
    pub environment: String,
    /// Descriptor metadata (reuses the OpenAPI `info` object).
    pub info: Info,
    /// Concrete observed participants, keyed by actor id. Omitted when empty.
    #[serde(default, skip_serializing_if = "Actors::is_empty")]
    pub actors: Actors,
    /// Abstract, recastable roles, keyed by role id. Omitted when empty.
    #[serde(default, skip_serializing_if = "Roles::is_empty")]
    pub roles: Roles,
    /// Directed role→role channels, keyed by channel id. Omitted when empty.
    #[serde(default, skip_serializing_if = "Channels::is_empty")]
    pub channels: Channels,
}

impl Environment {
    /// A new descriptor with the given metadata title/version and no participants.
    #[must_use]
    pub fn new(title: impl Into<String>, version: impl Into<String>) -> Self {
        Self {
            environment: VERSION.to_string(),
            info: Info {
                title: title.into(),
                version: version.into(),
                description: None,
            },
            actors: Actors::new(),
            roles: Roles::new(),
            channels: Channels::new(),
        }
    }
}

impl Default for Environment {
    fn default() -> Self {
        Self::new(String::new(), "0.0.0")
    }
}

/// The provenance of a descriptor element (D-CART-4) — how it came to be. This is
/// what makes the hand-tuning feedback loop possible: a `derived` element carries
/// the heuristic `basis`, and because a human `asserted` value is stored
/// distinctly, the diff between "what cartographer would derive fresh" and what an
/// expert kept is always computable.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Provenance {
    /// Which tier produced this element.
    pub tier: ProvenanceTier,
    /// For `derived` elements, the heuristic that produced it (why). Empty otherwise.
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub basis: String,
}

impl Provenance {
    /// An `observed` provenance (an immutable fact).
    #[must_use]
    pub fn observed() -> Self {
        Self {
            tier: ProvenanceTier::Observed,
            basis: String::new(),
        }
    }

    /// A `derived` provenance carrying the heuristic `basis` that produced it.
    #[must_use]
    pub fn derived(basis: impl Into<String>) -> Self {
        Self {
            tier: ProvenanceTier::Derived,
            basis: basis.into(),
        }
    }

    /// An `asserted` (human-authoritative) provenance.
    #[must_use]
    pub fn asserted() -> Self {
        Self {
            tier: ProvenanceTier::Asserted,
            basis: String::new(),
        }
    }

    /// True when this is the default provenance (`derived`, no basis); used to omit
    /// the slot on output.
    #[must_use]
    pub fn is_default(&self) -> bool {
        *self == Self::default()
    }
}

/// Which tier of [`Provenance`] produced an element.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ProvenanceTier {
    /// An immutable observed fact.
    Observed,
    /// cartographer's interpretation (the default for synthesized elements).
    #[default]
    Derived,
    /// A human override; authoritative and preserved across re-synthesis (EM-E1).
    Asserted,
}

/// A concrete observed participant — the evidence layer. Holds the bindings we
/// saw and the roles it plays. One actor may play more than one role.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Actor {
    /// An optional human-facing title.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    /// The role ids this actor plays. Omitted when empty.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub plays: Vec<String>,
    /// Observed concrete bindings attributed to this actor. Omitted when empty.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub bindings: Vec<Binding>,
    /// How this actor came to be (D-CART-4 provenance). Omitted when the default
    /// (`derived`, no basis).
    #[serde(default, skip_serializing_if = "Provenance::is_default")]
    pub provenance: Provenance,
}

/// One observed concrete binding — evidence that an actor was reached at this
/// address. Subordinate to the role; never the unit of description.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Binding {
    /// The URI scheme (e.g. `"http"`, `"https"`), if observed.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub scheme: Option<String>,
    /// The host (no scheme or port), if observed.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub host: Option<String>,
    /// The TCP port, if observed.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub port: Option<u16>,
    /// Observed evidence for this binding (counts, time window).
    #[serde(default, skip_serializing_if = "Observed::is_empty")]
    pub observed: Observed,
}

/// Observed evidence — how many interactions were attributed here and the time
/// window over which they were seen.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Observed {
    /// Number of interactions attributed here.
    pub interactions: u64,
    /// Earliest capture time (ms since the Unix epoch), if known.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub first_ms: Option<u64>,
    /// Latest capture time (ms since the Unix epoch), if known.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_ms: Option<u64>,
}

impl Observed {
    /// True when no evidence has been recorded (used to omit the slot on output).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.interactions == 0 && self.first_ms.is_none() && self.last_ms.is_none()
    }
}

/// An abstract, recastable participant role — **the substitution unit** (D-CART-4).
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Role {
    /// An optional human-facing title.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    /// The parts this role plays in channels (`client` and/or `server`).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub plays: Vec<RolePart>,
    /// Security this role *presents* (as a client), if observed.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub presents: Option<Security>,
    /// Security this role *requires* (as a server), if observed.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub requires: Option<Security>,
    /// Child role ids when this role has been subdivided; the parent is the union
    /// of its children (additive refinement, D-CART-4). Omitted when none.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub children: Vec<String>,
    /// How this role came to be (D-CART-4 provenance). Omitted when the default
    /// (`derived`, no basis).
    #[serde(default, skip_serializing_if = "Provenance::is_default")]
    pub provenance: Provenance,
}

/// The part a role plays in a channel.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RolePart {
    /// Initiates requests.
    Client,
    /// Responds to requests.
    Server,
}

/// Security a role presents or requires, as observed. A minimal, OpenAPI-aligned
/// shape: a classified scheme plus the header/parameter names that carried the
/// credential. Credential *values* are never captured (names only).
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Security {
    /// The scheme kind (e.g. `"apiKey"`, `"http"`, `"mutualTLS"`), if classified.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kind: Option<String>,
    /// Where the credential was carried (e.g. `"header"`, `"query"`).
    #[serde(default, rename = "in", skip_serializing_if = "Option::is_none")]
    pub location: Option<String>,
    /// The header/parameter names that carried identity (values never captured).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub names: Vec<String>,
}

/// A directed role→role channel carrying a message contract.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Channel {
    /// The initiating (client) role id.
    pub from: String,
    /// The responding (server) role id.
    pub to: String,
    /// The wire protocol (e.g. `"http/1.1"`, `"http/1.1+tls"`), if known.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub protocol: Option<String>,
    /// A reference to the message contract (the synthesized OpenAPI document).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub contract: Option<ContractRef>,
    /// Transport-level facts observed on the channel.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub transport: Option<Transport>,
    /// Observed evidence (counts, time window).
    #[serde(default, skip_serializing_if = "Observed::is_empty")]
    pub observed: Observed,
    /// How this channel came to be (D-CART-4 provenance). Omitted when the default
    /// (`derived`, no basis).
    #[serde(default, skip_serializing_if = "Provenance::is_default")]
    pub provenance: Provenance,
}

/// A reference to a message-contract document (an OpenAPI spec).
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct ContractRef {
    /// The reference (a relative path or URI to the OpenAPI document).
    #[serde(rename = "$ref")]
    pub reference: String,
}

/// Transport-level facts observed on a channel.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Transport {
    /// Whether TLS was observed on the channel, if known.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tls: Option<bool>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::format::{SpecFormat, parse_environment, serialize_environment};

    /// A representative descriptor exercising every layer, provenance tier, and
    /// the `$ref` contract link.
    fn sample() -> Environment {
        let mut env = Environment::new("wordy — observed environment", "2026-06-26");
        env.info.description = Some("Synthesized from observed interactions.".to_string());

        env.actors.insert(
            "proc-wordy".to_string(),
            Actor {
                title: Some("wordy service process".to_string()),
                plays: vec!["srv-wordy".to_string()],
                bindings: vec![Binding {
                    scheme: Some("https".to_string()),
                    host: Some("wordy.internal".to_string()),
                    port: Some(443),
                    observed: Observed {
                        interactions: 11,
                        first_ms: Some(1_700_000_000_000),
                        last_ms: Some(1_700_000_500_000),
                    },
                }],
                provenance: Provenance::observed(),
            },
        );

        env.roles.insert(
            "srv-wordy".to_string(),
            Role {
                title: Some("wordy server".to_string()),
                plays: vec![RolePart::Server],
                presents: None,
                requires: Some(Security {
                    kind: Some("apiKey".to_string()),
                    location: Some("header".to_string()),
                    names: vec!["X-Wordy-User".to_string(), "X-Wordy-Locale".to_string()],
                }),
                children: Vec::new(),
                provenance: Provenance::derived("single observed inbound seam"),
            },
        );
        env.roles.insert(
            "wordy-client".to_string(),
            Role {
                title: None,
                plays: vec![RolePart::Client],
                presents: Some(Security {
                    kind: Some("apiKey".to_string()),
                    location: Some("header".to_string()),
                    names: vec!["X-Wordy-User".to_string()],
                }),
                requires: None,
                children: vec!["wordy-client-spellcheck".to_string()],
                provenance: Provenance::asserted(),
            },
        );

        env.channels.insert(
            "wordy-client->srv-wordy".to_string(),
            Channel {
                from: "wordy-client".to_string(),
                to: "srv-wordy".to_string(),
                protocol: Some("http/1.1+tls".to_string()),
                contract: Some(ContractRef {
                    reference: "./wordy-openapi.yaml".to_string(),
                }),
                transport: Some(Transport { tls: Some(true) }),
                observed: Observed {
                    interactions: 11,
                    first_ms: None,
                    last_ms: None,
                },
                provenance: Provenance::derived("single observed inbound seam"),
            },
        );
        env
    }

    fn round_trip(env: &Environment, format: SpecFormat) -> Environment {
        let text = serialize_environment(env, format).expect("serialize");
        parse_environment(&text, format).expect("parse")
    }

    #[test]
    fn sample_round_trips_yaml() {
        let env = sample();
        assert_eq!(round_trip(&env, SpecFormat::Yaml), env);
    }

    #[test]
    fn sample_round_trips_json() {
        let env = sample();
        assert_eq!(round_trip(&env, SpecFormat::Json), env);
    }

    #[test]
    fn yaml_and_json_parse_to_the_same_value() {
        let env = sample();
        assert_eq!(round_trip(&env, SpecFormat::Yaml), round_trip(&env, SpecFormat::Json));
    }

    #[test]
    fn empty_environment_round_trips_both_formats() {
        let env = Environment::new("empty", "0.0.0");
        for format in [SpecFormat::Yaml, SpecFormat::Json] {
            assert_eq!(round_trip(&env, format), env);
        }
    }

    #[test]
    fn contract_ref_serializes_as_dollar_ref() {
        let json = serialize_environment(&sample(), SpecFormat::Json).unwrap();
        assert!(json.contains("\"$ref\": \"./wordy-openapi.yaml\""));
    }

    #[test]
    fn contract_ref_survives_round_trip() {
        let back = round_trip(&sample(), SpecFormat::Yaml);
        assert_eq!(
            back.channels["wordy-client->srv-wordy"].contract,
            Some(ContractRef {
                reference: "./wordy-openapi.yaml".to_string(),
            })
        );
    }

    #[test]
    fn role_part_serializes_snake_case() {
        let mut env = Environment::new("t", "1");
        env.roles.insert(
            "r".to_string(),
            Role {
                plays: vec![RolePart::Server, RolePart::Client],
                ..Default::default()
            },
        );
        let yaml = serialize_environment(&env, SpecFormat::Yaml).unwrap();
        assert!(yaml.contains("server"));
        assert!(yaml.contains("client"));
        assert!(!yaml.contains("Server"));
        assert!(!yaml.contains("Client"));
    }

    #[test]
    fn security_in_field_is_renamed() {
        let yaml = serialize_environment(&sample(), SpecFormat::Yaml).unwrap();
        assert!(yaml.contains("in: header"));
        assert!(!yaml.contains("location:"));
    }

    #[test]
    fn observed_provenance_survives() {
        let back = round_trip(&sample(), SpecFormat::Json);
        assert_eq!(back.actors["proc-wordy"].provenance.tier, ProvenanceTier::Observed);
    }

    #[test]
    fn asserted_provenance_survives() {
        let back = round_trip(&sample(), SpecFormat::Yaml);
        assert_eq!(
            back.roles["wordy-client"].provenance.tier,
            ProvenanceTier::Asserted
        );
    }

    #[test]
    fn derived_basis_survives() {
        let back = round_trip(&sample(), SpecFormat::Json);
        assert_eq!(
            back.channels["wordy-client->srv-wordy"].provenance,
            Provenance::derived("single observed inbound seam")
        );
    }

    #[test]
    fn default_provenance_is_omitted_but_round_trips() {
        let mut env = Environment::new("t", "1");
        env.roles.insert(
            "r".to_string(),
            Role {
                plays: vec![RolePart::Server],
                provenance: Provenance::default(),
                ..Default::default()
            },
        );
        let yaml = serialize_environment(&env, SpecFormat::Yaml).unwrap();
        assert!(!yaml.contains("provenance"));
        assert_eq!(parse_environment(&yaml, SpecFormat::Yaml).unwrap(), env);
    }

    #[test]
    fn empty_observed_is_omitted() {
        let mut env = Environment::new("t", "1");
        env.channels.insert(
            "c".to_string(),
            Channel {
                from: "a".to_string(),
                to: "b".to_string(),
                ..Default::default()
            },
        );
        let yaml = serialize_environment(&env, SpecFormat::Yaml).unwrap();
        assert!(!yaml.contains("observed"));
    }

    #[test]
    fn observed_counts_survive() {
        let mut env = Environment::new("t", "1");
        env.actors.insert(
            "a".to_string(),
            Actor {
                bindings: vec![Binding {
                    observed: Observed {
                        interactions: 7,
                        first_ms: Some(1),
                        last_ms: Some(2),
                    },
                    ..Default::default()
                }],
                ..Default::default()
            },
        );
        assert_eq!(round_trip(&env, SpecFormat::Json), env);
    }

    #[test]
    fn children_survive() {
        let back = round_trip(&sample(), SpecFormat::Yaml);
        assert_eq!(
            back.roles["wordy-client"].children,
            vec!["wordy-client-spellcheck".to_string()]
        );
    }

    #[test]
    fn unknown_fields_are_ignored() {
        // Forward compatibility: a descriptor carrying fields this version does not
        // model still loads (the extra fields are simply dropped).
        let yaml = "environment: 0.1.0\ninfo:\n  title: t\n  version: '1'\nfuture_field: 42\n";
        let env = parse_environment(yaml, SpecFormat::Yaml).expect("parse");
        assert_eq!(env.info.title, "t");
    }
}

