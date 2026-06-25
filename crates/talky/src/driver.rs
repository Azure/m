// Copyright (c) Microsoft Corporation.

//! The stress driver for `talky` (Windows only): fan out workers onto the
//! Windows thread pool, each looping over synthesized requests until the
//! configured time / count bound, then merge their statistics.
//!
//! Using `windows-threadpool` (rather than `std::thread`) is deliberate — it
//! keeps `talky` on the Microsoft-native stack end-to-end and mirrors `wordy`'s
//! own use of the OS thread pool. `squeaky` is the Rust-native counterpart
//! (Tokio).

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use windows_threadpool::submit_once;

use crate::config::{Config, Endpoint};
use crate::stats::{Outcome, RouteStats};
use crate::winhttp::WinHttpClient;
use crate::workload::{Rng, Workload};

/// The result of a stress run.
pub struct RunReport {
    /// Per-route (and overall) statistics.
    pub route_stats: RouteStats,
    /// Wall-clock duration of the run.
    pub elapsed: Duration,
}

impl RunReport {
    /// Render the overall summary followed by the per-route breakdown.
    pub fn render(&self) -> String {
        let overall = self.route_stats.overall();
        let mut out = overall.report(self.elapsed, "talky");
        out.push_str(&self.route_stats.report());
        out
    }
}

/// Run the stress workload described by `config`.
pub fn run(config: &Config) -> Result<RunReport, String> {
    let endpoints = Arc::new(config.select_endpoints()?);
    let workload = Arc::new(Workload::new(&config.routes));
    let client = Arc::new(
        WinHttpClient::new(config.request_timeout_ms)
            .map_err(|e| format!("WinHttpOpen failed (Win32 error {e})"))?,
    );

    let deadline = (config.duration_secs > 0)
        .then(|| Instant::now() + Duration::from_secs(config.duration_secs));
    let max_per_worker =
        (config.max_requests > 0).then(|| config.max_requests.div_ceil(u64::from(config.workers)));

    let results: Arc<Mutex<Vec<RouteStats>>> = Arc::new(Mutex::new(Vec::new()));
    let start = Instant::now();

    let mut works = Vec::with_capacity(config.workers as usize);
    for worker_id in 0..config.workers {
        let workload = Arc::clone(&workload);
        let client = Arc::clone(&client);
        let endpoints = Arc::clone(&endpoints);
        let results = Arc::clone(&results);
        let user = config.user.clone();
        let seed = config
            .seed
            .wrapping_add(u64::from(worker_id).wrapping_mul(0x9E37_79B9_7F4A_7C15));
        let work = submit_once(move || {
            let stats = run_worker(WorkerContext {
                workload: &workload,
                client: &client,
                endpoints: &endpoints,
                user: &user,
                seed,
                worker_id,
                deadline,
                max_per_worker,
            });
            results.lock().expect("results mutex").push(stats);
        })
        .map_err(|e| format!("thread-pool submit failed: {e}"))?;
        works.push(work);
    }

    for work in works {
        work.wait();
    }
    let elapsed = start.elapsed();

    let mut route_stats = RouteStats::default();
    for worker_stats in results.lock().expect("results mutex").iter() {
        route_stats.merge(worker_stats);
    }
    Ok(RunReport {
        route_stats,
        elapsed,
    })
}

/// The inputs one worker needs.
struct WorkerContext<'a> {
    workload: &'a Workload,
    client: &'a WinHttpClient,
    endpoints: &'a [Endpoint],
    user: &'a str,
    seed: u64,
    worker_id: u32,
    deadline: Option<Instant>,
    max_per_worker: Option<u64>,
}

/// Drive one worker until its bound, returning its statistics.
fn run_worker(ctx: WorkerContext<'_>) -> RouteStats {
    let mut rng = Rng::new(ctx.seed);
    let mut stats = RouteStats::default();
    let mut count = 0u64;
    let mut endpoint_index = ctx.worker_id as usize;

    loop {
        if let Some(deadline) = ctx.deadline
            && Instant::now() >= deadline
        {
            break;
        }
        if let Some(max) = ctx.max_per_worker
            && count >= max
        {
            break;
        }

        let request = ctx.workload.next_request(&mut rng);
        let endpoint = &ctx.endpoints[endpoint_index % ctx.endpoints.len()];
        endpoint_index = endpoint_index.wrapping_add(1);

        let started = Instant::now();
        let outcome = match ctx.client.send(endpoint, &request, ctx.user) {
            Ok(status) => Outcome::Status(status),
            Err(_) => Outcome::TransportError,
        };
        stats.record(request.route, outcome, started.elapsed());
        count += 1;
    }
    stats
}
