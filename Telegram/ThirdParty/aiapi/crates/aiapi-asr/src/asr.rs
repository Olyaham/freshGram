use aiapi_config::SecretStore;
use aiapi_types::{CancelToken, Transcript};
use std::path::Path;

pub const DEFAULT_GEMINI_BASE: &str = "https://generativelanguage.googleapis.com";

#[derive(Clone, Debug)]
pub struct AsrOptions {
    pub provider: String,
    pub model: String,
    pub base_url: String,
    pub secret_name: Option<String>,
    pub language: String,
    pub temperature: f32,
    pub timeout_secs: u64,
    pub total_deadline_secs: u64,
    pub prompt: Option<String>,
}

#[derive(Debug)]
pub struct Unsupported(pub String);

impl std::fmt::Display for Unsupported {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}
impl std::error::Error for Unsupported {}

pub async fn transcribe_audio(
    secrets: &SecretStore,
    options: &AsrOptions,
    path: &Path,
    filename: &str,
    mime_type: Option<&str>,
    cancel: &CancelToken,
) -> anyhow::Result<Transcript> {
    cancel.check()?;
    let key = options
        .secret_name
        .as_deref()
        .map(|name| secrets.require(name))
        .transpose()?;
    let client = reqwest::Client::builder()
        .redirect(reqwest::redirect::Policy::none())
        .build()
        .map_err(aiapi_http::Error::from)?;
    let mut result = match options.provider.as_str() {
        "groq" | "custom" | "openai_compatible" => {
            crate::custom::transcribe_custom_audio(
                &client,
                crate::custom::CustomRequest {
                    api_base: &options.base_url,
                    api_key: key.as_deref().unwrap_or(""),
                    model: &options.model,
                    temperature: options.temperature,
                    language: &options.language,
                    timeout_secs: options.timeout_secs,
                    path,
                    filename,
                    mime_type,
                },
                cancel,
            )
            .await?
        }
        "gemini" => {
            crate::gemini::transcribe_gemini_audio(
                &client,
                crate::gemini::GeminiRequest {
                    api_base: &options.base_url,
                    api_key: key.as_deref().unwrap_or(""),
                    model: &options.model,
                    language: &options.language,
                    prompt: options.prompt.as_deref().unwrap_or(""),
                    timeout_secs: options.timeout_secs,
                    total_deadline_secs: options.total_deadline_secs,
                    path,
                    filename,
                    mime_type,
                },
                cancel,
            )
            .await?
        }
        _ => return Err(Unsupported("unsupported ASR transport".into()).into()),
    };
    result.provider = options.provider.clone();
    Ok(result)
}

pub async fn transcribe_audio_with_shadow(
    secrets: &SecretStore,
    primary: &AsrOptions,
    shadow: Option<&AsrOptions>,
    path: &Path,
    filename: &str,
    mime_type: Option<&str>,
    cancel: &CancelToken,
) -> anyhow::Result<(Transcript, Vec<Transcript>)> {
    let main = transcribe_audio(secrets, primary, path, filename, mime_type, cancel);
    let second = async {
        match shadow {
            Some(options) => transcribe_audio(secrets, options, path, filename, mime_type, cancel)
                .await
                .map(Some),
            None => Ok(None),
        }
    };
    let (main, second) = tokio::join!(main, second);
    cancel.check()?;
    match (main, second) {
        (Ok(main), Ok(Some(second))) => Ok((main, vec![second])),
        (Ok(main), _) => Ok((main, Vec::new())),
        (Err(_), Ok(Some(second))) => Ok((second, Vec::new())),
        (Err(error), _) => Err(error),
    }
}
