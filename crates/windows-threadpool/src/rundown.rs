// Copyright (c) Microsoft Corporation.

//! Process-rundown query (TP-D4 boundary: the unsafe resolution lives in `ffi`).
//!
//! Teardown paths that join on thread-pool callbacks (`Work::wait`,
//! `WaitForThreadpoolWorkCallbacks`) must not run during process rundown: the OS
//! loader has already terminated every other thread, so the worker threads those
//! waits would block on are gone and the wait would hang forever. Consult
//! [`process_rundown_in_progress`] first; when true, skip the wait and leak,
//! letting the OS reclaim the address space. This mirrors PIL
//! `process_rundown_in_progress()` and the mwin32 D16 leak-on-terminate rule.

/// Whether the OS loader has begun process rundown (ntdll `RtlDllShutdownInProgress`).
///
/// True **only** during process termination; a live `FreeLibrary` unload reports
/// false, so normal quiesce runs. Reports false on platforms or builds where the
/// export cannot be resolved.
#[must_use]
pub fn process_rundown_in_progress() -> bool {
    crate::ffi::dll_shutdown_in_progress()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn not_in_rundown_while_running() {
        // A normally-running test process is not tearing down; the call resolves
        // RtlDllShutdownInProgress and reports false.
        assert!(!process_rundown_in_progress());
    }
}

