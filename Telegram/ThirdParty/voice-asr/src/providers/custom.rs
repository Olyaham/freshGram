//! Custom user-defined profile of the shared OpenAI-compatible transport.
//!
//! Any `/audio/transcriptions` endpoint (self-hosted whisper.cpp server,
//! third-party gateways): base URL + key + model, same wire as Groq.

use crate::config::AsrConfig;
use crate::types::{CancelToken, Transcript, TranscriptError};

use super::{TranscribeInput, TranscriptionProvider, openai_compat::OpenAiCompatTranscriber};

pub struct CustomProvider {
    inner: OpenAiCompatTranscriber,
}

impl CustomProvider {
    pub fn new(cfg: &AsrConfig, client: reqwest::Client) -> Self {
        Self {
            inner: OpenAiCompatTranscriber {
                api_base: cfg.custom.api_base.clone(),
                api_key: cfg.custom.api_key.clone(),
                model: cfg.custom.model.clone(),
                temperature: cfg.custom.temperature,
                language: cfg.language.clone(),
                timeout_secs: cfg.custom.timeout_secs,
                client,
            },
        }
    }
}

impl TranscriptionProvider for CustomProvider {
    fn name(&self) -> &'static str {
        "custom"
    }

    async fn transcribe(
        &self,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<Transcript, TranscriptError> {
        let out = self.inner.transcribe(input, cancel).await?;
        Ok(Transcript {
            // Labeled by `Engine` from the registry; see `Provider::name`.
            provider: String::new(),
            model: out.model,
            request_id: out.request_id,
            text: out.text,
            segments: out.segments,
        })
    }
}
