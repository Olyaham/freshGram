use genai::chat::{ChatMessage, ChatResponse, Tool};

use aiapi_config::CoreConfig;

use crate::genai_transport::{
    GenAiChatRequest, GenAiRequest, GenAiTransport, ImageInput, ModelTarget,
};
use crate::profiles::LlmProfiles;
use crate::profiles::{RouteRequirements, RouteSelection};
use crate::types::{
    GeneratedText, GenerationAudit, LlmAttempt, LlmTransportError, StructuredOutput,
    ValidationFailure,
};

pub type OutputValidator<'a> = dyn Fn(&str) -> anyhow::Result<()> + Send + Sync + 'a;

pub struct GenerateTextOptions<'a> {
    /// Task route name from the supplied profiles.
    pub route: &'a str,
    pub system_prompt: Option<&'a str>,
    pub prompt: &'a str,
    pub image_base64: Option<&'a str>,
    pub temperature: f32,
    pub num_predict: u32,
    pub output_validator: Option<&'a OutputValidator<'a>>,
    pub structured_output: Option<StructuredOutput<'a>>,
}

pub struct GenerateChatOptions<'a> {
    /// Skip earlier compatible profiles when retrying a timed-out turn.
    pub fallback_offset: usize,
    /// Task route name from the supplied profiles.
    pub route: &'a str,
    pub system_prompt: Option<&'a str>,
    pub messages: Vec<ChatMessage>,
    pub tools: Option<Vec<Tool>>,
    pub requires_images: bool,
    pub requires_tools: bool,
    pub previous_response_id: Option<String>,
    pub temperature: f32,
    /// Requested output budget, optionally capped to the selected model.
    pub num_predict: u32,
    pub output_budget_policy: OutputBudgetPolicy,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum OutputBudgetPolicy {
    /// Preserve the requested budget and validate it during route resolution.
    #[default]
    Fixed,
    /// Cap each fallback request to the selected model's configured output limit.
    AdaptToModel,
}

const VALIDATION_RETRY_ATTEMPTS: usize = 1;

pub struct GeneratedChat {
    pub response: ChatResponse,
    pub provider: String,
    pub model: String,
}

struct GenerateOnceRequest<'a> {
    system_prompt: Option<&'a str>,
    prompt: &'a str,
    image_base64: Option<&'a str>,
    temperature: f32,
    num_predict: u32,
    structured_output: Option<StructuredOutput<'a>>,
}

pub async fn generate_chat_checked(
    config: &CoreConfig,
    profiles: &LlmProfiles,
    options: GenerateChatOptions<'_>,
) -> anyhow::Result<ChatResponse> {
    generate_chat_audited_checked(config, profiles, options)
        .await
        .map(|generated| generated.response)
}

pub async fn generate_chat_audited_checked(
    config: &CoreConfig,
    profiles: &LlmProfiles,
    options: GenerateChatOptions<'_>,
) -> anyhow::Result<GeneratedChat> {
    generate_chat_with_profile_checked(config, profiles, options).await
}

pub async fn generate_text_checked(
    config: &CoreConfig,
    profiles: &LlmProfiles,
    options: GenerateTextOptions<'_>,
) -> anyhow::Result<GeneratedText> {
    generate_text_with_profile_checked(config, profiles, options).await
}

async fn generate_text_with_profile_checked(
    config: &CoreConfig,
    profiles: &crate::profiles::LlmProfiles,
    options: GenerateTextOptions<'_>,
) -> anyhow::Result<GeneratedText> {
    let route = options.route;
    let requirements = RouteRequirements {
        requires_images: options.image_base64.is_some(),
        requires_system_prompt: options.system_prompt.is_some(),
        num_predict: Some(options.num_predict),
        // Любой из режимов профиля задаёт transport contract. PromptOnly намеренно
        // допустим: JSON-контракт остаётся в prompt и проверяется validator-ом.
        structured_output: None,
        ..RouteRequirements::default()
    };
    let resolved = profiles.resolve_route(route, &requirements)?;
    let mut last_error = None;
    let mut attempts = Vec::new();

    for (fallback_index, selection) in resolved.selections.iter().enumerate() {
        let mut attempt_prompt = options.prompt.to_string();
        for attempt in 0..=VALIDATION_RETRY_ATTEMPTS {
            let generation = generate_profile_once(
                config,
                selection,
                GenerateOnceRequest {
                    system_prompt: options.system_prompt,
                    prompt: &attempt_prompt,
                    image_base64: options.image_base64,
                    temperature: options.temperature,
                    num_predict: options.num_predict,
                    structured_output: options.structured_output,
                },
            )
            .await;
            match generation {
                Ok(mut generation) => {
                    let llm_attempt = generation.attempts.pop().expect("generation has attempt");
                    if let Some(validate) = options.output_validator
                        && let Err(err) = validate(&generation.content)
                    {
                        let validation_reason = err
                            .downcast_ref::<ValidationFailure>()
                            .map_or(crate::types::ValidationFailureReason::Unknown, |failure| {
                                failure.reason
                            });
                        let err = crate::types::validation_error(validation_reason);
                        attempts.push(LlmAttempt {
                            outcome: "validation_failed".to_string(),
                            validation_reason: Some(validation_reason),
                            ..llm_attempt
                        });
                        // Безопасно: validator возвращает только причину
                        // проверки, без промпта и текста ответа.
                        tracing::warn!(
                            route,
                            fallback_index,
                            attempt,
                            provider = selection.provider_key,
                            model = selection.model.model,
                            outcome = "validation_failed",
                            error = format!("{err:#}"),
                            "LLM profile attempt failed validation"
                        );
                        last_error = Some(err);
                        if attempt < VALIDATION_RETRY_ATTEMPTS {
                            attempt_prompt = validation_retry_prompt(
                                options.prompt,
                                &format!("{:#}", last_error.as_ref().expect("validation error")),
                            );
                            continue;
                        }
                        if !resolved.fallback_on_validation_failure {
                            return Err(last_error
                                .expect("validation error")
                                .context(GenerationAudit { attempts }));
                        }
                        break;
                    }
                    attempts.push(llm_attempt);
                    generation.attempts = attempts;
                    if fallback_index > 0 {
                        tracing::info!(
                            route,
                            fallback_index,
                            provider = selection.provider_key,
                            model = selection.model.model,
                            "LLM profile fallback succeeded"
                        );
                    }
                    return Ok(generation);
                }
                Err(err) => {
                    let empty_response = is_empty_response(&err);
                    let outcome = classify_attempt_error(&err);
                    // Безопасно: generate_profile_once маппит транспорт в
                    // LlmTransportError без тел ответов, URL и ключей.
                    tracing::warn!(
                        route,
                        fallback_index,
                        attempt,
                        provider = selection.provider_key,
                        model = selection.model.model,
                        outcome = outcome.as_str(),
                        error = format!("{err:#}"),
                        "LLM profile attempt failed"
                    );
                    attempts.push(LlmAttempt {
                        provider: selection.provider_key.to_string(),
                        model: selection.model.model.clone(),
                        outcome,
                        validation_reason: None,
                    });
                    last_error = Some(err);
                    if empty_response && attempt < VALIDATION_RETRY_ATTEMPTS {
                        attempt_prompt = validation_retry_prompt(
                            options.prompt,
                            "the model returned an empty response; return the complete requested output",
                        );
                        continue;
                    }
                    break;
                }
            }
        }
    }

    Err(last_error
        .unwrap_or_else(|| {
            anyhow::anyhow!("no compatible LLM profile generation attempts were configured")
        })
        .context(GenerationAudit { attempts }))
}

async fn generate_chat_with_profile_checked(
    config: &CoreConfig,
    profiles: &crate::profiles::LlmProfiles,
    options: GenerateChatOptions<'_>,
) -> anyhow::Result<GeneratedChat> {
    let route = options.route;
    let requirements = chat_route_requirements(&options);
    let resolved = profiles.resolve_route(route, &requirements)?;
    let mut last_error = None;

    let offset = options
        .fallback_offset
        .min(resolved.selections.len().saturating_sub(1));
    for (fallback_index, selection) in resolved.selections.iter().enumerate().skip(offset) {
        match generate_chat_profile_once(config, selection, &options).await {
            Ok(response) => {
                if fallback_index > 0 {
                    tracing::info!(
                        route,
                        fallback_index,
                        provider = selection.provider_key,
                        model = selection.model.model,
                        "LLM chat profile fallback succeeded"
                    );
                }
                return Ok(GeneratedChat {
                    response,
                    provider: selection.provider_key.into(),
                    model: selection.model.model.clone(),
                });
            }
            Err(error) => {
                tracing::warn!(
                    route,
                    fallback_index,
                    provider = selection.provider_key,
                    model = selection.model.model,
                    outcome = classify_attempt_error(&error),
                    "LLM chat profile attempt failed"
                );
                last_error = Some(error);
            }
        }
    }

    Err(last_error.unwrap_or_else(|| {
        anyhow::anyhow!("no compatible LLM profile chat attempts were configured")
    }))
}

fn chat_route_requirements(options: &GenerateChatOptions<'_>) -> RouteRequirements {
    RouteRequirements {
        requires_images: options.requires_images,
        requires_tools: options.requires_tools || options.tools.is_some(),
        requires_system_prompt: options.system_prompt.is_some(),
        num_predict: match options.output_budget_policy {
            OutputBudgetPolicy::Fixed => Some(options.num_predict),
            OutputBudgetPolicy::AdaptToModel => None,
        },
        ..RouteRequirements::default()
    }
}

async fn generate_chat_profile_once(
    config: &CoreConfig,
    selection: &RouteSelection<'_>,
    options: &GenerateChatOptions<'_>,
) -> anyhow::Result<ChatResponse> {
    let api_key = if selection.provider.api_key_env.is_empty() {
        String::new()
    } else {
        config
            .secrets
            .require(&selection.provider.api_key_env)
            .map_err(|_| {
                anyhow::anyhow!(
                    "LLM profile provider {:?} requires configured secret {}",
                    selection.provider_key,
                    selection.provider.api_key_env
                )
            })?
    };
    let max_tokens = output_budget_for_model(
        options.num_predict,
        options.output_budget_policy,
        selection.capabilities.max_output_tokens,
    );
    let transport = GenAiTransport::cached(config.llm_proxy_url.as_deref())?;
    transport
        .chat(GenAiChatRequest {
            model: ModelTarget {
                adapter: selection.provider.genai_adapter(),
                endpoint: &selection.provider.base_url,
                api_key: &api_key,
                model: &selection.model.model,
            },
            system_prompt: options.system_prompt.map(str::to_owned),
            messages: options.messages.clone(),
            tools: options.tools.clone(),
            previous_response_id: options.previous_response_id.clone(),
            temperature: options.temperature,
            max_tokens,
            timeout: std::time::Duration::from_secs(selection.capabilities.request_timeout_sec),
            reasoning: selection.capabilities.thinking,
            reasoning_budget: Some(config.gemini_thinking_budget),
            egress: selection.provider.egress,
        })
        .await
        .map_err(anyhow::Error::new)
}

fn output_budget_for_model(requested: u32, policy: OutputBudgetPolicy, model_limit: u32) -> u32 {
    match policy {
        OutputBudgetPolicy::Fixed => requested,
        OutputBudgetPolicy::AdaptToModel => requested.min(model_limit),
    }
}

async fn generate_profile_once(
    config: &CoreConfig,
    selection: &RouteSelection<'_>,
    request: GenerateOnceRequest<'_>,
) -> anyhow::Result<GeneratedText> {
    let GenerateOnceRequest {
        system_prompt,
        prompt,
        image_base64,
        temperature,
        num_predict,
        structured_output,
        ..
    } = request;
    let api_key = if selection.provider.api_key_env.is_empty() {
        String::new()
    } else {
        config
            .secrets
            .require(&selection.provider.api_key_env)
            .map_err(|_| {
                anyhow::anyhow!(
                    "LLM profile provider {:?} requires configured secret {}",
                    selection.provider_key,
                    selection.provider.api_key_env
                )
            })?
    };
    let image_base64 = image_base64.filter(|_| selection.capabilities.supports_images);
    let timeout = std::time::Duration::from_secs(selection.capabilities.request_timeout_sec);
    let image = image_base64.map(|base64| ImageInput {
        mime_type: "image/jpeg",
        base64,
        file_name: Some("image.jpg"),
    });
    let transport = GenAiTransport::cached(config.llm_proxy_url.as_deref())?;
    let response = transport
        .generate(GenAiRequest {
            model: ModelTarget {
                adapter: selection.provider.genai_adapter(),
                endpoint: &selection.provider.base_url,
                api_key: &api_key,
                model: &selection.model.model,
            },
            system_prompt,
            prompt,
            image,
            temperature,
            max_tokens: num_predict,
            timeout,
            reasoning: selection.capabilities.thinking,
            reasoning_budget: Some(config.gemini_thinking_budget),
            structured_output_mode: selection.capabilities.structured_output,
            structured_output,
            extra_body: None,
            egress: selection.provider.egress,
        })
        .await
        .map_err(anyhow::Error::new)?;
    Ok(GeneratedText {
        provider: selection.provider_key.to_string(),
        model: selection.model.model.clone(),
        content: response,
        image_used: image_base64.is_some(),
        attempts: vec![LlmAttempt {
            provider: selection.provider_key.to_string(),
            model: selection.model.model.clone(),
            outcome: "success".to_string(),
            validation_reason: None,
        }],
    })
}

fn classify_attempt_error(error: &anyhow::Error) -> String {
    match error.downcast_ref::<LlmTransportError>() {
        Some(LlmTransportError::HttpStatus(429)) => "http_429".to_string(),
        Some(LlmTransportError::HttpStatus(status)) if *status >= 500 => "http_5xx".to_string(),
        Some(LlmTransportError::HttpStatus(status)) => format!("http_{status}"),
        Some(LlmTransportError::Configuration) => "configuration".to_string(),
        Some(LlmTransportError::Timeout) => "timeout".to_string(),
        Some(LlmTransportError::EmptyResponse) => "empty_response".to_string(),
        Some(LlmTransportError::InvalidResponse) => "invalid_response".to_string(),
        Some(LlmTransportError::UnsupportedFeature) => "unsupported_feature".to_string(),
        Some(LlmTransportError::StructuredOutputRejected) => {
            "structured_output_fallback".to_string()
        }
        None if error
            .downcast_ref::<reqwest::Error>()
            .is_some_and(reqwest::Error::is_timeout) =>
        {
            "timeout".to_string()
        }
        None => "error".to_string(),
    }
}

fn is_empty_response(error: &anyhow::Error) -> bool {
    matches!(
        error.downcast_ref::<LlmTransportError>(),
        Some(LlmTransportError::EmptyResponse)
    )
}

fn validation_retry_prompt(original_prompt: &str, validation_error: &str) -> String {
    format!(
        "{original_prompt}\n\nThe previous output failed validation: {validation_error}. Return a new answer that follows the requested format and constraints."
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::profiles::LlmProfiles;
    use aiapi_config::CoreConfig;
    use axum::{
        Json, Router, extract::State, http::StatusCode, response::IntoResponse, routing::post,
    };
    use serde_json::{Value, json};
    use tokio::sync::mpsc;

    fn core() -> CoreConfig {
        CoreConfig::default()
    }

    #[tokio::test]
    async fn timed_out_native_turn_can_retry_another_profile_and_report_its_identity() {
        use std::sync::{
            Arc,
            atomic::{AtomicUsize, Ordering},
        };
        let primary_calls = Arc::new(AtomicUsize::new(0));
        let calls = primary_calls.clone();
        let reply = || {
            Json(
                json!({"id":"mock-chat","object":"chat.completion","created":1,"model":"mock-model",
            "choices":[{"index":0,"message":{"role":"assistant","content":"Готово"},"finish_reason":"stop"}]}),
            )
        };
        let primary = post(move || {
            let calls = calls.clone();
            async move {
                calls.fetch_add(1, Ordering::SeqCst);
                tokio::time::sleep(std::time::Duration::from_millis(250)).await;
                reply()
            }
        });
        let app = Router::new()
            .route("/primary/v1/chat/completions", primary)
            .route(
                "/fallback/v1/chat/completions",
                post(move || async move { reply() }),
            );
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let server = tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });
        let mut topology = String::new();
        for name in ["primary", "fallback"] {
            topology.push_str(&format!(
                r#"
[providers.{name}]
driver = "openai_compatible"
base_url = "http://{address}/{name}/v1"
api_key_env = "PROFILE_NATIVE_RETRY_TEST_KEY"
[models.{name}]
provider = "{name}"
model = "mock-{name}"
[models.{name}.capabilities]
supports_images = false
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 4096
max_output_tokens = 1024
request_timeout_sec = 2
thinking = "none"
"#
            ));
        }
        topology.push_str("\n[routes.ask]\nmodels = ['primary', 'fallback']\n[runtime]\n");
        let mut core = core();
        core.secrets = core
            .secrets
            .with_override("PROFILE_NATIVE_RETRY_TEST_KEY", "test-key");
        let profiles = LlmProfiles::from_toml(&topology).unwrap();
        let options = |fallback_offset| GenerateChatOptions {
            fallback_offset,
            route: "ask",
            system_prompt: Some("test"),
            messages: vec![ChatMessage::user("test")],
            tools: None,
            requires_images: false,
            requires_tools: true,
            previous_response_id: None,
            temperature: 0.0,
            num_predict: 128,
            output_budget_policy: OutputBudgetPolicy::Fixed,
        };
        assert!(
            tokio::time::timeout(
                std::time::Duration::from_millis(100),
                generate_chat_audited_checked(&core, &profiles, options(0))
            )
            .await
            .is_err()
        );
        let generated = generate_chat_audited_checked(&core, &profiles, options(1))
            .await
            .unwrap();
        server.abort();
        assert_eq!(primary_calls.load(Ordering::SeqCst), 1);
        assert_eq!(generated.provider, "fallback");
        assert_eq!(generated.model, "mock-fallback");
        assert_eq!(generated.response.first_text(), Some("Готово"));
    }

    #[test]
    fn chat_route_image_requirement_keeps_only_vision_models() {
        let options = GenerateChatOptions {
            fallback_offset: 0,
            route: "ask",
            system_prompt: Some("system"),
            messages: Vec::new(),
            tools: None,
            requires_images: true,
            requires_tools: true,
            previous_response_id: None,
            temperature: 0.0,
            num_predict: 128,
            output_budget_policy: OutputBudgetPolicy::Fixed,
        };
        let profiles = LlmProfiles::from_toml(
            r#"
[providers.acme]
driver = "openai_compatible"
base_url = "http://127.0.0.1:9/v1"
api_key_env = "AIAPI_ASK_TEST_KEY"

[models.ask_vision]
provider = "acme"
model = "acme/vision"
[models.ask_vision.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 4096
max_output_tokens = 4096
request_timeout_sec = 5
thinking = "none"

[models.ask_free]
provider = "acme"
model = "acme/free"
[models.ask_free.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 4096
max_output_tokens = 4096
request_timeout_sec = 5
thinking = "none"

[models.ask_mini]
provider = "acme"
model = "acme/mini"
[models.ask_mini.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 4096
max_output_tokens = 2048
request_timeout_sec = 5
thinking = "none"

[models.ask_base]
provider = "acme"
model = "acme/base"
[models.ask_base.capabilities]
supports_images = false
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 32768
max_output_tokens = 16384
request_timeout_sec = 5
thinking = "none"

[routes.ask]
models = ["ask_vision", "ask_free", "ask_mini", "ask_base"]
"#,
        )
        .unwrap();

        let resolved = profiles
            .resolve_route("ask", &chat_route_requirements(&options))
            .unwrap();

        assert_eq!(resolved.selections.len(), 3);
        assert_eq!(resolved.selections[0].model.model, "acme/vision");
        assert_eq!(resolved.selections[1].model.model, "acme/free");
        assert_eq!(resolved.selections[2].model.model, "acme/mini");
    }

    #[test]
    fn ask_output_budget_is_capped_to_each_selected_model_profile() {
        assert_eq!(
            output_budget_for_model(16_384, OutputBudgetPolicy::AdaptToModel, 4_096),
            4_096
        );
        assert_eq!(
            output_budget_for_model(16_384, OutputBudgetPolicy::AdaptToModel, 16_384),
            16_384
        );

        let options = GenerateChatOptions {
            fallback_offset: 0,
            route: "ask",
            system_prompt: Some("system"),
            messages: Vec::new(),
            tools: None,
            requires_images: false,
            requires_tools: true,
            previous_response_id: None,
            temperature: 0.0,
            num_predict: 16_384,
            output_budget_policy: OutputBudgetPolicy::AdaptToModel,
        };
        let requirements = chat_route_requirements(&options);
        assert_eq!(requirements.num_predict, None);

        let profiles = LlmProfiles::from_toml(
            r#"
[providers.acme]
driver = "openai_compatible"
base_url = "http://127.0.0.1:9/v1"
api_key_env = "AIAPI_ASK_TEST_KEY"

[models.ask_vision]
provider = "acme"
model = "acme/vision"
[models.ask_vision.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 4096
max_output_tokens = 4096
request_timeout_sec = 5
thinking = "none"

[models.ask_free]
provider = "acme"
model = "acme/free"
[models.ask_free.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 4096
max_output_tokens = 4096
request_timeout_sec = 5
thinking = "none"

[models.ask_mini]
provider = "acme"
model = "acme/mini"
[models.ask_mini.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 4096
max_output_tokens = 2048
request_timeout_sec = 5
thinking = "none"

[models.ask_base]
provider = "acme"
model = "acme/base"
[models.ask_base.capabilities]
supports_images = false
supports_tools = true
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 32768
max_output_tokens = 16384
request_timeout_sec = 5
thinking = "none"

[routes.ask]
models = ["ask_vision", "ask_free", "ask_mini", "ask_base"]
"#,
        )
        .unwrap();
        let route = profiles.resolve_route("ask", &requirements).unwrap();
        assert_eq!(route.selections.len(), 4);
        assert_eq!(route.selections[1].model.model, "acme/free");
        assert_eq!(
            output_budget_for_model(
                options.num_predict,
                options.output_budget_policy,
                route.selections[1].capabilities.max_output_tokens,
            ),
            4_096
        );
    }

    #[tokio::test]
    async fn profile_prompt_only_omits_response_format_and_validator_accepts_json_contract() {
        async fn capture(
            State(sender): State<mpsc::UnboundedSender<Value>>,
            Json(body): Json<Value>,
        ) -> Json<Value> {
            sender.send(body).unwrap();
            Json(json!({
                "id": "profile-test",
                "object": "chat.completion",
                "created": 0,
                "model": "profile-model",
                "choices": [{
                    "index": 0,
                    "message": {"role": "assistant", "content": "{\"ok\":true}"},
                    "finish_reason": "stop"
                }]
            }))
        }

        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let (sender, mut receiver) = mpsc::unbounded_channel();
        let server = tokio::spawn(async move {
            axum::serve(
                listener,
                Router::new()
                    .route("/v1/chat/completions", post(capture))
                    .with_state(sender),
            )
            .await
            .unwrap();
        });
        let core = core();
        let profiles = LlmProfiles::from_toml(&format!(
            r#"
[providers.test]
driver = "openai_compatible"
base_url = "http://{address}/v1"
api_key_env = "PROFILE_PROMPT_ONLY_TEST_KEY"

[models.test]
provider = "test"
model = "profile-model"
[models.test.capabilities]
supports_images = false
supports_tools = false
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 4096
max_output_tokens = 256
request_timeout_sec = 5
thinking = "none"

[routes.profile_test]
models = ["test"]

[runtime]
"#
        ))
        .unwrap();
        let mut core = core;
        core.secrets = core
            .secrets
            .with_override("PROFILE_PROMPT_ONLY_TEST_KEY", "test-key");
        let schema = json!({"type": "object"});
        let validator = |content: &str| -> anyhow::Result<()> {
            let value: Value = serde_json::from_str(content)?;
            anyhow::ensure!(value["ok"] == true, "JSON contract is invalid");
            Ok(())
        };

        let generated = generate_text_checked(
            &core,
            &profiles,
            GenerateTextOptions {
                route: "profile_test",
                system_prompt: Some("return a JSON object"),
                prompt: "{\"contract\":\"JSON only\"}",
                image_base64: None,
                temperature: 0.0,
                num_predict: 64,
                output_validator: Some(&validator),
                structured_output: Some(StructuredOutput {
                    name: "test_contract",
                    schema: &schema,
                }),
            },
        )
        .await
        .unwrap();

        assert_eq!(generated.provider, "test");
        assert_eq!(generated.model, "profile-model");
        assert_eq!(generated.content, "{\"ok\":true}");
        let request = receiver.recv().await.unwrap();
        assert!(request.get("response_format").is_none());
        assert_eq!(request["model"], "profile-model");
        server.abort();
    }

    fn switching_profiles(
        address: std::net::SocketAddr,
        primary_path: &str,
        fallback_path: &str,
        fallback_on_validation_failure: bool,
    ) -> LlmProfiles {
        LlmProfiles::from_toml(&format!(
            r#"
[providers.primary]
driver = "openai_compatible"
base_url = "http://{address}/{primary_path}/v1"
api_key_env = "PROFILE_SWITCH_PRIMARY_KEY"
[providers.fallback]
driver = "openai_compatible"
base_url = "http://{address}/{fallback_path}/v1"
api_key_env = "PROFILE_SWITCH_FALLBACK_KEY"

[models.primary]
provider = "primary"
model = "primary-model"
[models.primary.capabilities]
supports_images = false
supports_tools = false
supports_system_prompt = true
structured_output = "json_schema"
context_window_tokens = 4096
max_output_tokens = 256
request_timeout_sec = 5
thinking = "none"

[models.fallback]
provider = "fallback"
model = "fallback-model"
[models.fallback.capabilities]
supports_images = false
supports_tools = false
supports_system_prompt = true
structured_output = "json_schema"
context_window_tokens = 4096
max_output_tokens = 256
request_timeout_sec = 5
thinking = "none"

[routes.switch]
models = ["primary", "fallback"]
fallback_on_validation_failure = {fallback_on_validation_failure}

[runtime]
"#
        ))
        .unwrap()
    }

    fn profile_options<'a>(validator: Option<&'a OutputValidator<'a>>) -> GenerateTextOptions<'a> {
        GenerateTextOptions {
            route: "switch",
            system_prompt: Some("system"),
            prompt: "prompt",
            image_base64: None,
            temperature: 0.0,
            num_predict: 64,
            output_validator: validator,
            structured_output: None,
        }
    }

    fn completion(content: &str) -> Value {
        json!({
            "id": "switch-test",
            "object": "chat.completion",
            "created": 0,
            "model": "profile-model",
            "choices": [{
                "index": 0,
                "message": {"role": "assistant", "content": content},
                "finish_reason": "stop"
            }]
        })
    }

    #[tokio::test]
    async fn profile_transport_failure_falls_back_and_records_attempts() {
        async fn primary() -> impl IntoResponse {
            (StatusCode::INTERNAL_SERVER_ERROR, "unavailable")
        }
        async fn fallback() -> Json<Value> {
            Json(completion("fallback response"))
        }

        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let server = tokio::spawn(async move {
            axum::serve(
                listener,
                Router::new()
                    .route("/primary/v1/chat/completions", post(primary))
                    .route("/fallback/v1/chat/completions", post(fallback)),
            )
            .await
            .unwrap();
        });
        let mut core = core();
        core.secrets = core
            .secrets
            .with_override("PROFILE_SWITCH_PRIMARY_KEY", "test-key")
            .with_override("PROFILE_SWITCH_FALLBACK_KEY", "test-key");
        let profiles = switching_profiles(address, "primary", "fallback", false);

        let generated = generate_text_checked(&core, &profiles, profile_options(None))
            .await
            .unwrap();

        assert_eq!(generated.content, "fallback response");
        assert_eq!(generated.attempts.len(), 2);
        assert_eq!(generated.attempts[0].provider, "primary");
        assert_eq!(generated.attempts[0].outcome, "http_5xx");
        assert_eq!(generated.attempts[1].provider, "fallback");
        assert_eq!(generated.attempts[1].outcome, "success");
        server.abort();
    }

    #[tokio::test]
    async fn profile_validation_fallback_obeys_route_flag() {
        async fn invalid() -> Json<Value> {
            Json(completion("invalid"))
        }
        async fn valid() -> Json<Value> {
            Json(completion("valid"))
        }

        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let server = tokio::spawn(async move {
            axum::serve(
                listener,
                Router::new()
                    .route("/invalid/v1/chat/completions", post(invalid))
                    .route("/valid/v1/chat/completions", post(valid)),
            )
            .await
            .unwrap();
        });
        let validator = |content: &str| -> anyhow::Result<()> {
            if content == "valid" {
                Ok(())
            } else {
                Err(crate::types::validation_error(
                    crate::types::ValidationFailureReason::TooShort,
                ))
            }
        };
        let mut core = core();
        core.secrets = core
            .secrets
            .with_override("PROFILE_SWITCH_PRIMARY_KEY", "test-key")
            .with_override("PROFILE_SWITCH_FALLBACK_KEY", "test-key");
        let profiles = switching_profiles(address, "invalid", "valid", false);
        let error = generate_text_checked(&core, &profiles, profile_options(Some(&validator)))
            .await
            .unwrap_err();
        assert_eq!(
            error
                .downcast_ref::<GenerationAudit>()
                .unwrap()
                .attempts
                .len(),
            2
        );
        assert_eq!(
            error.downcast_ref::<ValidationFailure>().unwrap().reason,
            crate::types::ValidationFailureReason::TooShort
        );

        let profiles = switching_profiles(address, "invalid", "valid", true);
        let generated = generate_text_checked(&core, &profiles, profile_options(Some(&validator)))
            .await
            .unwrap();
        assert_eq!(generated.content, "valid");
        assert_eq!(generated.attempts.len(), 3);
        assert_eq!(generated.attempts[0].outcome, "validation_failed");
        assert_eq!(
            generated.attempts[0].validation_reason,
            Some(crate::types::ValidationFailureReason::TooShort)
        );
        assert_eq!(generated.attempts[1].outcome, "validation_failed");
        assert_eq!(
            generated.attempts[1].validation_reason,
            Some(crate::types::ValidationFailureReason::TooShort)
        );
        assert_eq!(generated.attempts[2].provider, "fallback");
        assert_eq!(generated.attempts[2].validation_reason, None);
        server.abort();
    }

    #[test]
    fn classifies_typed_empty_responses_for_retry() {
        let empty = anyhow::Error::new(LlmTransportError::empty_response());
        assert!(is_empty_response(&empty));
        assert_eq!(classify_attempt_error(&empty), "empty_response");
    }
}
