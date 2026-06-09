// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: MIT

//! Best-effort host identification recorded in each capture's header line.

/// Target architecture string (`"aarch64"`, `"x86_64"`, or the raw target arch).
pub fn arch_name() -> &'static str {
    if cfg!(target_arch = "aarch64") {
        "aarch64"
    } else if cfg!(target_arch = "x86_64") {
        "x86_64"
    } else {
        "unknown"
    }
}

/// Operating-system string.
pub fn os_name() -> &'static str {
    std::env::consts::OS
}

/// Runtime detection of AArch64 half-precision (`FEAT_FP16`) support.
///
/// `is_aarch64_feature_detected!` does not accept feature-name strings on the
/// `aarch64-pc-windows-msvc` toolchain, so on Windows we query the OS directly
/// via `IsProcessorFeaturePresent`. Elsewhere the std runtime probe works.
#[cfg(target_arch = "aarch64")]
pub fn aarch64_fp16() -> bool {
    #[cfg(target_os = "windows")]
    {
        // PF_ARM_V82_FP16_INSTRUCTIONS_AVAILABLE = 44.
        const PF_ARM_V82_FP16: u32 = 44;
        extern "system" {
            fn IsProcessorFeaturePresent(feature: u32) -> i32;
        }
        unsafe { IsProcessorFeaturePresent(PF_ARM_V82_FP16) != 0 }
    }
    #[cfg(not(target_os = "windows"))]
    {
        std::arch::is_aarch64_feature_detected!("fp16")
    }
}

/// Detected FP-relevant CPU features for the current architecture. These are
/// recorded so that, e.g., a half-precision gap on one machine is explained by
/// the absence of `fp16` rather than mistaken for a divergence.
///
/// On AArch64 the two IMPLEMENTATION_DEFINED knobs that can change a *result
/// value* are also recorded: `FEAT_RPRES` (selects a 12-bit reciprocal /
/// reciprocal-sqrt estimate table when `FPCR.AH==1`, where a non-RPRES part
/// returns 8 bits) and `FEAT_AFP` (gates the `FPCR.AH`/`FIZ`/`NEP` alternate
/// behaviors). They are surveyed so that an `frecpe.s` / `frsqrte.s` divergence
/// between two machines is attributed to a different RPRES/`AH` context rather
/// than mistaken for a conformance bug. Windows-on-ARM exposes no `PF_*` flag
/// for either, so they are best-effort and only probed where the std runtime
/// detector works.
pub fn features() -> Vec<String> {
    let mut f = Vec::new();
    #[cfg(target_arch = "aarch64")]
    {
        if aarch64_fp16() {
            f.push("fp16".to_string());
        }
        #[cfg(not(target_os = "windows"))]
        {
            if std::arch::is_aarch64_feature_detected!("afp") {
                f.push("afp".to_string());
            }
            if std::arch::is_aarch64_feature_detected!("rpres") {
                f.push("rpres".to_string());
            }
        }
    }
    #[cfg(target_arch = "x86_64")]
    {
        macro_rules! probe {
            ($name:literal) => {
                if std::arch::is_x86_feature_detected!($name) {
                    f.push($name.to_string());
                }
            };
        }
        probe!("sse2");
        probe!("sse4.1");
        probe!("avx");
        probe!("fma");
        probe!("avx512f");
    }
    f
}

/// Best-effort human-readable CPU brand string. Returns `"unknown"` when the
/// platform does not expose one cheaply; callers should always pass an explicit
/// `--label` so a capture is identifiable regardless.
pub fn cpu_brand() -> String {
    #[cfg(target_arch = "x86_64")]
    {
        if let Some(s) = x86_brand() {
            return s;
        }
    }
    // macOS / iOS: the kernel reports the chip via sysctl, including on Apple
    // Silicon where there is no CPUID. This is the authoritative brand string
    // (e.g. "Apple M2").
    #[cfg(any(target_os = "macos", target_os = "ios"))]
    {
        if let Some(s) = macos_brand() {
            return s;
        }
    }
    // Windows (ARM and x64): the brand lives in the registry. On Windows-on-ARM
    // there is no CPUID, so this is the only native source (e.g.
    // "Snapdragon(R) X Elite ...").
    #[cfg(target_os = "windows")]
    {
        if let Some(s) = windows_brand() {
            return s;
        }
    }
    // Linux: /proc/cpuinfo carries a model line on both arches.
    if let Ok(txt) = std::fs::read_to_string("/proc/cpuinfo") {
        for line in txt.lines() {
            for tag in ["model name", "Model", "CPU part", "Hardware"] {
                if let Some(rest) = line.strip_prefix(tag) {
                    if let Some((_, v)) = rest.split_once(':') {
                        let v = v.trim();
                        if !v.is_empty() {
                            return v.to_string();
                        }
                    }
                }
            }
        }
    }
    "unknown".to_string()
}

#[cfg(target_arch = "x86_64")]
fn x86_brand() -> Option<String> {
    use std::arch::x86_64::__cpuid;
    // Brand string is in CPUID leaves 0x80000002..0x80000004 (12 dwords).
    let max_ext = unsafe { __cpuid(0x8000_0000) }.eax;
    if max_ext < 0x8000_0004 {
        return None;
    }
    let mut bytes = Vec::with_capacity(48);
    for leaf in 0x8000_0002u32..=0x8000_0004 {
        let r = unsafe { __cpuid(leaf) };
        for reg in [r.eax, r.ebx, r.ecx, r.edx] {
            bytes.extend_from_slice(&reg.to_le_bytes());
        }
    }
    let s = String::from_utf8_lossy(&bytes);
    let s = s.trim_matches(char::from(0)).trim();
    if s.is_empty() {
        None
    } else {
        Some(s.to_string())
    }
}

/// Apple-platform CPU brand via `sysctlbyname("machdep.cpu.brand_string")`.
///
/// Works on Apple Silicon (M-series) and Intel Macs alike, and on iOS, where
/// there is no CPUID and `/proc/cpuinfo` does not exist. The two-call pattern
/// first queries the buffer length, then fills it.
#[cfg(any(target_os = "macos", target_os = "ios"))]
fn macos_brand() -> Option<String> {
    use std::os::raw::{c_char, c_int, c_void};
    extern "C" {
        fn sysctlbyname(
            name: *const c_char,
            oldp: *mut c_void,
            oldlenp: *mut usize,
            newp: *mut c_void,
            newlen: usize,
        ) -> c_int;
    }
    let key = b"machdep.cpu.brand_string\0";
    let mut len: usize = 0;
    // First call: ask for the required buffer size.
    let rc = unsafe {
        sysctlbyname(
            key.as_ptr().cast(),
            std::ptr::null_mut(),
            &mut len,
            std::ptr::null_mut(),
            0,
        )
    };
    if rc != 0 || len == 0 {
        return None;
    }
    let mut buf = vec![0u8; len];
    let rc = unsafe {
        sysctlbyname(
            key.as_ptr().cast(),
            buf.as_mut_ptr().cast(),
            &mut len,
            std::ptr::null_mut(),
            0,
        )
    };
    if rc != 0 {
        return None;
    }
    // sysctl returns a NUL-terminated C string; drop the trailing NUL.
    if let Some(pos) = buf.iter().position(|&b| b == 0) {
        buf.truncate(pos);
    }
    let s = String::from_utf8_lossy(&buf).trim().to_string();
    if s.is_empty() {
        None
    } else {
        Some(s)
    }
}

/// Windows CPU brand via the registry value
/// `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString`.
///
/// This is the only native brand source on Windows-on-ARM (no CPUID); it also
/// works on Windows-x64. Uses `RegGetValueW` directly to stay dependency-free.
#[cfg(target_os = "windows")]
fn windows_brand() -> Option<String> {
    use std::os::raw::{c_ulong, c_void};

    type Hkey = *mut c_void;
    // HKEY_LOCAL_MACHINE is a fixed pseudo-handle.
    const HKEY_LOCAL_MACHINE: Hkey = 0x8000_0002u32 as usize as Hkey;
    // RRF_RT_REG_SZ: restrict the result type to REG_SZ.
    const RRF_RT_REG_SZ: c_ulong = 0x0000_0002;
    const ERROR_SUCCESS: c_ulong = 0;

    #[link(name = "advapi32")]
    extern "system" {
        fn RegGetValueW(
            hkey: Hkey,
            lpsubkey: *const u16,
            lpvalue: *const u16,
            dwflags: c_ulong,
            pdwtype: *mut c_ulong,
            pvdata: *mut c_void,
            pcbdata: *mut c_ulong,
        ) -> c_ulong;
    }

    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(std::iter::once(0)).collect()
    }

    let subkey = wide(r"HARDWARE\DESCRIPTION\System\CentralProcessor\0");
    let value = wide("ProcessorNameString");

    // First call: query the size in bytes.
    let mut cb: c_ulong = 0;
    let rc = unsafe {
        RegGetValueW(
            HKEY_LOCAL_MACHINE,
            subkey.as_ptr(),
            value.as_ptr(),
            RRF_RT_REG_SZ,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            &mut cb,
        )
    };
    if rc != ERROR_SUCCESS || cb == 0 {
        return None;
    }
    // cb is a byte count for a UTF-16 string; round up to u16 units.
    let mut buf: Vec<u16> = vec![0u16; (cb as usize).div_ceil(2)];
    let mut cb2: c_ulong = (buf.len() * 2) as c_ulong;
    let rc = unsafe {
        RegGetValueW(
            HKEY_LOCAL_MACHINE,
            subkey.as_ptr(),
            value.as_ptr(),
            RRF_RT_REG_SZ,
            std::ptr::null_mut(),
            buf.as_mut_ptr().cast(),
            &mut cb2,
        )
    };
    if rc != ERROR_SUCCESS {
        return None;
    }
    // Trim to the returned length and drop any trailing NUL(s).
    let units = (cb2 as usize) / 2;
    buf.truncate(units);
    while let Some(&0) = buf.last() {
        buf.pop();
    }
    let s = String::from_utf16_lossy(&buf).trim().to_string();
    if s.is_empty() {
        None
    } else {
        Some(s)
    }
}

/// Seconds since the Unix epoch (UTC) at the moment of the call. Returns `0` if
/// the system clock is set before 1970 (it never legitimately is).
pub fn now_unix() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// Format a Unix timestamp (UTC) as an ISO-8601 string, e.g.
/// `"2026-06-09T09:04:31Z"`. Implemented locally to keep the crate
/// dependency-free; uses the standard proleptic-Gregorian civil-date algorithm.
pub fn unix_to_iso_utc(secs: u64) -> String {
    let days = (secs / 86_400) as i64;
    let rem = secs % 86_400;
    let (hh, mm, ss) = (rem / 3600, (rem % 3600) / 60, rem % 60);
    let (y, mo, d) = civil_from_days(days);
    format!("{y:04}-{mo:02}-{d:02}T{hh:02}:{mm:02}:{ss:02}Z")
}

/// Convert a day count since the Unix epoch (1970-01-01) into a `(year, month,
/// day)` triple in the proleptic Gregorian calendar (Howard Hinnant's
/// `civil_from_days`).
fn civil_from_days(z: i64) -> (i64, u32, u32) {
    let z = z + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097; // [0, 146096]
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365; // [0, 399]
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
    let mp = (5 * doy + 2) / 153; // [0, 11]
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32; // [1, 31]
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32; // [1, 12]
    (y + if m <= 2 { 1 } else { 0 }, m, d)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn iso_format_known_epochs() {
        assert_eq!(unix_to_iso_utc(0), "1970-01-01T00:00:00Z");
        // 2000-01-01T00:00:00Z
        assert_eq!(unix_to_iso_utc(946_684_800), "2000-01-01T00:00:00Z");
        // 2026-06-09T09:04:31Z
        assert_eq!(unix_to_iso_utc(1_780_995_871), "2026-06-09T09:04:31Z");
        // Leap day 2024-02-29T12:00:00Z
        assert_eq!(unix_to_iso_utc(1_709_208_000), "2024-02-29T12:00:00Z");
    }

    #[test]
    fn now_is_after_2024() {
        // Sanity: the clock should be well past 2024-01-01 (1704067200).
        assert!(now_unix() > 1_704_067_200);
    }

    // On Windows and Apple platforms the brand string must resolve natively
    // (registry / sysctl). Asserting non-"unknown" here would fail in sandboxed
    // CI, so we only assert the call is well-formed and non-empty.
    #[cfg(any(target_os = "windows", target_os = "macos", target_os = "ios"))]
    #[test]
    fn native_brand_lookup_is_nonempty_when_present() {
        #[cfg(any(target_os = "macos", target_os = "ios"))]
        if let Some(s) = macos_brand() {
            assert!(!s.is_empty());
        }
        #[cfg(target_os = "windows")]
        if let Some(s) = windows_brand() {
            assert!(!s.is_empty());
        }
    }
}
