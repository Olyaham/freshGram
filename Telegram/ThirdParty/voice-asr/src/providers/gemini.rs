use super::{TranscribeInput, TranscriptionProvider};
use crate::config::{AsrConfig, GeminiConfig};
use crate::types::{CancelToken, Transcript, TranscriptError, map_transcript_error};

pub struct GeminiProvider {
    cfg: GeminiConfig,
    language: String,
    client: reqwest::Client,
}

impl GeminiProvider {
    pub fn new(cfg: &AsrConfig, client: reqwest::Client) -> Self {
        Self {
            cfg: cfg.gemini.clone(),
            language: cfg.language.clone(),
            client,
        }
    }
}

impl TranscriptionProvider for GeminiProvider {
    fn name(&self) -> &'static str {
        "gemini"
    }

    async fn transcribe(
        &self,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<Transcript, TranscriptError> {
        aiapi_asr::gemini::transcribe_gemini_audio(
            &self.client,
            aiapi_asr::gemini::GeminiRequest {
                api_base: &self.cfg.api_base,
                api_key: &self.cfg.api_key,
                model: &self.cfg.model,
                language: &self.language,
                prompt: &self.cfg.prompt,
                timeout_secs: self.cfg.timeout_secs,
                total_deadline_secs: self.cfg.total_deadline_secs,
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
