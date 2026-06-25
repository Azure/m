// Copyright (c) Microsoft Corporation.

//! Stress statistics: per-worker accumulation, merging, and a final report.

use std::time::Duration;

use crate::config::Route;

/// The outcome of a single request attempt.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Outcome {
    /// The server returned an HTTP status code.
    Status(u16),
    /// The request failed before a status was received (connect / timeout / I/O).
    TransportError,
}

/// Accumulated counters and latency extremes for a run (or a worker's slice).
#[derive(Debug, Clone, Default)]
pub struct Stats {
    /// Total request attempts.
    pub attempts: u64,
    /// Responses with status `< 400`.
    pub success: u64,
    /// Responses with status `400..=499`.
    pub client_error: u64,
    /// Responses with status `>= 500`.
    pub server_error: u64,
    /// Attempts that never received a status.
    pub transport_error: u64,
    /// Sum of all observed latencies (for computing the mean).
    pub latency_total: Duration,
    /// The fastest observed latency.
    pub latency_min: Option<Duration>,
    /// The slowest observed latency.
    pub latency_max: Option<Duration>,
}

impl Stats {
    /// Record one attempt's outcome and latency.
    pub fn record(&mut self, outcome: Outcome, latency: Duration) {
        self.attempts += 1;
        match outcome {
            Outcome::Status(code) if code < 400 => self.success += 1,
            Outcome::Status(code) if code < 500 => self.client_error += 1,
            Outcome::Status(_) => self.server_error += 1,
            Outcome::TransportError => self.transport_error += 1,
        }
        self.latency_total += latency;
        self.latency_min = Some(self.latency_min.map_or(latency, |m| m.min(latency)));
        self.latency_max = Some(self.latency_max.map_or(latency, |m| m.max(latency)));
    }

    /// Fold another `Stats` into this one.
    pub fn merge(&mut self, other: &Stats) {
        self.attempts += other.attempts;
        self.success += other.success;
        self.client_error += other.client_error;
        self.server_error += other.server_error;
        self.transport_error += other.transport_error;
        self.latency_total += other.latency_total;
        self.latency_min = min_option(self.latency_min, other.latency_min);
        self.latency_max = max_option(self.latency_max, other.latency_max);
    }

    /// The mean latency over all recorded attempts, or zero if none.
    pub fn mean_latency(&self) -> Duration {
        if self.attempts == 0 {
            Duration::ZERO
        } else {
            self.latency_total / self.attempts as u32
        }
    }

    /// Render a multi-line human-readable report over the given wall-clock span.
    pub fn report(&self, elapsed: Duration, label: &str) -> String {
        let secs = elapsed.as_secs_f64().max(f64::MIN_POSITIVE);
        let throughput = self.attempts as f64 / secs;
        let mut out = String::new();
        out.push_str(&format!("{label} stress summary\n"));
        out.push_str(&format!("  elapsed:         {:.2}s\n", elapsed.as_secs_f64()));
        out.push_str(&format!("  attempts:        {}\n", self.attempts));
        out.push_str(&format!("  2xx/3xx success: {}\n", self.success));
        out.push_str(&format!("  4xx client err:  {}\n", self.client_error));
        out.push_str(&format!("  5xx server err:  {}\n", self.server_error));
        out.push_str(&format!("  transport err:   {}\n", self.transport_error));
        out.push_str(&format!("  throughput:      {throughput:.1} req/s\n"));
        out.push_str(&format!(
            "  latency min/avg/max: {} / {} / {}\n",
            fmt_ms(self.latency_min.unwrap_or_default()),
            fmt_ms(self.mean_latency()),
            fmt_ms(self.latency_max.unwrap_or_default()),
        ));
        out
    }
}

/// Per-route breakdown keyed by [`Route`], for an optional finer report.
#[derive(Debug, Clone, Default)]
pub struct RouteStats {
    entries: Vec<(Route, Stats)>,
}

impl RouteStats {
    /// Record an attempt against a route.
    pub fn record(&mut self, route: Route, outcome: Outcome, latency: Duration) {
        self.slot(route).record(outcome, latency);
    }

    /// Merge another per-route breakdown.
    pub fn merge(&mut self, other: &RouteStats) {
        for (route, stats) in &other.entries {
            self.slot(*route).merge(stats);
        }
    }

    /// The mutable stats slot for a route (created on first use).
    fn slot(&mut self, route: Route) -> &mut Stats {
        if let Some(idx) = self.entries.iter().position(|(r, _)| *r == route) {
            &mut self.entries[idx].1
        } else {
            self.entries.push((route, Stats::default()));
            &mut self.entries.last_mut().expect("just pushed").1
        }
    }

    /// The overall stats across all routes.
    pub fn overall(&self) -> Stats {
        let mut total = Stats::default();
        for (_, stats) in &self.entries {
            total.merge(stats);
        }
        total
    }

    /// A per-route report block.
    pub fn report(&self) -> String {
        let mut out = String::from("  per-route attempts:\n");
        let mut entries = self.entries.clone();
        entries.sort_by_key(|(route, _)| format!("{route:?}"));
        for (route, stats) in entries {
            out.push_str(&format!(
                "    {:<14} attempts={:<7} ok={:<7} 4xx={:<5} 5xx={:<5} err={:<5} avg={}\n",
                format!("{route:?}"),
                stats.attempts,
                stats.success,
                stats.client_error,
                stats.server_error,
                stats.transport_error,
                fmt_ms(stats.mean_latency()),
            ));
        }
        out
    }
}

/// Combine two optional minima.
fn min_option(a: Option<Duration>, b: Option<Duration>) -> Option<Duration> {
    match (a, b) {
        (Some(a), Some(b)) => Some(a.min(b)),
        (some, None) | (None, some) => some,
    }
}

/// Combine two optional maxima.
fn max_option(a: Option<Duration>, b: Option<Duration>) -> Option<Duration> {
    match (a, b) {
        (Some(a), Some(b)) => Some(a.max(b)),
        (some, None) | (None, some) => some,
    }
}

/// Format a duration as milliseconds with one decimal.
fn fmt_ms(d: Duration) -> String {
    format!("{:.1}ms", d.as_secs_f64() * 1000.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn buckets_status_codes() {
        let mut s = Stats::default();
        s.record(Outcome::Status(200), Duration::from_millis(10));
        s.record(Outcome::Status(204), Duration::from_millis(20));
        s.record(Outcome::Status(404), Duration::from_millis(5));
        s.record(Outcome::Status(500), Duration::from_millis(30));
        s.record(Outcome::TransportError, Duration::from_millis(50));
        assert_eq!(s.attempts, 5);
        assert_eq!(s.success, 2);
        assert_eq!(s.client_error, 1);
        assert_eq!(s.server_error, 1);
        assert_eq!(s.transport_error, 1);
    }

    #[test]
    fn tracks_latency_extremes_and_mean() {
        let mut s = Stats::default();
        s.record(Outcome::Status(200), Duration::from_millis(10));
        s.record(Outcome::Status(200), Duration::from_millis(30));
        s.record(Outcome::Status(200), Duration::from_millis(20));
        assert_eq!(s.latency_min, Some(Duration::from_millis(10)));
        assert_eq!(s.latency_max, Some(Duration::from_millis(30)));
        assert_eq!(s.mean_latency(), Duration::from_millis(20));
    }

    #[test]
    fn merge_combines_counts_and_extremes() {
        let mut a = Stats::default();
        a.record(Outcome::Status(200), Duration::from_millis(10));
        let mut b = Stats::default();
        b.record(Outcome::Status(500), Duration::from_millis(40));
        a.merge(&b);
        assert_eq!(a.attempts, 2);
        assert_eq!(a.success, 1);
        assert_eq!(a.server_error, 1);
        assert_eq!(a.latency_min, Some(Duration::from_millis(10)));
        assert_eq!(a.latency_max, Some(Duration::from_millis(40)));
    }

    #[test]
    fn empty_stats_have_zero_mean() {
        assert_eq!(Stats::default().mean_latency(), Duration::ZERO);
    }

    #[test]
    fn route_stats_aggregate_to_overall() {
        let mut rs = RouteStats::default();
        rs.record(Route::Health, Outcome::Status(200), Duration::from_millis(1));
        rs.record(Route::Anagram, Outcome::Status(200), Duration::from_millis(3));
        rs.record(Route::Anagram, Outcome::Status(400), Duration::from_millis(2));
        let overall = rs.overall();
        assert_eq!(overall.attempts, 3);
        assert_eq!(overall.success, 2);
        assert_eq!(overall.client_error, 1);
    }

    #[test]
    fn report_mentions_key_figures() {
        let mut s = Stats::default();
        s.record(Outcome::Status(200), Duration::from_millis(10));
        let r = s.report(Duration::from_secs(1), "talky");
        assert!(r.contains("talky stress summary"));
        assert!(r.contains("attempts:"));
        assert!(r.contains("throughput:"));
    }
}
