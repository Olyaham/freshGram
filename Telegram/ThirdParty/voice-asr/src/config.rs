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
        }
    }
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
