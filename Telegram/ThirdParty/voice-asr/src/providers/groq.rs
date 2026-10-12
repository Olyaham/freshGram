//! Groq preset profile of the shared OpenAI-compatible transport.
//!
//! Flow adapted from NedoBot (`Mar2ianen/NedoBot`, MIT):
//! `verbose_json` + `timestamp_granularities[]=segment`, request id kept for
//! diagnostics.

use crate::config::AsrConfig;
use crate::types::{CancelToken, Transcript, TranscriptError};

use super::{TranscribeInput, TranscriptionProvider, openai_compat::OpenAiCompatTranscriber};

pub struct GroqProvider {
    inner: OpenAiCompatTranscriber,
}

impl GroqProvider {
    pub fn new(cfg: &AsrConfig, client: reqwest::Client) -> Self {
        Self {
            inner: OpenAiCompatTranscriber {
                api_base: cfg.groq.api_base.clone(),
                api_key: cfg.groq.api_key.clone(),
                model: cfg.groq.model.clone(),
                temperature: cfg.groq.temperature,
                language: cfg.language.clone(),
                timeout_secs: cfg.groq.timeout_secs,
                client,
            },
        }
    }
}

impl TranscriptionProvider for GroqProvider {
    fn name(&self) -> &'static str {
        "groq"
    }

    async fn transcribe(
        &self,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<Transcript, TranscriptError> {
        cancel.check()?;
        if self.inner.api_key.trim().is_empty() {
            return Err(TranscriptError::Unsupported("groq api key is empty".into()));
        }
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
