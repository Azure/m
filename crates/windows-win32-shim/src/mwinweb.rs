// Copyright (c) Microsoft Corporation.

//! The in-process web-host activation seam (`windows-win32-shim` SHIM-D18) — the
//! ABI side of platform-isolation's safe web `RequestHandler` surface (M8).
//!
//! The shim is loaded into the web host as a load-time dependency (via the
//! aliasobj relink, MW5) and inserts a request handler at the host's **public
//! activation seam** — the IIS native-module path `RegisterModule` →
//! `IHttpModuleRegistrationInfo::SetRequestNotifications` →
//! `IHttpModuleFactory::GetHttpModule` → `CHttpModule` notifications. Only public
//! Windows SDK names are used.
//!
//! Per SHIM-D2 this is the only place raw caller pointers and the hand-rolled
//! IIS module vtables are touched, so the module opts back into `unsafe_code`
//! (the crate root denies it). The decision to hand-roll the module vtables with
//! raw types — rather than bind a real IIS-hosting crate — keeps the dependency
//! story uniform with the rest of the shim (Design Autonomy, SHIM-D18).
//!
//! ## Residency probe (MW11-1)
//!
//! [`mShimWebProbe`] is a zero-argument diagnostic export that returns a fixed
//! sentinel ([`SHIM_WEB_PROBE_TAG`]). It carries no isolation behavior; it exists
//! so a relinked host (or the link-proof harness) can confirm the shim is
//! resident and its exported entry points are reachable in the host process. It
//! is shim-internal — it mirrors no Win32 name — so it is **not** part of the
//! Win32 alias roster (`windows_win32_shim.def` / `windows_win32_shim_aliases.ndjson`).

#![allow(unsafe_code)]

/// The sentinel [`mShimWebProbe`] returns: the ASCII tag `"MWEB"` packed
/// big-endian (`M`=0x4D, `W`=0x57, `E`=0x45, `B`=0x42). A caller that reads this
/// value back from the exported symbol has proven the shim is resident. The
/// exact value is arbitrary; changing it only invalidates a residency check, it
/// is not a behavioral contract.
pub const SHIM_WEB_PROBE_TAG: u32 = 0x4D57_4542;

/// Residency probe (MW11-1): returns [`SHIM_WEB_PROBE_TAG`] so a host or the
/// link-proof harness can confirm the shim is loaded and its exports resolve.
/// Carries no isolation behavior.
#[unsafe(no_mangle)]
pub extern "system" fn mShimWebProbe() -> u32 {
    SHIM_WEB_PROBE_TAG
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn probe_returns_the_residency_tag() {
        assert_eq!(mShimWebProbe(), SHIM_WEB_PROBE_TAG);
        // The tag is the ASCII bytes "MWEB" packed big-endian.
        assert_eq!(SHIM_WEB_PROBE_TAG.to_be_bytes(), *b"MWEB");
    }
}
