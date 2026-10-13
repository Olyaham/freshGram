//! Shared transcript types without I/O or provider implementations.

use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};

/// A timestamped transcription segment.
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct AsrSegment {
    pub start_sec: f32,
    pub end_sec: f32,
    pub text: String,
}

/// Raw transcription returned by one provider.
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct Transcript {
    pub provider: String,
    pub model: String,
    pub request_id: Option<String>,
    pub text: String,
    pub segments: Vec<AsrSegment>,
}

/// Shared cancellation state for host and provider operations.
#[derive(Clone, Debug, Default)]
pub struct CancelToken {
    flag: Arc<AtomicBool>,
    notify: Arc<tokio::sync::Notify>,
}

impl CancelToken {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn cancel(&self) {
        self.flag.store(true, Ordering::SeqCst);
        self.notify.notify_waiters();
    }

    pub fn is_cancelled(&self) -> bool {
        self.flag.load(Ordering::SeqCst)
    }

    pub async fn cancelled(&self) {
        loop {
            let notified = self.notify.notified();
            tokio::pin!(notified);
            notified.as_mut().enable();
            if self.is_cancelled() {
                return;
            }
            notified.await;
        }
    }

    /// Atomic flag for native cancellation bridges, such as Whisper abort.
    pub fn flag(&self) -> Arc<AtomicBool> {
        Arc::clone(&self.flag)
    }

    pub fn check(&self) -> Result<(), Cancelled> {
        if self.is_cancelled() {
            Err(Cancelled)
        } else {
            Ok(())
        }
    }
}

/// Typed cancellation marker recognized by [`is_cancelled`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Cancelled;

impl std::fmt::Display for Cancelled {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("cancelled")
    }
}

impl std::error::Error for Cancelled {}

/// Distinguish cancellation from a provider failure across anyhow boundaries.
pub fn is_cancelled(error: &anyhow::Error) -> bool {
    error.is::<Cancelled>()
}
