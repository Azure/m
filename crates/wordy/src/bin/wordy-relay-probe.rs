// Copyright (c) Microsoft Corporation.
//
// `wordy-relay-probe` — a one-shot driver of `wordy`'s real `merriam` relay
// client (MW18-4). It performs a single custom-dictionary relay operation and
// reports the outcome **solely through its exit code**, because when this binary
// is relinked against the shim's alias object its `WriteFile` (and thus stdout)
// is rerouted into the shim and swallowed — the same reason `linkproof` /
// `egressproof` are exit-code-driven.
//
// Usage: wordy-relay-probe <add|contains> <host> <port> <word> [user]
//
// Exit codes:
//   0  the operation returned true   (add: newly added; contains: present)
//   1  the operation returned false  (add: already there; contains: absent)
//   2  the backing answered non-200  (e.g. a buffered 202 ack)
//   3  the transport failed          (e.g. the target refused the connection)
//   4  usage error
//   5  the response could not be parsed

#[cfg(windows)]
fn main() {
    use wordy::relay::{MerriamClient, RelayError};

    let args: Vec<String> = std::env::args().collect();
    if args.len() < 5 {
        eprintln!("usage: wordy-relay-probe <add|contains> <host> <port> <word> [user]");
        std::process::exit(4);
    }
    let op = args[1].as_str();
    let host = args[2].clone();
    let port: u16 = match args[3].parse() {
        Ok(p) => p,
        Err(_) => std::process::exit(4),
    };
    let word = args[4].as_str();
    let user = args.get(5).map(String::as_str).unwrap_or("probe");

    let client = MerriamClient::new(host, port);
    let result = match op {
        "add" => client.add("en-US", user, word),
        "contains" => client.contains("en-US", user, word),
        _ => {
            eprintln!("unknown op {op:?}");
            std::process::exit(4);
        }
    };

    let code = match result {
        Ok(true) => 0,
        Ok(false) => 1,
        Err(RelayError::Upstream { .. }) => 2,
        Err(RelayError::Transport(_)) => 3,
        Err(RelayError::Parse(_)) => 5,
    };
    std::process::exit(code);
}

#[cfg(not(windows))]
fn main() {
    eprintln!("wordy-relay-probe is Windows-only");
    std::process::exit(4);
}
