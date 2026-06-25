// Copyright (c) Microsoft Corporation.

//! `wordy-host` — a Hostable Web Core (HWC) activator / readiness pre-flight for
//! `wordy`.
//!
//! `wordy` is an IIS native module (`wordy.dll`). This binary hosts it under
//! genuine **Hostable Web Core**:
//!
//! 1. **Discover** the genuine HWC engine at the absolute `inetsrv` path
//!    (`%windir%\System32\inetsrv\hwebcore.dll`).
//! 2. **Locate** the built `wordy.dll` next to this executable.
//! 3. **Generate** an `applicationHost.config` + root `web.config` that register
//!    `wordy.dll` as a global module and bind a site on a high local port.
//! 4. **Report** an HWC readiness verdict and exit `0` (so it is safe to run in
//!    CI and on machines without HWC).
//!
//! With `WORDY_HOST_PROBE=1` it additionally `LoadLibraryExW`s the real engine by
//! absolute path with the `inetsrv` dependency directory and resolves its three
//! exports, proving the dynamic-load seam, then frees it without activating.
//!
//! With `WORDY_HOST_ACTIVATE=1` it performs **genuine activation** (MW16-2):
//! loads the engine, `WebCoreActivate`s it against the generated config (which
//! loads the pinned `wordy.dll`), optionally drives a localhost HTTP smoke check
//! (`WORDY_HOST_HTTP=1`, MW16-3), then `WebCoreShutdown`s. The IIS vtables are
//! pinned to the real `httpserv.h` layout (MW16-1), so the genuine host's calls
//! land on `wordy`'s methods. Activation is opt-in because it starts a real web
//! server in-process.

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

    /// The HWC instance name passed to `WebCoreActivate`.
    const INSTANCE_NAME: &str = "wordy";

    /// Run the pre-flight (and, when opted in, genuine activation).
    pub fn run() -> ExitCode {
        println!("wordy-host — Hostable Web Core host for wordy");

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

        let config = match generate_configs(wordy_dll.as_deref(), &inetsrv) {
            Ok(paths) => {
                println!("  [wrote]   applicationHost.config: {}", paths.app_host.display());
                println!("  [wrote]   web.config:             {}", paths.web.display());
                Some(paths)
            }
            Err(e) => {
                eprintln!("  [error]   could not generate configs: {e}");
                None
            }
        };

        if std::env::var_os("WORDY_HOST_PROBE").is_some() {
            if hwc_present {
                probe_engine(&hwebcore, &inetsrv);
            } else {
                println!("  [skip]    WORDY_HOST_PROBE set but HWC engine is absent");
            }
        }

        if std::env::var_os("WORDY_HOST_ACTIVATE").is_some() {
            if !hwc_present {
                println!("  [skip]    WORDY_HOST_ACTIVATE set but HWC engine is absent");
                return ExitCode::SUCCESS;
            }
            let Some(config) = config else {
                eprintln!("  [error]   cannot activate without generated config");
                return ExitCode::FAILURE;
            };
            return match activate(&hwebcore, &inetsrv, &config) {
                Ok(()) => ExitCode::SUCCESS,
                Err(()) => ExitCode::FAILURE,
            };
        }

        println!("  [note]    set WORDY_HOST_ACTIVATE=1 to start the genuine host (MW16-2/3).");
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

    /// Generate an `applicationHost.config` + root `web.config` that load
    /// `wordy.dll` as a global module and bind a site, into a temp directory.
    fn generate_configs(wordy_dll: Option<&Path>, inetsrv: &Path) -> std::io::Result<ConfigPaths> {
        let dir = std::env::temp_dir().join("wordy-host");
        std::fs::create_dir_all(&dir)?;
        let site_root = dir.join("wwwroot");
        std::fs::create_dir_all(&site_root)?;

        let image = wordy_dll
            .map(|p| p.display().to_string())
            .unwrap_or_else(|| "wordy.dll".to_string());

        let app_host = dir.join("applicationHost.config");
        std::fs::write(&app_host, application_host_config(&image, &site_root, inetsrv))?;

        let web = dir.join("web.config");
        std::fs::write(&web, WEB_CONFIG)?;

        Ok(ConfigPaths { app_host, web })
    }

    /// Render an `applicationHost.config` for HWC: declares the config sections,
    /// an unmanaged application pool, the HTTP listener adapter, a site bound on
    /// [`SITE_PORT`], the core IIS pipeline modules loaded from `inetsrv`
    /// (protocol support, anonymous auth, request filtering, static file), and
    /// `wordy.dll` as the final global + enabled module. This config is accepted
    /// by `WebCoreActivate` and drives the request pipeline through to per-request
    /// module creation.
    fn application_host_config(image: &str, site_root: &Path, inetsrv: &Path) -> String {
        let image = xml_escape(image);
        let site_root = xml_escape(&site_root.display().to_string());
        let bin = xml_escape(&inetsrv.display().to_string());
        format!(
            r#"<?xml version="1.0" encoding="UTF-8"?>
<configuration>
  <configSections>
    <sectionGroup name="system.applicationHost">
      <section name="applicationPools" allowDefinition="AppHostOnly" overrideModeDefault="Deny" />
      <section name="listenerAdapters" allowDefinition="AppHostOnly" overrideModeDefault="Deny" />
      <section name="sites" allowDefinition="AppHostOnly" overrideModeDefault="Deny" />
      <section name="webLimits" allowDefinition="AppHostOnly" overrideModeDefault="Deny" />
    </sectionGroup>
    <sectionGroup name="system.webServer">
      <section name="globalModules" allowDefinition="AppHostOnly" overrideModeDefault="Deny" />
      <section name="handlers" overrideModeDefault="Allow" />
      <section name="modules" allowDefinition="MachineToApplication" overrideModeDefault="Allow" />
      <section name="httpProtocol" overrideModeDefault="Allow" />
      <sectionGroup name="security">
        <section name="authentication">
          <section name="anonymousAuthentication" overrideModeDefault="Allow" />
        </section>
        <section name="requestFiltering" overrideModeDefault="Allow" />
      </sectionGroup>
    </sectionGroup>
  </configSections>
  <system.applicationHost>
    <applicationPools>
      <add name="WordyPool" managedRuntimeVersion="" managedPipelineMode="Integrated" autoStart="true" />
    </applicationPools>
    <listenerAdapters>
      <add name="http" />
    </listenerAdapters>
    <sites>
      <site name="wordy" id="1" serverAutoStart="true">
        <application path="/" applicationPool="WordyPool">
          <virtualDirectory path="/" physicalPath="{site_root}" />
        </application>
        <bindings>
          <binding protocol="http" bindingInformation="*:{SITE_PORT}:localhost" />
        </bindings>
      </site>
    </sites>
  </system.applicationHost>
  <system.webServer>
    <globalModules>
      <add name="ProtocolSupportModule" image="{bin}\protsup.dll" />
      <add name="AnonymousAuthenticationModule" image="{bin}\authanon.dll" />
      <add name="RequestFilteringModule" image="{bin}\modrqflt.dll" />
      <add name="StaticFileModule" image="{bin}\static.dll" />
      <add name="WordyModule" image="{image}" />
    </globalModules>
    <modules>
      <add name="ProtocolSupportModule" />
      <add name="AnonymousAuthenticationModule" />
      <add name="RequestFilteringModule" />
      <add name="StaticFileModule" />
      <add name="WordyModule" />
    </modules>
    <security>
      <authentication>
        <anonymousAuthentication enabled="true" userName="" />
      </authentication>
    </security>
  </system.webServer>
</configuration>
"#
        )
    }

    /// The root `web.config` passed to `WebCoreActivate`.
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
    /// dependency directory, resolve its three exports, and free it — proving the
    /// dynamic-load seam without activating.
    fn probe_engine(hwebcore: &Path, inetsrv: &Path) {
        let Some(module) = load_engine(hwebcore, inetsrv) else {
            return;
        };
        println!("  [load]    hwebcore.dll mapped into the process");
        for export in ["WebCoreActivate", "WebCoreShutdown", "WebCoreSetMetadata"] {
            if resolve(module, export).is_some() {
                println!("  [export]  {export} resolved");
            } else {
                println!("  [missing] {export} not found");
            }
        }
        // SAFETY: module is the live HMODULE returned by load_engine.
        unsafe { windows_sys::Win32::Foundation::FreeLibrary(module) };
        println!("  [free]    hwebcore.dll released (engine was not activated)");
    }

    /// `WebCoreActivate(PCWSTR appHostConfig, PCWSTR rootWebConfig, PCWSTR instanceName)`.
    type WebCoreActivateFn =
        unsafe extern "system" fn(*const u16, *const u16, *const u16) -> i32;
    /// `WebCoreShutdown(DWORD fImmediate)`.
    type WebCoreShutdownFn = unsafe extern "system" fn(u32) -> i32;

    /// Genuinely activate the host: load the engine, `WebCoreActivate` against the
    /// generated config (loading `wordy.dll`), optionally drive an HTTP smoke
    /// check, then `WebCoreShutdown`.
    fn activate(hwebcore: &Path, inetsrv: &Path, config: &ConfigPaths) -> Result<(), ()> {
        let Some(module) = load_engine(hwebcore, inetsrv) else {
            return Err(());
        };
        println!("  [load]    hwebcore.dll mapped into the process");

        let (Some(activate_proc), Some(shutdown_proc)) =
            (resolve(module, "WebCoreActivate"), resolve(module, "WebCoreShutdown"))
        else {
            eprintln!("  [error]   could not resolve WebCoreActivate / WebCoreShutdown");
            // SAFETY: module is the live HMODULE from load_engine.
            unsafe { windows_sys::Win32::Foundation::FreeLibrary(module) };
            return Err(());
        };

        // SAFETY: the resolved exports have the documented HWC signatures.
        let web_core_activate: WebCoreActivateFn = unsafe { std::mem::transmute(activate_proc) };
        // SAFETY: as above.
        let web_core_shutdown: WebCoreShutdownFn = unsafe { std::mem::transmute(shutdown_proc) };

        let app_host_w = wide(&app_host_path(config).display().to_string());
        let web_w = wide(&config.web.display().to_string());
        let instance_w = wide(INSTANCE_NAME);

        println!("  [activate] WebCoreActivate(instance={INSTANCE_NAME}, port={SITE_PORT}) ...");
        // SAFETY: all three arguments are valid NUL-terminated wide strings that
        // outlive the call.
        let hr =
            unsafe { web_core_activate(app_host_w.as_ptr(), web_w.as_ptr(), instance_w.as_ptr()) };

        let result = if hr == 0 {
            println!("  [ok]      host activated (HRESULT 0x00000000)");
            if std::env::var_os("WORDY_HOST_HTTP").is_some() {
                crate::http::drive_smoke_routes(SITE_PORT)
            } else {
                Ok(())
            }
        } else {
            eprintln!(
                "  [error]   WebCoreActivate failed: HRESULT 0x{:08X}{}",
                hr as u32,
                describe_hresult(hr)
            );
            Err(())
        };

        if hr == 0 {
            // SAFETY: the engine is active; request an immediate shutdown.
            let shr = unsafe { web_core_shutdown(1) };
            if shr == 0 {
                println!("  [ok]      host shut down");
            } else {
                eprintln!("  [warn]    WebCoreShutdown returned HRESULT 0x{:08X}", shr as u32);
            }
        }

        // SAFETY: module is the live HMODULE from load_engine; freed after shutdown.
        unsafe { windows_sys::Win32::Foundation::FreeLibrary(module) };
        result
    }

    /// The `applicationHost.config` path to activate: the `WORDY_HOST_CONFIG`
    /// override if set, else the generated one.
    fn app_host_path(config: &ConfigPaths) -> PathBuf {
        std::env::var_os("WORDY_HOST_CONFIG")
            .map(PathBuf::from)
            .unwrap_or_else(|| config.app_host.clone())
    }

    /// A friendly suffix for the well-known HWC activation error codes.
    fn describe_hresult(hr: i32) -> &'static str {
        match hr as u32 {
            // HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING)
            0x8007_0420 => " (a host is already active in this process)",
            // HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE)
            0x8007_0426 => " (no host is active)",
            // HRESULT_FROM_WIN32(ERROR_INVALID_DATA)
            0x8007_000D => " (the configuration is invalid)",
            // HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
            0x8007_0002 => " (a configured file was not found)",
            _ => "",
        }
    }

    /// `LoadLibraryExW` the engine by absolute path, adding the `inetsrv`
    /// dependency directory; returns the live module handle or `None`.
    fn load_engine(hwebcore: &Path, inetsrv: &Path) -> Option<*mut core::ffi::c_void> {
        use windows_sys::Win32::System::LibraryLoader::{
            AddDllDirectory, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR,
            LoadLibraryExW,
        };
        let inetsrv_w = wide(&inetsrv.display().to_string());
        // SAFETY: inetsrv_w is a valid NUL-terminated wide string.
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
            eprintln!("  [error]   LoadLibraryExW(hwebcore.dll) failed: {err}");
            None
        } else {
            Some(module)
        }
    }

    /// Resolve an export from the loaded engine.
    fn resolve(
        module: *mut core::ffi::c_void,
        name: &str,
    ) -> Option<unsafe extern "system" fn() -> isize> {
        let name = c_string(name);
        // SAFETY: module is a live HMODULE; name is a NUL-terminated PCSTR.
        unsafe { windows_sys::Win32::System::LibraryLoader::GetProcAddress(module, name.as_ptr()) }
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

/// A minimal, dependency-free HTTP/1.1 client used to drive the activated host
/// over real TCP (MW16-3). Only used on Windows, behind genuine activation.
#[cfg(windows)]
mod http {
    use std::io::{Read, Write};
    use std::net::TcpStream;
    use std::time::Duration;

    /// Drive each `wordy` route once against `127.0.0.1:port` and assert the
    /// expected status / body fragments. Returns `Err` on the first failure.
    pub fn drive_smoke_routes(port: u16) -> Result<(), ()> {
        let checks: &[(&str, &str, &str, u16, &str)] = &[
            ("GET", "/healthz", "", 200, "\"status\""),
            ("POST", "/spellcheck", r#"{"words":["hello","helo"]}"#, 200, "\"results\""),
            ("POST", "/anagram", r#"{"template":"c.t","tray":"a","wildcards":0}"#, 200, "cat"),
            ("GET", "/shared?pattern=c.t", "", 200, "cat"),
            ("POST", "/custom/widget", "", 200, "\"added\""),
            ("GET", "/custom/widget", "", 200, "\"exists\""),
            ("DELETE", "/custom/widget", "", 200, "\"removed\""),
        ];
        let mut ok = true;
        for &(method, path, body, want_status, want_fragment) in checks {
            match request(port, method, path, body) {
                Ok((status, response_body)) => {
                    let pass = status == want_status && response_body.contains(want_fragment);
                    println!(
                        "  [http]    {method} {path} -> {status} {}",
                        if pass { "ok" } else { "UNEXPECTED" }
                    );
                    ok &= pass;
                }
                Err(e) => {
                    eprintln!("  [http]    {method} {path} -> error: {e}");
                    ok = false;
                }
            }
        }
        if ok { Ok(()) } else { Err(()) }
    }

    /// Issue one HTTP/1.1 request and return `(status, body)`.
    fn request(port: u16, method: &str, path: &str, body: &str) -> std::io::Result<(u16, String)> {
        let mut stream = TcpStream::connect(("127.0.0.1", port))?;
        stream.set_read_timeout(Some(Duration::from_secs(5)))?;
        stream.set_write_timeout(Some(Duration::from_secs(5)))?;

        let mut req = format!(
            "{method} {path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nX-Wordy-User: wordy-host\r\n"
        );
        req.push_str("Content-Type: application/json\r\n");
        req.push_str(&format!("Content-Length: {}\r\n", body.len()));
        req.push_str("\r\n");
        req.push_str(body);
        stream.write_all(req.as_bytes())?;

        let mut raw = Vec::new();
        stream.read_to_end(&mut raw)?;
        let text = String::from_utf8_lossy(&raw).into_owned();
        if std::env::var_os("WORDY_HOST_DUMP").is_some() {
            eprintln!("--- raw response for {method} {path} ---\n{text}\n--- end ---");
        }
        let status = parse_status(&text).unwrap_or(0);
        let body = text.split_once("\r\n\r\n").map(|(_, b)| b.to_string()).unwrap_or_default();
        Ok((status, body))
    }

    /// Parse the status code from an HTTP response's status line.
    fn parse_status(response: &str) -> Option<u16> {
        response.lines().next()?.split_whitespace().nth(1)?.parse().ok()
    }
}
