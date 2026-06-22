// Copyright (c) Microsoft Corporation.

//! The minted-handle table (SHIM-D3 / mwin32 D11).
//!
//! Entry points that hand a `HANDLE` / `HKEY` back to a caller mint a value with
//! a reserved bit pattern that cannot collide with a real OS handle or a
//! predefined `HKEY`, then intern the backing state behind that value. The
//! reserved encoding (mwin32 D11) is, for a 32-bit-safe value:
//!
//! - bits 31 and above: clear,
//! - bit 30: set,
//! - bit 29: clear,
//! - bits 2..=28: a sequence number,
//! - bits 0..=1: clear.
//!
//! This keeps minted values unambiguous against predefined `HKEY`s (the
//! `0x8000_000N` range, which has bit 31 set) and against the low-bit values
//! callers commonly stash in handles.
//!
//! Predefined `HKEY` constants are never interned; they resolve directly to a
//! [`WellKnownRoot`] (and from there to a session-vended root path), so a raw
//! value is unambiguously either predefined or a real interned handle.

use std::collections::HashMap;
use std::sync::Mutex;

use windows_platform_isolation::{DirEntry, FilePath, KeyPath, WellKnownRoot};

/// The integer width a Win32 handle is interned and compared as. Win32 handles
/// are pointer-sized but de-facto constrained to 32 bits (they round-trip
/// between 32- and 64-bit processes); the reserved minting pattern stays inside
/// 31 bits, so a `usize` holds every value losslessly.
pub type RawHandle = usize;

// --- Reserved minting pattern (mwin32 D11) ----------------------------------

/// Bit 30: set in every minted value.
const BIT30: RawHandle = 1 << 30;
/// Bit 29: clear in every minted value.
const BIT29: RawHandle = 1 << 29;
/// The two low bits: clear in every minted value.
const LOW_TWO_BITS: RawHandle = 0b11;
/// The sequence number occupies bits 2..=28 (27 bits).
const SEQUENCE_BITS: u32 = 27;
/// Mask selecting a 27-bit sequence number before it is shifted into place.
const SEQUENCE_MASK: RawHandle = (1 << SEQUENCE_BITS) - 1;
/// Bits 0..=1 are reserved (clear), so the sequence is shifted left by two.
const SEQUENCE_SHIFT: u32 = 2;
/// Shift used to test that nothing is set at or above bit 31.
const HIGH_SHIFT: u32 = 31;

/// Whether `value` matches the reserved minted-handle bit pattern.
///
/// This is a pure pattern test on the value; it does not consult any table, so
/// a match means only that the value is in our namespace, not that it is
/// currently live.
#[must_use]
pub fn is_minted_value(value: RawHandle) -> bool {
    (value >> HIGH_SHIFT) == 0
        && (value & BIT30) != 0
        && (value & BIT29) == 0
        && (value & LOW_TWO_BITS) == 0
}

/// Compose a minted handle value from a sequence number, per the reserved
/// encoding.
fn mint_value(sequence: RawHandle) -> RawHandle {
    ((sequence & SEQUENCE_MASK) << SEQUENCE_SHIFT) | BIT30
}

// --- Predefined HKEY resolution ---------------------------------------------

/// The Win32 predefined `HKEY` constants this shim resolves to a
/// [`WellKnownRoot`]. The values mirror the platform `HKEY_*` constants exactly.
const HKEY_CLASSES_ROOT: u32 = 0x8000_0000;
const HKEY_CURRENT_USER: u32 = 0x8000_0001;
const HKEY_LOCAL_MACHINE: u32 = 0x8000_0002;
const HKEY_USERS: u32 = 0x8000_0003;
const HKEY_CURRENT_CONFIG: u32 = 0x8000_0005;

/// Resolve a raw handle value that names a predefined `HKEY` to its
/// [`WellKnownRoot`], or `None` if it is not one of the supported predefined
/// roots.
///
/// Predefined `HKEY`s are `(HKEY)(ULONG_PTR)(LONG)0x8000_000N`: because bit 31
/// is set, the value **sign-extends** on 64-bit (`0x8000_0002` becomes
/// `0xFFFF_FFFF_8000_0002`). This function accepts both the bare 32-bit form and
/// the sign-extended form, and rejects any other upper-bit pattern.
#[must_use]
pub fn predefined_root(value: RawHandle) -> Option<WellKnownRoot> {
    let low = value as u32;
    // Accept only the bare low-32 form or its 64-bit sign extension; any other
    // upper bits mean this is not a predefined HKEY.
    let upper = value >> 32;
    let sign_extended = (low & 0x8000_0000) != 0;
    let upper_ok = upper == 0 || (sign_extended && upper == u32::MAX as RawHandle);
    if !upper_ok {
        return None;
    }
    match low {
        HKEY_CLASSES_ROOT => Some(WellKnownRoot::ClassesRoot),
        HKEY_CURRENT_USER => Some(WellKnownRoot::CurrentUser),
        HKEY_LOCAL_MACHINE => Some(WellKnownRoot::LocalMachine),
        HKEY_USERS => Some(WellKnownRoot::Users),
        HKEY_CURRENT_CONFIG => Some(WellKnownRoot::CurrentConfig),
        _ => None,
    }
}

/// Whether a raw handle value names a predefined `HKEY` (resolvable to a
/// [`WellKnownRoot`]).
#[must_use]
pub fn is_predefined_value(value: RawHandle) -> bool {
    predefined_root(value).is_some()
}

// --- Interned payloads ------------------------------------------------------

/// The state behind a minted file (`mCreateFileW`) handle. A minted file handle
/// resolves to the public path the caller opened (pre-redirection) plus a
/// sequential byte position; later milestones attach the backing isolation file
/// object.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FileHandleState {
    /// The public path the caller passed to `mCreateFileW`.
    pub path: FilePath,
    /// The sequential byte position consulted/advanced by content operations.
    pub position: u64,
}

/// The state behind a minted find-enumeration (`mFindFirstFileW`) handle. A find
/// handle names a position in a directory listing: the entries are captured when
/// the enumeration opens and the cursor advances on each `mFindNextFileW`.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FindEnumerationState {
    /// The buffered directory entries, in enumeration (ordinal) order.
    pub entries: Vec<DirEntry>,
    /// The index of the next entry to yield.
    pub cursor: usize,
}

/// The payload interned behind a minted handle value.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum HandlePayload {
    /// An open registry key, addressed by its resolved absolute path.
    RegistryKey(KeyPath),
    /// An open file handle's state.
    File(FileHandleState),
    /// A directory-enumeration cursor.
    Find(FindEnumerationState),
}

// --- The table --------------------------------------------------------------

struct TableInner {
    next_sequence: RawHandle,
    entries: HashMap<RawHandle, HandlePayload>,
}

/// A table of minted handles and their interned payloads.
///
/// `intern` mints a fresh value carrying a payload; `with` borrows a payload by
/// handle; `close` releases one. Predefined `HKEY`s are never interned here —
/// closing one is a success no-op (matching Win32 `RegCloseKey`).
pub struct HandleTable {
    inner: Mutex<TableInner>,
}

impl HandleTable {
    /// Create an empty handle table.
    #[must_use]
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(TableInner {
                next_sequence: 1,
                entries: HashMap::new(),
            }),
        }
    }

    /// Intern `payload`, returning a freshly minted handle value for it.
    ///
    /// The minted value always satisfies [`is_minted_value`] and never collides
    /// with a live entry already in the table.
    pub fn intern(&self, payload: HandlePayload) -> RawHandle {
        use std::collections::hash_map::Entry;
        let mut inner = self.inner.lock().expect("handle table poisoned");
        loop {
            let sequence = inner.next_sequence;
            inner.next_sequence = inner.next_sequence.wrapping_add(1);
            let value = mint_value(sequence);
            if let Entry::Vacant(slot) = inner.entries.entry(value) {
                slot.insert(payload);
                return value;
            }
            // Extremely unlikely wrap-around collision; keep minting.
        }
    }

    /// Borrow the payload interned behind `value`, invoking `f` with it.
    ///
    /// Returns `None` if `value` is not currently interned (including any
    /// predefined `HKEY`, which is never interned).
    pub fn with<R>(&self, value: RawHandle, f: impl FnOnce(&HandlePayload) -> R) -> Option<R> {
        let inner = self.inner.lock().expect("handle table poisoned");
        inner.entries.get(&value).map(f)
    }

    /// Mutably borrow the payload interned behind `value`, invoking `f` with it.
    ///
    /// Returns `None` if `value` is not currently interned.
    pub fn with_mut<R>(
        &self,
        value: RawHandle,
        f: impl FnOnce(&mut HandlePayload) -> R,
    ) -> Option<R> {
        let mut inner = self.inner.lock().expect("handle table poisoned");
        inner.entries.get_mut(&value).map(f)
    }

    /// Release the entry interned behind `value`.
    ///
    /// Returns `true` if a live entry was released, or if `value` names a
    /// predefined `HKEY` (closing one is a success no-op). Returns `false` if
    /// `value` was neither interned nor predefined.
    pub fn close(&self, value: RawHandle) -> bool {
        if is_predefined_value(value) {
            return true;
        }
        let mut inner = self.inner.lock().expect("handle table poisoned");
        inner.entries.remove(&value).is_some()
    }

    /// The number of live interned entries (diagnostic).
    #[must_use]
    pub fn len(&self) -> usize {
        self.inner.lock().expect("handle table poisoned").entries.len()
    }

    /// Whether the table has no live interned entries.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

impl Default for HandleTable {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_key() -> KeyPath {
        KeyPath::parse("HKEY_LOCAL_MACHINE\\Software")
    }

    #[test]
    fn minted_values_satisfy_reserved_pattern() {
        let table = HandleTable::new();
        for _ in 0..1000 {
            let h = table.intern(HandlePayload::RegistryKey(sample_key()));
            assert!(is_minted_value(h), "minted value {h:#x} failed the pattern");
            // Bit 30 set, bit 29 clear, low two bits clear, nothing above bit 30.
            assert_eq!(h & BIT30, BIT30);
            assert_eq!(h & BIT29, 0);
            assert_eq!(h & LOW_TWO_BITS, 0);
            assert_eq!(h >> HIGH_SHIFT, 0);
        }
    }

    #[test]
    fn minted_values_never_collide_with_predefined_or_low_os_values() {
        // Predefined HKEYs and small OS-style values must never match the
        // minted pattern.
        for predefined in [
            HKEY_CLASSES_ROOT,
            HKEY_CURRENT_USER,
            HKEY_LOCAL_MACHINE,
            HKEY_USERS,
            HKEY_CURRENT_CONFIG,
        ] {
            assert!(!is_minted_value(predefined as RawHandle));
        }
        for low in [0usize, 1, 2, 3, 4, 0x10, 0x1234] {
            assert!(!is_minted_value(low));
        }
    }

    #[test]
    fn intern_deref_round_trips_each_payload_variant() {
        let table = HandleTable::new();

        let key = table.intern(HandlePayload::RegistryKey(sample_key()));
        let file = table.intern(HandlePayload::File(FileHandleState {
            path: FilePath::from_utf8("C:\\dir\\file.txt"),
            position: 42,
        }));
        let find = table.intern(HandlePayload::Find(FindEnumerationState {
            entries: Vec::new(),
            cursor: 7,
        }));

        // Distinct values.
        assert_ne!(key, file);
        assert_ne!(file, find);
        assert_ne!(key, find);

        table
            .with(key, |p| match p {
                HandlePayload::RegistryKey(path) => assert_eq!(path, &sample_key()),
                other => panic!("expected RegistryKey, got {other:?}"),
            })
            .expect("key handle should be live");
        table
            .with(file, |p| match p {
                HandlePayload::File(state) => {
                    assert_eq!(state.position, 42);
                    assert_eq!(state.path, FilePath::from_utf8("C:\\dir\\file.txt"));
                }
                other => panic!("expected File, got {other:?}"),
            })
            .expect("file handle should be live");
        table
            .with(find, |p| match p {
                HandlePayload::Find(state) => {
                    assert_eq!(state.cursor, 7);
                    assert!(state.entries.is_empty());
                }
                other => panic!("expected Find, got {other:?}"),
            })
            .expect("find handle should be live");
    }

    #[test]
    fn close_releases_interned_and_rejects_unknown() {
        let table = HandleTable::new();
        let h = table.intern(HandlePayload::RegistryKey(sample_key()));
        assert_eq!(table.len(), 1);

        assert!(table.close(h));
        assert!(table.is_empty());
        // Closing again finds nothing (no longer interned, not predefined).
        assert!(!table.close(h));
        // An unrelated minted-shaped value that was never interned.
        assert!(!table.close(mint_value(999_999)));
    }

    #[test]
    fn close_on_predefined_is_success_noop() {
        let table = HandleTable::new();
        for predefined in [
            HKEY_LOCAL_MACHINE,
            HKEY_CURRENT_USER,
            HKEY_CLASSES_ROOT,
            HKEY_USERS,
            HKEY_CURRENT_CONFIG,
        ] {
            assert!(table.close(predefined as RawHandle));
        }
    }

    #[test]
    fn with_on_unknown_handle_returns_none() {
        let table = HandleTable::new();
        assert!(table.with(mint_value(123), |_| ()).is_none());
        assert!(table.with(HKEY_LOCAL_MACHINE as RawHandle, |_| ()).is_none());
    }

    #[test]
    fn predefined_root_resolves_bare_and_sign_extended_forms() {
        assert_eq!(
            predefined_root(HKEY_LOCAL_MACHINE as RawHandle),
            Some(WellKnownRoot::LocalMachine)
        );
        assert_eq!(
            predefined_root(HKEY_CURRENT_USER as RawHandle),
            Some(WellKnownRoot::CurrentUser)
        );
        assert_eq!(
            predefined_root(HKEY_CLASSES_ROOT as RawHandle),
            Some(WellKnownRoot::ClassesRoot)
        );
        assert_eq!(
            predefined_root(HKEY_USERS as RawHandle),
            Some(WellKnownRoot::Users)
        );
        assert_eq!(
            predefined_root(HKEY_CURRENT_CONFIG as RawHandle),
            Some(WellKnownRoot::CurrentConfig)
        );

        // Sign-extended 64-bit form of HKEY_LOCAL_MACHINE.
        let sign_extended: RawHandle = 0xFFFF_FFFF_8000_0002;
        assert_eq!(
            predefined_root(sign_extended),
            Some(WellKnownRoot::LocalMachine)
        );
    }

    #[test]
    fn predefined_root_rejects_non_predefined_and_garbled_upper_bits() {
        // Not a predefined constant.
        assert_eq!(predefined_root(0x8000_0099), None);
        // Unsupported predefined values (no WellKnownRoot equivalent).
        assert_eq!(predefined_root(0x8000_0004), None); // HKEY_PERFORMANCE_DATA
        assert_eq!(predefined_root(0x8000_0007), None); // CURRENT_USER_LOCAL_SETTINGS
        // Garbled upper bits (neither zero nor a clean sign extension).
        assert_eq!(predefined_root(0x0000_0001_8000_0002), None);
        // A minted value is not predefined.
        assert_eq!(predefined_root(mint_value(5)), None);
    }
}
