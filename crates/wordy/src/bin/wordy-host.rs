// Copyright (c) Microsoft Corporation.

//! `wordy-host` — a Hostable Web Core (HWC) activator / readiness pre-flight for
//! `wordy`.
//!
//! `wordy` is an IIS native module (`wordy.dll`). The intended end state is to
//! host it under genuine **Hostable Web Core** — loading the real
//! `hwebcore.dll`, generating an `applicationHost.config` + `web.config` that
//! load `wordy.dll`, calling `WebCoreActivate`, and driving every REST route
//! over real HTTP (MW13-5 / MW16).
//!
//! This binary performs the parts of that flow that are **safe and runnable on
//! an ordinary developer machine today**:
//!
//! 1. **Discover** the genuine HWC engine at the absolute `inetsrv` path
//!    (`%windir%\System32\inetsrv\hwebcore.dll`). A bare-name load fails because
//!    the engine has a large dependency closure under `inetsrv`; the correct
//!    seam is an absolute-path `LoadLibraryExW` with the `inetsrv` directory
//!    added to the dependency search (see the `WORDY_HOST_PROBE` step).
//! 2. **Locate** the built `wordy.dll` next to this executable.
//! 3. **Generate** a representative `applicationHost.config` + `web.config` that
//!    register `wordy.dll` as a global module and bind a site.
//! 4. **Report** an HWC readiness verdict and exit `0` either way (so it is safe
//!    to run in CI and on machines without HWC).
//!
//! Setting `WORDY_HOST_PROBE=1` additionally exercises the genuine dynamic-load
//! seam: it `LoadLibraryExW`s the real `hwebcore.dll` by absolute path with the
//! `inetsrv` dependency directory and resolves its three exports
//! (`WebCoreActivate` / `WebCoreShutdown` / `WebCoreSetMetadata`), then frees it
//! — proving the engine loads and exports resolve, without activating it.
//!
//! **Deferred (MW16):** calling `WebCoreActivate` and driving live HTTP requires
//! `wordy`'s modeled IIS vtables (`src/iis.rs`) to be pinned to the genuine
//! `httpserv.h` layout first — activating a real host against the current modeled
//! subset would mis-dispatch host calls. That precise vtable pinning plus live
//! activation is tracked as its own milestone.

#[cfg(windows)]
fn main() -> std::process::ExitCode {
    hosted::run()
}

#[cfg(not(windows))]
fn main() -> std::process::ExitCode {
    eprintln!("wordy-host requires Windows with Hostable Web Core (IIS-HostableWebCore).");
    std::process::ExitCode::SUCCESS
}

#[cfg(windows)]
mod hosted {
    use std::path::{Path, PathBuf};
    use std::process::ExitCode;

    /// Relative location of the HWC engine under the Windows directory.
    const HWEBCORE_RELATIVE: &str = r"System32\inetsrv\hwebcore.dll";

    /// The site port the generated config binds (a high, unprivileged port).
    const SITE_PORT: u16 = 8080;

    /// Run the readiness pre-flight, returning success even when HWC is absent.
    pub fn run() -> ExitCode {
        println!("wordy-host — Hostable Web Core readiness pre-flight");

        let windir = match std::env::var("windir").or_else(|_| std::env::var("SystemRoot")) {
            Ok(dir) => PathBuf::from(dir),
            Err(_) => {
                eprintln!("  could not resolve %windir%; cannot locate HWC");
                return ExitCode::SUCCESS;
            }
        };
        let inetsrv = windir.join(r"System32\inetsrv");
        let hwebcore = windir.join(HWEBCORE_RELATIVE);

        let hwc_present = hwebcore.is_file();
        if hwc_present {
            println!("  [found]   HWC engine: {}", hwebcore.display());
        } else {
            println!(
                "  [absent]  HWC engine not found at {} (install IIS-HostableWebCore to enable genuine hosting)",
                hwebcore.display()
            );
        }

        let wordy_dll = locate_wordy_dll();
        match &wordy_dll {
            Some(dll) => println!("  [found]   wordy module: {}", dll.display()),
            None => println!("  [absent]  wordy.dll not found next to this executable"),
        }

        match generate_configs(wordy_dll.as_deref()) {
            Ok(paths) => {
                println!("  [wrote]   applicationHost.config: {}", paths.app_host.display());
                println!("  [wrote]   web.config:             {}", paths.web.display());
            }
            Err(e) => eprintln!("  [error]   could not generate configs: {e}"),
        }

        if std::env::var_os("WORDY_HOST_PROBE").is_some() {
            if hwc_present {
                probe_engine(&hwebcore, &inetsrv);
            } else {
                println!("  [skip]    WORDY_HOST_PROBE set but HWC engine is absent");
            }
        }

        println!(
            "  [note]    genuine WebCoreActivate + live HTTP is deferred until the modeled IIS\n            vtables are pinned to httpserv.h (MW16); this pre-flight does not activate."
        );

        ExitCode::SUCCESS
    }

    /// Paths to the generated configuration files.
    struct ConfigPaths {
        app_host: PathBuf,
        web: PathBuf,
    }

    /// Locate `wordy.dll` next to the running executable (the cargo target dir).
    fn locate_wordy_dll() -> Option<PathBuf> {
        let exe = std::env::current_exe().ok()?;
        let dir = exe.parent()?;
        let dll = dir.join("wordy.dll");
        dll.is_file().then_some(dll)
    }

    /// Generate a representative `applicationHost.config` + `web.config` that load
    /// `wordy.dll` as a global module and bind a site, into a temp directory.
    fn generate_configs(wordy_dll: Option<&Path>) -> std::io::Result<ConfigPaths> {
        let dir = std::env::temp_dir().join("wordy-host");
        std::fs::create_dir_all(&dir)?;
        let site_root = dir.join("wwwroot");
        std::fs::create_dir_all(&site_root)?;

        let image = wordy_dll
            .map(|p| p.display().to_string())
            .unwrap_or_else(|| "wordy.dll".to_string());

        let app_host = dir.join("applicationHost.config");
        std::fs::write(&app_host, application_host_config(&image, &site_root))?;

        let web = dir.join("web.config");
        std::fs::write(&web, WEB_CONFIG)?;

        Ok(ConfigPaths { app_host, web })
    }

    /// Render an `applicationHost.config` registering `wordy.dll` as a global
    /// native module and binding a single site on [`SITE_PORT`].
    fn application_host_config(image: &str, site_root: &Path) -> String {
        let image = xml_escape(image);
        let site_root = xml_escape(&site_root.display().to_string());
        format!(
            r#"<?xml version="1.0" encoding="UTF-8"?>
<configuration>
  <system.webServer>
    <globalModules>
      <add name="WordyModule" image="{image}" />
    </globalModules>
    <modules>
      <add name="WordyModule" />
    </modules>
  </system.webServer>
  <system.applicationHost>
    <sites>
      <site name="wordy" id="1">
        <application path="/">
          <virtualDirectory path="/" physicalPath="{site_root}" />
        </application>
        <bindings>
          <binding protocol="http" bindingInformation="*:{SITE_PORT}:localhost" />
        </bindings>
      </site>
    </sites>
  </system.applicationHost>
</configuration>
"#
        )
    }

    /// The site `web.config` enabling the `wordy` module for the application.
    const WEB_CONFIG: &str = r#"<?xml version="1.0" encoding="UTF-8"?>
<configuration>
  <system.webServer>
    <modules>
      <add name="WordyModule" />
    </modules>
  </system.webServer>
</configuration>
"#;

    /// Escape the five XML metacharacters in an attribute value.
    fn xml_escape(value: &str) -> String {
        let mut out = String::with_capacity(value.len());
        for ch in value.chars() {
            match ch {
                '&' => out.push_str("&amp;"),
                '<' => out.push_str("&lt;"),
                '>' => out.push_str("&gt;"),
                '"' => out.push_str("&quot;"),
                '\'' => out.push_str("&apos;"),
                _ => out.push(ch),
            }
        }
        out
    }

    /// Load the genuine `hwebcore.dll` by absolute path with the `inetsrv`
    /// dependency directory, resolve its three exports, and free it.
    ///
    /// This proves the dynamic-load seam works (a bare-name load fails with
    /// `ERROR_MOD_NOT_FOUND` because of the engine's `inetsrv` dependency
    /// closure). It deliberately does **not** call `WebCoreActivate`.
    fn probe_engine(hwebcore: &Path, inetsrv: &Path) {
        use windows_sys::Win32::Foundation::FreeLibrary;
        use windows_sys::Win32::System::LibraryLoader::{
            AddDllDirectory, GetProcAddress, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, LoadLibraryExW,
        };

        let inetsrv_w = wide(&inetsrv.display().to_string());
        // SAFETY: inetsrv_w is a valid NUL-terminated wide string; AddDllDirectory
        // returns a cookie (or null) and has no other effect.
        let cookie = unsafe { AddDllDirectory(inetsrv_w.as_ptr()) };
        if cookie.is_null() {
            println!("  [warn]    AddDllDirectory({}) failed", inetsrv.display());
        }

        let hwebcore_w = wide(&hwebcore.display().to_string());
        // SAFETY: hwebcore_w is a valid NUL-terminated wide string; the search
        // flags include the DLL's own load directory for its dependency closure.
        let module = unsafe {
            LoadLibraryExW(
                hwebcore_w.as_ptr(),
                std::ptr::null_mut(),
                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR,
            )
        };
        if module.is_null() {
            let err = std::io::Error::last_os_error();
            println!("  [error]   LoadLibraryExW(hwebcore.dll) failed: {err}");
            return;
        }
        println!("  [load]    hwebcore.dll mapped into the process");

        for export in ["WebCoreActivate", "WebCoreShutdown", "WebCoreSetMetadata"] {
            let name = c_string(export);
            // SAFETY: module is a live HMODULE; name is a NUL-terminated PCSTR.
            let proc = unsafe { GetProcAddress(module, name.as_ptr()) };
            if proc.is_some() {
                println!("  [export]  {export} resolved");
            } else {
                println!("  [missing] {export} not found");
            }
        }

        // SAFETY: module is the live HMODULE returned by LoadLibraryExW.
        unsafe { FreeLibrary(module) };
        println!("  [free]    hwebcore.dll released (engine was not activated)");
    }

    /// Build a NUL-terminated wide string.
    fn wide(value: &str) -> Vec<u16> {
        value.encode_utf16().chain(std::iter::once(0)).collect()
    }

    /// Build a NUL-terminated narrow C string.
    fn c_string(value: &str) -> Vec<u8> {
        let mut bytes = value.as_bytes().to_vec();
        bytes.push(0);
        bytes
    }
}
