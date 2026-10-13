use std::collections::{BTreeMap, BTreeSet};

use serde::Deserialize;

/// Host-agnostic LLM topology: providers, models, routes. Unknown
/// TOML sections are ignored so host files may carry their own knobs.
#[derive(Debug, Clone, Default, Deserialize)]
pub struct LlmProfiles {
    pub providers: BTreeMap<String, ProviderProfile>,
    pub models: BTreeMap<String, ModelProfile>,
    pub routes: BTreeMap<String, RouteProfile>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ProviderProfile {
    pub driver: LlmDriver,
    pub base_url: String,
    #[serde(default)]
    pub api_key_env: String,
    #[serde(default)]
    pub adapter: Option<GenAiAdapter>,
    #[serde(default)]
    pub egress: Egress,
}

#[derive(Debug, Clone, Copy, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum LlmDriver {
    Gemini,
    OllamaNative,
    OpenaiCompatible,
}

#[derive(Debug, Clone, Copy, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum GenAiAdapter {
    OpenAi,
    Gemini,
    Groq,
    OpenRouter,
    OllamaCloud,
}

#[derive(Debug, Clone, Copy, Default, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Egress {
    #[default]
    Direct,
    Proxy,
}

impl ProviderProfile {
    pub fn genai_adapter(&self) -> GenAiAdapter {
        self.adapter.unwrap_or(match self.driver {
            LlmDriver::Gemini => GenAiAdapter::Gemini,
            LlmDriver::OllamaNative => GenAiAdapter::OllamaCloud,
            LlmDriver::OpenaiCompatible => GenAiAdapter::OpenAi,
        })
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ModelProfile {
    pub provider: String,
    pub model: String,
    pub capabilities: ModelCapabilities,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ModelCapabilities {
    pub supports_images: bool,
    pub supports_tools: bool,
    pub supports_system_prompt: bool,
    pub structured_output: StructuredOutputMode,
    pub context_window_tokens: u32,
    pub max_output_tokens: u32,
    pub request_timeout_sec: u64,
    pub thinking: ThinkingMode,
}

#[derive(Debug, Clone, Copy, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum StructuredOutputMode {
    JsonSchema,
    JsonObject,
    PromptOnly,
}

#[derive(Debug, Clone, Copy, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ThinkingMode {
    None,
    Default,
    Budget,
    LevelLow,
    LevelHigh,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RouteProfile {
    pub models: Vec<String>,
    #[serde(default)]
    pub fallback_on_validation_failure: bool,
}

#[derive(Debug, Clone, Default)]
pub struct RouteRequirements {
    pub requires_images: bool,
    pub requires_tools: bool,
    pub requires_system_prompt: bool,
    pub structured_output: Option<StructuredOutputMode>,
    pub num_predict: Option<u32>,
}

/// Validated route target ready for transport construction.
///
/// Borrows `LlmProfiles`; resolution does not access secrets or runtime configuration.
#[derive(Debug, Clone, Copy)]
pub struct RouteSelection<'a> {
    pub model: &'a ModelProfile,
    pub provider_key: &'a str,
    pub provider: &'a ProviderProfile,
    pub capabilities: &'a ModelCapabilities,
}

/// Compatible model selections in configured fallback order.
#[derive(Debug, Clone)]
pub struct ResolvedRoute<'a> {
    pub selections: Vec<RouteSelection<'a>>,
    pub fallback_on_validation_failure: bool,
}

impl LlmProfiles {
    pub fn from_path(path: &str) -> anyhow::Result<Self> {
        let content = std::fs::read_to_string(path)
            .map_err(|err| anyhow::anyhow!("failed to read LLM profiles file {path:?}: {err}"))?;
        Self::from_toml(&content)
    }

    pub fn from_toml(content: &str) -> anyhow::Result<Self> {
        let profiles: Self = toml::from_str(content)
            .map_err(|err| anyhow::anyhow!("failed to parse LLM profiles TOML: {err}"))?;
        profiles.validate()?;
        Ok(profiles)
    }

    pub fn validate(&self) -> anyhow::Result<()> {
        if self.providers.is_empty() {
            anyhow::bail!("LLM profiles must define at least one provider");
        }
        if self.models.is_empty() {
            anyhow::bail!("LLM profiles must define at least one model");
        }

        for (name, provider) in &self.providers {
            validate_profile_name("provider", name)?;
            validate_http_url("provider", name, &provider.base_url)?;
        }
        for (name, model) in &self.models {
            validate_profile_name("model", name)?;
            if !self.providers.contains_key(&model.provider) {
                anyhow::bail!(
                    "model profile {name:?} references unknown provider {:?}",
                    model.provider
                );
            }
            require_non_empty("model", name, "model", &model.model)?;
            let provider = &self.providers[&model.provider];
            validate_capabilities(name, provider, &model.capabilities)?;
        }
        for (name, route) in &self.routes {
            validate_profile_name("route", name)?;
            if route.models.is_empty() {
                anyhow::bail!("route profile {name:?} must define at least one model");
            }
            if route.fallback_on_validation_failure && route.models.len() < 2 {
                anyhow::bail!(
                    "route profile {name:?} enables fallback_on_validation_failure but has no fallback model"
                );
            }

            let mut seen = BTreeSet::new();
            for model in &route.models {
                if !self.models.contains_key(model) {
                    anyhow::bail!("route profile {name:?} references unknown model {model:?}");
                }
                if !seen.insert(model) {
                    anyhow::bail!(
                        "route profile {name:?} references model {model:?} more than once"
                    );
                }
            }
            self.resolve_route(name, &RouteRequirements::default())?;
        }

        Ok(())
    }

    pub fn resolve_route<'a>(
        &'a self,
        route_name: &str,
        requirements: &RouteRequirements,
    ) -> anyhow::Result<ResolvedRoute<'a>> {
        let route = self
            .routes
            .get(route_name)
            .ok_or_else(|| anyhow::anyhow!("unknown LLM route {route_name:?}"))?;
        let mut selections = Vec::with_capacity(route.models.len());
        let mut unmet_requirement = None;

        for model_key in &route.models {
            let model = self.models.get(model_key).ok_or_else(|| {
                anyhow::anyhow!(
                    "route profile {route_name:?} references unknown model {model_key:?}"
                )
            })?;
            let provider_key = model.provider.as_str();
            let provider = self.providers.get(provider_key).ok_or_else(|| {
                anyhow::anyhow!(
                    "route profile {route_name:?} model {model_key:?} references unknown provider {provider_key:?}"
                )
            })?;
            if let Err(error) =
                ensure_capabilities(route_name, model_key, &model.capabilities, requirements)
            {
                unmet_requirement.get_or_insert(error);
                continue;
            }
            selections.push(RouteSelection {
                model,
                provider_key,
                provider,
                capabilities: &model.capabilities,
            });
        }

        if selections.is_empty() {
            let unmet_requirement =
                unmet_requirement.expect("routes must contain at least one model");
            anyhow::bail!(
                "route {route_name:?} has no compatible models for the requested requirements: {unmet_requirement}"
            );
        }

        Ok(ResolvedRoute {
            selections,
            fallback_on_validation_failure: route.fallback_on_validation_failure,
        })
    }
}

fn ensure_capabilities(
    route_name: &str,
    model_name: &str,
    capabilities: &ModelCapabilities,
    requirements: &RouteRequirements,
) -> anyhow::Result<()> {
    if requirements.requires_images && !capabilities.supports_images {
        anyhow::bail!(
            "route {route_name:?} requires images but model {model_name:?} does not support them"
        );
    }
    if requirements.requires_tools && !capabilities.supports_tools {
        anyhow::bail!(
            "route {route_name:?} requires tools but model {model_name:?} does not support them"
        );
    }
    if requirements.requires_system_prompt && !capabilities.supports_system_prompt {
        anyhow::bail!(
            "route {route_name:?} requires a system prompt but model {model_name:?} does not support it"
        );
    }
    if let Some(structured_output) = requirements.structured_output
        && capabilities.structured_output != structured_output
    {
        anyhow::bail!(
            "route {route_name:?} requires structured output mode {structured_output:?} but model {model_name:?} declares {:?}",
            capabilities.structured_output
        );
    }
    if let Some(num_predict) = requirements.num_predict
        && num_predict > capabilities.max_output_tokens
    {
        anyhow::bail!(
            "route {route_name:?} requires num_predict {num_predict} but model {model_name:?} allows at most {} output tokens",
            capabilities.max_output_tokens
        );
    }
    Ok(())
}

fn validate_profile_name(kind: &str, name: &str) -> anyhow::Result<()> {
    if name.trim().is_empty() {
        anyhow::bail!("{kind} profile name must not be empty");
    }
    Ok(())
}

fn require_non_empty(kind: &str, name: &str, field: &str, value: &str) -> anyhow::Result<()> {
    if value.trim().is_empty() {
        anyhow::bail!("{kind} profile {name:?} requires non-empty {field}");
    }
    Ok(())
}

fn validate_http_url(kind: &str, name: &str, value: &str) -> anyhow::Result<()> {
    let valid = reqwest::Url::parse(value)
        .is_ok_and(|url| matches!(url.scheme(), "http" | "https") && url.host_str().is_some());
    if !valid {
        anyhow::bail!("{kind} profile {name:?} base_url must be an absolute HTTP(S) URL");
    }
    Ok(())
}

fn validate_capabilities(
    name: &str,
    provider: &ProviderProfile,
    capabilities: &ModelCapabilities,
) -> anyhow::Result<()> {
    if capabilities.context_window_tokens == 0 {
        anyhow::bail!("model profile {name:?} context_window_tokens must be greater than 0");
    }
    if capabilities.max_output_tokens == 0 {
        anyhow::bail!("model profile {name:?} max_output_tokens must be greater than 0");
    }
    if capabilities.max_output_tokens > capabilities.context_window_tokens {
        anyhow::bail!(
            "model profile {name:?} max_output_tokens must not exceed context_window_tokens"
        );
    }
    if capabilities.request_timeout_sec == 0 {
        anyhow::bail!("model profile {name:?} request_timeout_sec must be greater than 0");
    }
    let adapter = provider.genai_adapter();
    // Compatible chat transports уже сериализуют explicit reasoning_effort.
    // Числовой budget и provider-specific default этим протоколам не добавляем.
    let supports_explicit_effort = provider.driver == LlmDriver::OpenaiCompatible
        && matches!(adapter, GenAiAdapter::OpenAi | GenAiAdapter::OpenRouter)
        && matches!(
            capabilities.thinking,
            ThinkingMode::LevelLow | ThinkingMode::LevelHigh
        );
    if capabilities.thinking != ThinkingMode::None
        && !matches!(adapter, GenAiAdapter::Gemini | GenAiAdapter::Groq)
        && !supports_explicit_effort
    {
        anyhow::bail!(
            "model profile {name:?} enables thinking for an unsupported provider adapter {adapter:?}"
        );
    }
    if capabilities.thinking == ThinkingMode::Default && adapter != GenAiAdapter::Groq {
        anyhow::bail!(
            "model profile {name:?} uses the Groq-only default thinking mode with adapter {adapter:?}"
        );
    }
    match capabilities.structured_output {
        StructuredOutputMode::JsonSchema
        | StructuredOutputMode::JsonObject
        | StructuredOutputMode::PromptOnly => {}
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const EXAMPLE_PROFILES: &str = include_str!("../../../profiles/llm_profiles.toml.example");

    const VALID_PROFILES: &str = r#"
[providers.ollama_cloud]
driver = "ollama_native"
base_url = "https://ollama.com"
api_key_env = "OLLAMA_API_KEY"

[models.ollama_memory]
provider = "ollama_cloud"
model = "gemma4:31b"

[models.ollama_memory.capabilities]
supports_images = false
supports_tools = true
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 32768
max_output_tokens = 4096
request_timeout_sec = 120
thinking = "none"

[routes.memory]
models = ["ollama_memory"]

[runtime]
"#;

    #[test]
    fn committed_example_parses_and_validates() {
        let profiles = LlmProfiles::from_toml(EXAMPLE_PROFILES).unwrap();

        assert!(profiles.routes.contains_key("text"));
        let resolved = profiles
            .resolve_route("text", &RouteRequirements::default())
            .unwrap();
        assert_eq!(resolved.selections.len(), 2);
        assert!(resolved.fallback_on_validation_failure);
    }

    #[test]
    fn unknown_top_level_sections_are_ignored() {
        let extended = VALID_PROFILES.to_string() + "\n[chats]\nfoo = 1\n";
        let profiles = LlmProfiles::from_toml(&extended).unwrap();

        assert!(profiles.routes.contains_key("memory"));
    }

    #[test]
    fn parses_and_validates_provider_model_and_route_profiles() {
        let profiles = LlmProfiles::from_toml(VALID_PROFILES).unwrap();

        assert_eq!(profiles.providers.len(), 1);
        assert_eq!(profiles.models.len(), 1);
        assert_eq!(profiles.routes["memory"].models, ["ollama_memory"]);
        assert!(profiles.models["ollama_memory"].capabilities.supports_tools);
    }

    const THREE_PROVIDER_TOPOLOGY: &str = r#"
[providers.groq]
driver = "openai_compatible"
base_url = "https://api.groq.com/openai/v1"
api_key_env = "GROQ_API_KEY"

[providers.openrouter]
driver = "openai_compatible"
base_url = "https://openrouter.ai/api/v1"
api_key_env = "OPENROUTER_API_KEY"

[providers.ollama_cloud]
driver = "ollama_native"
base_url = "https://ollama.com"
api_key_env = "OLLAMA_API_KEY"

[models.groq_qwen]
provider = "groq"
model = "qwen-test-27b"
[models.groq_qwen.capabilities]
supports_images = false
supports_tools = true
supports_system_prompt = true
structured_output = "json_schema"
context_window_tokens = 131072
max_output_tokens = 16384
request_timeout_sec = 180
thinking = "level_high"

[models.openrouter_free]
provider = "openrouter"
model = "openrouter/free"
[models.openrouter_free.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 32768
max_output_tokens = 4096
request_timeout_sec = 120
thinking = "none"

[models.ollama_mini]
provider = "ollama_cloud"
model = "mini-test"
[models.ollama_mini.capabilities]
supports_images = false
supports_tools = true
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 32768
max_output_tokens = 8192
request_timeout_sec = 60
thinking = "none"

[routes.cleanup]
models = ["groq_qwen", "openrouter_free", "ollama_mini"]
fallback_on_validation_failure = true
"#;
    #[test]
    fn route_resolution_returns_ordered_selections_across_providers() {
        let profiles = LlmProfiles::from_toml(THREE_PROVIDER_TOPOLOGY).unwrap();

        let selections = profiles
            .resolve_route("cleanup", &RouteRequirements::default())
            .unwrap();

        assert_eq!(selections.selections.len(), 3);
        assert_eq!(
            (
                selections.selections[0].provider_key,
                selections.selections[0].model.model.as_str()
            ),
            ("groq", "qwen-test-27b")
        );
        assert_eq!(
            selections.selections[0].provider.driver,
            LlmDriver::OpenaiCompatible
        );
        assert_eq!(
            selections.selections[0].capabilities.structured_output,
            StructuredOutputMode::JsonSchema
        );
        assert_eq!(
            (
                selections.selections[1].provider_key,
                selections.selections[1].model.model.as_str()
            ),
            ("openrouter", "openrouter/free")
        );
        assert_eq!(
            selections.selections[1].capabilities.structured_output,
            StructuredOutputMode::JsonObject
        );
        assert_eq!(
            (
                selections.selections[2].provider_key,
                selections.selections[2].model.model.as_str()
            ),
            ("ollama_cloud", "mini-test")
        );
        assert!(selections.fallback_on_validation_failure);
    }

    const ASK_TOPOLOGY: &str = r#"
[providers.acme]
driver = "openai_compatible"
base_url = "https://api.acme.test/v1"
api_key_env = "ACME_API_KEY"

[providers.baker]
driver = "openai_compatible"
base_url = "https://api.baker.test/v1"
api_key_env = "BAKER_API_KEY"

[models.ask_alpha]
provider = "acme"
model = "acme-alpha"
[models.ask_alpha.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "json_schema"
context_window_tokens = 131072
max_output_tokens = 16384
request_timeout_sec = 180
thinking = "level_high"

[models.ask_beta]
provider = "acme"
model = "acme-beta"
[models.ask_beta.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 32768
max_output_tokens = 4096
request_timeout_sec = 120
thinking = "none"

[models.ask_gamma]
provider = "baker"
model = "acme-gamma"
[models.ask_gamma.capabilities]
supports_images = true
supports_tools = true
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 32768
max_output_tokens = 8192
request_timeout_sec = 60
thinking = "none"

[models.ask_delta]
provider = "baker"
model = "acme-delta"
[models.ask_delta.capabilities]
supports_images = false
supports_tools = true
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 32768
max_output_tokens = 4096
request_timeout_sec = 60
thinking = "none"

[routes.ask]
models = ["ask_alpha", "ask_beta", "ask_gamma", "ask_delta"]
"#;
    #[test]
    fn vision_models_keep_order_and_model_specific_output_caps() {
        let profiles = LlmProfiles::from_toml(ASK_TOPOLOGY).unwrap();

        let selections = profiles
            .resolve_route("ask", &RouteRequirements::default())
            .unwrap();

        assert_eq!(selections.selections.len(), 4);
        assert_eq!(selections.selections[0].model.model, "acme-alpha");
        assert_eq!(selections.selections[1].model.model, "acme-beta");
        assert_eq!(selections.selections[2].model.model, "acme-gamma");
        assert_eq!(selections.selections[3].model.model, "acme-delta");
        assert!(selections.selections[1].capabilities.supports_images);
        assert!(selections.selections[1].capabilities.supports_tools);
        assert_eq!(
            selections.selections[0].capabilities.max_output_tokens,
            16_384
        );
        assert_eq!(
            selections.selections[1].capabilities.max_output_tokens,
            4_096
        );
        assert_eq!(
            selections.selections[2].capabilities.max_output_tokens,
            8_192
        );
        assert_eq!(
            selections.selections[0].capabilities.thinking,
            ThinkingMode::LevelHigh
        );
        assert_eq!(
            selections.selections[1].capabilities.thinking,
            ThinkingMode::None
        );
        assert_eq!(
            selections.selections[2].capabilities.thinking,
            ThinkingMode::None
        );
        assert_eq!(
            selections.selections[3].capabilities.thinking,
            ThinkingMode::None
        );
        assert_eq!(
            selections.selections[0].capabilities.request_timeout_sec,
            180
        );
        assert_eq!(
            selections.selections[1].capabilities.request_timeout_sec,
            120
        );
    }

    #[test]
    fn route_order_follows_topology_definition() {
        let profiles = LlmProfiles::from_toml(ASK_TOPOLOGY).unwrap();
        let route = &profiles.routes["ask"].models;
        assert_eq!(route, &["ask_alpha", "ask_beta", "ask_gamma", "ask_delta",]);
        assert_eq!(profiles.models["ask_beta"].model, "acme-beta");
        assert_eq!(
            profiles.models["ask_beta"].capabilities.max_output_tokens,
            4_096
        );
    }

    #[test]
    fn compatible_transports_accept_explicit_reasoning_levels() {
        let profiles = LlmProfiles::from_toml(VALID_PROFILES).unwrap();
        let mut capabilities = profiles.models["ollama_memory"].capabilities.clone();
        for adapter in [
            GenAiAdapter::OpenAi,
            GenAiAdapter::OpenRouter,
            GenAiAdapter::Groq,
        ] {
            let provider = ProviderProfile {
                driver: LlmDriver::OpenaiCompatible,
                base_url: "https://api.example.com/v1".into(),
                api_key_env: "TEST_API_KEY".into(),
                adapter: Some(adapter),
                egress: Egress::Direct,
            };
            for thinking in [ThinkingMode::LevelLow, ThinkingMode::LevelHigh] {
                capabilities.thinking = thinking;
                validate_capabilities("ask", &provider, &capabilities).unwrap();
            }
        }
    }

    #[test]
    fn compatible_transports_reject_unsupported_numeric_thinking_budget() {
        let profiles = LlmProfiles::from_toml(VALID_PROFILES).unwrap();
        let mut capabilities = profiles.models["ollama_memory"].capabilities.clone();
        capabilities.thinking = ThinkingMode::Budget;
        for adapter in [GenAiAdapter::OpenAi, GenAiAdapter::OpenRouter] {
            let provider = ProviderProfile {
                driver: LlmDriver::OpenaiCompatible,
                base_url: "https://api.example.com/v1".into(),
                api_key_env: "TEST_API_KEY".into(),
                adapter: Some(adapter),
                egress: Egress::Direct,
            };
            assert!(validate_capabilities("ask", &provider, &capabilities).is_err());
        }
    }

    #[test]
    fn route_resolution_skips_incompatible_models_and_preserves_text_fallbacks() {
        let profiles = LlmProfiles::from_toml(VISION_TOPOLOGY).unwrap();

        let image_route = profiles
            .resolve_route(
                "caption",
                &RouteRequirements {
                    requires_images: true,
                    ..RouteRequirements::default()
                },
            )
            .unwrap();
        let image_models: Vec<_> = image_route
            .selections
            .iter()
            .map(|selection| (selection.provider_key, selection.model.model.as_str()))
            .collect();

        assert_eq!(
            image_models,
            [("acme", "acme-flash"), ("acme", "acme-pro"),]
        );
        assert!(image_route.fallback_on_validation_failure);

        let text_route = profiles
            .resolve_route("caption", &RouteRequirements::default())
            .unwrap();
        let text_models: Vec<_> = text_route
            .selections
            .iter()
            .map(|selection| (selection.provider_key, selection.model.model.as_str()))
            .collect();

        assert_eq!(
            text_models,
            [
                ("acme", "acme-flash"),
                ("acme", "acme-pro"),
                ("baker", "baker-text"),
            ]
        );
    }

    const VISION_TOPOLOGY: &str = r#"
[providers.acme]
driver = "gemini"
base_url = "https://generativelanguage.googleapis.com/v1beta"
api_key_env = "GEMINI_API_KEY"

[providers.baker]
driver = "openai_compatible"
base_url = "https://api.baker.test/v1"
api_key_env = "BAKER_API_KEY"

[models.acme_flash]
provider = "acme"
model = "acme-flash"
[models.acme_flash.capabilities]
supports_images = true
supports_tools = false
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 1000000
max_output_tokens = 8192
request_timeout_sec = 60
thinking = "level_low"

[models.acme_pro]
provider = "acme"
model = "acme-pro"
[models.acme_pro.capabilities]
supports_images = true
supports_tools = false
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 1000000
max_output_tokens = 8192
request_timeout_sec = 60
thinking = "level_low"

[models.baker_text]
provider = "baker"
model = "baker-text"
[models.baker_text.capabilities]
supports_images = false
supports_tools = false
supports_system_prompt = true
structured_output = "json_object"
context_window_tokens = 32768
max_output_tokens = 4096
request_timeout_sec = 60
thinking = "none"

[routes.caption]
models = ["acme_flash", "acme_pro", "baker_text"]
fallback_on_validation_failure = true
"#;

    #[test]
    fn route_resolution_rejects_unsupported_structured_output_mode() {
        let profiles = LlmProfiles::from_toml(VALID_PROFILES).unwrap();
        let requirements = RouteRequirements {
            requires_system_prompt: true,
            structured_output: Some(StructuredOutputMode::JsonSchema),
            ..RouteRequirements::default()
        };

        let error = profiles
            .resolve_route("memory", &requirements)
            .unwrap_err()
            .to_string();

        assert!(error.contains("route \"memory\""));
        assert!(error.contains("model \"ollama_memory\""));
        assert!(error.contains("structured output mode JsonSchema"));
    }

    #[test]
    fn route_resolution_rejects_unsupported_system_prompt() {
        let profiles = LlmProfiles::from_toml(&VALID_PROFILES.replace(
            "supports_system_prompt = true",
            "supports_system_prompt = false",
        ))
        .unwrap();
        let requirements = RouteRequirements {
            requires_system_prompt: true,
            ..RouteRequirements::default()
        };

        let error = profiles
            .resolve_route("memory", &requirements)
            .unwrap_err()
            .to_string();

        assert!(error.contains("route \"memory\""));
        assert!(error.contains("model \"ollama_memory\""));
        assert!(error.contains("requires a system prompt"));
    }

    #[test]
    fn route_resolution_rejects_num_predict_above_model_output_limit() {
        let profiles = LlmProfiles::from_toml(VALID_PROFILES).unwrap();
        let requirements = RouteRequirements {
            num_predict: Some(4097),
            ..RouteRequirements::default()
        };

        let error = profiles
            .resolve_route("memory", &requirements)
            .unwrap_err()
            .to_string();

        assert!(error.contains("route \"memory\""));
        assert!(error.contains("model \"ollama_memory\""));
        assert!(error.contains("num_predict 4097"));
        assert!(error.contains("at most 4096 output tokens"));
    }

    #[test]
    fn loads_profiles_from_a_file() {
        let file = tempfile::NamedTempFile::new().unwrap();
        std::fs::write(file.path(), VALID_PROFILES).unwrap();

        let profiles = LlmProfiles::from_path(file.path().to_str().unwrap()).unwrap();

        assert!(profiles.routes.contains_key("memory"));
    }

    #[test]
    fn rejects_route_referencing_unknown_model() {
        let invalid = VALID_PROFILES.replace("ollama_memory\"]", "missing_model\"]");

        let error = LlmProfiles::from_toml(&invalid).unwrap_err().to_string();

        assert!(error.contains("unknown model"));
    }

    #[test]
    fn rejects_unknown_optional_route_field() {
        let invalid = VALID_PROFILES.replace(
            "models = [\"ollama_memory\"]",
            "models = [\"ollama_memory\"]\nfallback_on_validation_faliure = true",
        );

        let error = LlmProfiles::from_toml(&invalid).unwrap_err().to_string();

        assert!(error.contains("unknown field `fallback_on_validation_faliure`"));
    }

    #[test]
    fn rejects_invalid_provider_endpoint() {
        let invalid = VALID_PROFILES.replace("https://ollama.com", "not a URL");

        let error = LlmProfiles::from_toml(&invalid).unwrap_err().to_string();

        assert!(error.contains("base_url must be an absolute HTTP(S) URL"));
    }

    #[test]
    fn rejects_output_limit_larger_than_context_window() {
        let invalid =
            VALID_PROFILES.replace("max_output_tokens = 4096", "max_output_tokens = 65536");

        let error = LlmProfiles::from_toml(&invalid).unwrap_err().to_string();

        assert!(error.contains("max_output_tokens must not exceed context_window_tokens"));
    }

    #[test]
    fn route_resolution_rejects_missing_required_capability() {
        let profiles = LlmProfiles::from_toml(VALID_PROFILES).unwrap();
        let requirements = RouteRequirements {
            requires_images: true,
            ..RouteRequirements::default()
        };

        let error = profiles
            .resolve_route("memory", &requirements)
            .unwrap_err()
            .to_string();

        assert!(error.contains("requires images"));
    }
}
