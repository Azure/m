// Copyright (c) Microsoft Corporation.

//! The Win32 COM-activation C ABI (`mCoCreateInstance` / `mCoCreateInstanceEx`
//! / `mCoGetClassObject` + passthrough lifecycle) — MW10.
//!
//! These entry points mirror the ole32 activation prototypes so the shim is a
//! drop-in for the COM half of the C++ `mwin32` surface. Each body does only the
//! ABI marshaling: decode the caller's `CLSID` / `IID`, ask the safe
//! [`ComState`](crate::com::ComState) policy how the call should behave
//! (SHIM-D17), and either vend a shim-supplied COM object or forward to the real
//! ole32 activation. The substitution / observation logic lives in
//! [`crate::com`]; this module holds no policy.
//!
//! Per SHIM-D2 this is the only place raw caller pointers, raw `GUID` / `HRESULT`
//! values, and the hand-rolled COM vtables are touched, so the module opts back
//! into `unsafe_code` (the crate root denies it). The decision to hand-roll the
//! `IUnknown` / `IClassFactory` vtables with raw `windows-sys` types — rather
//! than pull the heavier `windows` COM macro stack into this crate — keeps the
//! dependency story uniform with the rest of the shim (Design Autonomy,
//! SHIM-D17).
//!
//! The two FFI-boundary clippy lints are allowed module-wide for the same
//! reasons as [`crate::mwinload`]:
//! - `not_unsafe_ptr_arg_deref`: every entry point is a C export, not a Rust API;
//! - `too_many_arguments`: the argument lists are fixed by the Win32 prototypes.
//!
//! ## Transparency (SHIM-D17)
//!
//! Any activation whose `CLSID` has no registered shim factory is forwarded
//! untouched — the caller's verbatim arguments reach the real ole32 entry point,
//! and in [`ComMode::Off`](crate::com::ComMode::Off) the call is a pure
//! passthrough that records nothing. A substitute object is a real, refcounted
//! COM object the caller releases through the standard `IUnknown` contract.

#![allow(unsafe_code)]
#![allow(clippy::not_unsafe_ptr_arg_deref, clippy::too_many_arguments)]

use core::ffi::c_void;
use core::ptr::null_mut;
use core::sync::atomic::{AtomicU32, Ordering, fence};

use windows_sys::Win32::Foundation::{BOOL, E_INVALIDARG, E_NOINTERFACE, E_POINTER, S_OK};
use windows_sys::Win32::System::Com::{
    CLSCTX, COSERVERINFO, CoCreateInstance, CoCreateInstanceEx, CoGetClassObject, MULTI_QI,
};
use windows_sys::core::{GUID, HRESULT};

use crate::com::{ActivationDisposition, ActivationExDisposition, ClassObjectDisposition, Guid};
use crate::session::session;

/// `CLASS_E_NOAGGREGATION` — the COM error a class object returns when asked to
/// aggregate but it does not support aggregation. Fixed by COM (`0x8004_0110`);
/// `windows-sys` 0.59 does not surface it, so it is named here. Changing the
/// value is meaningless, not a breaking change of ours.
const CLASS_E_NOAGGREGATION: HRESULT = 0x8004_0110_u32 as HRESULT;

/// `CO_S_NOTALLINTERFACES` — the COM success code a multi-`QueryInterface`
/// activation returns when some, but not all, requested interfaces were
/// produced. Fixed by COM (`0x0008_0012`); `windows-sys` 0.59 does not surface
/// it, so it is named here. Changing the value is meaningless, not a breaking
/// change of ours.
const CO_S_NOTALLINTERFACES: HRESULT = 0x0008_0012;

/// `IID_IUnknown` — the base COM interface. Fixed by COM; changing it is
/// meaningless, not a breaking change of ours.
const IID_IUNKNOWN: Guid = Guid::new(0x0000_0000, 0x0000, 0x0000, [0xc0, 0, 0, 0, 0, 0, 0, 0x46]);
/// `IID_IClassFactory` — the class-object interface `CoGetClassObject` returns.
/// Fixed by COM.
const IID_ICLASSFACTORY: Guid =
    Guid::new(0x0000_0001, 0x0000, 0x0000, [0xc0, 0, 0, 0, 0, 0, 0, 0x46]);

/// Decode a raw `windows-sys` `GUID` into the policy module's [`Guid`].
fn to_guid(g: &GUID) -> Guid {
    Guid::new(g.data1, g.data2, g.data3, g.data4)
}

// --- Hand-rolled COM vtables (SHIM-D17) -------------------------------------

/// The `IUnknown` vtable layout (`QueryInterface` / `AddRef` / `Release`), the
/// COM-mandated prefix of every interface vtable.
#[repr(C)]
struct IUnknownVtbl {
    query_interface:
        unsafe extern "system" fn(*mut c_void, *const GUID, *mut *mut c_void) -> HRESULT,
    add_ref: unsafe extern "system" fn(*mut c_void) -> u32,
    release: unsafe extern "system" fn(*mut c_void) -> u32,
}

/// The `IClassFactory` vtable layout: the `IUnknown` prefix plus
/// `CreateInstance` / `LockServer`.
#[repr(C)]
struct IClassFactoryVtbl {
    base: IUnknownVtbl,
    create_instance: unsafe extern "system" fn(
        *mut c_void,
        *mut c_void,
        *const GUID,
        *mut *mut c_void,
    ) -> HRESULT,
    lock_server: unsafe extern "system" fn(*mut c_void, BOOL) -> HRESULT,
}

// --- Shim object (a substitute COM instance) --------------------------------

/// A shim-supplied COM object: an `IUnknown`-shaped, heap-allocated, refcounted
/// object that claims to implement one interface `iid` (besides `IUnknown`).
/// `#[repr(C)]` with the vtable pointer first is the COM object layout; the
/// trailing fields are private shim state the COM client never touches.
#[repr(C)]
struct ShimObject {
    /// The COM client dereferences this to dispatch; it is never read by Rust,
    /// only by the ABI client through the vtable contract.
    #[allow(dead_code)]
    vtable: *const IUnknownVtbl,
    refcount: AtomicU32,
    iid: Guid,
}

/// The single shared `IUnknown` vtable every [`ShimObject`] points at.
static SHIM_OBJECT_VTBL: IUnknownVtbl = IUnknownVtbl {
    query_interface: object_query_interface,
    add_ref: object_add_ref,
    release: object_release,
};

/// `IUnknown::QueryInterface` for a [`ShimObject`]: succeeds for `IUnknown` and
/// the object's declared `iid` (returning the same pointer with a bumped
/// refcount), else `E_NOINTERFACE`.
unsafe extern "system" fn object_query_interface(
    this: *mut c_void,
    riid: *const GUID,
    ppv: *mut *mut c_void,
) -> HRESULT {
    if ppv.is_null() {
        return E_POINTER;
    }
    if riid.is_null() {
        // SAFETY: ppv is non-null (checked above).
        unsafe { *ppv = null_mut() };
        return E_POINTER;
    }
    // SAFETY: riid is a non-null caller GUID; this points at a live ShimObject.
    let want = to_guid(unsafe { &*riid });
    let obj = this.cast::<ShimObject>();
    // SAFETY: this is a live ShimObject pointer minted by mint_shim_object.
    let iid = unsafe { (*obj).iid };
    if want == IID_IUNKNOWN || want == iid {
        // SAFETY: obj is live; bump the refcount and hand back the same pointer.
        unsafe {
            (*obj).refcount.fetch_add(1, Ordering::Relaxed);
            *ppv = this;
        }
        S_OK
    } else {
        // SAFETY: ppv is non-null.
        unsafe { *ppv = null_mut() };
        E_NOINTERFACE
    }
}

/// `IUnknown::AddRef` for a [`ShimObject`].
unsafe extern "system" fn object_add_ref(this: *mut c_void) -> u32 {
    let obj = this.cast::<ShimObject>();
    // SAFETY: this is a live ShimObject pointer.
    unsafe { (*obj).refcount.fetch_add(1, Ordering::Relaxed) + 1 }
}

/// `IUnknown::Release` for a [`ShimObject`]; reclaims the heap allocation when
/// the last reference is dropped (the canonical `Arc`-style ordering).
unsafe extern "system" fn object_release(this: *mut c_void) -> u32 {
    let obj = this.cast::<ShimObject>();
    // SAFETY: this is a live ShimObject pointer.
    let prev = unsafe { (*obj).refcount.fetch_sub(1, Ordering::Release) };
    if prev == 1 {
        fence(Ordering::Acquire);
        // SAFETY: the last reference is gone; reclaim the Box minted in
        // mint_shim_object and drop it exactly once.
        drop(unsafe { Box::from_raw(obj) });
        0
    } else {
        prev - 1
    }
}

/// Mint a shim COM object claiming interface `iid`, returning a refcounted
/// (count = 1) `IUnknown`-shaped pointer the caller releases through the COM
/// contract. The pointer is a real heap object — not a SHIM-D3 sentinel — so it
/// satisfies `QueryInterface` / `AddRef` / `Release` like any COM object.
#[must_use]
pub fn mint_shim_object(iid: Guid) -> *mut c_void {
    let obj = Box::new(ShimObject {
        vtable: &SHIM_OBJECT_VTBL,
        refcount: AtomicU32::new(1),
        iid,
    });
    Box::into_raw(obj).cast::<c_void>()
}

// --- Shim class factory -----------------------------------------------------

/// A shim-supplied `IClassFactory`: a refcounted COM object whose
/// `CreateInstance` vends a [`ShimObject`] for any IID in its `supported`
/// snapshot (captured from the safe registry when the factory was handed out).
#[repr(C)]
struct ShimFactory {
    /// Read by the COM client through the vtable contract, never by Rust.
    #[allow(dead_code)]
    vtable: *const IClassFactoryVtbl,
    refcount: AtomicU32,
    supported: Vec<Guid>,
}

/// The single shared `IClassFactory` vtable every [`ShimFactory`] points at.
static SHIM_FACTORY_VTBL: IClassFactoryVtbl = IClassFactoryVtbl {
    base: IUnknownVtbl {
        query_interface: factory_query_interface,
        add_ref: factory_add_ref,
        release: factory_release,
    },
    create_instance: factory_create_instance,
    lock_server: factory_lock_server,
};

/// `IUnknown::QueryInterface` for a [`ShimFactory`]: succeeds for `IUnknown` and
/// `IClassFactory`, else `E_NOINTERFACE`.
unsafe extern "system" fn factory_query_interface(
    this: *mut c_void,
    riid: *const GUID,
    ppv: *mut *mut c_void,
) -> HRESULT {
    if ppv.is_null() {
        return E_POINTER;
    }
    if riid.is_null() {
        // SAFETY: ppv is non-null.
        unsafe { *ppv = null_mut() };
        return E_POINTER;
    }
    // SAFETY: riid is a non-null caller GUID.
    let want = to_guid(unsafe { &*riid });
    if want == IID_IUNKNOWN || want == IID_ICLASSFACTORY {
        let factory = this.cast::<ShimFactory>();
        // SAFETY: this is a live ShimFactory pointer.
        unsafe {
            (*factory).refcount.fetch_add(1, Ordering::Relaxed);
            *ppv = this;
        }
        S_OK
    } else {
        // SAFETY: ppv is non-null.
        unsafe { *ppv = null_mut() };
        E_NOINTERFACE
    }
}

/// `IUnknown::AddRef` for a [`ShimFactory`].
unsafe extern "system" fn factory_add_ref(this: *mut c_void) -> u32 {
    let factory = this.cast::<ShimFactory>();
    // SAFETY: this is a live ShimFactory pointer.
    unsafe { (*factory).refcount.fetch_add(1, Ordering::Relaxed) + 1 }
}

/// `IUnknown::Release` for a [`ShimFactory`]; reclaims the allocation (and its
/// `supported` vector) at the last reference.
unsafe extern "system" fn factory_release(this: *mut c_void) -> u32 {
    let factory = this.cast::<ShimFactory>();
    // SAFETY: this is a live ShimFactory pointer.
    let prev = unsafe { (*factory).refcount.fetch_sub(1, Ordering::Release) };
    if prev == 1 {
        fence(Ordering::Acquire);
        // SAFETY: last reference gone; reclaim and drop the Box exactly once.
        drop(unsafe { Box::from_raw(factory) });
        0
    } else {
        prev - 1
    }
}

/// `IClassFactory::CreateInstance`: vend a [`ShimObject`] for a supported IID.
/// Aggregation is unsupported (`CLASS_E_NOAGGREGATION` when `punk_outer` is
/// non-null); an unsupported IID yields `E_NOINTERFACE`.
unsafe extern "system" fn factory_create_instance(
    this: *mut c_void,
    punk_outer: *mut c_void,
    riid: *const GUID,
    ppv: *mut *mut c_void,
) -> HRESULT {
    if ppv.is_null() {
        return E_POINTER;
    }
    // SAFETY: ppv is non-null.
    unsafe { *ppv = null_mut() };
    if !punk_outer.is_null() {
        return CLASS_E_NOAGGREGATION;
    }
    if riid.is_null() {
        return E_POINTER;
    }
    // SAFETY: riid is a non-null caller GUID.
    let want = to_guid(unsafe { &*riid });
    let factory = this.cast::<ShimFactory>();
    // SAFETY: this is a live ShimFactory pointer.
    let supported = unsafe { &(*factory).supported };
    if want == IID_IUNKNOWN || supported.contains(&want) {
        let obj = mint_shim_object(want);
        // SAFETY: ppv is non-null.
        unsafe { *ppv = obj };
        S_OK
    } else {
        E_NOINTERFACE
    }
}

/// `IClassFactory::LockServer`: a no-op for an in-process shim factory.
unsafe extern "system" fn factory_lock_server(_this: *mut c_void, _flock: BOOL) -> HRESULT {
    S_OK
}

/// Mint a shim class factory that can create objects for `supported` IIDs,
/// returning a refcounted (count = 1) `IClassFactory`-shaped pointer.
#[must_use]
pub fn mint_shim_factory(supported: Vec<Guid>) -> *mut c_void {
    let factory = Box::new(ShimFactory {
        vtable: &SHIM_FACTORY_VTBL,
        refcount: AtomicU32::new(1),
        supported,
    });
    Box::into_raw(factory).cast::<c_void>()
}

// --- COM activation exports (SHIM-D17) --------------------------------------

/// `mCoCreateInstance` — the single-interface COM activation shim. Off mode and
/// any unregistered CLSID forward verbatim to the real ole32 `CoCreateInstance`;
/// in substitute mode a registered factory vends a shim object (or fails with
/// `E_NOINTERFACE` when it cannot supply the requested interface).
#[unsafe(no_mangle)]
pub extern "system" fn mCoCreateInstance(
    rclsid: *const GUID,
    punkouter: *mut c_void,
    dwclscontext: CLSCTX,
    riid: *const GUID,
    ppv: *mut *mut c_void,
) -> HRESULT {
    if ppv.is_null() {
        return E_POINTER;
    }
    // SAFETY: ppv is non-null (checked above).
    unsafe { *ppv = null_mut() };
    if rclsid.is_null() || riid.is_null() {
        return E_POINTER;
    }
    // SAFETY: rclsid / riid are non-null caller GUIDs.
    let clsid = to_guid(unsafe { &*rclsid });
    // SAFETY: riid is a non-null caller GUID.
    let iid = to_guid(unsafe { &*riid });
    match session().with_com(|com| com.on_create_instance(clsid, iid, dwclscontext)) {
        ActivationDisposition::Substitute(want) => {
            // SAFETY: ppv is non-null.
            unsafe { *ppv = mint_shim_object(want) };
            S_OK
        }
        ActivationDisposition::NoInterface => E_NOINTERFACE,
        ActivationDisposition::Forward => {
            // SAFETY: forward the caller's verbatim arguments to real ole32.
            unsafe { CoCreateInstance(rclsid, punkouter, dwclscontext, riid, ppv) }
        }
    }
}

/// `mCoCreateInstanceEx` — the multi-`QueryInterface` COM activation shim. Off
/// mode and any unregistered CLSID forward verbatim to the real ole32
/// `CoCreateInstanceEx`; in substitute mode a registered factory fills each
/// `MULTI_QI` slot it supports with a shim object and fails the rest with
/// `E_NOINTERFACE`, returning `S_OK` / `CO_S_NOTALLINTERFACES` / `E_NOINTERFACE`
/// per the COM contract.
#[unsafe(no_mangle)]
pub extern "system" fn mCoCreateInstanceEx(
    rclsid: *const GUID,
    punkouter: *mut c_void,
    dwclscontext: CLSCTX,
    pserverinfo: *const COSERVERINFO,
    dwcount: u32,
    presults: *mut MULTI_QI,
) -> HRESULT {
    if rclsid.is_null() {
        return E_POINTER;
    }
    if dwcount == 0 || presults.is_null() {
        return E_INVALIDARG;
    }
    // SAFETY: rclsid is a non-null caller GUID.
    let clsid = to_guid(unsafe { &*rclsid });
    let count = dwcount as usize;
    // Snapshot the requested IIDs from the MULTI_QI array.
    let mut iids = Vec::with_capacity(count);
    for i in 0..count {
        // SAFETY: presults points at dwcount MULTI_QI entries (caller contract).
        let entry = unsafe { &*presults.add(i) };
        if entry.pIID.is_null() {
            return E_POINTER;
        }
        // SAFETY: pIID is a non-null caller GUID.
        iids.push(to_guid(unsafe { &*entry.pIID }));
    }
    match session().with_com(|com| com.on_create_instance_ex(clsid, &iids, dwclscontext)) {
        ActivationExDisposition::Forward => {
            // SAFETY: forward the caller's verbatim arguments to real ole32.
            unsafe {
                CoCreateInstanceEx(rclsid, punkouter, dwclscontext, pserverinfo, dwcount, presults)
            }
        }
        ActivationExDisposition::Substitute(slots) => {
            if !punkouter.is_null() {
                return CLASS_E_NOAGGREGATION;
            }
            let mut produced = 0usize;
            for (i, slot) in slots.into_iter().enumerate() {
                // SAFETY: presults has dwcount entries; i < count.
                let entry = unsafe { &mut *presults.add(i) };
                match slot {
                    Some(want) => {
                        entry.pItf = mint_shim_object(want);
                        entry.hr = S_OK;
                        produced += 1;
                    }
                    None => {
                        entry.pItf = null_mut();
                        entry.hr = E_NOINTERFACE;
                    }
                }
            }
            if produced == count {
                S_OK
            } else if produced == 0 {
                E_NOINTERFACE
            } else {
                CO_S_NOTALLINTERFACES
            }
        }
    }
}

/// `mCoGetClassObject` — the class-object activation shim. Off mode and any
/// unregistered CLSID forward verbatim to the real ole32 `CoGetClassObject`; in
/// substitute mode a registered CLSID vends a shim `IClassFactory` (when the
/// caller asks for `IUnknown` / `IClassFactory`, else `E_NOINTERFACE`).
#[unsafe(no_mangle)]
pub extern "system" fn mCoGetClassObject(
    rclsid: *const GUID,
    dwclscontext: CLSCTX,
    pvreserved: *const c_void,
    riid: *const GUID,
    ppv: *mut *mut c_void,
) -> HRESULT {
    if ppv.is_null() {
        return E_POINTER;
    }
    // SAFETY: ppv is non-null (checked above).
    unsafe { *ppv = null_mut() };
    if rclsid.is_null() || riid.is_null() {
        return E_POINTER;
    }
    // SAFETY: rclsid / riid are non-null caller GUIDs.
    let clsid = to_guid(unsafe { &*rclsid });
    // SAFETY: riid is a non-null caller GUID.
    let iid = to_guid(unsafe { &*riid });
    match session().with_com(|com| com.on_get_class_object(clsid, iid, dwclscontext)) {
        ClassObjectDisposition::Factory(iids) => {
            // A shim class object only implements IUnknown / IClassFactory.
            if iid == IID_IUNKNOWN || iid == IID_ICLASSFACTORY {
                // SAFETY: ppv is non-null.
                unsafe { *ppv = mint_shim_factory(iids) };
                S_OK
            } else {
                E_NOINTERFACE
            }
        }
        ClassObjectDisposition::Forward => {
            // SAFETY: forward the caller's verbatim arguments to real ole32.
            unsafe { CoGetClassObject(rclsid, dwclscontext, pvreserved, riid, ppv) }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// An IID a shim object/factory vends in the tests.
    const TEST_IID: Guid = Guid::new(0xaaaa_bbbb, 0xcccc, 0xdddd, [8, 7, 6, 5, 4, 3, 2, 1]);
    /// An IID nothing vends.
    const OTHER_IID: Guid = Guid::new(0xdead_beef, 0, 0, [0, 0, 0, 0, 0, 0, 0, 0]);

    /// Read the `IUnknown` vtable a COM object points at.
    ///
    /// # Safety
    ///
    /// `obj` must be a live COM object whose first field is an `IUnknown` vtable
    /// pointer.
    unsafe fn iunknown_vtbl(obj: *mut c_void) -> *const IUnknownVtbl {
        // SAFETY: a COM object begins with its vtable pointer.
        unsafe { *obj.cast::<*const IUnknownVtbl>() }
    }

    /// Read the `IClassFactory` vtable a factory object points at.
    ///
    /// # Safety
    ///
    /// `obj` must be a live `IClassFactory`-shaped object.
    unsafe fn iclassfactory_vtbl(obj: *mut c_void) -> *const IClassFactoryVtbl {
        // SAFETY: a class-factory object begins with its vtable pointer.
        unsafe { *obj.cast::<*const IClassFactoryVtbl>() }
    }

    /// Call `QueryInterface` through the object's vtable.
    unsafe fn query_interface(obj: *mut c_void, iid: Guid) -> (HRESULT, *mut c_void) {
        let raw = GUID {
            data1: iid.data1,
            data2: iid.data2,
            data3: iid.data3,
            data4: iid.data4,
        };
        let mut out: *mut c_void = null_mut();
        // SAFETY: obj is a live COM object; out is a valid out-pointer.
        let hr = unsafe { ((*iunknown_vtbl(obj)).query_interface)(obj, &raw, &mut out) };
        (hr, out)
    }

    /// Call `Release` through the object's vtable.
    unsafe fn release(obj: *mut c_void) -> u32 {
        // SAFETY: obj is a live COM object.
        unsafe { ((*iunknown_vtbl(obj)).release)(obj) }
    }

    /// Call `AddRef` through the object's vtable.
    unsafe fn add_ref(obj: *mut c_void) -> u32 {
        // SAFETY: obj is a live COM object.
        unsafe { ((*iunknown_vtbl(obj)).add_ref)(obj) }
    }

    #[test]
    fn shim_object_query_interface_and_refcount() {
        let obj = mint_shim_object(TEST_IID);
        assert!(!obj.is_null());

        // SAFETY: obj is a freshly minted live object.
        unsafe {
            // IUnknown succeeds and bumps the count (1 -> 2).
            let (hr, p) = query_interface(obj, IID_IUNKNOWN);
            assert_eq!(hr, S_OK);
            assert_eq!(p, obj);
            // The declared IID succeeds too (2 -> 3).
            let (hr, p) = query_interface(obj, TEST_IID);
            assert_eq!(hr, S_OK);
            assert_eq!(p, obj);
            // An unknown IID fails and nulls the out-pointer.
            let (hr, p) = query_interface(obj, OTHER_IID);
            assert_eq!(hr, E_NOINTERFACE);
            assert!(p.is_null());

            // AddRef then unwind every reference; the last Release frees it.
            assert_eq!(add_ref(obj), 4);
            assert_eq!(release(obj), 3);
            assert_eq!(release(obj), 2);
            assert_eq!(release(obj), 1);
            assert_eq!(release(obj), 0);
        }
    }

    #[test]
    fn shim_factory_create_instance_vends_supported_iid() {
        let factory = mint_shim_factory(vec![TEST_IID]);
        assert!(!factory.is_null());

        // SAFETY: factory is a freshly minted live class factory.
        unsafe {
            let vtbl = iclassfactory_vtbl(factory);

            // QueryInterface for IClassFactory succeeds.
            let (hr, p) = query_interface(factory, IID_ICLASSFACTORY);
            assert_eq!(hr, S_OK);
            assert_eq!(p, factory);
            assert_eq!(release(factory), 1);

            // CreateInstance vends an object for the supported IID.
            let mut obj: *mut c_void = null_mut();
            let raw = GUID {
                data1: TEST_IID.data1,
                data2: TEST_IID.data2,
                data3: TEST_IID.data3,
                data4: TEST_IID.data4,
            };
            let hr = ((*vtbl).create_instance)(factory, null_mut(), &raw, &mut obj);
            assert_eq!(hr, S_OK);
            assert!(!obj.is_null());
            // The vended object honors the requested IID.
            let (hr, _) = query_interface(obj, TEST_IID);
            assert_eq!(hr, S_OK);
            assert_eq!(release(obj), 1);
            assert_eq!(release(obj), 0);

            // An unsupported IID fails.
            let other = GUID {
                data1: OTHER_IID.data1,
                data2: OTHER_IID.data2,
                data3: OTHER_IID.data3,
                data4: OTHER_IID.data4,
            };
            let mut none: *mut c_void = null_mut();
            let hr = ((*vtbl).create_instance)(factory, null_mut(), &other, &mut none);
            assert_eq!(hr, E_NOINTERFACE);
            assert!(none.is_null());

            // Aggregation is refused.
            let mut agg: *mut c_void = null_mut();
            let outer = core::ptr::dangling_mut::<c_void>();
            let hr = ((*vtbl).create_instance)(factory, outer, &raw, &mut agg);
            assert_eq!(hr, CLASS_E_NOAGGREGATION);
            assert!(agg.is_null());

            // Release the factory.
            assert_eq!(release(factory), 0);
        }
    }
}
