//! Provider configuration. Plain data; secrets stay in host storage
//! (OS keyring) and are only passed through here at call time.

use serde::{Deserialize, Serialize};

use super::types::AsrProvider;

pub const DEFAULT_GROQ_BASE: &str = "https://api.groq.com/openai/v1";
pub const DEFAULT_GROQ_MODEL: &str = "whisper-large-v3-turbo";
pub const DEFAULT_GEMINI_BASE: &str = "https://generativelanguage.googleapis.com";
pub const DEFAULT_GEMINI_MODEL: &str = "gemini-3.5-transcribe";

fn default_language() -> String {
    "auto".to_string()
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct AsrConfig {
    pub provider: AsrProvider,
    pub fallback: Option<AsrProvider>,
    #[serde(default = "default_language")]
    pub language: String,
    pub groq: GroqConfig,
    pub gemini: GeminiConfig,
    pub custom: CustomAsrConfig,
    pub local: LocalConfig,
    pub shadow: Option<AsrProvider>,
    pub cleanup: Option<CleanupConfig>,
    pub proxy_url: Option<String>,
    pub network_error: String,
}

impl Default for AsrConfig {
    fn default() -> Self {
        Self {
            provider: AsrProvider::Groq,
            fallback: Some(AsrProvider::Gemini),
            language: default_language(),
            groq: GroqConfig::default(),
            gemini: GeminiConfig::default(),
            custom: CustomAsrConfig::default(),
            local: LocalConfig::default(),
            shadow: None,
            cleanup: None,
            proxy_url: None,
            network_error: String::new(),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CleanupConfig {
    pub provider: AsrProvider,
    pub model: String,
}

pub fn discovery_for(
    cfg: &AsrConfig,
    provider: AsrProvider,
) -> Option<(
    aiapi_llm::discovery::DiscoveryOptions,
    aiapi_config::SecretStore,
)> {
    use aiapi_llm::discovery::{DiscoveryOptions, DiscoveryProtocol};
    let (base, key, protocol, metadata_provider) = match provider {
        AsrProvider::Groq => (
            &cfg.groq.api_base,
            &cfg.groq.api_key,
            DiscoveryProtocol::OpenaiCompatible,
            (cfg.groq.api_base.trim_end_matches('/') == DEFAULT_GROQ_BASE).then_some("groq"),
        ),
        AsrProvider::Gemini => (
            &cfg.gemini.api_base,
            &cfg.gemini.api_key,
            DiscoveryProtocol::Gemini,
            (cfg.gemini.api_base.trim_end_matches('/') == DEFAULT_GEMINI_BASE).then_some("google"),
        ),
        AsrProvider::Custom => (
            &cfg.custom.api_base,
            &cfg.custom.api_key,
            DiscoveryProtocol::OpenaiCompatible,
            None,
        ),
        AsrProvider::Local => return None,
    };
    Some((
        DiscoveryOptions {
            provider: provider.name().into(),
            protocol,
            base_url: base.clone(),
            secret_name: (!key.trim().is_empty()).then(|| "API_KEY".into()),
            cache_ttl_secs: 300,
            overrides: Default::default(),
            metadata_url: metadata_provider.map(|_| "https://models.dev/api.json".into()),
            metadata_provider: metadata_provider.map(str::to_string),
        },
        aiapi_config::SecretStore::explicit().with_override("API_KEY", key),
    ))
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct GroqConfig {
    pub api_base: String,
    pub api_key: String,
    pub model: String,
    pub temperature: f32,
    pub timeout_secs: u64,
}

impl Default for GroqConfig {
    fn default() -> Self {
        Self {
            api_base: DEFAULT_GROQ_BASE.to_string(),
            api_key: String::new(),
            model: DEFAULT_GROQ_MODEL.to_string(),
            temperature: 0.0,
            timeout_secs: 120,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct GeminiConfig {
    pub api_base: String,
    pub api_key: String,
    pub model: String,
    pub prompt: String,
    pub timeout_secs: u64,
    pub total_deadline_secs: u64,
}

impl Default for GeminiConfig {
    fn default() -> Self {
        Self {
            api_base: DEFAULT_GEMINI_BASE.to_string(),
            api_key: String::new(),
            model: DEFAULT_GEMINI_MODEL.to_string(),
            prompt: String::new(),
            timeout_secs: 180,
            total_deadline_secs: 420,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct CustomAsrConfig {
    pub api_base: String,
    pub api_key: String,
    pub model: String,
    pub temperature: f32,
    pub timeout_secs: u64,
}

impl Default for CustomAsrConfig {
    fn default() -> Self {
        Self {
            api_base: String::new(),
            api_key: String::new(),
            model: String::new(),
            temperature: 0.0,
            timeout_secs: 120,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default)]
pub struct LocalConfig {
    pub model_path: String,
    pub n_threads: u16,
    pub use_gpu: bool,
}

impl Default for LocalConfig {
    fn default() -> Self {
        let threads = std::thread::available_parallelism()
            .map(|n| (n.get() / 2).clamp(1, 8) as u16)
            .unwrap_or(4);
        Self {
            model_path: String::new(),
            n_threads: threads,
            use_gpu: true,
        }
    }
}
