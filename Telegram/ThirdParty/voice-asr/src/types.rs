//! Core transcript types, cancellation token, and errors.

use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};

use thiserror::Error;

/// Transcription backend selector.
#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AsrProvider {
    Groq,
    Gemini,
    Custom,
    Local,
}

impl AsrProvider {
    pub fn name(self) -> &'static str {
        match self {
            AsrProvider::Groq => "groq",
            AsrProvider::Gemini => "gemini",
            AsrProvider::Custom => "custom",
            AsrProvider::Local => "local",
        }
    }
}

/// One timestamped transcript piece.
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct AsrSegment {
    pub start_sec: f32,
    pub end_sec: f32,
    pub text: String,
}

/// Transcription result. No raw provider payloads are kept: the host gets
/// text plus segments only.
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct Transcript {
    pub provider: String,
    pub model: String,
    pub request_id: Option<String>,
    pub text: String,
    pub segments: Vec<AsrSegment>,
}

/// Shared cancellation flag. Cheap to clone; every clone observes one state.
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

    #[cfg(feature = "local")]
    pub(crate) fn flag(&self) -> Arc<AtomicBool> {
        Arc::clone(&self.flag)
    }

    pub fn check(&self) -> Result<(), TranscriptError> {
        if self.is_cancelled() {
            Err(TranscriptError::Cancelled)
        } else {
            Ok(())
        }
    }
}

#[derive(Debug, Error)]
pub enum TranscriptError {
    #[error("cancelled")]
    Cancelled,
    #[error("transport: {0}")]
    Transport(String),
    #[error("api error {status}: {message}")]
    Api { status: u16, message: String },
    #[error("parse: {0}")]
    Parse(String),
    #[error("io: {0}")]
    Io(String),
    #[error("model: {0}")]
    Model(String),
    #[error("timeout")]
    Timeout,
    #[error("unsupported: {0}")]
    Unsupported(String),
}

/// Strip URLs/secrets from transport errors before they reach callers or logs.
pub(crate) fn clean_http_error(err: reqwest::Error) -> String {
    err.without_url().to_string()
}

pub(crate) fn map_http_error(err: reqwest::Error) -> TranscriptError {
    let clean = err.without_url();
    if clean.is_timeout() {
        TranscriptError::Timeout
    } else {
        TranscriptError::Transport(clean.to_string())
    }
}
