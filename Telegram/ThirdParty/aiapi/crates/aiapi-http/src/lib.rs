use std::collections::HashMap;
use std::sync::{LazyLock, Mutex};
use std::time::Duration;

#[derive(Clone, Eq, PartialEq, Hash)]
struct ClientKey {
    timeout: Duration,
    proxy_url: Option<String>,
}

static CLIENTS: LazyLock<Mutex<HashMap<ClientKey, reqwest::Client>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

pub fn client(timeout: Duration) -> anyhow::Result<reqwest::Client> {
    client_with_proxy(timeout, None)
}

pub fn client_with_proxy(
    timeout: Duration,
    proxy_url: Option<&str>,
) -> anyhow::Result<reqwest::Client> {
    let proxy_url = proxy_url
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_string);
    let key = ClientKey { timeout, proxy_url };

    let mut clients = CLIENTS
        .lock()
        .map_err(|err| anyhow::anyhow!("HTTP client cache lock poisoned: {err}"))?;
    if let Some(client) = clients.get(&key) {
        return Ok(client.clone());
    }

    let mut builder = reqwest::Client::builder()
        .no_proxy()
        .redirect(reqwest::redirect::Policy::none())
        .timeout(timeout);
    if let Some(proxy_url) = key.proxy_url.as_deref() {
        builder = builder.proxy(reqwest::Proxy::all(proxy_url).map_err(Error::from)?);
    }
    let client = builder.build().map_err(Error::from)?;
    if clients.len() >= 64 {
        clients.clear();
    }
    clients.insert(key, client.clone());
    Ok(client)
}

/// Sanitize a transport error without exposing its URL or response body.
pub fn clean_error(err: reqwest::Error) -> anyhow::Error {
    Error::from(err).into()
}

#[derive(Debug)]
pub enum Error {
    Transport(String),
    Api(u16),
    Parse(String),
    Timeout,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Transport(message) | Self::Parse(message) => f.write_str(message),
            Self::Api(status) => write!(f, "provider HTTP {status}"),
            Self::Timeout => f.write_str("provider request timed out"),
        }
    }
}

impl std::error::Error for Error {}

impl From<reqwest::Error> for Error {
    fn from(error: reqwest::Error) -> Self {
        if error.is_timeout() {
            Self::Timeout
        } else {
            Self::Transport(error.without_url().to_string())
        }
    }
}

pub fn check_status(response: &reqwest::Response) -> Result<(), Error> {
    let status = response.status();
    if status.is_success() {
        Ok(())
    } else {
        Err(Error::Api(status.as_u16()))
    }
}

const MAX_JSON_BYTES: usize = 4 * 1024 * 1024;

/// Parse size-bounded JSON with status checking and sanitized errors.
pub async fn parse_json<T: serde::de::DeserializeOwned>(
    response: reqwest::Response,
) -> anyhow::Result<T> {
    parse_json_with_limit(response, MAX_JSON_BYTES).await
}

pub async fn parse_json_with_limit<T: serde::de::DeserializeOwned>(
    mut response: reqwest::Response,
    limit: usize,
) -> anyhow::Result<T> {
    check_status(&response)?;
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(Error::from)? {
        if chunk.len() > limit - bytes.len() {
            return Err(Error::Parse("provider response exceeds size limit".into()).into());
        }
        bytes.extend_from_slice(&chunk);
    }
    serde_json::from_slice(&bytes)
        .map_err(|_| Error::Parse("invalid provider response".into()).into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::{Router, routing::get};

    #[tokio::test]
    async fn limits_body_before_parsing_and_sanitizes_status_errors() {
        let app = Router::new()
            .route("/large", get(|| async { "x".repeat(4096) }))
            .route(
                "/failed",
                get(|| async { (axum::http::StatusCode::UNAUTHORIZED, "private response") }),
            );
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let base = format!("http://{}", listener.local_addr().unwrap());
        let server = tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });
        let client = client(Duration::from_secs(5)).unwrap();
        let error = parse_json_with_limit::<serde_json::Value>(
            client.get(format!("{base}/large")).send().await.unwrap(),
            1024,
        )
        .await
        .unwrap_err();
        assert_eq!(error.to_string(), "provider response exceeds size limit");
        assert!(error.is::<Error>());
        let error = parse_json::<serde_json::Value>(
            client.get(format!("{base}/failed")).send().await.unwrap(),
        )
        .await
        .unwrap_err();
        assert!(matches!(
            error.downcast_ref::<Error>(),
            Some(Error::Api(401))
        ));
        assert!(!error.to_string().contains("private response"));
        server.abort();
    }
}
