//! Gemini Transcribe via the Files + Interactions API.
//!
//! Flow adapted from NedoBot (`Mar2ianen/NedoBot`, MIT): resumable file
//! upload → `POST /v1beta/interactions` with a `transcription_config`
//! (`verbatim` mode, BCP-47 language) → text extraction → best-effort
//! deletion of the uploaded file. No `genai`-style chat crate covers this
//! endpoint, so the protocol is spoken directly.

use std::time::Duration;

use serde::Deserialize;
use serde_json::Value;

use crate::audio::infer_audio_mime;
use crate::config::{AsrConfig, GeminiConfig};
use crate::types::{CancelToken, Transcript, TranscriptError, map_http_error};

use super::{TranscribeInput, TranscriptionProvider};

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

    fn base(&self) -> String {
        self.cfg.api_base.trim_end_matches('/').to_string()
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
        cancel.check()?;
        if self.cfg.api_key.trim().is_empty() {
            return Err(TranscriptError::Unsupported(
                "gemini api key is empty".to_string(),
            ));
        }
        if self.cfg.model.trim().is_empty() {
            return Err(TranscriptError::Unsupported(
                "gemini model is empty".to_string(),
            ));
        }
        let mime = infer_audio_mime(&input.filename, input.mime.as_deref()).ok_or_else(|| {
            TranscriptError::Unsupported(
                "gemini audio transcription needs an audio mime type; \
                 route video notes to a capable provider"
                    .to_string(),
            )
        })?;

        let mut uploaded = None;
        let result = tokio::select! {
            biased;
            _ = cancel.cancelled() => Err(TranscriptError::Cancelled),
            result = tokio::time::timeout(
                Duration::from_secs(self.cfg.total_deadline_secs),
                self.run(input, &mime, &mut uploaded),
            ) => result.map_err(|_| TranscriptError::Timeout).and_then(|result| result),
        };
        if let Some(file) = uploaded {
            self.delete_file(&file.name).await;
        }
        result
    }
}

#[derive(Debug, Deserialize)]
struct GeminiFileUploadResponse {
    file: GeminiFile,
}

#[derive(Clone, Debug, Deserialize)]
struct GeminiFile {
    name: String,
    uri: String,
}

impl GeminiProvider {
    async fn run(
        &self,
        input: &TranscribeInput,
        mime: &str,
        uploaded: &mut Option<GeminiFile>,
    ) -> Result<Transcript, TranscriptError> {
        let source = tokio::fs::File::open(&input.audio_path)
            .await
            .map_err(|e| TranscriptError::Io(e.to_string()))?;
        let length = source
            .metadata()
            .await
            .map_err(|e| TranscriptError::Io(e.to_string()))?
            .len();
        let upload_url = self.start_upload(length, mime).await?;
        let file = self
            .finalize_upload(&upload_url, source, length, mime)
            .await?;
        *uploaded = Some(file.clone());
        let interaction = self.create_transcription(&file, mime).await?;

        let text = extract_transcription_text(&interaction).ok_or_else(|| {
            TranscriptError::Parse("gemini transcribe returned no text output".to_string())
        })?;
        let request_id = interaction
            .get("id")
            .and_then(Value::as_str)
            .map(str::to_string);
        Ok(Transcript {
            // Labeled by `Engine` from the registry; see `Provider::name`.
            provider: String::new(),
            model: self.cfg.model.clone(),
            request_id,
            text,
            segments: Vec::new(),
        })
    }

    async fn start_upload(
        &self,
        content_length: u64,
        mime: &str,
    ) -> Result<String, TranscriptError> {
        let base = self.base();
        let key = self.cfg.api_key.trim();
        let response = self
            .client
            .post(format!("{base}/upload/v1beta/files"))
            .timeout(Duration::from_secs(self.cfg.timeout_secs))
            .header("x-goog-api-key", key)
            .header("X-Goog-Upload-Protocol", "resumable")
            .header("X-Goog-Upload-Command", "start")
            .header(
                "X-Goog-Upload-Header-Content-Length",
                content_length.to_string(),
            )
            .header("X-Goog-Upload-Header-Content-Type", mime)
            .json(&serde_json::json!({ "file": { "display_name": "voice-asr" } }))
            .send()
            .await
            .map_err(map_http_error)?;
        let status = response.status();
        if !status.is_success() {
            return Err(TranscriptError::Api {
                status: status.as_u16(),
                message: status
                    .canonical_reason()
                    .unwrap_or("upload failed")
                    .to_string(),
            });
        }
        response
            .headers()
            .get("x-goog-upload-url")
            .and_then(|value| value.to_str().ok())
            .map(str::to_string)
            .ok_or_else(|| {
                TranscriptError::Parse("gemini files api returned no upload url".to_string())
            })
    }

    async fn finalize_upload(
        &self,
        upload_url: &str,
        source: tokio::fs::File,
        length: u64,
        mime: &str,
    ) -> Result<GeminiFile, TranscriptError> {
        let response = self
            .client
            .post(upload_url)
            .timeout(Duration::from_secs(self.cfg.timeout_secs))
            .header("Content-Length", length.to_string())
            .header("X-Goog-Upload-Offset", "0")
            .header("X-Goog-Upload-Command", "upload, finalize")
            .header("Content-Type", mime)
            .body(reqwest::Body::wrap_stream(
                tokio_util::io::ReaderStream::new(source),
            ))
            .send()
            .await
            .map_err(map_http_error)?;
        super::transport::json::<GeminiFileUploadResponse>(response)
            .await
            .map(|body| body.file)
    }

    async fn create_transcription(
        &self,
        file: &GeminiFile,
        mime: &str,
    ) -> Result<Value, TranscriptError> {
        // Audio uploads use a direct client: proxy settings for text LLM
        // routes must never silently move them to another egress.
        let base = self.base();
        let key = self.cfg.api_key.trim();
        let mut input = Vec::with_capacity(2);
        let prompt = self.cfg.prompt.trim();
        if !prompt.is_empty() {
            input.push(serde_json::json!({ "type": "text", "text": prompt }));
        }
        input.push(serde_json::json!({
            "type": "audio",
            "uri": file.uri,
            "mime_type": mime,
        }));
        let call = self
            .client
            .post(format!("{base}/v1beta/interactions"))
            .header("x-goog-api-key", key)
            .timeout(Duration::from_secs(self.cfg.timeout_secs))
            .json(&serde_json::json!({
                "model": self.cfg.model,
                "input": input,
                "generation_config": {
                    "transcription_config": {
                        "language_codes": gemini_language_codes(&self.language),
                        "mode": { "type": "verbatim" },
                    }
                }
            }));
        let response = call.send().await.map_err(map_http_error)?;
        super::transport::json(response).await
    }

    async fn delete_file(&self, name: &str) {
        // Best effort: transcription results never depend on cleanup.
        let base = self.base();
        let key = self.cfg.api_key.trim();
        let _ = self
            .client
            .delete(format!("{base}/v1beta/{name}"))
            .header("x-goog-api-key", key)
            .timeout(Duration::from_secs(10))
            .send()
            .await;
    }
}

fn extract_transcription_text(value: &Value) -> Option<String> {
    if let Some(text) = value.get("output_text").and_then(Value::as_str) {
        let text = text.trim();
        if !text.is_empty() {
            return Some(text.to_string());
        }
    }

    fn collect(value: &Value, parts: &mut Vec<String>) {
        match value {
            Value::Object(object) => {
                if object.get("type").and_then(Value::as_str) == Some("text")
                    && let Some(text) = object.get("text").and_then(Value::as_str)
                    && !text.trim().is_empty()
                {
                    parts.push(text.trim().to_string());
                }
                for nested in object.values() {
                    collect(nested, parts);
                }
            }
            Value::Array(items) => {
                for item in items {
                    collect(item, parts);
                }
            }
            _ => {}
        }
    }

    let mut parts = Vec::new();
    if let Some(steps) = value.get("steps") {
        collect(steps, &mut parts);
    }
    if parts.is_empty()
        && let Some(outputs) = value.get("outputs")
    {
        collect(outputs, &mut parts);
    }
    (!parts.is_empty()).then(|| parts.join("\n"))
}

fn gemini_language_codes(language: &str) -> Vec<String> {
    let language = language.trim();
    if language.is_empty() || language.eq_ignore_ascii_case("auto") {
        return Vec::new();
    }
    let code = match language.to_ascii_lowercase().as_str() {
        "ru" => "ru-RU",
        "en" => "en-US",
        "uk" => "uk-UA",
        "be" => "be-BY",
        _ => language,
    };
    vec![code.to_string()]
}
