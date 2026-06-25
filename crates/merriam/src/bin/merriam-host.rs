// Copyright (c) Microsoft Corporation.
//
// `merriam-host` — run the dictionary-store service over http.sys.
//
// Configuration (environment):
//   MERRIAM_URL   full UrlPrefix to bind (default http://127.0.0.1:8099/)
//   MERRIAM_PORT  port for the default 127.0.0.1 URL (ignored if MERRIAM_URL set)
//   MERRIAM_ROOT  store root directory (default %TEMP%\merriam-store)
//
// On a successful bind it prints a readiness line and serves until terminated.
// Binding needs a urlacl reservation or elevation; otherwise it exits non-zero
// with a hint.

#[cfg(windows)]
fn main() {
    use std::io::Write;
    use std::sync::atomic::AtomicBool;

    use merriam::{ERROR_ACCESS_DENIED, Server, Service, Store};

    let url = std::env::var("MERRIAM_URL").unwrap_or_else(|_| {
        let port = std::env::var("MERRIAM_PORT").unwrap_or_else(|_| "8099".to_string());
        format!("http://127.0.0.1:{port}/")
    });
    let root = std::env::var("MERRIAM_ROOT").unwrap_or_else(|_| {
        std::env::temp_dir().join("merriam-store").to_string_lossy().into_owned()
    });

    let service = Service::new(Store::new(&root));

    match Server::bind(&url) {
        Ok(server) => {
            println!("merriam listening on {url} (root={root})");
            let _ = std::io::stdout().flush();
            // Never set: serve until the process is terminated.
            let stop = AtomicBool::new(false);
            server.serve(&service, &stop);
        }
        Err(code) => {
            eprintln!("merriam: failed to bind {url}: error {code}");
            if code == ERROR_ACCESS_DENIED {
                eprintln!(
                    "hint: reserve the URL (e.g. `netsh http add urlacl url={url} user=<you>`) \
                     or run elevated."
                );
            }
            std::process::exit(1);
        }
    }
}

#[cfg(not(windows))]
fn main() {
    eprintln!("merriam is Windows-only");
    std::process::exit(1);
}
