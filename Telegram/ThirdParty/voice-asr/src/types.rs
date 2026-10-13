//! Core transcript types, provider selector, and errors.
//!
//! Cancellation and transcript shapes are shared with AiApi
//! (`aiapi-types`) so hosts and the core observe one state.

use thiserror::Error;

pub use aiapi_types::{AsrSegment, CancelToken, Transcript};

#[derive(Clone, Copy, Debug, Eq, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TranscriptRenderMode {
    Short,
    Chapters,
    File,
}

#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct TranscriptChapter {
    pub title: String,
    pub start_sec: f32,
    pub end_sec: Option<f32>,
    pub text: String,
}

#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
pub struct CleanTranscript {
    pub mode: TranscriptRenderMode,
    pub text: String,
    pub chapters: Vec<TranscriptChapter>,
    pub short_summary: Option<String>,
}

#[derive(serde::Serialize)]
pub struct ProcessedTranscript {
    pub raw: Transcript,
    pub alternatives: Vec<Transcript>,
    pub clean: Option<CleanTranscript>,
    pub cleanup_provider: Option<String>,
    pub cleanup_model: Option<String>,
    pub cleanup_attempts: Vec<aiapi_llm::types::LlmAttempt>,
    pub no_speech: bool,
}

impl ProcessedTranscript {
    pub fn text(&self) -> &str {
        if self.no_speech {
            return "";
        }
        self.clean
            .as_ref()
            .map_or(&self.raw.text, |clean| &clean.text)
    }
}

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

impl From<aiapi_types::Cancelled> for TranscriptError {
    fn from(_: aiapi_types::Cancelled) -> Self {
        TranscriptError::Cancelled
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
pub(crate) fn map_http_error(err: reqwest::Error) -> TranscriptError {
    let clean = err.without_url();
    if clean.is_timeout() {
        TranscriptError::Timeout
    } else {
        TranscriptError::Transport(clean.to_string())
    }
}

/// Preserve core error categories at the ABI boundary.
pub(crate) fn map_transcript_error(error: anyhow::Error) -> TranscriptError {
    if aiapi_types::is_cancelled(&error) {
        TranscriptError::Cancelled
    } else if let Some(error) = error.downcast_ref::<aiapi_http::Error>() {
        match error {
            aiapi_http::Error::Timeout => TranscriptError::Timeout,
            aiapi_http::Error::Parse(message) => TranscriptError::Parse(message.clone()),
            aiapi_http::Error::Api(status) => TranscriptError::Api {
                status: *status,
                message: "provider request failed".into(),
            },
            aiapi_http::Error::Transport(message) => TranscriptError::Transport(message.clone()),
        }
    } else if error.is::<std::io::Error>() {
        TranscriptError::Io(error.to_string())
    } else if let Some(error) = error.downcast_ref::<aiapi_llm::types::LlmTransportError>() {
        use aiapi_llm::types::LlmTransportError;
        match error {
            LlmTransportError::Timeout => TranscriptError::Timeout,
            LlmTransportError::Configuration | LlmTransportError::UnsupportedFeature => {
                TranscriptError::Unsupported(error.to_string())
            }
            LlmTransportError::HttpStatus(status) => TranscriptError::Api {
                status: *status,
                message: "provider request failed".into(),
            },
            _ => TranscriptError::Parse(error.to_string()),
        }
    } else if error.is::<aiapi_asr::asr::Unsupported>() || error.is::<aiapi_config::MissingSecret>()
    {
        TranscriptError::Unsupported(error.to_string())
    } else {
        TranscriptError::Transport(error.to_string())
    }
}
