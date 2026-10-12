# voice-asr

Pluggable voice transcription with an isolated C ABI for desktop clients.

Providers: **Groq** (`/audio/transcriptions`, `verbose_json` + segment
timestamps), **Gemini Transcribe** (Files upload + Interactions API,
`verbatim` mode), **Custom** (any OpenAI-compatible `/audio/transcriptions`
endpoint: own base URL + key + model), and an optional **local Whisper**
engine (`whisper-rs` over whisper.cpp, feature `local`).

No OpenAI-client or chat-LLM crate is used for audio: the one that matters
here does not support Groq-specific behavior, and no chat crate covers the
Gemini Transcribe endpoint — both protocols are spoken directly. See
`ATTRIBUTION.md` for what was borrowed, from whom, and under which license.

## Layout

- `src/types.rs` — transcript types, `CancelToken`, errors (URLs scrubbed).
- `src/config.rs` — plain-data `AsrConfig` (JSON-serializable for FFI).
- `src/audio.rs` — MIME inference; WAV gating for the local engine.
- `src/providers/` — `TranscriptionProvider` trait + Groq / Gemini / local.
- `src/engine.rs` — runtime, shared HTTP client, primary → fallback order.
  Gemini video notes route straight to the fallback provider.
- `src/ffi.rs` — the ONLY module containing `unsafe`. Opaque handles,
  panic boundaries, exactly-once async callback.
- `include/voice_asr.h` — C header with the full threading contract.
- `tests/mock_providers.rs` — axum mock-server tests, no real network.

## Config JSON (FFI)

```json
{
  "provider": "groq",
  "fallback": "gemini",
  "language": "ru",
  "groq": {
    "api_base": "https://api.groq.com/openai/v1",
    "api_key": "…",
    "model": "whisper-large-v3-turbo",
    "temperature": 0.0,
    "timeout_secs": 120
  },
  "gemini": {
    "api_base": "https://generativelanguage.googleapis.com",
    "api_key": "…",
    "model": "gemini-3.5-transcribe",
    "prompt": "",
    "timeout_secs": 180,
    "total_deadline_secs": 420
  },
  "local": { "model_path": "/…/ggml-small.bin", "n_threads": 4, "use_gpu": true },
  "custom": {
    "api_base": "https://my-server/v1",
    "api_key": "…",
    "model": "whisper-large-v3-turbo",
    "temperature": 0.0,
    "timeout_secs": 120
  }
}
```

Secrets live in the host's protected storage and are only passed through here
at call time. Omit `language` or set `"auto"` for server-side detection
(Groq); Gemini maps common codes to BCP-47 (`ru` → `ru-RU`).

## Build flavors

```bash
cargo check                                    # default: cloud-only, no ML runtime
cargo check --features local                    # optional local CPU engine
cargo check --features local-vulkan            # cross-vendor GPU build
cargo check --features local-cuda              # NVIDIA build
cargo check --no-default-features              # cloud providers only
cargo test --no-default-features               # fast: mock-server tests
```

Select one GPU flavor (`local-cuda`, `local-vulkan`, `local-metal`,
`local-hipblas`, `local-sycl`). CoreML is a separate macOS encoder option and
needs a compatible encoder model; it may be paired with Metal. The host loads
the separately installed local component only for a local request. It does not
automatically download or select a build for the hardware.

`vasr_abi_version()` identifies ABI 1. `vasr_capabilities()` reports compiled
backends, not available hardware or the device used by a request. GPU selection
is delegated to whisper.cpp. CoreML may use Apple acceleration when configured;
no generic Windows/Linux NPU backend is currently implemented.

Uploads stream from disk. Cancellation interrupts cloud I/O, and provider
deadlines cover response bodies as well as upload/send. Once a Gemini file
identifier is known, deletion is attempted after success, failure, cancellation
and timeout with its own ten-second cleanup budget. Cancellation before that
identifier arrives cannot delete an unknown remote file. Local model loading
is synchronous on the worker; inference checks the abort token.

The crate denies `unsafe` outside its explicitly allowed `ffi` module. Response
JSON is size-bounded; malformed transcripts fail instead of becoming silence.
Raw API error bodies are never returned to the host. Custom endpoints may omit
the API key for self-hosted services.

Local cancellation uses an owned atomic flag through a synchronous `ffi`
bridge. This avoids the incorrect closure-pointer cast and leaked callback
allocation in `whisper-rs 0.16.0`'s `set_abort_callback_safe` helper.

## C usage sketch

```c
VasrEngine *engine = vasr_engine_new();
VasrTranscript *t = vasr_transcribe(engine, config_json, "/tmp/a.ogg",
                                    "a.ogg", NULL, NULL);
if (t) {
    printf("%s\n", vasr_transcript_text(t));
    vasr_transcript_free(t);
} else {
    fprintf(stderr, "%d: %s\n",
            vasr_last_error_code(), vasr_last_error_message());
}
vasr_engine_free(engine);
```

Async: `vasr_transcribe_async(...)` invokes the callback exactly once on a
worker thread; `vasr_job_free` joins it. The engine must outlive its jobs.

## Local engine input

The host decodes audio (FFmpeg) to **16 kHz mono 16-bit PCM WAV**; anything
else is rejected with `VASR_UNSUPPORTED`. One job = one unique temp path;
temp files use RAII auto-remove paths.

## License

MIT — see `LICENSE-MIT` and `ATTRIBUTION.md`.
