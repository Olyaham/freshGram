use std::path::Path;
use std::time::Duration;

use aiapi_http as http;
use aiapi_types::{AsrSegment, CancelToken, Cancelled, Transcript};
use reqwest::multipart::{Form, Part};
use serde::Deserialize;

pub struct CustomRequest<'a> {
    pub api_base: &'a str,
    pub api_key: &'a str,
    pub model: &'a str,
    pub temperature: f32,
    pub language: &'a str,
    pub timeout_secs: u64,
    pub path: &'a Path,
    pub filename: &'a str,
    pub mime_type: Option<&'a str>,
}

#[derive(Deserialize)]
struct Response {
    text: String,
    #[serde(default)]
    segments: Vec<Segment>,
    #[serde(default)]
    x_groq: Option<Meta>,
}

#[derive(Deserialize)]
struct Segment {
    #[serde(default)]
    start: f32,
    #[serde(default)]
    end: f32,
    #[serde(default)]
    text: String,
}

#[derive(Deserialize)]
struct Meta {
    id: Option<String>,
}

pub async fn transcribe_custom_audio(
    client: &reqwest::Client,
    request: CustomRequest<'_>,
    cancel: &CancelToken,
) -> anyhow::Result<Transcript> {
    cancel.check()?;
    tokio::select! {
        biased;
        _ = cancel.cancelled() => Err(Cancelled.into()),
        result = tokio::time::timeout(
            Duration::from_secs(request.timeout_secs), run_request(client, &request),
        ) => result.map_err(|_| http::Error::Timeout)?,
    }
}

async fn run_request(
    client: &reqwest::Client,
    request: &CustomRequest<'_>,
) -> anyhow::Result<Transcript> {
    if request.api_base.trim().is_empty() || request.model.trim().is_empty() {
        return Err(crate::asr::Unsupported("ASR endpoint or model is empty".into()).into());
    }
    let mut file = Part::file(request.path)
        .await?
        .file_name(request.filename.to_string());
    if let Some(mime) = request.mime_type {
        file = file.mime_str(mime).map_err(http::Error::from)?;
    }
    let mut form = Form::new()
        .text("model", request.model.trim().to_string())
        .text("response_format", "verbose_json")
        .text("temperature", request.temperature.to_string())
        .text("timestamp_granularities[]", "segment")
        .part("file", file);
    let language = request.language.trim();
    if !language.is_empty() && !language.eq_ignore_ascii_case("auto") {
        form = form.text("language", language.to_string());
    }
    let endpoint = format!(
        "{}/audio/transcriptions",
        request.api_base.trim().trim_end_matches('/')
    );
    let call = client.post(endpoint).multipart(form);
    let call = if request.api_key.trim().is_empty() {
        call
    } else {
        call.bearer_auth(request.api_key.trim())
    };
    let response = call.send().await.map_err(http::Error::from)?;
    let parsed: Response = http::parse_json(response).await?;
    if parsed.segments.iter().any(|segment| {
        !segment.start.is_finite()
            || !segment.end.is_finite()
            || segment.start < 0.0
            || segment.end < segment.start
    }) {
        return Err(http::Error::Parse("invalid transcription segment timeline".into()).into());
    }
    Ok(Transcript {
        provider: "openai_compatible".into(),
        model: request.model.trim().into(),
        request_id: parsed.x_groq.and_then(|meta| meta.id),
        text: parsed.text.trim().into(),
        segments: parsed
            .segments
            .into_iter()
            .map(|segment| AsrSegment {
                start_sec: segment.start,
                end_sec: segment.end,
                text: segment.text.trim().into(),
            })
            .filter(|segment| !segment.text.is_empty())
            .collect(),
    })
}
