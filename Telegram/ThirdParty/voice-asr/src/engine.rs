//! Engine: owns the shared HTTP client, runs primary → fallback selection.
//!
//! The engine holds NO async runtime: async callers use `transcribe_async`
//! on their own runtime, while the blocking FFI path builds a throwaway
//! current-thread runtime per call. This keeps drops safe in every context.

use crate::audio::infer_audio_mime;
use crate::config::AsrConfig;
use crate::providers::{TranscribeInput, provider_for};
use crate::types::{AsrProvider, CancelToken, Transcript, TranscriptError};

pub struct Engine {
    client: reqwest::Client,
}

impl Engine {
    pub fn new() -> Result<Self, TranscriptError> {
        // No default timeout: each provider applies its own deadline so a
        // slow Gemini upload cannot inherit a Groq-sized budget and back.
        let client = reqwest::Client::builder()
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .map_err(crate::types::map_http_error)?;
        Ok(Self { client })
    }

    /// Blocking transcription for plain threads (FFI, UI workers).
    /// Must NOT be called from inside an async context.
    pub fn transcribe(
        &self,
        cfg: &AsrConfig,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<Transcript, TranscriptError> {
        tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .thread_name("voice-asr-sync")
            .build()
            .map_err(|e| TranscriptError::Io(e.to_string()))?
            .block_on(self.transcribe_async(cfg, input, cancel))
    }

    pub async fn transcribe_async(
        &self,
        cfg: &AsrConfig,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<Transcript, TranscriptError> {
        // Gemini audio transcription rejects video notes outright: skip it
        // and go straight to the fallback provider instead of failing.
        let mut order = Vec::with_capacity(2);
        if cfg.provider == AsrProvider::Gemini
            && infer_audio_mime(&input.filename, input.mime.as_deref()).is_none()
            && let Some(fallback) = cfg.fallback
        {
            order.push(fallback);
        } else {
            order.push(cfg.provider);
            if let Some(fallback) = cfg.fallback
                && fallback != cfg.provider
            {
                order.push(fallback);
            }
        }

        let mut last = TranscriptError::Unsupported("no provider configured".to_string());
        for provider_id in order {
            let provider = match provider_for(provider_id, cfg, self.client.clone()) {
                Ok(provider) => provider,
                Err(error) => {
                    last = error;
                    continue;
                }
            };
            cancel.check()?;
            let result = provider.transcribe(input, cancel).await;
            cancel.check()?;
            match result {
                Ok(mut transcript) => {
                    // Single source of truth for the provider label.
                    transcript.provider = provider.name().to_string();
                    return Ok(transcript);
                }
                Err(TranscriptError::Cancelled) => {
                    return Err(TranscriptError::Cancelled);
                }
                Err(error) => {
                    last = error;
                }
            }
        }
        Err(last)
    }
}
