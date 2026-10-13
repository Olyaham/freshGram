use aiapi_http as http;
use aiapi_types::{CancelToken, Cancelled, Transcript};
use serde::Deserialize;
use serde_json::Value;
use std::path::Path;
use std::time::Duration;

pub struct GeminiRequest<'a> {
    pub api_base: &'a str,
    pub api_key: &'a str,
    pub model: &'a str,
    pub language: &'a str,
    pub prompt: &'a str,
    pub timeout_secs: u64,
    pub total_deadline_secs: u64,
    pub path: &'a Path,
    pub filename: &'a str,
    pub mime_type: Option<&'a str>,
}

#[derive(Clone, Deserialize)]
struct File {
    name: String,
    #[serde(default)]
    uri: String,
}
#[derive(Deserialize)]
struct Upload {
    file: File,
}

pub async fn transcribe_gemini_audio(
    client: &reqwest::Client,
    request: GeminiRequest<'_>,
    cancel: &CancelToken,
) -> anyhow::Result<Transcript> {
    cancel.check()?;
    if request.api_key.trim().is_empty()
        || request.model.trim().is_empty()
        || request.api_base.trim().is_empty()
    {
        return Err(
            crate::asr::Unsupported("Gemini endpoint, key or model is empty".into()).into(),
        );
    }
    let mime =
        crate::mime::infer_audio_mime(request.filename, request.mime_type).ok_or_else(|| {
            crate::asr::Unsupported("Gemini transcription requires audio MIME".into())
        })?;
    let mut uploaded = None;
    let result = tokio::select! {
        biased;
        _ = cancel.cancelled() => Err(Cancelled.into()),
        result = tokio::time::timeout(
            Duration::from_secs(request.total_deadline_secs),
            run(client, &request, &mime, &mut uploaded),
        ) => result.map_err(|_| anyhow::Error::new(http::Error::Timeout)).and_then(|value| value),
    };
    if let Some(file) = uploaded {
        let _ = client
            .delete(format!(
                "{}/v1beta/{}",
                request.api_base.trim().trim_end_matches('/'),
                file.name
            ))
            .header("x-goog-api-key", request.api_key.trim())
            .timeout(Duration::from_secs(10))
            .send()
            .await;
    }
    result
}

async fn run(
    client: &reqwest::Client,
    request: &GeminiRequest<'_>,
    mime: &str,
    uploaded: &mut Option<File>,
) -> anyhow::Result<Transcript> {
    let source = tokio::fs::File::open(request.path).await?;
    let length = source.metadata().await?.len();
    let base = request.api_base.trim().trim_end_matches('/');
    let timeout = Duration::from_secs(request.timeout_secs);
    let response = client
        .post(format!("{base}/upload/v1beta/files"))
        .timeout(timeout)
        .header("x-goog-api-key", request.api_key.trim())
        .header("X-Goog-Upload-Protocol", "resumable")
        .header("X-Goog-Upload-Command", "start")
        .header("X-Goog-Upload-Header-Content-Length", length.to_string())
        .header("X-Goog-Upload-Header-Content-Type", mime)
        .json(&serde_json::json!({"file":{"display_name":"aiapi-asr-upload"}}))
        .send()
        .await
        .map_err(http::Error::from)?;
    http::check_status(&response)?;
    let upload_url = response
        .headers()
        .get("x-goog-upload-url")
        .and_then(|value| value.to_str().ok())
        .ok_or_else(|| http::Error::Parse("Gemini upload URL is missing".into()))?
        .to_string();
    let response = client
        .post(upload_url)
        .timeout(timeout)
        .header("Content-Length", length.to_string())
        .header("X-Goog-Upload-Offset", "0")
        .header("X-Goog-Upload-Command", "upload, finalize")
        .header("Content-Type", mime)
        .body(reqwest::Body::wrap_stream(
            tokio_util::io::ReaderStream::new(source),
        ))
        .send()
        .await
        .map_err(http::Error::from)?;
    let upload: Upload = http::parse_json(response).await?;
    *uploaded = Some(upload.file.clone());
    if upload.file.uri.trim().is_empty() || upload.file.name.trim().is_empty() {
        return Err(http::Error::Parse("Gemini upload file reference is incomplete".into()).into());
    }
    let mut input = Vec::with_capacity(2);
    if !request.prompt.trim().is_empty() {
        input.push(serde_json::json!({"type":"text","text":request.prompt.trim()}));
    }
    input.push(serde_json::json!({"type":"audio","uri":upload.file.uri,"mime_type":mime}));
    let response = client
        .post(format!("{base}/v1beta/interactions"))
        .header("x-goog-api-key", request.api_key.trim())
        .timeout(timeout)
        .json(&serde_json::json!({
            "model":request.model,
            "input":input,
            "generation_config":{"transcription_config":{
                "language_codes":crate::lang::bcp47_codes(request.language),
                "mode":{"type":"verbatim"}
            }}
        }))
        .send()
        .await
        .map_err(http::Error::from)?;
    let interaction: Value = http::parse_json(response).await?;
    let text = extract_text(&interaction)
        .ok_or_else(|| http::Error::Parse("Gemini transcription returned no text output".into()))?;
    Ok(Transcript {
        provider: "gemini".into(),
        model: request.model.into(),
        request_id: interaction
            .get("id")
            .and_then(Value::as_str)
            .map(str::to_string),
        text,
        segments: Vec::new(),
    })
}

fn extract_text(value: &Value) -> Option<String> {
    if let Some(text) = value.get("output_text").and_then(Value::as_str) {
        return Some(text.trim().into());
    }
    fn collect(value: &Value, parts: &mut Vec<String>) {
        match value {
            Value::Object(object) => {
                if object.get("type").and_then(Value::as_str) == Some("text")
                    && let Some(text) = object.get("text").and_then(Value::as_str)
                {
                    parts.push(text.trim().into());
                }
                for value in object.values() {
                    collect(value, parts);
                }
            }
            Value::Array(values) => {
                for value in values {
                    collect(value, parts);
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

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    #[test]
    fn extracts_each_supported_interaction_shape() {
        for value in [
            json!({"output_text":" Hello "}),
            json!({"outputs":[{"type":"text","text":" Hello "}]}),
            json!({"steps":[{"content":[{"type":"text","text":" Hello "}]}]}),
        ] {
            assert_eq!(extract_text(&value).as_deref(), Some("Hello"));
        }
        assert_eq!(extract_text(&json!({"error":"not speech"})), None);
        assert_eq!(
            extract_text(&json!({"output_text":""})),
            Some(String::new())
        );
    }
}
