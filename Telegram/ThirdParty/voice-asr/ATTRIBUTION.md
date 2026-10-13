# Attribution

This crate stands on the work below. Code reuse and idea reuse are tracked
separately on purpose: GPL-licensed projects contributed **design principles
only — no code was copied from them**, so this crate stays MIT-licensed.

## Linked / adapted code

- **whisper.cpp** — `ggml-org/whisper.cpp`, © Georgi Gerganov and
  contributors, **MIT**. The local inference engine, linked through
  `whisper-rs`. Model weights (OpenAI Whisper, ggml format) are MIT-licensed;
  each model file keeps its own terms.
- **whisper-rs** — © tazz4843, **Unlicense** (public-domain dedication).
  Safe Rust bindings used for the local provider (`WhisperContext`,
  `FullParams`, segment iteration, abort callback).
- **NedoBot** — `Mar2ianen/NedoBot`, **MIT**. Adapted with gratitude:
  `src/features/voice/asr.rs` (Groq multipart flow with `verbose_json` +
  segment granularities, Gemini resumable-upload → Interactions →
  extract → delete flow, primary/shadow fallback semantics, MIME inference,
  timeouts, URL-scrubbed errors), `src/features/voice/types.rs`
  (transcript/segment shapes), `src/features/voice/download.rs`
  (temp-file + duration/size limit discipline), and the axum mock-server
  test patterns.
- **AiApi-rs** — Olyaham and Mar2ianen, **MIT**. Safe ASR transports,
  model inventories, shared types and LLM routing are vendored under
  `../aiapi`. Preserve its license and `ATTRIBUTION.md` when redistributing.
  Cleanup logic and its prompt were copied from NedoBot and adapted here,
  including JSON parsing, length/number checks and chapter timestamps.

## Ideas only (no code copied; their licenses do not touch this crate)

- **Nagram XF** `TranscribeHelper` (`Keeperorowner/NagramXF`, **GPL-3.0**,
  by risin42/tehcneko) — provider switch with AUTO fallback order,
  `useTranscribeAI` gate over the stock transcribe button, per-provider
  credential dialogs, fail-fast on missing downloads, video-to-`.m4a` remux.
- **Extraless** `app/exteraless/speech` (`exteraless/exteraless`, **GPL-3.0**) —
  `VoskManager` shape (model manifest with hash, `.ready` marker, single
  download worker, per-language selection) and `VoskTranscriber` shape
  (16 kHz mono chunked feed, model cache with release on switch).
- **Nagram** `LLMTranslator` (`NextAlone/Nagram`, **GPL-3.0**) — provider map
  of base URL + default model, multi-key round-robin, backoff with jitter,
  `testConnection`/`fetchModels` diagnostics.
- **Vosk** (Alpha Cephei, **Apache-2.0**) — on-device engine concepts that
  informed the local-provider lifecycle (per-language models, readiness gate).

## This crate

**MIT**, © 2026 voice-asr contributors — see `LICENSE-MIT`.

`Cargo.toml` authors: `Mar2ianen`, `Olyaham`.
