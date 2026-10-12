//! Mock-server tests for the Groq and Gemini providers.
//!
//! Patterns adapted from NedoBot (`Mar2ianen/NedoBot`, MIT): axum mock
//! servers, multipart wire assertions, no real network.

use std::net::SocketAddr;

use axum::{
    Json, Router,
    extract::Multipart,
    http::{HeaderMap, StatusCode},
    routing::{delete, post},
};
use serde_json::{Value, json};
use tokio::net::TcpListener;

use voice_asr::{
    AsrConfig, AsrProvider, CancelToken, CustomAsrConfig, Engine, GeminiConfig, GroqConfig,
    TranscribeInput, TranscriptError, audio::infer_audio_mime,
};

fn test_input(dir: &tempfile::TempDir, name: &str, mime: Option<&str>) -> TranscribeInput {
    let path = dir.path().join(name);
    std::fs::write(&path, b"FAKEAUDIO").unwrap();
    TranscribeInput {
        audio_path: path,
        filename: name.to_string(),
        mime: mime.map(str::to_string),
    }
}

async fn serve(app: Router) -> (SocketAddr, tokio::task::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr = listener.local_addr().unwrap();
    let handle = tokio::spawn(async move {
        axum::serve(listener, app).await.unwrap();
    });
    (addr, handle)
}

// ---------------------------------------------------------------------------
// Groq
// ---------------------------------------------------------------------------

async fn groq_strict(headers: HeaderMap, mut form: Multipart) -> Json<Value> {
    assert_eq!(
        headers.get("authorization").unwrap(),
        "Bearer test-groq-key"
    );
    let mut fields = std::collections::HashMap::new();
    let mut file_bytes = Vec::new();
    let mut file_name = String::new();
    while let Some(field) = form.next_field().await.unwrap() {
        let name = field.name().unwrap().to_string();
        if name == "file" {
            file_name = field.file_name().unwrap().to_string();
            file_bytes = field.bytes().await.unwrap().to_vec();
        } else {
            fields.insert(name, field.text().await.unwrap());
        }
    }
    assert_eq!(fields.get("model").unwrap(), "whisper-large-v3-turbo");
    assert_eq!(fields.get("response_format").unwrap(), "verbose_json");
    assert_eq!(fields.get("language").unwrap(), "ru");
    assert_eq!(fields.get("timestamp_granularities[]").unwrap(), "segment");
    assert_eq!(file_name, "voice-message.ogg");
    assert_eq!(file_bytes, b"FAKEAUDIO");
    Json(json!({
        "text": " Привет, мир ",
        "segments": [{ "start": 0.0, "end": 1.25, "text": " Привет, мир " }],
        "x_groq": { "id": "req_mock_123" },
    }))
}

async fn groq_loose(body: axum::body::Bytes) -> Json<Value> {
    assert!(!body.is_empty());
    Json(json!({ "text": "fallback ok", "segments": [] }))
}

fn groq_config(addr: SocketAddr) -> AsrConfig {
    AsrConfig {
        provider: AsrProvider::Groq,
        fallback: None,
        language: "ru".to_string(),
        groq: GroqConfig {
            api_base: format!("http://{addr}/openai/v1"),
            api_key: "test-groq-key".to_string(),
            ..GroqConfig::default()
        },
        ..AsrConfig::default()
    }
}

#[tokio::test]
async fn groq_multipart_wire_format_and_parsing() {
    let app = Router::new().route("/openai/v1/audio/transcriptions", post(groq_strict));
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let transcript = engine
        .transcribe_async(
            &groq_config(addr),
            &test_input(&dir, "voice-message.ogg", Some("audio/ogg")),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    server.abort();
    assert_eq!(transcript.provider, "groq");
    assert_eq!(transcript.model, "whisper-large-v3-turbo");
    assert_eq!(transcript.request_id.as_deref(), Some("req_mock_123"));
    assert_eq!(transcript.text, "Привет, мир");
    assert_eq!(transcript.segments.len(), 1);
    assert_eq!(transcript.segments[0].text, "Привет, мир");
    assert!((transcript.segments[0].end_sec - 1.25).abs() < f32::EPSILON);
}

#[tokio::test]
async fn groq_missing_key_fails_before_network() {
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let config = AsrConfig {
        groq: GroqConfig {
            api_key: String::new(),
            ..GroqConfig::default()
        },
        ..AsrConfig::default()
    };
    let error = engine
        .transcribe_async(
            &config,
            &test_input(&dir, "a.ogg", Some("audio/ogg")),
            &CancelToken::new(),
        )
        .await
        .unwrap_err();
    assert!(matches!(error, TranscriptError::Unsupported(_)));
}

#[tokio::test]
async fn cancelled_job_stays_cancelled() {
    let app = Router::new().route("/openai/v1/audio/transcriptions", post(groq_loose));
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let token = CancelToken::new();
    token.cancel();
    let error = engine
        .transcribe_async(&groq_config(addr), &test_input(&dir, "a.ogg", None), &token)
        .await
        .unwrap_err();
    server.abort();
    assert!(matches!(error, TranscriptError::Cancelled));
}

#[tokio::test]
async fn cancellation_interrupts_a_waiting_response() {
    let entered = std::sync::Arc::new(tokio::sync::Notify::new());
    let seen = entered.clone();
    let app = Router::new().route(
        "/openai/v1/audio/transcriptions",
        post(move || {
            let seen = seen.clone();
            async move {
                seen.notify_one();
                std::future::pending::<Json<Value>>().await
            }
        }),
    );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let token = CancelToken::new();
    let cancelling = token.clone();
    let watcher = tokio::spawn(async move {
        entered.notified().await;
        cancelling.cancel();
    });
    let result = tokio::time::timeout(
        std::time::Duration::from_secs(2),
        engine.transcribe_async(&groq_config(addr), &test_input(&dir, "a.ogg", None), &token),
    )
    .await
    .expect("cancellation must not wait for the request deadline");
    watcher.await.unwrap();
    server.abort();
    assert!(matches!(result, Err(TranscriptError::Cancelled)));
}

#[tokio::test]
async fn malformed_transcription_json_is_not_silence() {
    let app = Router::new().route(
        "/openai/v1/audio/transcriptions",
        post(|body: axum::body::Bytes| async move {
            assert!(!body.is_empty());
            Json(json!({"error": "not a transcript"}))
        }),
    );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let result = Engine::new()
        .unwrap()
        .transcribe_async(
            &groq_config(addr),
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await;
    server.abort();
    assert!(matches!(result, Err(TranscriptError::Parse(_))));
}

#[tokio::test]
async fn custom_endpoint_allows_an_empty_key() {
    let app = Router::new().route(
        "/audio/transcriptions",
        post(|headers: HeaderMap, body: axum::body::Bytes| async move {
            assert!(!body.is_empty());
            assert!(!headers.contains_key("authorization"));
            Json(json!({"text": "local service"}))
        }),
    );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let mut config = groq_config(addr);
    config.provider = AsrProvider::Custom;
    config.custom.api_base = format!("http://{addr}");
    config.custom.model = "local-whisper".into();
    let result = Engine::new()
        .unwrap()
        .transcribe_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    server.abort();
    assert_eq!(result.text, "local service");
}

async fn interrupted_gemini_cleans_up(cancelled: bool) {
    use std::sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
    };
    let entered = Arc::new(tokio::sync::Notify::new());
    let seen = entered.clone();
    let deleted = Arc::new(AtomicUsize::new(0));
    let count = deleted.clone();
    let app = Router::new()
        .route("/upload/v1beta/files", post(gemini_start))
        .route("/upload/session", post(gemini_finalize))
        .route(
            "/v1beta/interactions",
            post(move || {
                let seen = seen.clone();
                async move {
                    seen.notify_one();
                    std::future::pending::<Json<Value>>().await
                }
            }),
        )
        .route(
            "/v1beta/files/abc",
            delete(move || {
                let count = count.clone();
                async move {
                    count.fetch_add(1, Ordering::SeqCst);
                    StatusCode::OK
                }
            }),
        );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let mut config = gemini_config(addr, AsrProvider::Gemini, None);
    config.gemini.total_deadline_secs = 1;
    let token = CancelToken::new();
    let cancelling = token.clone();
    let watcher = tokio::spawn(async move {
        entered.notified().await;
        if cancelled {
            cancelling.cancel();
        }
    });
    let result = tokio::time::timeout(
        std::time::Duration::from_secs(3),
        Engine::new().unwrap().transcribe_async(
            &config,
            &test_input(&dir, "voice.ogg", Some("audio/ogg")),
            &token,
        ),
    )
    .await
    .unwrap()
    .unwrap_err();
    watcher.await.unwrap();
    server.abort();
    assert_eq!(deleted.load(Ordering::SeqCst), 1);
    assert!(matches!(
        (&result, cancelled),
        (TranscriptError::Cancelled, true) | (TranscriptError::Timeout, false)
    ));
}

#[tokio::test]
async fn gemini_cancellation_deletes_uploaded_file() {
    interrupted_gemini_cleans_up(true).await;
}

#[tokio::test]
async fn gemini_total_timeout_deletes_uploaded_file() {
    interrupted_gemini_cleans_up(false).await;
}

// ---------------------------------------------------------------------------
// Custom OpenAI-compatible endpoint: own base URL and model.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn custom_endpoint_uses_own_base_and_model() {
    async fn handler(headers: HeaderMap, mut form: Multipart) -> Json<Value> {
        assert_eq!(headers.get("authorization").unwrap(), "Bearer custom-key");
        let mut model = String::new();
        while let Some(field) = form.next_field().await.unwrap() {
            if field.name().unwrap() == "model" {
                model = field.text().await.unwrap();
            }
        }
        assert_eq!(model, "my-whisper");
        Json(json!({ "text": "custom ok", "segments": [] }))
    }
    let app = Router::new().route("/compat/audio/transcriptions", post(handler));
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let config = AsrConfig {
        provider: AsrProvider::Custom,
        fallback: None,
        language: "auto".to_string(),
        custom: CustomAsrConfig {
            api_base: format!("http://{addr}/compat"),
            api_key: "custom-key".to_string(),
            model: "my-whisper".to_string(),
            timeout_secs: 10,
            ..CustomAsrConfig::default()
        },
        ..AsrConfig::default()
    };
    let transcript = engine
        .transcribe_async(
            &config,
            &test_input(&dir, "a.mp3", Some("audio/mp3")),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    server.abort();
    assert_eq!(transcript.provider, "custom");
    assert_eq!(transcript.model, "my-whisper");
    assert_eq!(transcript.text, "custom ok");
}

#[tokio::test]
async fn custom_endpoint_without_base_is_unsupported() {
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let config = AsrConfig {
        provider: AsrProvider::Custom,
        fallback: None,
        ..AsrConfig::default()
    };
    let error = engine
        .transcribe_async(
            &config,
            &test_input(&dir, "a.mp3", Some("audio/mp3")),
            &CancelToken::new(),
        )
        .await
        .unwrap_err();
    assert!(matches!(error, TranscriptError::Unsupported(_)));
}

// ---------------------------------------------------------------------------
// Gemini mock flow: start -> finalize -> interactions -> delete
// ---------------------------------------------------------------------------

async fn gemini_start(headers: HeaderMap) -> (HeaderMap, Json<Value>) {
    assert!(headers.contains_key("x-goog-api-key"));
    let host = headers["host"].to_str().unwrap().to_string();
    let mut out = HeaderMap::new();
    out.insert(
        "x-goog-upload-url",
        format!("http://{host}/upload/session").parse().unwrap(),
    );
    (out, Json(json!({})))
}

async fn gemini_finalize(body: axum::body::Bytes) -> Json<Value> {
    assert_eq!(body, b"FAKEAUDIO".as_slice());
    Json(json!({ "file": { "name": "files/abc", "uri": "https://x/files/abc" } }))
}

async fn gemini_interact(headers: HeaderMap, Json(body): Json<Value>) -> Json<Value> {
    assert!(headers.contains_key("x-goog-api-key"));
    assert_eq!(body["model"], "gemini-3.5-transcribe");
    let input = body["input"].as_array().unwrap();
    let audio = match input.as_slice() {
        [prompt, audio] => {
            assert_eq!(prompt["type"], "text");
            assert_eq!(prompt["text"], "Caption every sound");
            audio
        }
        [audio] => audio,
        _ => panic!("unexpected input parts"),
    };
    assert_eq!(audio["type"], "audio");
    assert_eq!(audio["mime_type"], "audio/ogg");
    assert_eq!(
        body["generation_config"]["transcription_config"]["mode"]["type"],
        "verbatim"
    );
    assert_eq!(
        body["generation_config"]["transcription_config"]["language_codes"],
        json!(["ru-RU"])
    );
    Json(json!({ "output_text": "  Привет из Gemini ", "id": "gen_1" }))
}

async fn gemini_delete() -> StatusCode {
    StatusCode::OK
}

fn gemini_app() -> Router {
    Router::new()
        .route("/upload/v1beta/files", post(gemini_start))
        .route("/upload/session", post(gemini_finalize))
        .route("/v1beta/interactions", post(gemini_interact))
        .route("/v1beta/files/abc", delete(gemini_delete))
}

fn gemini_config(
    addr: SocketAddr,
    provider: AsrProvider,
    fallback: Option<AsrProvider>,
) -> AsrConfig {
    AsrConfig {
        provider,
        fallback,
        language: "ru".to_string(),
        gemini: GeminiConfig {
            api_base: format!("http://{addr}"),
            api_key: "test-gemini-key".to_string(),
            timeout_secs: 10,
            total_deadline_secs: 30,
            ..GeminiConfig::default()
        },
        ..AsrConfig::default()
    }
}

#[tokio::test]
async fn gemini_full_flow_upload_interact_delete() {
    let (addr, server) = serve(gemini_app()).await;
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let transcript = engine
        .transcribe_async(
            &gemini_config(addr, AsrProvider::Gemini, None),
            &test_input(&dir, "voice.ogg", Some("audio/ogg")),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    server.abort();
    assert_eq!(transcript.provider, "gemini");
    assert_eq!(transcript.model, "gemini-3.5-transcribe");
    assert_eq!(transcript.request_id.as_deref(), Some("gen_1"));
    assert_eq!(transcript.text, "Привет из Gemini");
    assert!(transcript.segments.is_empty());
}

#[tokio::test]
async fn gemini_custom_prompt_leads_the_input_parts() {
    let (addr, server) = serve(gemini_app()).await;
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let mut config = gemini_config(addr, AsrProvider::Gemini, None);
    config.gemini.prompt = "Caption every sound".to_string();
    let transcript = engine
        .transcribe_async(
            &config,
            &test_input(&dir, "voice.ogg", Some("audio/ogg")),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    server.abort();
    assert_eq!(transcript.text, "Привет из Gemini");
}

#[tokio::test]
async fn gemini_video_note_routes_to_groq_fallback() {
    let groq = Router::new().route("/openai/v1/audio/transcriptions", post(groq_loose));
    let (addr, server) = serve(groq).await;
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let config = AsrConfig {
        groq: GroqConfig {
            api_base: format!("http://{addr}/openai/v1"),
            api_key: "k".to_string(),
            timeout_secs: 10,
            ..GroqConfig::default()
        },
        ..gemini_config(addr, AsrProvider::Gemini, Some(AsrProvider::Groq))
    };
    let transcript = engine
        .transcribe_async(
            &config,
            &test_input(&dir, "round.mp4", Some("video/mp4")),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    server.abort();
    assert_eq!(transcript.provider, "groq");
    assert_eq!(transcript.text, "fallback ok");
}

#[tokio::test]
async fn gemini_video_without_fallback_is_unsupported() {
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let error = engine
        .transcribe_async(
            &gemini_config("127.0.0.1:1".parse().unwrap(), AsrProvider::Gemini, None),
            &test_input(&dir, "round.mp4", Some("video/mp4")),
            &CancelToken::new(),
        )
        .await
        .unwrap_err();
    assert!(matches!(error, TranscriptError::Unsupported(_)));
}

// ---------------------------------------------------------------------------
// MIME inference and local-engine gating (no model file needed).
// ---------------------------------------------------------------------------

#[test]
fn mime_inference_rejects_video_and_infers_audio_suffix() {
    assert_eq!(
        infer_audio_mime("voice.oga", None).as_deref(),
        Some("audio/ogg")
    );
    assert_eq!(infer_audio_mime("note.mp4", Some("video/mp4")), None);
    assert_eq!(
        infer_audio_mime("a.bin", Some("audio/mpeg")).as_deref(),
        Some("audio/mpeg")
    );
}

#[tokio::test]
async fn local_provider_without_model_path_is_unsupported() {
    let dir = tempfile::TempDir::new().unwrap();
    let engine = Engine::new().unwrap();
    let config = AsrConfig {
        provider: AsrProvider::Local,
        fallback: None,
        ..AsrConfig::default()
    };
    let error = engine
        .transcribe_async(
            &config,
            &test_input(&dir, "a.wav", Some("audio/wav")),
            &CancelToken::new(),
        )
        .await
        .unwrap_err();
    assert!(matches!(error, TranscriptError::Unsupported(_)));
}
