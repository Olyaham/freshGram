use serde::Deserialize;
use std::collections::BTreeSet;

use crate::time;
use crate::types::{CleanTranscript, TranscriptChapter, TranscriptRenderMode};
use aiapi_config::SecretStore;
use aiapi_llm::profiles::LlmProfiles;
use aiapi_llm::service::{GenerateTextOptions, generate_text_checked};
use aiapi_types::Transcript;

const CLEANUP_PROMPT: &str = include_str!("../prompts/voice_cleanup.md");
const MIN_CLEANUP_CONTENT_PERCENT: usize = 65;
const MAX_CLEANUP_CONTENT_PERCENT: usize = 135;

pub fn profiles_for(
    cfg: &crate::config::AsrConfig,
    cleanup: &crate::config::CleanupConfig,
    metadata: Option<&aiapi_llm::discovery::ModelMetadata>,
) -> Result<(LlmProfiles, SecretStore), crate::types::TranscriptError> {
    use aiapi_llm::profiles::*;
    let (base_url, key, driver, adapter) = match cleanup.provider {
        crate::types::AsrProvider::Groq => (
            cfg.groq.api_base.trim_end_matches('/').to_string(),
            cfg.groq.api_key.as_str(),
            LlmDriver::OpenaiCompatible,
            GenAiAdapter::Groq,
        ),
        crate::types::AsrProvider::Gemini => (
            format!("{}/v1beta", cfg.gemini.api_base.trim_end_matches('/')),
            cfg.gemini.api_key.as_str(),
            LlmDriver::Gemini,
            GenAiAdapter::Gemini,
        ),
        crate::types::AsrProvider::Custom => (
            cfg.custom.api_base.trim_end_matches('/').to_string(),
            cfg.custom.api_key.as_str(),
            LlmDriver::OpenaiCompatible,
            GenAiAdapter::OpenAi,
        ),
        crate::types::AsrProvider::Local => {
            return Err(crate::types::TranscriptError::Unsupported(
                "local Whisper does not generate text".into(),
            ));
        }
    };
    if cleanup.provider != crate::types::AsrProvider::Custom && key.trim().is_empty() {
        return Err(crate::types::TranscriptError::Unsupported(
            "cleanup API key is empty".into(),
        ));
    }
    if metadata.is_some_and(|known| {
        known.supports_text_generation == Some(false)
            || known
                .input
                .as_ref()
                .is_some_and(|input| !input.iter().any(|modality| modality == "text"))
            || known
                .output
                .as_ref()
                .is_some_and(|output| !output.iter().any(|modality| modality == "text"))
    }) {
        return Err(crate::types::TranscriptError::Unsupported(
            "cleanup model does not generate text".into(),
        ));
    }
    let context_window_tokens = metadata
        .and_then(|known| known.context_tokens)
        .and_then(|tokens| u32::try_from(tokens).ok())
        .filter(|tokens| *tokens > 0)
        .unwrap_or(32768);
    let max_output_tokens = metadata
        .and_then(|known| known.output_tokens)
        .and_then(|tokens| u32::try_from(tokens).ok())
        .filter(|tokens| *tokens > 0)
        .unwrap_or(4096)
        .min(4096)
        .min(context_window_tokens);
    let profiles = LlmProfiles {
        providers: [(
            cleanup.provider.name().into(),
            ProviderProfile {
                driver,
                base_url,
                api_key_env: if key.trim().is_empty() {
                    String::new()
                } else {
                    "CLEANUP_KEY".into()
                },
                adapter: Some(adapter),
                egress: if cfg
                    .proxy_url
                    .as_ref()
                    .is_some_and(|url| !url.trim().is_empty())
                {
                    Egress::Proxy
                } else {
                    Egress::Direct
                },
            },
        )]
        .into(),
        models: [(
            "cleanup".into(),
            ModelProfile {
                provider: cleanup.provider.name().into(),
                model: cleanup.model.clone(),
                capabilities: ModelCapabilities {
                    supports_images: false,
                    supports_tools: false,
                    supports_system_prompt: metadata
                        .and_then(|known| known.supports_system_prompt)
                        .unwrap_or(true),
                    structured_output: StructuredOutputMode::PromptOnly,
                    context_window_tokens,
                    max_output_tokens,
                    request_timeout_sec: 120,
                    thinking: ThinkingMode::None,
                },
            },
        )]
        .into(),
        routes: [(
            "voice_cleanup".into(),
            RouteProfile {
                models: vec!["cleanup".into()],
                fallback_on_validation_failure: false,
            },
        )]
        .into(),
    };
    profiles
        .validate()
        .map_err(|error| crate::types::TranscriptError::Unsupported(error.to_string()))?;
    Ok((
        profiles,
        SecretStore::explicit().with_override("CLEANUP_KEY", key),
    ))
}

pub struct CleanupResult {
    pub provider: String,
    pub model: String,
    pub transcript: CleanTranscript,
    pub attempts: Vec<aiapi_llm::types::LlmAttempt>,
}

/// Host-selected cleanup generation and rendering parameters.
#[derive(Debug, Clone)]
pub struct CleanupOptions {
    pub temperature: f32,
    pub max_tokens: u32,
    pub short_limit: usize,
    pub proxy_url: Option<String>,
    pub thinking_budget: u32,
}

pub async fn cleanup_transcript(
    secrets: &SecretStore,
    profiles: &LlmProfiles,
    transcript: &Transcript,
    alternatives: &[Transcript],
    options: &CleanupOptions,
) -> anyhow::Result<CleanupResult> {
    let prompt = build_user_prompt(options.short_limit, transcript, alternatives);
    let validator = |output: &str| {
        use aiapi_llm::types::{ValidationFailureReason, validation_error};
        let clean = parse_cleanup_json(output)
            .map_err(|_| validation_error(ValidationFailureReason::InvalidJson))?;
        validate_cleanup_against_asr(&clean, transcript, alternatives)
            .map_err(|_| validation_error(ValidationFailureReason::UnsupportedContent))
    };
    let generation = match generate_cleanup_content(secrets, profiles, &prompt, options, &validator)
        .await
    {
        Ok(generation) => generation,
        Err(err) => {
            tracing::warn!(%err, "all voice cleanup providers failed, using raw ASR transcript");
            return Ok(CleanupResult {
                provider: "raw_asr_fallback".to_string(),
                model: "none".to_string(),
                attempts: err
                    .downcast_ref::<aiapi_llm::types::GenerationAudit>()
                    .map(|audit| audit.attempts.clone())
                    .unwrap_or_default(),
                transcript: normalize_cleanup(
                    plain_cleanup(&transcript.text),
                    transcript,
                    options.short_limit,
                ),
            });
        }
    };

    let clean = match parse_cleanup_json(&generation.content).and_then(|clean| {
        validate_cleanup_against_asr(&clean, transcript, alternatives).map(|_| clean)
    }) {
        Ok(clean) => clean,
        Err(err) => {
            tracing::warn!(%err, "voice cleanup changed ASR beyond safe limits, using raw ASR transcript");
            plain_cleanup(&transcript.text)
        }
    };

    Ok(CleanupResult {
        provider: generation.provider,
        model: generation.model,
        transcript: normalize_cleanup(clean, transcript, options.short_limit),
        attempts: generation.attempts,
    })
}

async fn generate_cleanup_content(
    secrets: &SecretStore,
    profiles: &LlmProfiles,
    prompt: &str,
    options: &CleanupOptions,
    validator: &aiapi_llm::service::OutputValidator<'_>,
) -> anyhow::Result<aiapi_llm::types::GeneratedText> {
    let core = aiapi_config::CoreConfig {
        secrets: secrets.clone(),
        llm_proxy_url: options.proxy_url.clone(),
        gemini_thinking_budget: options.thinking_budget,
    };
    let supports_system_prompt = profiles
        .resolve_route("voice_cleanup", &Default::default())?
        .selections
        .iter()
        .all(|selection| selection.capabilities.supports_system_prompt);
    let combined = if supports_system_prompt {
        prompt.to_string()
    } else {
        format!("{CLEANUP_PROMPT}\n\n{prompt}")
    };
    generate_text_checked(
        &core,
        profiles,
        GenerateTextOptions {
            route: "voice_cleanup",
            system_prompt: supports_system_prompt.then_some(CLEANUP_PROMPT),
            prompt: &combined,
            image_base64: None,
            temperature: options.temperature,
            num_predict: options.max_tokens,
            output_validator: Some(validator),
            structured_output: None,
        },
    )
    .await
}

fn build_user_prompt(
    short_limit: usize,
    transcript: &Transcript,
    alternatives: &[Transcript],
) -> String {
    let source = format_transcript_source(transcript);
    let alternative_sources = alternatives
        .iter()
        .enumerate()
        .map(|(index, alternative)| {
            format!(
                "ALTERNATIVE_SOURCE_TRANSCRIPT_{} ({}/{}):\n{}",
                index + 1,
                alternative.provider,
                alternative.model,
                format_transcript_source(alternative),
            )
        })
        .collect::<Vec<_>>();
    let alternatives = if alternative_sources.is_empty() {
        "ALTERNATIVE_SOURCE_TRANSCRIPTS: none".to_string()
    } else {
        alternative_sources.join("\n\n")
    };

    format!(
        "SHORT_LIMIT={short_limit}\n\nPRIMARY_SOURCE_TRANSCRIPT ({}/{}):\n{source}\n\n{alternatives}",
        transcript.provider, transcript.model,
    )
}

fn format_transcript_source(transcript: &Transcript) -> String {
    if transcript.segments.is_empty() {
        transcript.text.clone()
    } else {
        transcript
            .segments
            .iter()
            .map(|segment| {
                format!(
                    "[{}-{}] {}",
                    time::format_timestamp(segment.start_sec),
                    time::format_timestamp(segment.end_sec),
                    segment.text
                )
            })
            .collect::<Vec<_>>()
            .join("\n")
    }
}

fn validate_cleanup_against_asr(
    clean: &CleanTranscript,
    transcript: &Transcript,
    _alternatives: &[Transcript],
) -> anyhow::Result<()> {
    validate_cleanup_text(&transcript.text, &clean.text)?;

    if !clean.chapters.is_empty() {
        let duration = transcript
            .segments
            .iter()
            .map(|segment| segment.end_sec)
            .filter(|seconds| seconds.is_finite() && *seconds > 0.0)
            .reduce(f32::max);
        let mut previous_start = 0.0;
        for chapter in &clean.chapters {
            if chapter.start_sec < previous_start
                || duration.is_some_and(|duration| {
                    chapter.start_sec > duration
                        || chapter.end_sec.is_some_and(|end| end > duration)
                })
            {
                anyhow::bail!("cleanup chapter timestamp is outside the source timeline");
            }
            previous_start = chapter.start_sec;
            validate_numbers(&transcript.text, &chapter.title)?;
        }

        let chapters = clean
            .chapters
            .iter()
            .map(|chapter| chapter.text.as_str())
            .collect::<Vec<_>>()
            .join(" ");
        validate_cleanup_text(&transcript.text, &chapters)?;
    }

    if let Some(summary) = &clean.short_summary {
        validate_numbers(&transcript.text, summary)?;
    }

    Ok(())
}

fn validate_cleanup_text(raw: &str, cleaned: &str) -> anyhow::Result<()> {
    let raw_len = content_len(raw);
    let cleaned_len = content_len(cleaned);
    if raw_len >= 100
        && (cleaned_len * 100 < raw_len * MIN_CLEANUP_CONTENT_PERCENT
            || cleaned_len * 100 > raw_len * MAX_CLEANUP_CONTENT_PERCENT)
    {
        anyhow::bail!(
            "cleanup content length changed too much: raw={raw_len}, cleaned={cleaned_len}"
        );
    }

    validate_numbers(raw, cleaned)
}

fn validate_numbers(raw: &str, cleaned: &str) -> anyhow::Result<()> {
    let raw_numbers = number_tokens(raw);
    let added_numbers = number_tokens(cleaned)
        .difference(&raw_numbers)
        .cloned()
        .collect::<Vec<_>>();
    if !added_numbers.is_empty() {
        anyhow::bail!("cleanup introduced numbers not present in ASR");
    }

    Ok(())
}

fn content_len(text: &str) -> usize {
    text.chars().filter(|ch| ch.is_alphanumeric()).count()
}

fn number_tokens(text: &str) -> BTreeSet<String> {
    text.split(|ch: char| !ch.is_ascii_digit())
        .filter(|token| !token.is_empty())
        .map(ToOwned::to_owned)
        .collect()
}

fn parse_cleanup_json(value: &str) -> anyhow::Result<CleanTranscript> {
    let json = strip_code_fence(value.trim());
    let response: CleanupResponse = serde_json::from_str(json)?;
    let text = response.text.trim().to_string();
    if text.is_empty() {
        anyhow::bail!("cleanup response text is empty");
    }

    let mode = match response.mode.as_deref() {
        Some("chapters") => TranscriptRenderMode::Chapters,
        Some("file") => TranscriptRenderMode::File,
        _ => TranscriptRenderMode::Short,
    };

    Ok(CleanTranscript {
        mode,
        text,
        chapters: response
            .chapters
            .into_iter()
            .filter_map(|chapter| {
                let start_sec = time::parse_timestamp(&chapter.start)?;
                let end_sec = match chapter.end.as_deref() {
                    Some(end) => Some(time::parse_timestamp(end)?),
                    None => None,
                };
                if end_sec.is_some_and(|end| end < start_sec) {
                    return None;
                }
                Some(TranscriptChapter {
                    title: chapter.title.trim().to_string(),
                    start_sec,
                    end_sec,
                    text: chapter.text.trim().to_string(),
                })
            })
            .filter(|chapter| !chapter.title.is_empty() && !chapter.text.is_empty())
            .collect(),
        short_summary: response.short_summary.map(|value| value.trim().to_string()),
    })
}

fn plain_cleanup(value: &str) -> CleanTranscript {
    let text = value.trim().to_string();
    CleanTranscript {
        mode: TranscriptRenderMode::Short,
        text,
        chapters: Vec::new(),
        short_summary: None,
    }
}

fn normalize_cleanup(
    mut clean: CleanTranscript,
    _transcript: &Transcript,
    short_limit: usize,
) -> CleanTranscript {
    if clean.text.chars().count() <= short_limit || clean.chapters.is_empty() {
        clean.mode = TranscriptRenderMode::Short;
        clean.chapters.clear();
        return clean;
    }

    if clean.mode == TranscriptRenderMode::Short {
        clean.mode = TranscriptRenderMode::Chapters;
    }

    clean
}

fn strip_code_fence(value: &str) -> &str {
    let value = value.trim();
    let Some(rest) = value.strip_prefix("```") else {
        return value;
    };
    let rest = rest
        .strip_prefix("json")
        .or_else(|| rest.strip_prefix("JSON"))
        .unwrap_or(rest)
        .trim_start();
    rest.strip_suffix("```").unwrap_or(rest).trim()
}

#[derive(Debug, Deserialize)]
struct CleanupResponse {
    mode: Option<String>,
    text: String,
    #[serde(default)]
    chapters: Vec<CleanupChapter>,
    short_summary: Option<String>,
}

#[derive(Debug, Deserialize)]
struct CleanupChapter {
    title: String,
    start: String,
    end: Option<String>,
    text: String,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn long_cleanup_without_model_chapters_stays_plain_text() {
        let transcript = Transcript {
            provider: "groq".to_string(),
            model: "whisper".to_string(),
            request_id: None,
            text: "Длинный текст без явного деления на темы.".to_string(),
            segments: Vec::new(),
        };
        let clean = CleanTranscript {
            mode: TranscriptRenderMode::Short,
            text: "Длинный текст без явного деления на темы.".to_string(),
            chapters: Vec::new(),
            short_summary: None,
        };

        let normalized = normalize_cleanup(clean, &transcript, 20);
        assert_eq!(normalized.mode, TranscriptRenderMode::Short);
        assert!(normalized.chapters.is_empty());
    }

    #[test]
    fn cleanup_preserves_source_text_without_rewriting() {
        let clean = CleanTranscript {
            mode: TranscriptRenderMode::Short,
            text: "Грок и клинап через гемма.".to_string(),
            chapters: Vec::new(),
            short_summary: None,
        };
        let source = Transcript {
            provider: "groq".to_string(),
            model: "whisper".to_string(),
            request_id: None,
            text: String::new(),
            segments: Vec::new(),
        };

        let normalized = normalize_cleanup(clean, &source, 400);
        assert_eq!(normalized.text, "Грок и клинап через гемма.");
    }

    #[test]
    fn prompt_uses_timestamped_segments_without_duplicate_raw_text() {
        let transcript = Transcript {
            provider: "groq".to_string(),
            model: "whisper".to_string(),
            request_id: None,
            text: "дублировать этот текст не надо".to_string(),
            segments: vec![aiapi_types::AsrSegment {
                start_sec: 2.0,
                end_sec: 5.0,
                text: "единственный источник".to_string(),
            }],
        };

        let prompt = build_user_prompt(400, &transcript, &[]);

        assert!(prompt.contains("SOURCE_TRANSCRIPT"));
        assert!(prompt.contains("PRIMARY_SOURCE_TRANSCRIPT (groq/whisper)"));
        assert!(prompt.contains("[0:02-0:05] единственный источник"));
        assert!(!prompt.contains(&transcript.text));
    }

    #[test]
    fn prompt_includes_alternative_asr_without_replacing_primary() {
        let primary = transcript_with_text("Вышла Gemma 4 31B.");
        let alternative = Transcript {
            provider: "gemini".to_string(),
            model: "gemini-3.5-transcribe".to_string(),
            request_id: None,
            text: "Вышла Gemma 4 31B, кажется.".to_string(),
            segments: Vec::new(),
        };

        let prompt = build_user_prompt(400, &primary, &[alternative]);

        assert!(prompt.contains("PRIMARY_SOURCE_TRANSCRIPT (groq/whisper)"));
        assert!(prompt.contains("ALTERNATIVE_SOURCE_TRANSCRIPT_1 (gemini/gemini-3.5-transcribe)"));
        assert!(prompt.contains("Вышла Gemma 4 31B, кажется."));
    }

    #[test]
    fn cleanup_parses_valid_chapter_json() {
        let clean = parse_cleanup_json(
            r#"{
                "mode": "chapters",
                "text": "Сначала обсудили драйверы, затем игры.",
                "chapters": [{
                    "title": "Драйверы",
                    "start": "0:05",
                    "end": "1:10",
                    "text": "Обсудили новый драйвер."
                }],
                "short_summary": "Разговор о драйверах."
            }"#,
        )
        .unwrap();

        assert_eq!(clean.mode, TranscriptRenderMode::Chapters);
        assert_eq!(clean.chapters.len(), 1);
        assert_eq!(clean.chapters[0].start_sec, 5.0);
        assert_eq!(clean.chapters[0].end_sec, Some(70.0));
        assert_eq!(
            clean.short_summary.as_deref(),
            Some("Разговор о драйверах.")
        );
    }

    #[test]
    fn invalid_cleanup_json_falls_back_to_plain_asr_text() {
        assert!(parse_cleanup_json("не JSON").is_err());

        let fallback = plain_cleanup("  Исходная расшифровка.  ");
        assert_eq!(fallback.mode, TranscriptRenderMode::Short);
        assert_eq!(fallback.text, "Исходная расшифровка.");
        assert!(fallback.chapters.is_empty());
    }

    #[test]
    fn cleanup_rejects_new_numbers_and_excessive_rewrite() {
        let raw = "Gemma 4 31B ".repeat(40);
        let transcript = transcript_with_text(&raw);
        let with_new_number = plain_cleanup(&format!("{} 2027", raw));
        let rewritten = plain_cleanup("короткое резюме");

        assert!(validate_cleanup_against_asr(&with_new_number, &transcript, &[]).is_err());
        assert!(validate_cleanup_against_asr(&rewritten, &transcript, &[]).is_err());
    }

    #[test]
    fn cleanup_accepts_punctuation_edits_without_new_numbers() {
        let transcript = transcript_with_text("Вышла Gemma 4 31B и новый драйвер для Vulkan.");
        let clean = plain_cleanup("Вышла Gemma 4 31B, и новый драйвер для Vulkan.");

        assert!(validate_cleanup_against_asr(&clean, &transcript, &[]).is_ok());
    }

    #[test]
    fn cleanup_allows_removing_incoherent_asr_fragment() {
        let transcript = transcript_with_text(
            "Без Mac нельзя писать под iOS. Mac Windows GPT E FNI Mac Windows Linux. Продолжаем разговор.",
        );
        let clean = plain_cleanup("Без Mac нельзя писать под iOS. Продолжаем разговор.");

        assert!(validate_cleanup_against_asr(&clean, &transcript, &[]).is_ok());
    }

    #[test]
    fn cleanup_allows_word_corrections_without_new_numbers() {
        let transcript = transcript_with_text("Модель Fable 5 пока не вышла.");
        let clean = plain_cleanup("Модель Claude 5 пока не вышла.");

        assert!(validate_cleanup_against_asr(&clean, &transcript, &[]).is_ok());
    }

    #[test]
    fn cleanup_accepts_allowlisted_provider_normalization() {
        let transcript = transcript_with_text("Проверим грок и клинап.");
        let clean = plain_cleanup("Проверим groq и cleanup.");

        assert!(validate_cleanup_against_asr(&clean, &transcript, &[]).is_ok());
    }

    fn transcript_with_text(text: &str) -> Transcript {
        Transcript {
            provider: "groq".to_string(),
            model: "whisper".to_string(),
            request_id: None,
            text: text.to_string(),
            segments: Vec::new(),
        }
    }
}
