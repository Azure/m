# squeaky

A configurable HTTP stress client for [`wordy`](../wordy) built on the
**native Rust async web stack** — Tokio + `reqwest`. It is the Rust-native
counterpart to the WinHTTP-based [`talky`](../talky) client; the two share a
config schema but no code. Cross-platform.

## Usage

```sh
squeaky [config.json]   # defaults to squeaky.json in the working directory
```

The client loads its parameters from a JSON config (see
[`squeaky.example.json`](squeaky.example.json)), drives the configured worker
tasks against the endpoint(s) for the configured duration / request count, and
prints an overall + per-route summary.

## Configuration

| Field | Type | Default | Meaning |
|---|---|---|---|
| `endpoints` | `[string]` | — | Base URLs, IPv4 or IPv6 (`http://127.0.0.1:8080`, `http://[::1]:8080`). |
| `ip_version` | `"v4"`\|`"v6"`\|`"both"` | `both` | Which endpoint families to drive. |
| `workers` | int | `8` | Concurrent Tokio worker tasks. |
| `duration_secs` | int | `0` | Time bound (`0` = none). |
| `max_requests` | int | `0` | Total request bound (`0` = none). |
| `request_timeout_ms` | int | `5000` | Per-request `reqwest` timeout. |
| `user` | string | `squeaky` | `X-Wordy-User` identity. |
| `seed` | int | `1` | Workload PRNG seed (reproducible). |
| `routes` | `[{name,weight}]` | all routes | Weighted route mix. |

At least one of `duration_secs` / `max_requests` must be positive. Route names:
`health`, `spellcheck`, `anagram`, `shared_enum`, `custom_add`, `custom_exists`,
`custom_remove`, `custom_enum`.

## Note on the target

`wordy` is not yet reachable over genuine HTTP (live Hostable Web Core hosting is
deferred to windows-win32-shim **MW16**). Point `squeaky` at the live service
once it lands, or at a stand-in HTTP endpoint meanwhile. See
[`DESIGN-NOTES.md`](DESIGN-NOTES.md) for the design.
