use super::{TranscribeInput, TranscriptionProvider};
use crate::config::{AsrConfig, GroqConfig};
use crate::types::{CancelToken, Transcript, TranscriptError, map_transcript_error};

pub struct GroqProvider {
    cfg: GroqConfig,
    language: String,
    client: reqwest::Client,
}

impl GroqProvider {
    pub fn new(cfg: &AsrConfig, client: reqwest::Client) -> Self {
        Self {
            cfg: cfg.groq.clone(),
            language: cfg.language.clone(),
            client,
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
        if self.cfg.api_key.trim().is_empty() {
            return Err(TranscriptError::Unsupported("groq api key is empty".into()));
        }
        aiapi_asr::custom::transcribe_custom_audio(
            &self.client,
            aiapi_asr::custom::CustomRequest {
                api_base: &self.cfg.api_base,
                api_key: &self.cfg.api_key,
                model: &self.cfg.model,
                temperature: self.cfg.temperature,
                language: &self.language,
                timeout_secs: self.cfg.timeout_secs,
                path: &input.audio_path,
                filename: &input.filename,
                mime_type: input.mime.as_deref(),
            },
            cancel,
        )
        .await
        .map_err(map_transcript_error)
    }
}
