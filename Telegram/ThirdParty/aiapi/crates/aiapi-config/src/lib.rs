//! Core configuration for secrets and transports.
//! Voice/text policies and product configuration belong to hosts.

use std::collections::HashMap;
use std::fmt;

/// Host secret overrides with optional environment lookup.
/// Blank overrides suppress environment fallback for that name.
#[derive(Clone)]
pub struct SecretStore {
    overrides: HashMap<String, String>,
    environment: bool,
}

impl Default for SecretStore {
    fn default() -> Self {
        Self {
            overrides: HashMap::new(),
            environment: true,
        }
    }
}

impl fmt::Debug for SecretStore {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("SecretStore")
            .field("names", &self.overrides.keys().collect::<Vec<_>>())
            .finish_non_exhaustive()
    }
}

impl SecretStore {
    pub fn explicit() -> Self {
        Self {
            overrides: HashMap::new(),
            environment: false,
        }
    }
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_override(mut self, env_name: &str, value: &str) -> Self {
        self.overrides
            .insert(env_name.to_string(), value.to_string());
        self
    }

    pub fn get(&self, env_name: &str) -> Option<String> {
        if let Some(value) = self.overrides.get(env_name) {
            let value = value.trim();
            return (!value.is_empty()).then(|| value.to_string());
        }
        if !self.environment {
            return None;
        }
        std::env::var(env_name)
            .ok()
            .map(|value| value.trim().to_string())
            .filter(|value| !value.is_empty())
    }

    pub fn require(&self, env_name: &str) -> Result<String, MissingSecret> {
        self.get(env_name)
            .ok_or_else(|| MissingSecret(env_name.to_string()))
    }
}

/// Missing secret identified by its lookup name, never by a value.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MissingSecret(pub String);

impl fmt::Display for MissingSecret {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "secret {} is not configured", self.0)
    }
}

impl std::error::Error for MissingSecret {}

/// Secret and transport configuration; LLM topology is supplied separately.
#[derive(Debug, Clone)]
pub struct CoreConfig {
    pub secrets: SecretStore,
    pub llm_proxy_url: Option<String>,
    pub gemini_thinking_budget: u32,
}

impl Default for CoreConfig {
    fn default() -> Self {
        Self {
            secrets: SecretStore::new(),
            llm_proxy_url: None,
            gemini_thinking_budget: 1024,
        }
    }
}

impl CoreConfig {
    pub fn new(secrets: SecretStore) -> Self {
        Self {
            secrets,
            ..Self::default()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn override_beats_environment() {
        let store = SecretStore::new().with_override("AIAPI_TEST_KEY", "  vault-value  ");
        assert_eq!(store.get("AIAPI_TEST_KEY").as_deref(), Some("vault-value"));
    }

    #[test]
    fn blank_values_count_as_missing() {
        let store = SecretStore::new().with_override("AIAPI_TEST_KEY", "   ");
        assert_eq!(store.get("AIAPI_TEST_KEY"), None);
        assert!(store.require("AIAPI_DEFINITELY_MISSING_KEY_12345").is_err());
    }
}
