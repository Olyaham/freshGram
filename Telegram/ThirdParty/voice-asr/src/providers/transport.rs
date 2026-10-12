use serde::de::DeserializeOwned;

use crate::types::{TranscriptError, map_http_error};

pub async fn json<T: DeserializeOwned>(
    mut response: reqwest::Response,
) -> Result<T, TranscriptError> {
    let status = response.status();
    if !status.is_success() {
        return Err(TranscriptError::Api {
            status: status.as_u16(),
            message: status
                .canonical_reason()
                .unwrap_or("request failed")
                .to_string(),
        });
    }
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(map_http_error)? {
        if bytes.len() + chunk.len() > 4 * 1024 * 1024 {
            return Err(TranscriptError::Parse("response exceeds size limit".into()));
        }
        bytes.extend_from_slice(&chunk);
    }
    serde_json::from_slice(&bytes)
        .map_err(|_| TranscriptError::Parse("invalid transcription response".into()))
}
