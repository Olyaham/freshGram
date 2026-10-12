//! Provider abstraction and registry.
//!
//! Static dispatch through [`Provider`]: no trait objects, no boxing,
//! every backend is exhaustively matched.

mod custom;
mod gemini;
mod groq;
#[cfg(feature = "local")]
mod local;
mod openai_compat;
mod transport;

use std::path::PathBuf;

use crate::config::AsrConfig;
use crate::types::{AsrProvider, CancelToken, Transcript, TranscriptError};

/// Audio to transcribe. The file must already exist locally; downloading
/// and format conversion are the host's job.
pub struct TranscribeInput {
    pub audio_path: PathBuf,
    pub filename: String,
    pub mime: Option<String>,
}

pub(crate) trait TranscriptionProvider: Send + Sync {
    fn name(&self) -> &'static str;
    async fn transcribe(
        &self,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<Transcript, TranscriptError>;
}

pub(crate) enum Provider {
    Groq(groq::GroqProvider),
    Gemini(gemini::GeminiProvider),
    Custom(custom::CustomProvider),
    #[cfg(feature = "local")]
    Local(local::LocalWhisper),
}

impl Provider {
    pub(crate) fn name(&self) -> &'static str {
        match self {
            Provider::Groq(provider) => provider.name(),
            Provider::Gemini(provider) => provider.name(),
            Provider::Custom(provider) => provider.name(),
            #[cfg(feature = "local")]
            Provider::Local(provider) => provider.name(),
        }
    }

    pub(crate) async fn transcribe(
        &self,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<Transcript, TranscriptError> {
        match self {
            Provider::Groq(provider) => provider.transcribe(input, cancel).await,
            Provider::Gemini(provider) => provider.transcribe(input, cancel).await,
            Provider::Custom(provider) => provider.transcribe(input, cancel).await,
            #[cfg(feature = "local")]
            Provider::Local(provider) => provider.transcribe(input, cancel).await,
        }
    }
}

pub(crate) fn provider_for(
    provider: AsrProvider,
    cfg: &AsrConfig,
    client: reqwest::Client,
) -> Result<Provider, TranscriptError> {
    match provider {
        AsrProvider::Groq => Ok(Provider::Groq(groq::GroqProvider::new(cfg, client))),
        AsrProvider::Custom => Ok(Provider::Custom(custom::CustomProvider::new(cfg, client))),
        AsrProvider::Gemini => Ok(Provider::Gemini(gemini::GeminiProvider::new(cfg, client))),
        #[cfg(feature = "local")]
        AsrProvider::Local => Ok(Provider::Local(local::LocalWhisper::new(cfg))),
        #[cfg(not(feature = "local"))]
        AsrProvider::Local => Err(TranscriptError::Unsupported(
            "local engine was not compiled in (enable the `local` feature)".to_string(),
        )),
    }
}
