use aiapi_config::SecretStore;
use aiapi_http as http;
use aiapi_types::{CancelToken, Cancelled};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::hash::{Hash, Hasher};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum DiscoveryProtocol {
    OpenaiCompatible,
    Gemini,
    Ollama,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
pub struct ModelMetadata {
    pub context_tokens: Option<u64>,
    pub output_tokens: Option<u64>,
    pub supports_images: Option<bool>,
    pub supports_tools: Option<bool>,
    pub supports_system_prompt: Option<bool>,
    pub supports_text_generation: Option<bool>,
    pub supports_transcription: Option<bool>,
    pub input: Option<Vec<String>>,
    pub output: Option<Vec<String>>,
    #[serde(default)]
    pub methods: Vec<String>,
}

impl ModelMetadata {
    fn overlay(&mut self, other: &Self) {
        if other.context_tokens.is_some() {
            self.context_tokens = other.context_tokens;
        }
        if other.output_tokens.is_some() {
            self.output_tokens = other.output_tokens;
        }
        if other.supports_images.is_some() {
            self.supports_images = other.supports_images;
        }
        if other.supports_tools.is_some() {
            self.supports_tools = other.supports_tools;
        }
        if other.supports_system_prompt.is_some() {
            self.supports_system_prompt = other.supports_system_prompt;
        }
        if other.supports_text_generation.is_some() {
            self.supports_text_generation = other.supports_text_generation;
        }
        if other.supports_transcription.is_some() {
            self.supports_transcription = other.supports_transcription;
        }
        if other.input.is_some() {
            self.input.clone_from(&other.input);
        }
        if other.output.is_some() {
            self.output.clone_from(&other.output);
        }
        if !other.methods.is_empty() {
            self.methods.clone_from(&other.methods);
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct DiscoveredModel {
    pub id: String,
    pub name: String,
    pub metadata: ModelMetadata,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct ModelCatalog {
    pub provider: String,
    pub models: Vec<DiscoveredModel>,
    pub cached: bool,
    pub stale: bool,
    pub refresh_error: Option<String>,
    pub metadata_error: Option<String>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct DiscoveryOptions {
    pub provider: String,
    pub protocol: DiscoveryProtocol,
    pub base_url: String,
    pub secret_name: Option<String>,
    #[serde(default = "default_ttl")]
    pub cache_ttl_secs: u64,
    #[serde(default)]
    pub overrides: BTreeMap<String, ModelMetadata>,
    #[serde(default)]
    pub metadata_url: Option<String>,
    #[serde(default)]
    pub metadata_provider: Option<String>,
}

fn default_ttl() -> u64 {
    300
}

#[derive(Hash, PartialEq, Eq)]
struct CacheKey {
    provider: String,
    protocol: DiscoveryProtocol,
    endpoint: String,
    credentials: u64,
}

struct Entry {
    models: Vec<DiscoveredModel>,
    fetched: Instant,
}

#[derive(Default)]
pub struct ModelDiscovery {
    cache: Mutex<HashMap<CacheKey, Entry>>,
    metadata: Mutex<HashMap<String, (Arc<Value>, Instant)>>,
}

impl ModelDiscovery {
    pub async fn discover(
        &self,
        client: &reqwest::Client,
        secrets: &SecretStore,
        options: &DiscoveryOptions,
        force: bool,
        cancel: &CancelToken,
    ) -> anyhow::Result<ModelCatalog> {
        cancel.check()?;
        let secret = options
            .secret_name
            .as_deref()
            .map(|name| secrets.require(name))
            .transpose()?;
        let mut hash = std::collections::hash_map::DefaultHasher::new();
        secret.hash(&mut hash);
        let key = CacheKey {
            provider: options.provider.clone(),
            protocol: options.protocol,
            endpoint: options.base_url.trim().trim_end_matches('/').into(),
            credentials: hash.finish(),
        };
        let cached = self
            .cache
            .lock()
            .map_err(|_| anyhow::anyhow!("model cache lock poisoned"))?
            .get(&key)
            .map(|entry| (entry.models.clone(), entry.fetched.elapsed()));
        if !force
            && let Some((models, age)) = &cached
            && *age < Duration::from_secs(options.cache_ttl_secs)
        {
            let mut result = catalog(options, models.clone(), true, false, None);
            self.enrich(client, options, &mut result, cancel).await?;
            return Ok(result);
        }
        let fetched = tokio::select! {
            biased;
            _ = cancel.cancelled() => return Err(Cancelled.into()),
            result = tokio::time::timeout(Duration::from_secs(30), fetch_models(client, options, secret.as_deref())) => {
                result.map_err(|_| anyhow::Error::new(http::Error::Timeout)).and_then(|result| result)
            }
        };
        match fetched {
            Ok(models) => {
                {
                    let mut cache = self
                        .cache
                        .lock()
                        .map_err(|_| anyhow::anyhow!("model cache lock poisoned"))?;
                    if cache.len() >= 64 && !cache.contains_key(&key) {
                        cache
                            .retain(|_, entry| entry.fetched.elapsed() < Duration::from_secs(3600));
                        if cache.len() >= 64 {
                            cache.clear();
                        }
                    }
                    cache.insert(
                        key,
                        Entry {
                            models: models.clone(),
                            fetched: Instant::now(),
                        },
                    );
                }
                let mut result = catalog(options, models, false, false, None);
                self.enrich(client, options, &mut result, cancel).await?;
                Ok(result)
            }
            Err(error) => match cached {
                Some((models, _)) => {
                    let mut result = catalog(options, models, true, true, Some(error.to_string()));
                    self.enrich(client, options, &mut result, cancel).await?;
                    Ok(result)
                }
                None => Err(error),
            },
        }
    }

    pub fn known_model(
        &self,
        secrets: &SecretStore,
        options: &DiscoveryOptions,
        id: &str,
    ) -> Option<ModelMetadata> {
        let secret = options
            .secret_name
            .as_deref()
            .map(|name| secrets.require(name))
            .transpose()
            .ok()?;
        let mut hash = std::collections::hash_map::DefaultHasher::new();
        secret.hash(&mut hash);
        let key = CacheKey {
            provider: options.provider.clone(),
            protocol: options.protocol,
            endpoint: options.base_url.trim().trim_end_matches('/').into(),
            credentials: hash.finish(),
        };
        let mut metadata = self
            .cache
            .lock()
            .ok()?
            .get(&key)
            .and_then(|entry| entry.models.iter().find(|model| model.id == id))
            .map(|model| model.metadata.clone())
            .or_else(|| options.overrides.get(id).cloned())?;
        if let (Some(url), Some(provider)) = (&options.metadata_url, &options.metadata_provider)
            && let Some((document, _)) = self.metadata.lock().ok()?.get(url)
        {
            let mut catalog = ModelCatalog {
                provider: options.provider.clone(),
                models: vec![DiscoveredModel {
                    id: id.into(),
                    name: id.into(),
                    metadata,
                }],
                cached: true,
                stale: false,
                refresh_error: None,
                metadata_error: None,
            };
            enrich_from_models_dev(&mut catalog, document, provider);
            metadata = catalog.models.remove(0).metadata;
        }
        if let Some(overrides) = options.overrides.get(id) {
            metadata.overlay(overrides);
        }
        Some(metadata)
    }

    async fn enrich(
        &self,
        client: &reqwest::Client,
        options: &DiscoveryOptions,
        catalog: &mut ModelCatalog,
        cancel: &CancelToken,
    ) -> anyhow::Result<()> {
        let (Some(url), Some(provider)) = (&options.metadata_url, &options.metadata_provider)
        else {
            return Ok(());
        };
        let cached = self
            .metadata
            .lock()
            .map_err(|_| anyhow::anyhow!("metadata cache lock poisoned"))?
            .get(url)
            .map(|(document, fetched)| (document.clone(), fetched.elapsed()));
        let document = if let Some((document, age)) = &cached
            && *age < Duration::from_secs(3600)
        {
            Some(document.clone())
        } else {
            let fetched = tokio::select! {
                biased;
                _ = cancel.cancelled() => return Err(Cancelled.into()),
                result = async {
                    validate_endpoint(url)?;
                    let response = client.get(url).timeout(Duration::from_secs(10)).send().await.map_err(http::Error::from)?;
                    http::parse_json_with_limit::<Value>(response, 32 * 1024 * 1024).await
                } => result,
            };
            match fetched {
                Ok(document) => {
                    let document = Arc::new(document);
                    let mut cache = self
                        .metadata
                        .lock()
                        .map_err(|_| anyhow::anyhow!("metadata cache lock poisoned"))?;
                    if cache.len() >= 4 && !cache.contains_key(url) {
                        cache.clear();
                    }
                    cache.insert(url.clone(), (document.clone(), Instant::now()));
                    Some(document)
                }
                Err(error) => {
                    catalog.metadata_error = Some(error.to_string());
                    cached.map(|(document, _)| document)
                }
            }
        };
        if let Some(document) = document {
            enrich_from_models_dev(catalog, &document, provider);
        }
        Ok(())
    }
}

fn catalog(
    options: &DiscoveryOptions,
    models: Vec<DiscoveredModel>,
    cached: bool,
    stale: bool,
    refresh_error: Option<String>,
) -> ModelCatalog {
    let mut models: BTreeMap<String, DiscoveredModel> = models
        .into_iter()
        .map(|model| (model.id.clone(), model))
        .collect();
    for (id, metadata) in &options.overrides {
        models
            .entry(id.clone())
            .or_insert_with(|| DiscoveredModel {
                id: id.clone(),
                name: id.clone(),
                metadata: ModelMetadata::default(),
            })
            .metadata
            .overlay(metadata);
    }
    ModelCatalog {
        provider: options.provider.clone(),
        models: models.into_values().collect(),
        cached,
        stale,
        refresh_error,
        metadata_error: None,
    }
}

async fn get(
    client: &reqwest::Client,
    url: String,
    secret: Option<&str>,
    gemini: bool,
) -> anyhow::Result<Value> {
    let request = client.get(url).timeout(Duration::from_secs(15));
    let request = match secret.filter(|key| !key.is_empty()) {
        Some(key) if gemini => request.header("x-goog-api-key", key),
        Some(key) => request.bearer_auth(key),
        None => request,
    };
    http::parse_json(request.send().await.map_err(http::Error::from)?).await
}

fn strings(value: Option<&Value>) -> Option<Vec<String>> {
    value?.as_array().map(|array| {
        array
            .iter()
            .filter_map(Value::as_str)
            .map(str::to_string)
            .collect()
    })
}

fn validate_endpoint(base: &str) -> anyhow::Result<()> {
    let url =
        reqwest::Url::parse(base).map_err(|_| anyhow::anyhow!("invalid discovery endpoint"))?;
    if !matches!(url.scheme(), "http" | "https")
        || url.host().is_none()
        || url.query().is_some()
        || url.fragment().is_some()
        || !url.username().is_empty()
        || url.password().is_some()
    {
        anyhow::bail!("invalid discovery endpoint");
    }
    Ok(())
}

async fn fetch_models(
    client: &reqwest::Client,
    options: &DiscoveryOptions,
    secret: Option<&str>,
) -> anyhow::Result<Vec<DiscoveredModel>> {
    let base = options.base_url.trim().trim_end_matches('/');
    validate_endpoint(base)?;
    let mut result = Vec::new();
    match options.protocol {
        DiscoveryProtocol::OpenaiCompatible => {
            let json = get(client, format!("{base}/models"), secret, false).await?;
            let data = json
                .get("data")
                .and_then(Value::as_array)
                .ok_or_else(|| http::Error::Parse("model list has no data array".into()))?;
            for item in data {
                if let Some(id) = item
                    .get("id")
                    .and_then(Value::as_str)
                    .filter(|id| !id.is_empty())
                {
                    result.push(DiscoveredModel {
                        id: id.into(),
                        name: item
                            .get("name")
                            .and_then(Value::as_str)
                            .unwrap_or(id)
                            .into(),
                        metadata: ModelMetadata {
                            context_tokens: item
                                .get("context_window")
                                .or_else(|| item.get("max_model_len"))
                                .and_then(Value::as_u64),
                            output_tokens: item.get("max_output_tokens").and_then(Value::as_u64),
                            supports_system_prompt: item
                                .get("supports_system_prompt")
                                .and_then(Value::as_bool),
                            supports_tools: item.get("supports_tools").and_then(Value::as_bool),
                            supports_text_generation: item
                                .get("supports_text_generation")
                                .and_then(Value::as_bool),
                            supports_transcription: item
                                .get("supports_transcription")
                                .and_then(Value::as_bool),
                            input: strings(item.pointer("/modalities/input")),
                            output: strings(item.pointer("/modalities/output")),
                            ..Default::default()
                        },
                    });
                }
            }
        }
        DiscoveryProtocol::Gemini => {
            let base = if base.ends_with("/v1beta") || base.ends_with("/v1") {
                base.to_string()
            } else {
                format!("{base}/v1beta")
            };
            let mut tokens = BTreeSet::new();
            let mut token: Option<String> = None;
            loop {
                let mut url = reqwest::Url::parse(&format!("{base}/models"))?;
                url.query_pairs_mut().append_pair("pageSize", "100");
                if let Some(token) = &token {
                    url.query_pairs_mut().append_pair("pageToken", token);
                }
                let json = get(client, url.into(), secret, true).await?;
                let data = json
                    .get("models")
                    .and_then(Value::as_array)
                    .ok_or_else(|| http::Error::Parse("model list has no models array".into()))?;
                for item in data {
                    if let Some(id) = item.get("name").and_then(Value::as_str) {
                        let id = id.strip_prefix("models/").unwrap_or(id);
                        let methods = strings(item.get("supportedGenerationMethods"));
                        result.push(DiscoveredModel {
                            id: id.into(),
                            name: item
                                .get("displayName")
                                .and_then(Value::as_str)
                                .unwrap_or(id)
                                .into(),
                            metadata: ModelMetadata {
                                context_tokens: item.get("inputTokenLimit").and_then(Value::as_u64),
                                output_tokens: item.get("outputTokenLimit").and_then(Value::as_u64),
                                supports_text_generation: methods.as_ref().map(|methods| {
                                    methods.iter().any(|method| method == "generateContent")
                                }),
                                methods: methods.unwrap_or_default(),
                                ..Default::default()
                            },
                        });
                    }
                }
                match json
                    .get("nextPageToken")
                    .and_then(Value::as_str)
                    .filter(|token| !token.is_empty())
                {
                    Some(next) if tokens.len() < 100 && tokens.insert(next.to_string()) => {
                        token = Some(next.to_string())
                    }
                    Some(_) => {
                        return Err(
                            http::Error::Parse("model pagination loop or limit".into()).into()
                        );
                    }
                    None => break,
                }
            }
        }
        DiscoveryProtocol::Ollama => {
            let base = base.strip_suffix("/v1").unwrap_or(base);
            let json = get(client, format!("{base}/api/tags"), secret, false).await?;
            let data = json
                .get("models")
                .and_then(Value::as_array)
                .ok_or_else(|| http::Error::Parse("model list has no models array".into()))?;
            for item in data {
                if let Some(id) = item.get("name").and_then(Value::as_str) {
                    result.push(DiscoveredModel {
                        id: id.into(),
                        name: id.into(),
                        metadata: ModelMetadata::default(),
                    });
                }
            }
            let mut pending = result
                .iter()
                .enumerate()
                .map(|(index, model)| (index, model.id.clone()))
                .collect::<Vec<_>>()
                .into_iter();
            let mut active = tokio::task::JoinSet::new();
            let start = |index, id| {
                let client = client.clone();
                let url = format!("{base}/api/show");
                let secret = secret.map(str::to_string);
                async move { (index, ollama_metadata(client, url, secret, id).await) }
            };
            for (index, id) in pending.by_ref().take(4) {
                active.spawn(start(index, id));
            }
            let deadline = tokio::time::sleep(Duration::from_secs(10));
            tokio::pin!(deadline);
            while !active.is_empty() {
                tokio::select! {
                    _ = &mut deadline => break,
                    response = active.join_next() => {
                        if let Some(Ok((index, metadata))) = response { result[index].metadata = metadata; }
                        if let Some((index, id)) = pending.next() { active.spawn(start(index, id)); }
                    }
                }
            }
        }
    }
    result.sort_by(|a, b| a.id.cmp(&b.id));
    result.dedup_by(|a, b| a.id == b.id);
    Ok(result)
}

async fn ollama_metadata(
    client: reqwest::Client,
    url: String,
    secret: Option<String>,
    id: String,
) -> ModelMetadata {
    let request = client
        .post(url)
        .timeout(Duration::from_secs(5))
        .json(&serde_json::json!({"model":id}));
    let request = if let Some(key) = secret {
        request.bearer_auth(key)
    } else {
        request
    };
    let details: Value = match request.send().await {
        Ok(response) => http::parse_json(response).await.unwrap_or(Value::Null),
        Err(_) => Value::Null,
    };
    let capabilities = strings(details.get("capabilities"));
    ModelMetadata {
        context_tokens: details
            .get("model_info")
            .and_then(Value::as_object)
            .and_then(|info| {
                info.iter()
                    .find(|(key, _)| key.ends_with(".context_length"))
            })
            .and_then(|(_, value)| value.as_u64()),
        supports_images: capabilities
            .as_ref()
            .map(|caps| caps.iter().any(|cap| cap == "vision")),
        supports_tools: capabilities
            .as_ref()
            .map(|caps| caps.iter().any(|cap| cap == "tools")),
        supports_text_generation: capabilities
            .as_ref()
            .map(|caps| caps.iter().any(|cap| cap == "completion")),
        methods: capabilities.unwrap_or_default(),
        ..Default::default()
    }
}

pub fn enrich_from_models_dev(catalog: &mut ModelCatalog, document: &Value, provider: &str) {
    let Some(models) = document.get(provider).and_then(|value| value.get("models")) else {
        return;
    };
    for model in &mut catalog.models {
        if let Some(known) = models.get(&model.id) {
            let mut metadata = ModelMetadata {
                context_tokens: known.pointer("/limit/context").and_then(Value::as_u64),
                output_tokens: known.pointer("/limit/output").and_then(Value::as_u64),
                supports_tools: known.get("tool_call").and_then(Value::as_bool),
                input: strings(known.pointer("/modalities/input")),
                output: strings(known.pointer("/modalities/output")),
                ..Default::default()
            };
            metadata.supports_images = metadata
                .input
                .as_ref()
                .map(|input| input.contains(&"image".into()));
            metadata.overlay(&model.metadata);
            model.metadata = metadata;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::{Json, Router, extract::Query, http::StatusCode, routing::get};
    use serde_json::json;
    use std::sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    };

    async fn serve(app: Router) -> (String, tokio::task::JoinHandle<()>) {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let base = format!("http://{}", listener.local_addr().unwrap());
        let server = tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });
        (base, server)
    }

    fn options(base_url: String, protocol: DiscoveryProtocol) -> DiscoveryOptions {
        DiscoveryOptions {
            provider: "test".into(),
            protocol,
            base_url,
            secret_name: None,
            cache_ttl_secs: 300,
            overrides: BTreeMap::new(),
            metadata_url: None,
            metadata_provider: None,
        }
    }

    #[tokio::test]
    async fn gemini_paginates_and_does_not_guess_capabilities() {
        let app = Router::new().route("/v1beta/models", get(|Query(query): Query<HashMap<String, String>>| async move {
            match query.get("pageToken").map(String::as_str) {
                None => Json(json!({"models":[{"name":"models/model-a", "inputTokenLimit":32768}], "nextPageToken":"second + page"})),
                Some("second + page") => Json(json!({"models":[{"name":"models/model-b", "supportedGenerationMethods":["generateContent"]}]})),
                _ => panic!("pagination token must be URL encoded"),
            }
        }));
        let (base, server) = serve(app).await;
        let catalog = ModelDiscovery::default()
            .discover(
                &reqwest::Client::new(),
                &SecretStore::explicit(),
                &options(base, DiscoveryProtocol::Gemini),
                false,
                &CancelToken::new(),
            )
            .await
            .unwrap();
        server.abort();
        assert_eq!(catalog.models.len(), 2);
        assert_eq!(catalog.models[0].id, "model-a");
        assert_eq!(catalog.models[0].metadata.context_tokens, Some(32768));
        assert_eq!(catalog.models[0].metadata.supports_images, None);
        assert_eq!(catalog.models[1].metadata.methods, ["generateContent"]);
    }

    #[tokio::test]
    async fn stale_inventory_is_credential_scoped_and_overrides_win() {
        let failure = Arc::new(AtomicBool::new(false));
        let flag = failure.clone();
        let app = Router::new().route(
            "/models",
            get(move || {
                let flag = flag.clone();
                async move {
                    if flag.load(Ordering::SeqCst) {
                        (
                            StatusCode::UNAUTHORIZED,
                            Json(json!({"secret":"must not reach caller"})),
                        )
                    } else {
                        (
                            StatusCode::OK,
                            Json(json!({"data":[{"id":"model-a", "context_window":1000}]})),
                        )
                    }
                }
            }),
        );
        let (base, server) = serve(app).await;
        let cache = ModelDiscovery::default();
        let client = reqwest::Client::new();
        let secrets = SecretStore::explicit().with_override("KEY", "first-key");
        let mut options = options(base, DiscoveryProtocol::OpenaiCompatible);
        options.secret_name = Some("KEY".into());
        options.overrides.insert(
            "model-a".into(),
            ModelMetadata {
                context_tokens: Some(2000),
                ..Default::default()
            },
        );
        let first = cache
            .discover(&client, &secrets, &options, false, &CancelToken::new())
            .await
            .unwrap();
        assert!(!first.cached);
        assert_eq!(first.models[0].metadata.context_tokens, Some(2000));
        failure.store(true, Ordering::SeqCst);
        assert!(
            cache
                .discover(&client, &secrets, &options, false, &CancelToken::new())
                .await
                .unwrap()
                .cached
        );
        let stale = cache
            .discover(&client, &secrets, &options, true, &CancelToken::new())
            .await
            .unwrap();
        assert!(stale.stale);
        assert!(
            !stale
                .refresh_error
                .unwrap()
                .contains("must not reach caller")
        );
        let different = SecretStore::explicit().with_override("KEY", "second-key");
        assert!(
            cache
                .discover(&client, &different, &options, false, &CancelToken::new())
                .await
                .is_err()
        );
        server.abort();
    }

    #[tokio::test]
    async fn cancellation_never_returns_stale_success() {
        let app = Router::new().route("/models", get(|| async { Json(json!({"data":[]})) }));
        let (base, server) = serve(app).await;
        let cache = ModelDiscovery::default();
        let client = reqwest::Client::new();
        let secrets = SecretStore::explicit();
        let options = options(base, DiscoveryProtocol::OpenaiCompatible);
        cache
            .discover(&client, &secrets, &options, false, &CancelToken::new())
            .await
            .unwrap();
        let token = CancelToken::new();
        token.cancel();
        assert!(
            cache
                .discover(&client, &secrets, &options, false, &token)
                .await
                .unwrap_err()
                .is::<Cancelled>()
        );
        server.abort();
    }

    #[tokio::test]
    async fn metadata_is_cached_without_credentials_and_manual_overrides_win() {
        use axum::http::HeaderMap;
        use std::sync::atomic::AtomicUsize;
        let requests = Arc::new(AtomicUsize::new(0));
        let count = requests.clone();
        let app = Router::new()
            .route(
                "/models",
                get(|headers: HeaderMap| async move {
                    assert_eq!(headers["authorization"], "Bearer inventory-key");
                    Json(json!({"data":[{"id":"model-a","context_window":2000}]}))
                }),
            )
            .route(
                "/metadata",
                get(move |headers: HeaderMap| {
                    count.fetch_add(1, Ordering::SeqCst);
                    async move {
                        assert!(!headers.contains_key("authorization"));
                        assert!(!headers.contains_key("x-goog-api-key"));
                        Json(json!({"reference":{"models":{"model-a":{
                            "limit":{"context":1000,"output":64}, "tool_call":true,
                            "modalities":{"input":["text","image"],"output":["text"]}
                        }}}}))
                    }
                }),
            );
        let (base, server) = serve(app).await;
        let cache = ModelDiscovery::default();
        let client = reqwest::Client::new();
        let secrets = SecretStore::explicit().with_override("KEY", "inventory-key");
        let mut options = options(base.clone(), DiscoveryProtocol::OpenaiCompatible);
        options.secret_name = Some("KEY".into());
        options.metadata_url = Some(format!("{base}/metadata"));
        options.metadata_provider = Some("reference".into());
        options.overrides.insert(
            "model-a".into(),
            ModelMetadata {
                supports_tools: Some(false),
                ..Default::default()
            },
        );
        let catalog = cache
            .discover(&client, &secrets, &options, false, &CancelToken::new())
            .await
            .unwrap();
        let metadata = &catalog.models[0].metadata;
        assert_eq!(metadata.context_tokens, Some(2000));
        assert_eq!(metadata.output_tokens, Some(64));
        assert_eq!(metadata.supports_tools, Some(false));
        assert_eq!(metadata.supports_images, Some(true));
        let cached = cache
            .discover(&client, &secrets, &options, false, &CancelToken::new())
            .await
            .unwrap();
        assert!(cached.cached);
        assert_eq!(requests.load(Ordering::SeqCst), 1);
        let known = cache.known_model(&secrets, &options, "model-a").unwrap();
        assert_eq!(known.output_tokens, Some(64));
        assert_eq!(known.supports_tools, Some(false));
        server.abort();
    }

    #[tokio::test]
    async fn failed_metadata_does_not_discard_inventory() {
        let (base, server) = serve(
            Router::new()
                .route(
                    "/models",
                    get(|| async { Json(json!({"data":[{"id":"model-a"}]})) }),
                )
                .route(
                    "/metadata",
                    get(|| async { (StatusCode::INTERNAL_SERVER_ERROR, "private body") }),
                ),
        )
        .await;
        let mut options = options(base.clone(), DiscoveryProtocol::OpenaiCompatible);
        options.metadata_url = Some(format!("{base}/metadata"));
        options.metadata_provider = Some("reference".into());
        let catalog = ModelDiscovery::default()
            .discover(
                &reqwest::Client::new(),
                &SecretStore::explicit(),
                &options,
                false,
                &CancelToken::new(),
            )
            .await
            .unwrap();
        assert_eq!(catalog.models.len(), 1);
        assert_eq!(catalog.models[0].metadata.supports_images, None);
        assert_eq!(catalog.metadata_error.as_deref(), Some("provider HTTP 500"));
        server.abort();
    }

    #[tokio::test]
    async fn ollama_preserves_inventory_when_optional_show_fails() {
        use axum::routing::post;
        let app = Router::new()
            .route("/api/tags", get(|| async { Json(json!({"models":[{"name":"local-a"},{"name":"local-b"}]})) }))
            .route("/api/show", post(|Json(body): Json<Value>| async move {
                if body["model"] == "local-a" {
                    (StatusCode::OK, Json(json!({"capabilities":["completion","vision","tools"],"model_info":{"arch.context_length":8192}})))
                } else { (StatusCode::NOT_FOUND, Json(json!({"error":"missing metadata"}))) }
            }));
        let (base, server) = serve(app).await;
        let catalog = ModelDiscovery::default()
            .discover(
                &reqwest::Client::new(),
                &SecretStore::explicit(),
                &options(format!("{base}/v1"), DiscoveryProtocol::Ollama),
                false,
                &CancelToken::new(),
            )
            .await
            .unwrap();
        assert_eq!(catalog.models.len(), 2);
        assert_eq!(catalog.models[0].metadata.context_tokens, Some(8192));
        assert_eq!(catalog.models[0].metadata.supports_images, Some(true));
        assert_eq!(catalog.models[1].metadata.supports_images, None);
        server.abort();
    }

    #[tokio::test]
    async fn ollama_slow_metadata_cannot_discard_successful_inventory() {
        use axum::routing::post;
        let app = Router::new()
            .route("/api/tags", get(|| async { Json(json!({"models": (0..9).map(|index| json!({"name":format!("local-{index}")})).collect::<Vec<_>>()})) }))
            .route("/api/show", post(|| async { std::future::pending::<StatusCode>().await }));
        let (base, server) = serve(app).await;
        let discovery = ModelDiscovery::default();
        let client = reqwest::Client::new();
        let secret = SecretStore::explicit();
        let options = options(base, DiscoveryProtocol::Ollama);
        let cancel = CancelToken::new();
        let catalog = tokio::time::timeout(
            Duration::from_secs(12),
            discovery.discover(&client, &secret, &options, false, &cancel),
        )
        .await
        .unwrap()
        .unwrap();
        assert_eq!(catalog.models.len(), 9);
        assert!(
            catalog
                .models
                .iter()
                .all(|model| model.metadata.supports_images.is_none())
        );
        server.abort();
    }
}
