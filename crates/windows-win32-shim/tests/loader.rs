// Copyright (c) Microsoft Corporation.

//! MW9 loader-family integration test: drive the exported dynamic-loader ABI
//! (`mLoadLibraryW` / `mGetProcAddress` / `mGetModuleHandleW` / `mFreeLibrary`)
//! through the process-wide session, proving engine substitution, proc
//! redirection, observation, and off-mode transparency end to end.
//!
//! Unlike the unit tests (which exercise the policy on a local `LoaderState`),
//! this drives the real `#[no_mangle]` exports against the global `session()`,
//! so it covers the ABI marshaling, the session wiring, and the policy together
//! — the same path a client reaches through the redirected loader imports
//! (`/alternatename`, see `windows_win32_shim_aliases.ndjson`). The static
//! link-proof that those imports are redirected is `linkproof/run-linkproof.ps1`
//! (the loader family is now in the alias roster, so its `__imp_` slots are
//! emitted by `gen-alias-obj`); this test is the dynamic behavior proof.
//!
//! It is a single test function on purpose: the session loader state is a
//! process-wide singleton, so the substitute-mode phase and the off-mode phase
//! must run sequentially rather than race in parallel.

#![cfg(windows)]

use std::sync::{Arc, Mutex};

use windows_sys::Win32::Foundation::{FARPROC, TRUE};
use windows_sys::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};
use windows_win32_shim::mwinload::{
    mFreeLibrary, mGetModuleHandleW, mGetProcAddress, mLoadLibraryW,
};
use windows_win32_shim::session::session;
use windows_win32_shim::{LoaderEvent, LoaderMode, NullSink, ObservationSink, ProcQuery, ShimProc};

/// An observation sink that records every event into a shared buffer the test
/// can inspect after driving the exports.
#[derive(Clone, Default)]
struct Recorder {
    events: Arc<Mutex<Vec<LoaderEvent>>>,
}

impl ObservationSink for Recorder {
    fn observe(&mut self, event: LoaderEvent) {
        self.events.lock().expect("recorder poisoned").push(event);
    }
}

/// A NUL-terminated wide string for a `*W` entry-point argument.
fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(core::iter::once(0)).collect()
}

/// The integer address behind a `FARPROC` (`0` for the null "not found").
fn farproc_addr(p: FARPROC) -> usize {
    p.map_or(0, |f| f as usize)
}

#[test]
fn loader_family_end_to_end() {
    const ENGINE: &str = "PilTestEngine.dll";
    const ENGINE_PROC: &str = "PilTestEngineActivate";
    const SHIMMED_NAME: &str = "RegOpenKeyExW";
    let engine_proc_addr = ShimProc(0x4321_0000);
    let redirected_addr = ShimProc(0x5555_1000);

    let recorder = Recorder::default();
    let events = recorder.events.clone();

    let s = session();
    s.with_loader(|loader| {
        loader.mode = LoaderMode::Substitute;
        loader.engines.register_engine(ENGINE);
        loader.engines.add_proc(ENGINE, ENGINE_PROC, engine_proc_addr);
        loader.procs.seed(SHIMMED_NAME, redirected_addr);
        loader.set_sink(Box::new(recorder));
    });

    // --- Substitute phase ----------------------------------------------------

    // Loading the registered engine mints a sentinel HMODULE (no OS load).
    let engine_name = wide(ENGINE);
    let sentinel = mLoadLibraryW(engine_name.as_ptr());
    assert!(!sentinel.is_null(), "engine load must mint a sentinel");

    // GetModuleHandle resolves the same sentinel by name (case-insensitive).
    assert_eq!(
        mGetModuleHandleW(wide("piltestengine.dll").as_ptr()),
        sentinel,
        "GetModuleHandle must resolve the minted sentinel by name"
    );

    // The engine sentinel resolves the engine's proc to the shim body.
    let activate = mGetProcAddress(sentinel, c"PilTestEngineActivate".as_ptr().cast());
    assert_eq!(
        farproc_addr(activate),
        engine_proc_addr.0,
        "sentinel proc must resolve to the engine's shim proc"
    );

    // A proc the engine does not supply is the null result, never forwarded.
    let missing = mGetProcAddress(sentinel, c"NoSuchEngineProc".as_ptr().cast());
    assert_eq!(farproc_addr(missing), 0, "unknown sentinel proc is null");

    // Proc redirection: a shimmed name resolved against a REAL module returns the
    // shim body, not the genuine OS address.
    let kernel32 = mLoadLibraryW(wide("kernel32.dll").as_ptr());
    assert!(!kernel32.is_null(), "real load must forward and succeed");
    let redirected = mGetProcAddress(kernel32, c"RegOpenKeyExW".as_ptr().cast());
    assert_eq!(
        farproc_addr(redirected),
        redirected_addr.0,
        "a shimmed proc name must redirect to the shim body"
    );

    // Observation recorded every resolution.
    let recorded = events.lock().unwrap().clone();
    assert!(
        recorded.contains(&LoaderEvent::LoadLibrary {
            name: ENGINE.to_owned()
        }),
        "the engine load was observed"
    );
    assert!(
        recorded.iter().any(|e| matches!(
            e,
            LoaderEvent::GetModuleHandle { name } if name.eq_ignore_ascii_case(ENGINE)
        )),
        "the module-handle resolution was observed"
    );
    assert!(
        recorded.iter().any(|e| matches!(
            e,
            LoaderEvent::GetProcAddress { proc: ProcQuery::Named(n), .. } if n == ENGINE_PROC
        )),
        "the engine proc resolution was observed"
    );

    // Free releases the sentinel (no OS call) and frees the real module; the
    // released sentinel no longer resolves by name (it forwards and misses).
    assert_eq!(mFreeLibrary(sentinel), TRUE, "sentinel free reports success");
    assert_eq!(mFreeLibrary(kernel32), TRUE, "real free forwards and succeeds");
    assert!(
        mGetModuleHandleW(engine_name.as_ptr()).is_null(),
        "a released sentinel no longer resolves (forwarded, not loaded)"
    );

    // --- Off-mode transparency phase ----------------------------------------

    s.with_loader(|loader| {
        loader.mode = LoaderMode::Off;
        loader.set_sink(Box::new(NullSink));
    });

    let name = wide("kernel32.dll");
    let h_shim = mLoadLibraryW(name.as_ptr());
    // SAFETY: forwarding the same NUL-terminated wide string to the real loader.
    let h_real = unsafe { LoadLibraryW(name.as_ptr()) };
    assert_eq!(h_shim, h_real, "off-mode load is the real loader's result");

    let proc_name = c"GetCurrentProcessId";
    let p_shim = farproc_addr(mGetProcAddress(h_shim, proc_name.as_ptr().cast()));
    // SAFETY: resolving a genuine export against a genuine module handle.
    let p_real = farproc_addr(unsafe { GetProcAddress(h_real, proc_name.as_ptr().cast()) });
    assert_ne!(p_real, 0, "the genuine export resolves");
    assert_eq!(p_shim, p_real, "off-mode resolution is byte-for-byte the OS result");

    assert_eq!(mFreeLibrary(h_shim), TRUE);
    // SAFETY: balancing the extra real reference taken above.
    assert_eq!(unsafe { windows_sys::Win32::Foundation::FreeLibrary(h_real) }, TRUE);
}
