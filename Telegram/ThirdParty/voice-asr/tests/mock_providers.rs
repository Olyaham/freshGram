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
async fn gemini_incomplete_upload_reference_still_deletes_known_file() {
    use std::sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    };
    let deleted = Arc::new(AtomicBool::new(false));
    let flag = deleted.clone();
    let app = Router::new()
        .route("/upload/v1beta/files", post(gemini_start))
        .route(
            "/upload/session",
            post(|_: axum::body::Bytes| async { Json(json!({"file":{"name":"files/abc"}})) }),
        )
        .route(
            "/v1beta/files/abc",
            delete(move || {
                flag.store(true, Ordering::SeqCst);
                async { StatusCode::OK }
            }),
        );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let error = Engine::new()
        .unwrap()
        .transcribe_async(
            &gemini_config(addr, AsrProvider::Gemini, None),
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap_err();
    assert!(matches!(error, TranscriptError::Parse(_)));
    assert!(deleted.load(Ordering::SeqCst));
    server.abort();
}

#[tokio::test]
async fn malformed_segment_timeline_fails_instead_of_becoming_silence() {
    let (addr, server) = serve(Router::new().route(
        "/openai/v1/audio/transcriptions",
        post(|_: axum::body::Bytes| async {
            Json(json!({"text":"hello","segments":[{"start":4,"end":2,"text":"hello"}]}))
        }),
    ))
    .await;
    let dir = tempfile::TempDir::new().unwrap();
    let error = Engine::new()
        .unwrap()
        .transcribe_async(
            &groq_config(addr),
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap_err();
    assert!(matches!(error, TranscriptError::Parse(_)));
    server.abort();
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

#[tokio::test]
async fn shadow_failure_keeps_primary_and_success_preserves_both() {
    let app = Router::new()
        .route("/openai/v1/audio/transcriptions", post(groq_loose))
        .route(
            "/compat/audio/transcriptions",
            post(|_: axum::body::Bytes| async { Json(json!({"text":"second transcript"})) }),
        );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let mut config = groq_config(addr);
    config.shadow = Some(AsrProvider::Custom);
    let engine = Engine::new().unwrap();
    let first = engine
        .process_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    assert_eq!(first.raw.text, "fallback ok");
    assert!(first.alternatives.is_empty());
    config.custom.api_base = format!("http://{addr}/compat");
    config.custom.model = "custom-asr".into();
    let both = engine
        .process_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    assert_eq!(both.raw.provider, "groq");
    assert_eq!(both.alternatives[0].text, "second transcript");
    config.groq.api_key.clear();
    let salvaged = engine
        .process_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    assert_eq!(salvaged.raw.provider, "custom");
    server.abort();
}

#[tokio::test]
async fn cleanup_uses_text_model_and_keeps_raw_and_chapters() {
    let raw = "hello world ".repeat(40).trim().to_string();
    let clean = "Hello, world! ".repeat(40).trim().to_string();
    let asr_text = raw.clone();
    let clean_text = clean.clone();
    let app = Router::new()
        .route("/audio/transcriptions", post(move |_: axum::body::Bytes| {
            let text = asr_text.clone();
            async move { Json(json!({"text":text,"segments":[{"start":0,"end":60,"text":text}]})) }
        }))
        .route("/chat/completions", post(move |headers: HeaderMap, Json(request): Json<Value>| {
            let text = clean_text.clone();
            async move {
            assert!(!headers.contains_key("authorization"));
            assert_eq!(request["model"], "text-editor");
            let content = json!({"mode":"chapters","text":text,"chapters":[{"title":"Speech","start":"0:00","end":"1:00","text":text}],"short_summary":"Hello, world!"}).to_string();
            Json(json!({"id":"edit-1", "object":"chat.completion", "created":1, "model":"text-editor", "choices":[{"index":0,"message":{"role":"assistant","content":content},"finish_reason":"stop"}]}))
            }
        }));
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let mut config = groq_config(addr);
    config.provider = AsrProvider::Custom;
    config.custom.api_base = format!("http://{addr}");
    config.custom.model = "asr-model".into();
    config.cleanup = Some(voice_asr::config::CleanupConfig {
        provider: AsrProvider::Custom,
        model: "text-editor".into(),
    });
    let edited = Engine::new()
        .unwrap()
        .process_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    server.abort();
    assert_eq!(edited.raw.text, raw);
    assert_eq!(edited.text(), clean);
    let structured = edited.clean.as_ref().unwrap();
    assert_eq!(
        structured.mode,
        voice_asr::types::TranscriptRenderMode::Chapters
    );
    assert_eq!(structured.chapters.len(), 1);
    assert_eq!(structured.chapters[0].text, clean);
    assert_eq!(structured.chapters[0].end_sec, Some(60.0));
    assert_eq!(structured.short_summary.as_deref(), Some("Hello, world!"));
    assert_eq!(edited.cleanup_model.as_deref(), Some("text-editor"));
    assert_eq!(edited.cleanup_provider.as_deref(), Some("custom"));
    assert_eq!(edited.cleanup_attempts.len(), 1);
}

#[tokio::test]
async fn discovered_output_budget_and_validation_retry_reach_cleanup() {
    use axum::routing::get;
    use std::sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
    };
    let calls = Arc::new(AtomicUsize::new(0));
    let count = calls.clone();
    let app = Router::new()
        .route("/models", get(|| async { Json(json!({"data":[{"id":"text-editor","context_window":2048,"max_output_tokens":64,"supports_system_prompt":false}]})) }))
        .route("/audio/transcriptions", post(|_: axum::body::Bytes| async { Json(json!({"text":"hello world"})) }))
        .route("/chat/completions", post(move |Json(request): Json<Value>| {
            let attempt = count.fetch_add(1, Ordering::SeqCst);
            async move {
                assert_eq!(request["max_tokens"], 64);
                assert_eq!(request["messages"][0]["role"], "user");
                let content = if attempt == 0 { "not JSON" } else { "{\"mode\":\"short\",\"text\":\"Hello, world!\",\"chapters\":[]}" };
                Json(json!({"id":"edit-1", "object":"chat.completion", "created":1, "model":"text-editor", "choices":[{"index":0,"message":{"role":"assistant","content":content},"finish_reason":"stop"}]}))
            }
        }));
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let mut config = groq_config(addr);
    config.provider = AsrProvider::Custom;
    config.custom.api_base = format!("http://{addr}");
    config.custom.model = "asr-model".into();
    config.cleanup = Some(voice_asr::config::CleanupConfig {
        provider: AsrProvider::Custom,
        model: "text-editor".into(),
    });
    let edited = Engine::new()
        .unwrap()
        .process_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    assert_eq!(edited.text(), "Hello, world!");
    assert_eq!(calls.load(Ordering::SeqCst), 2);
    assert_eq!(edited.cleanup_attempts[0].outcome, "validation_failed");
    assert_eq!(edited.cleanup_attempts[1].outcome, "success");
    server.abort();
}

#[tokio::test]
async fn non_speech_marker_keeps_raw_and_skips_text_generation() {
    let (addr, server) = serve(Router::new().route(
        "/openai/v1/audio/transcriptions",
        post(|_: axum::body::Bytes| async { Json(json!({"text":"[silence]"})) }),
    ))
    .await;
    let dir = tempfile::TempDir::new().unwrap();
    let mut config = groq_config(addr);
    config.cleanup = Some(voice_asr::config::CleanupConfig {
        provider: AsrProvider::Groq,
        model: "text-editor".into(),
    });
    let result = Engine::new()
        .unwrap()
        .process_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    assert!(result.no_speech);
    assert_eq!(result.text(), "");
    assert_eq!(result.raw.text, "[silence]");
    assert!(result.cleanup_attempts.is_empty());
    server.abort();
}

#[tokio::test]
async fn failed_cleanup_preserves_raw_text_and_failed_attempt_audit() {
    let app = Router::new()
        .route(
            "/audio/transcriptions",
            post(|_: axum::body::Bytes| async { Json(json!({"text":"keep this raw text"})) }),
        )
        .route(
            "/chat/completions",
            post(|| async { (StatusCode::UNAUTHORIZED, "private API failure") }),
        );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let config = AsrConfig {
        provider: AsrProvider::Custom,
        fallback: None,
        custom: CustomAsrConfig {
            api_base: format!("http://{addr}"),
            model: "asr-model".into(),
            ..Default::default()
        },
        cleanup: Some(voice_asr::config::CleanupConfig {
            provider: AsrProvider::Custom,
            model: "text-editor".into(),
        }),
        ..Default::default()
    };
    let result = Engine::new()
        .unwrap()
        .process_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    assert_eq!(result.text(), "keep this raw text");
    assert_eq!(result.cleanup_provider.as_deref(), Some("raw_asr_fallback"));
    assert_eq!(result.cleanup_attempts.len(), 1);
    assert_eq!(result.cleanup_attempts[0].outcome, "http_401");
    assert!(
        !serde_json::to_string(&result)
            .unwrap()
            .contains("private API failure")
    );
    server.abort();
}

#[tokio::test]
async fn explicitly_incompatible_cleanup_model_does_not_receive_transcript() {
    use axum::routing::get;
    use std::sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    };
    let generated = Arc::new(AtomicBool::new(false));
    let flag = generated.clone();
    let app = Router::new()
        .route(
            "/models",
            get(|| async {
                Json(
                    json!({"data":[{"id":"embedding-model","supports_text_generation":false},
                    {"id":"speech-model","modalities":{"input":["audio"],"output":["text"]}}]}),
                )
            }),
        )
        .route(
            "/audio/transcriptions",
            post(|_: axum::body::Bytes| async { Json(json!({"text":"keep this raw text"})) }),
        )
        .route(
            "/chat/completions",
            post(move || {
                flag.store(true, Ordering::SeqCst);
                async { StatusCode::INTERNAL_SERVER_ERROR }
            }),
        );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let mut config = AsrConfig {
        provider: AsrProvider::Custom,
        fallback: None,
        custom: CustomAsrConfig {
            api_base: format!("http://{addr}"),
            model: "asr-model".into(),
            ..Default::default()
        },
        cleanup: Some(voice_asr::config::CleanupConfig {
            provider: AsrProvider::Custom,
            model: "embedding-model".into(),
        }),
        ..Default::default()
    };
    for model in ["embedding-model", "speech-model"] {
        config.cleanup.as_mut().unwrap().model = model.into();
        let result = Engine::new()
            .unwrap()
            .process_async(
                &config,
                &test_input(&dir, "a.ogg", None),
                &CancelToken::new(),
            )
            .await
            .unwrap();
        assert_eq!(result.text(), "keep this raw text");
        assert_eq!(result.cleanup_provider.as_deref(), Some("raw_asr_fallback"));
        assert!(!generated.load(Ordering::SeqCst));
    }
    server.abort();
}

#[tokio::test]
async fn cancellation_interrupts_automatic_cleanup_discovery() {
    use axum::routing::get;
    use std::sync::Arc;
    let started = Arc::new(tokio::sync::Notify::new());
    let signal = started.clone();
    let app = Router::new()
        .route(
            "/models",
            get(move || {
                signal.notify_one();
                async { std::future::pending::<StatusCode>().await }
            }),
        )
        .route(
            "/audio/transcriptions",
            post(|_: axum::body::Bytes| async { Json(json!({"text":"hello world"})) }),
        );
    let (addr, server) = serve(app).await;
    let dir = tempfile::TempDir::new().unwrap();
    let config = AsrConfig {
        provider: AsrProvider::Custom,
        fallback: None,
        custom: CustomAsrConfig {
            api_base: format!("http://{addr}"),
            model: "asr-model".into(),
            ..Default::default()
        },
        cleanup: Some(voice_asr::config::CleanupConfig {
            provider: AsrProvider::Custom,
            model: "text-editor".into(),
        }),
        ..Default::default()
    };
    let cancel = CancelToken::new();
    let token = cancel.clone();
    let task = tokio::spawn(async move {
        Engine::new()
            .unwrap()
            .process_async(&config, &test_input(&dir, "a.ogg", None), &token)
            .await
    });
    tokio::time::timeout(std::time::Duration::from_secs(2), started.notified())
        .await
        .unwrap();
    cancel.cancel();
    let error = tokio::time::timeout(std::time::Duration::from_secs(1), task)
        .await
        .unwrap()
        .unwrap()
        .err()
        .expect("cancelled processing must return an error");
    assert!(matches!(error, TranscriptError::Cancelled));
    server.abort();
}

#[tokio::test]
async fn invalid_cleanup_configuration_does_not_lose_recognition() {
    let (addr, server) =
        serve(Router::new().route("/openai/v1/audio/transcriptions", post(groq_loose))).await;
    let dir = tempfile::TempDir::new().unwrap();
    let mut config = groq_config(addr);
    config.cleanup = Some(voice_asr::config::CleanupConfig {
        provider: AsrProvider::Groq,
        model: String::new(),
    });
    let result = Engine::new()
        .unwrap()
        .process_async(
            &config,
            &test_input(&dir, "a.ogg", None),
            &CancelToken::new(),
        )
        .await
        .unwrap();
    server.abort();
    assert_eq!(result.text(), "fallback ok");
    assert!(result.clean.is_none());
}
