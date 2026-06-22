// Copyright (c) Microsoft Corporation.

//! Safe timer API over the Windows thread pool.
//!
//! [`Timer`] is a one-shot timer; [`PeriodicTimer`] re-fires on a fixed period.
//! Both run their callback on a pool thread (no dedicated thread is consumed
//! while waiting) and mirror the C++ `m::timer` / `m::periodic_timer` shape:
//! `schedule`, `is_set`, `cancel`, `wait`.

use std::time::Duration;

use windows_sys::Win32::Foundation::FILETIME;

use crate::error::ThreadPoolResult;
use crate::ffi;

/// Coalescing window handed to the OS, in milliseconds.
///
/// Zero keeps expirations prompt (the foundation favors predictability over the
/// power-saving coalescing the C++ library opts into with a 100 ms window).
const WINDOW_MS: u32 = 0;

/// Convert a relative delay into the negative-100-ns `FILETIME` the thread-pool
/// timer API uses for a relative due time.
fn relative_due_time(after: Duration) -> FILETIME {
    // 100-ns ticks, negated for "relative" per SetThreadpoolTimer. i128 math
    // avoids overflow for any plausible delay before the i64 cast.
    let ticks = (after.as_nanos() / 100) as i128;
    let raw = (-ticks) as i64 as u64;
    FILETIME {
        dwLowDateTime: raw as u32,
        dwHighDateTime: (raw >> 32) as u32,
    }
}

/// Saturate a `Duration` to whole milliseconds for the timer period.
fn period_ms(period: Duration) -> u32 {
    u32::try_from(period.as_millis()).unwrap_or(u32::MAX)
}

/// A one-shot thread-pool timer.
///
/// Dropping cancels any pending expiration and waits for an in-flight callback
/// before releasing OS resources.
pub struct Timer {
    inner: ffi::Timer,
}

impl Timer {
    /// Create an unset one-shot timer that runs `callback` when it expires.
    pub fn new<F>(callback: F) -> ThreadPoolResult<Self>
    where
        F: Fn() + Send + Sync + 'static,
    {
        Ok(Self {
            inner: ffi::Timer::new(Box::new(callback))?,
        })
    }

    /// Arm the timer to fire once, `after` from now.
    pub fn schedule(&self, after: Duration) {
        self.inner.set(relative_due_time(after), 0, WINDOW_MS);
    }

    /// Whether the timer has a pending expiration.
    #[must_use]
    pub fn is_set(&self) -> bool {
        self.inner.is_set()
    }

    /// Cancel a pending expiration. The timer can be re-armed afterward.
    pub fn cancel(&self) {
        self.inner.cancel();
    }

    /// Wait for an in-flight expiration callback to finish.
    pub fn wait(&self) {
        self.inner.wait_for_callbacks(false);
    }
}

/// A periodic thread-pool timer that re-fires on a fixed period until cancelled.
pub struct PeriodicTimer {
    inner: ffi::Timer,
}

impl PeriodicTimer {
    /// Create an unset periodic timer that runs `callback` on each expiration.
    pub fn new<F>(callback: F) -> ThreadPoolResult<Self>
    where
        F: Fn() + Send + Sync + 'static,
    {
        Ok(Self {
            inner: ffi::Timer::new(Box::new(callback))?,
        })
    }

    /// Arm the timer: first fire `first_after` from now, then every `period`.
    pub fn schedule(&self, first_after: Duration, period: Duration) {
        self.inner
            .set(relative_due_time(first_after), period_ms(period), WINDOW_MS);
    }

    /// Whether the timer has a pending expiration.
    #[must_use]
    pub fn is_set(&self) -> bool {
        self.inner.is_set()
    }

    /// Cancel future expirations. The timer can be re-armed afterward.
    pub fn cancel(&self) {
        self.inner.cancel();
    }

    /// Wait for an in-flight expiration callback to finish.
    pub fn wait(&self) {
        self.inner.wait_for_callbacks(false);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    use std::sync::atomic::{AtomicU32, Ordering};
    use std::time::Duration;

    #[test]
    fn one_shot_timer_fires_once() {
        let counter = Arc::new(AtomicU32::new(0));
        let c = Arc::clone(&counter);
        let timer = Timer::new(move || {
            c.fetch_add(1, Ordering::SeqCst);
        })
        .expect("create");
        timer.schedule(Duration::from_millis(5));
        // Poll for the fire rather than sleeping a fixed time.
        for _ in 0..200 {
            if counter.load(Ordering::SeqCst) == 1 {
                break;
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        timer.wait();
        assert_eq!(counter.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn periodic_timer_fires_repeatedly_then_cancels() {
        let counter = Arc::new(AtomicU32::new(0));
        let c = Arc::clone(&counter);
        let timer = PeriodicTimer::new(move || {
            c.fetch_add(1, Ordering::SeqCst);
        })
        .expect("create");
        timer.schedule(Duration::from_millis(2), Duration::from_millis(2));
        for _ in 0..500 {
            if counter.load(Ordering::SeqCst) >= 3 {
                break;
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        timer.cancel();
        timer.wait();
        assert!(
            counter.load(Ordering::SeqCst) >= 3,
            "expected at least 3 fires, got {}",
            counter.load(Ordering::SeqCst)
        );
    }

    #[test]
    fn cancel_before_fire_prevents_callback() {
        let counter = Arc::new(AtomicU32::new(0));
        let c = Arc::clone(&counter);
        let timer = Timer::new(move || {
            c.fetch_add(1, Ordering::SeqCst);
        })
        .expect("create");
        timer.schedule(Duration::from_secs(60));
        assert!(timer.is_set());
        timer.cancel();
        assert!(!timer.is_set());
        timer.wait();
        assert_eq!(counter.load(Ordering::SeqCst), 0);
    }
}
