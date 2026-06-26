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
