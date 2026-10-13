//! Engine: owns the shared HTTP client, runs primary → fallback selection.
//!
//! The engine holds NO async runtime: async callers use `transcribe_async`
//! on their own runtime, while the blocking FFI path builds a throwaway
//! current-thread runtime per call. This keeps drops safe in every context.

use crate::audio::infer_audio_mime;
use crate::config::AsrConfig;
use crate::providers::{TranscribeInput, provider_for};
use crate::types::{AsrProvider, CancelToken, ProcessedTranscript, Transcript, TranscriptError};
use aiapi_config::SecretStore;
use aiapi_llm::discovery::{DiscoveryOptions, ModelCatalog, ModelDiscovery};

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
        self.transcribe_raw_async(cfg, input, cancel).await
    }

    pub fn process(
        &self,
        cfg: &AsrConfig,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<ProcessedTranscript, TranscriptError> {
        tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .map_err(|error| TranscriptError::Io(error.to_string()))?
            .block_on(self.process_async(cfg, input, cancel))
    }

    pub async fn process_async(
        &self,
        cfg: &AsrConfig,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<ProcessedTranscript, TranscriptError> {
        let mut alternatives = Vec::new();
        let raw = if let Some(shadow) = cfg.shadow.filter(|shadow| *shadow != cfg.provider) {
            let mut primary_cfg = cfg.clone();
            primary_cfg.fallback = None;
            let mut shadow_cfg = primary_cfg.clone();
            shadow_cfg.provider = shadow;
            let (primary, second) = tokio::join!(
                self.transcribe_raw_async(&primary_cfg, input, cancel),
                self.transcribe_raw_async(&shadow_cfg, input, cancel),
            );
            cancel.check()?;
            match (primary, second) {
                (Ok(raw), Ok(second)) => {
                    if !crate::policy::has_speech(&raw) && crate::policy::has_speech(&second) {
                        alternatives.push(raw);
                        second
                    } else {
                        alternatives.push(second);
                        raw
                    }
                }
                (Ok(raw), _) => raw,
                (Err(_), Ok(second)) => second,
                (Err(error), Err(_)) => {
                    if let Some(fallback) = cfg
                        .fallback
                        .filter(|fallback| *fallback != cfg.provider && *fallback != shadow)
                    {
                        shadow_cfg.provider = fallback;
                        self.transcribe_raw_async(&shadow_cfg, input, cancel)
                            .await?
                    } else {
                        return Err(error);
                    }
                }
            }
        } else {
            self.transcribe_raw_async(cfg, input, cancel).await?
        };
        let mut result = ProcessedTranscript {
            no_speech: !crate::policy::has_speech(&raw),
            raw,
            alternatives,
            clean: None,
            cleanup_provider: None,
            cleanup_model: None,
            cleanup_attempts: Vec::new(),
        };
        if let Some(cleanup) = &cfg.cleanup
            && !result.no_speech
        {
            if crate::cleanup::profiles_for(cfg, cleanup, None).is_err() {
                cancel.check()?;
                result.cleanup_provider = Some("raw_asr_fallback".into());
                return Ok(result);
            }
            let metadata = if let Some((options, secrets)) =
                crate::config::discovery_for(cfg, cleanup.provider)
            {
                let discovery = model_discovery();
                let refreshed = discovery
                    .discover(&self.client, &secrets, &options, false, cancel)
                    .await;
                cancel.check()?;
                if let Ok(catalog) = refreshed {
                    catalog
                        .models
                        .into_iter()
                        .find(|model| model.id == cleanup.model)
                        .map(|model| model.metadata)
                } else {
                    discovery.known_model(&secrets, &options, &cleanup.model)
                }
            } else {
                None
            };
            let Ok((profiles, secrets)) =
                crate::cleanup::profiles_for(cfg, cleanup, metadata.as_ref())
            else {
                cancel.check()?;
                result.cleanup_provider = Some("raw_asr_fallback".into());
                return Ok(result);
            };
            let options = crate::cleanup::CleanupOptions {
                temperature: 0.0,
                max_tokens: profiles.models["cleanup"].capabilities.max_output_tokens,
                short_limit: 400,
                proxy_url: None,
                thinking_budget: 1024,
            };
            let clean = tokio::select! {
                biased;
                _ = cancel.cancelled() => return Err(TranscriptError::Cancelled),
                result = crate::cleanup::cleanup_transcript(&secrets, &profiles, &result.raw, &result.alternatives, &options) => result,
            }.map_err(crate::types::map_transcript_error)?;
            result.cleanup_provider = Some(clean.provider);
            result.cleanup_model = Some(clean.model);
            result.cleanup_attempts = clean.attempts;
            result.clean = Some(clean.transcript);
        }
        cancel.check()?;
        Ok(result)
    }

    pub fn discover(
        &self,
        options: &DiscoveryOptions,
        secrets: &SecretStore,
        force: bool,
        cancel: &CancelToken,
    ) -> Result<ModelCatalog, TranscriptError> {
        tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .map_err(|error| TranscriptError::Io(error.to_string()))?
            .block_on(model_discovery().discover(&self.client, secrets, options, force, cancel))
            .map_err(crate::types::map_transcript_error)
    }

    async fn transcribe_raw_async(
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

fn model_discovery() -> &'static ModelDiscovery {
    static DISCOVERY: std::sync::OnceLock<ModelDiscovery> = std::sync::OnceLock::new();
    DISCOVERY.get_or_init(ModelDiscovery::default)
}
