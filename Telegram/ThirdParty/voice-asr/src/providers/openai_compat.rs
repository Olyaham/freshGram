//! Shared OpenAI-compatible `/audio/transcriptions` transport.
//!
//! One transport, multiple profiles: Groq is the preset profile, `custom`
//! is the user-defined one (self-hosted or third-party OpenAI-compatible
//! endpoints). No branded per-vendor clients.

use std::time::Duration;

use reqwest::multipart::{Form, Part};
use serde::Deserialize;

use crate::types::{AsrSegment, CancelToken, TranscriptError, clean_http_error, map_http_error};

use super::TranscribeInput;

pub(crate) struct OpenAiCompatTranscriber {
    pub api_base: String,
    pub api_key: String,
    pub model: String,
    pub temperature: f32,
    pub language: String,
    pub timeout_secs: u64,
    pub client: reqwest::Client,
}

pub(crate) struct TranscriberOut {
    pub model: String,
    pub request_id: Option<String>,
    pub text: String,
    pub segments: Vec<AsrSegment>,
}

#[derive(Debug, Deserialize)]
struct TranscriptionResponse {
    text: String,
    #[serde(default)]
    segments: Vec<ResponseSegment>,
    #[serde(default)]
    x_groq: Option<ResponseMeta>,
}

#[derive(Debug, Deserialize)]
struct ResponseSegment {
    #[serde(default)]
    start: f32,
    #[serde(default)]
    end: f32,
    #[serde(default)]
    text: String,
}

#[derive(Debug, Deserialize)]
struct ResponseMeta {
    id: Option<String>,
}

impl OpenAiCompatTranscriber {
    fn endpoint(&self) -> String {
        format!(
            "{}/audio/transcriptions",
            self.api_base.trim_end_matches('/')
        )
    }

    pub async fn transcribe(
        &self,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<TranscriberOut, TranscriptError> {
        tokio::select! {
            biased;
            _ = cancel.cancelled() => Err(TranscriptError::Cancelled),
            result = tokio::time::timeout(
                Duration::from_secs(self.timeout_secs), self.run(input, cancel),
            ) => result.map_err(|_| TranscriptError::Timeout)?,
        }
    }

    async fn run(
        &self,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<TranscriberOut, TranscriptError> {
        cancel.check()?;
        if self.api_base.trim().is_empty() {
            return Err(TranscriptError::Unsupported(
                "openai-compatible api base is empty".to_string(),
            ));
        }
        if self.model.trim().is_empty() {
            return Err(TranscriptError::Unsupported(
                "openai-compatible model is empty".to_string(),
            ));
        }

        let mut file = Part::file(&input.audio_path)
            .await
            .map_err(|e| TranscriptError::Io(e.to_string()))?
            .file_name(input.filename.clone());
        if let Some(mime) = input.mime.as_deref() {
            file = file
                .mime_str(mime)
                .map_err(|e| TranscriptError::Parse(clean_http_error(e)))?;
        }
        let mut form = Form::new()
            .text("model", self.model.clone())
            .text("response_format", "verbose_json")
            .text("temperature", self.temperature.to_string())
            .text("timestamp_granularities[]", "segment")
            .part("file", file);
        // "auto"/empty means server-side detection; unknown values are
        // rejected by strict servers, so omit instead of sending them.
        if !self.language.trim().is_empty() && self.language.trim() != "auto" {
            form = form.text("language", self.language.clone());
        }

        let call = self.client.post(self.endpoint()).multipart(form);
        let call = if self.api_key.trim().is_empty() {
            call
        } else {
            call.bearer_auth(self.api_key.trim())
        };
        let send = call.send();
        let response = tokio::time::timeout(Duration::from_secs(self.timeout_secs), send)
            .await
            .map_err(|_| TranscriptError::Timeout)?
            .map_err(map_http_error)?;
        cancel.check()?;

        let parsed: TranscriptionResponse = super::transport::json(response).await?;

        let text = parsed.text.trim().to_string();
        let segments = parsed
            .segments
            .into_iter()
            .map(|segment| AsrSegment {
                start_sec: segment.start,
                end_sec: segment.end,
                text: segment.text.trim().to_string(),
            })
            .filter(|segment| !segment.text.is_empty())
            .collect();
        Ok(TranscriberOut {
            model: self.model.clone(),
            request_id: parsed.x_groq.and_then(|meta| meta.id),
            text,
            segments,
        })
    }
}
