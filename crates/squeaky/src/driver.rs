// Copyright (c) Microsoft Corporation.

//! The async stress driver for `squeaky`: fan out Tokio worker tasks, each
//! looping over synthesized requests (driven by `reqwest`) until the configured
//! time / count bound, then merge their statistics.
//!
//! Using Tokio + `reqwest` is the Rust-native counterpart to `talky`'s WinHTTP +
//! Windows-thread-pool stack: the two clients exercise `wordy` over two
//! different technology stacks while sharing a config schema.

use std::sync::Arc;
use std::time::{Duration, Instant};

use reqwest::{Client, Method};

use crate::config::{Config, Endpoint};
use crate::stats::{Outcome, RouteStats};
use crate::workload::{LogicalRequest, Method as RouteMethod, Rng, Workload};

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
        let mut out = overall.report(self.elapsed, "squeaky");
        out.push_str(&self.route_stats.report());
        out
    }
}

/// Run the stress workload described by `config`.
pub async fn run(config: &Config) -> Result<RunReport, String> {
    let endpoints = Arc::new(config.select_endpoints()?);
    let workload = Arc::new(Workload::new(&config.routes));
    let client = Client::builder()
        .timeout(Duration::from_millis(u64::from(config.request_timeout_ms)))
        .build()
        .map_err(|e| format!("failed to build HTTP client: {e}"))?;

    let deadline = (config.duration_secs > 0)
        .then(|| Instant::now() + Duration::from_secs(config.duration_secs));
    let max_per_worker =
        (config.max_requests > 0).then(|| config.max_requests.div_ceil(u64::from(config.workers)));

    let start = Instant::now();
    let mut handles = Vec::with_capacity(config.workers as usize);
    for worker_id in 0..config.workers {
        let seed = config
            .seed
            .wrapping_add(u64::from(worker_id).wrapping_mul(0x9E37_79B9_7F4A_7C15));
        let ctx = WorkerContext {
            client: client.clone(),
            workload: Arc::clone(&workload),
            endpoints: Arc::clone(&endpoints),
            user: config.user.clone(),
            seed,
            worker_id,
            deadline,
            max_per_worker,
        };
        handles.push(tokio::spawn(run_worker(ctx)));
    }

    let mut route_stats = RouteStats::default();
    for handle in handles {
        let stats = handle.await.map_err(|e| format!("worker task failed: {e}"))?;
        route_stats.merge(&stats);
    }
    let elapsed = start.elapsed();

    Ok(RunReport {
        route_stats,
        elapsed,
    })
}

/// The inputs one worker task owns.
struct WorkerContext {
    client: Client,
    workload: Arc<Workload>,
    endpoints: Arc<Vec<Endpoint>>,
    user: String,
    seed: u64,
    worker_id: u32,
    deadline: Option<Instant>,
    max_per_worker: Option<u64>,
}

/// Drive one worker until its bound, returning its statistics.
async fn run_worker(ctx: WorkerContext) -> RouteStats {
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
        let url = format!("{}{}", endpoint.base_url(), request.path);

        let started = Instant::now();
        let outcome = send(&ctx.client, &request, &url, &ctx.user).await;
        stats.record(request.route, outcome, started.elapsed());
        count += 1;
    }
    stats
}

/// Issue one request and classify its outcome.
async fn send(client: &Client, request: &LogicalRequest, url: &str, user: &str) -> Outcome {
    let method = match request.method {
        RouteMethod::Get => Method::GET,
        RouteMethod::Post => Method::POST,
        RouteMethod::Delete => Method::DELETE,
    };
    let mut builder = client.request(method, url).header("X-Wordy-User", user);
    if let Some(body) = &request.body {
        builder = builder
            .header("Content-Type", "application/json")
            .body(body.clone());
    }
    match builder.send().await {
        Ok(response) => Outcome::Status(response.status().as_u16()),
        Err(_) => Outcome::TransportError,
    }
}
