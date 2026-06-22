// Copyright (c) Microsoft Corporation.

//! `block_on`: drive a future to completion on the calling thread (EX-D3).

use std::future::Future;
use std::pin::pin;
use std::sync::{Arc, Condvar, Mutex};
use std::task::{Context, Poll, Wake, Waker};

/// Park/unpark state shared between the blocking thread and the waker.
struct Parker {
    /// `true` once the future has been woken since the last park.
    notified: Mutex<bool>,
    cv: Condvar,
}

impl Parker {
    fn new() -> Arc<Self> {
        Arc::new(Self {
            notified: Mutex::new(false),
            cv: Condvar::new(),
        })
    }

    /// Block until [`Parker::unpark`] (i.e. `Wake`) is signalled, then consume
    /// the notification.
    fn park(&self) {
        let mut notified = self.notified.lock().unwrap();
        while !*notified {
            notified = self.cv.wait(notified).unwrap();
        }
        *notified = false;
    }
}

impl Wake for Parker {
    fn wake(self: Arc<Self>) {
        self.wake_by_ref();
    }

    fn wake_by_ref(self: &Arc<Self>) {
        let mut notified = self.notified.lock().unwrap();
        *notified = true;
        self.cv.notify_one();
    }
}

/// Drive `future` to completion on the calling thread.
///
/// Polls the future, and whenever it returns `Pending`, parks the calling
/// thread until the future's waker is signalled. This uses no thread-pool
/// threads, so it composes with [`crate::Executor::spawn`]: blocking on a
/// `JoinHandle` polls the pooled task and parks until a pool thread completes
/// it (EX-D3).
pub fn block_on<F: Future>(future: F) -> F::Output {
    let parker = Parker::new();
    let waker: Waker = parker.clone().into();
    let mut cx = Context::from_waker(&waker);
    let mut future = pin!(future);

    loop {
        match future.as_mut().poll(&mut cx) {
            Poll::Ready(output) => return output,
            Poll::Pending => parker.park(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::future::Future;
    use std::pin::Pin;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::task::Waker as StdWaker;
    use std::thread;
    use std::time::Duration;

    #[test]
    fn ready_future_returns_immediately() {
        assert_eq!(block_on(async { 7 }), 7);
    }

    #[test]
    fn awaiting_chain_of_ready_points_resolves() {
        let value = block_on(async {
            let a = async { 2 }.await;
            let b = async { 3 }.await;
            a * b
        });
        assert_eq!(value, 6);
    }

    /// A future that returns `Pending` once, arranging to be woken from another
    /// thread, then completes — exercising the park/unpark path.
    struct WakeFromThread {
        polled: AtomicBool,
        woke: Arc<AtomicBool>,
    }

    impl Future for WakeFromThread {
        type Output = u32;

        fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<u32> {
            if self.polled.swap(true, Ordering::SeqCst) {
                Poll::Ready(42)
            } else {
                let waker: StdWaker = cx.waker().clone();
                let woke = Arc::clone(&self.woke);
                thread::spawn(move || {
                    thread::sleep(Duration::from_millis(5));
                    woke.store(true, Ordering::SeqCst);
                    waker.wake();
                });
                Poll::Pending
            }
        }
    }

    #[test]
    fn parks_until_woken_from_another_thread() {
        let woke = Arc::new(AtomicBool::new(false));
        let fut = WakeFromThread {
            polled: AtomicBool::new(false),
            woke: Arc::clone(&woke),
        };
        assert_eq!(block_on(fut), 42);
        assert!(woke.load(Ordering::SeqCst));
    }
}
