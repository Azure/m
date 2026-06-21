// Copyright (c) Microsoft Corporation.

//! The reified operation model and the [`Surface`] seam (D10).
//!
//! Every registry operation is a value — a [`Request`] — and every result is a
//! [`Response`]. Providers and decorators all speak this single vocabulary
//! through one object-safe method, [`Surface::invoke`]. This is the seam where
//! the cross-cutting layers (logging, journaling, fault injection) are written
//! once, surface-agnostically: the journaling verb stream *is* the `Request`
//! enum (D4), and fault injection is a `match` on `Request`.
//!
//! [`TreeSurface`] is the leaf provider for the first cut (D15): it lowers each
//! `Request` onto an in-memory [`OverlayTree`].

use crate::error::Result;
use crate::path::KeyPath;
use crate::tree::{OverlayTree, ValueData};
use crate::wstr::{OrdinalCasing, Utf16};

/// A registry operation, modeled as data (D10). This is also the journaling
/// verb stream (D4).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Request {
    /// Test whether a key exists.
    KeyExists { path: KeyPath },
    /// Create a key (and any missing ancestors).
    CreateKey { path: KeyPath },
    /// Delete a key and its subtree.
    DeleteKey { path: KeyPath },
    /// Read a value.
    ReadValue { path: KeyPath, name: Utf16 },
    /// Set (or replace) a value.
    WriteValue {
        path: KeyPath,
        name: Utf16,
        data: ValueData,
    },
    /// Delete a value.
    DeleteValue { path: KeyPath, name: Utf16 },
    /// Enumerate immediate subkey names.
    EnumKeys { path: KeyPath },
    /// Enumerate values.
    EnumValues { path: KeyPath },
}

/// The result of invoking a [`Request`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Response {
    /// No payload (a successful mutation).
    Unit,
    /// A key-existence answer.
    Exists(bool),
    /// A single value.
    Value(ValueData),
    /// A list of subkey names (ordinal-ordered).
    Names(Vec<Utf16>),
    /// A list of values (ordinal-ordered by name).
    Values(Vec<(Utf16, ValueData)>),
}

/// The decorator/provider seam (D10): one object-safe method through which all
/// registry operations flow. Logging, journaling, and fault injection are
/// implemented once as `Surface` wrappers, regardless of the underlying surface.
pub trait Surface {
    /// Execute a request and produce a response.
    fn invoke(&mut self, req: &Request) -> Result<Response>;
}

/// The leaf provider: lowers requests onto an in-memory [`OverlayTree`] (D15).
pub struct TreeSurface<C: OrdinalCasing> {
    tree: OverlayTree<C>,
}

impl<C: OrdinalCasing> TreeSurface<C> {
    /// Wrap an overlay tree as a surface.
    pub fn new(tree: OverlayTree<C>) -> Self {
        Self { tree }
    }

    /// The underlying tree (for inspection / proving copy-on-write isolation).
    #[must_use]
    pub fn tree(&self) -> &OverlayTree<C> {
        &self.tree
    }
}

impl<C: OrdinalCasing> Surface for TreeSurface<C> {
    fn invoke(&mut self, req: &Request) -> Result<Response> {
        match req {
            Request::KeyExists { path } => Ok(Response::Exists(self.tree.key_exists(path))),
            Request::CreateKey { path } => {
                self.tree.create_key(path);
                Ok(Response::Unit)
            }
            Request::DeleteKey { path } => {
                self.tree.delete_key(path);
                Ok(Response::Unit)
            }
            Request::ReadValue { path, name } => {
                self.tree.get_value(path, name).map(Response::Value)
            }
            Request::WriteValue { path, name, data } => {
                self.tree.set_value(path, name.clone(), data.clone());
                Ok(Response::Unit)
            }
            Request::DeleteValue { path, name } => {
                self.tree.delete_value(path, name);
                Ok(Response::Unit)
            }
            Request::EnumKeys { path } => self.tree.enum_subkeys(path).map(Response::Names),
            Request::EnumValues { path } => self.tree.enum_values(path).map(Response::Values),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::error::RegistryError;
    use crate::tree::Hive;
    use crate::wstr::AsciiOrdinalCasing;

    fn w(s: &str) -> Utf16 {
        Utf16::from_utf8(s)
    }

    fn surface() -> TreeSurface<AsciiOrdinalCasing> {
        let c = AsciiOrdinalCasing;
        let mut base = Hive::new();
        base.insert_value(
            &c,
            &KeyPath::parse("Software\\App"),
            w("Name"),
            ValueData::String(w("base")),
        );
        TreeSurface::new(OverlayTree::new(c, base))
    }

    #[test]
    fn read_value_request() {
        let mut s = surface();
        let resp = s.invoke(&Request::ReadValue {
            path: KeyPath::parse("Software\\App"),
            name: w("Name"),
        });
        assert_eq!(resp, Ok(Response::Value(ValueData::String(w("base")))));
    }

    #[test]
    fn write_then_read_request() {
        let mut s = surface();
        let app = KeyPath::parse("Software\\App");
        assert_eq!(
            s.invoke(&Request::WriteValue {
                path: app.clone(),
                name: w("Name"),
                data: ValueData::Dword(9),
            }),
            Ok(Response::Unit)
        );
        assert_eq!(
            s.invoke(&Request::ReadValue {
                path: app,
                name: w("Name"),
            }),
            Ok(Response::Value(ValueData::Dword(9)))
        );
    }

    #[test]
    fn key_exists_request() {
        let mut s = surface();
        assert_eq!(
            s.invoke(&Request::KeyExists {
                path: KeyPath::parse("Software\\App"),
            }),
            Ok(Response::Exists(true))
        );
        assert_eq!(
            s.invoke(&Request::KeyExists {
                path: KeyPath::parse("Software\\Nope"),
            }),
            Ok(Response::Exists(false))
        );
    }

    #[test]
    fn create_and_delete_key_requests() {
        let mut s = surface();
        let k = KeyPath::parse("Software\\New");
        assert_eq!(
            s.invoke(&Request::CreateKey { path: k.clone() }),
            Ok(Response::Unit)
        );
        assert_eq!(
            s.invoke(&Request::KeyExists { path: k.clone() }),
            Ok(Response::Exists(true))
        );
        assert_eq!(
            s.invoke(&Request::DeleteKey { path: k.clone() }),
            Ok(Response::Unit)
        );
        assert_eq!(
            s.invoke(&Request::KeyExists { path: k }),
            Ok(Response::Exists(false))
        );
    }

    #[test]
    fn delete_value_request() {
        let mut s = surface();
        let app = KeyPath::parse("Software\\App");
        assert_eq!(
            s.invoke(&Request::DeleteValue {
                path: app.clone(),
                name: w("Name"),
            }),
            Ok(Response::Unit)
        );
        assert_eq!(
            s.invoke(&Request::ReadValue {
                path: app,
                name: w("Name"),
            }),
            Err(RegistryError::ValueNotFound)
        );
    }

    #[test]
    fn enum_keys_and_values_requests() {
        let mut s = surface();
        let app = KeyPath::parse("Software\\App");
        s.invoke(&Request::WriteValue {
            path: app.clone(),
            name: w("Extra"),
            data: ValueData::Dword(1),
        })
        .unwrap();

        let names = s.invoke(&Request::EnumKeys {
            path: KeyPath::parse("Software"),
        });
        assert_eq!(names, Ok(Response::Names(vec![w("App")])));

        match s
            .invoke(&Request::EnumValues { path: app })
            .unwrap()
        {
            Response::Values(v) => {
                let got: Vec<String> = v.iter().map(|(n, _)| n.to_utf8().unwrap()).collect();
                assert_eq!(got, vec!["Extra".to_string(), "Name".to_string()]);
            }
            other => panic!("expected Values, got {other:?}"),
        }
    }

    #[test]
    fn read_missing_key_propagates_error() {
        let mut s = surface();
        assert_eq!(
            s.invoke(&Request::ReadValue {
                path: KeyPath::parse("Nope"),
                name: w("X"),
            }),
            Err(RegistryError::KeyNotFound)
        );
    }
}
