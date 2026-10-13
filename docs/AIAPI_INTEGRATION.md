# AiApi integration review

## Architecture

`Telegram/ThirdParty/aiapi` is a checksummed source snapshot of AiApi-rs.
The Rust core owns provider transports, explicit/environment secret lookup,
model discovery, capabilities and LLM routing. The `voice-asr` adapter owns
Local Whisper, transcript cleanup prompts/policy, no-speech detection and the
C ABI. The application owns encrypted credentials, provider/model selection,
media decoding and result presentation.

The snapshot contains the original MIT notices and attribution to
Mar2ianen/NedoBot. It does not require a neighboring checkout to compile.
The installer copies adapter/core license and attribution files beside the
dynamic component; Windows portable packaging includes those notices.

## Client flow

- Settings are under **freshGram Settings → General**. Provider credentials,
  ASR models and the optional cleanup model are separate settings.
- Dialogs preload configured providers' inventories, reuse the credential-scoped
  TTL cache and offer explicit refresh. Reported limits are shown in the model
  picker. Unknown capabilities are retained; incompatible reported modalities
  are filtered. Manual model IDs remain supported.
- Optional shadow ASR preserves both raw hypotheses. A successful shadow can
  recover a primary failure or explicit no-speech result.
- Cleanup is disabled by default. When enabled, it automatically refreshes
  catalog metadata, applies the selected model's output budget/system-prompt
  contract, validates its JSON and uses one validation retry. Failure preserves
  the raw transcript with a safe attempt audit.
- Transcript details expose raw/alternative/clean text, chapters and summaries
  in a selectable dialog. Copy restrictions are checked before exposing it.
  Backend details are session-local, following the transcription entry lifetime.
- Local Whisper is a separately installed native component with its own model
  and engine settings. Client local requests use their separate configuration.

## Review findings addressed

- Restored streaming uploads and independent request/total deadlines.
- Preserved typed HTTP/API/JSON/timeout/cancellation errors through the C ABI.
- Kept Gemini best-effort file deletion after success, failure, cancellation and
  timeout, including an incomplete upload response that contains a known file ID.
- Added bounded response parsing: 4 MiB for provider JSON and 32 MiB for the
  optional reference catalog. Provider credentials are not sent to models.dev.
- Corrected no-key OpenAI-compatible generation using genai's request override.
- Preserved terminal cleanup attempt audit and raw text on failure.
- Guarded asynchronous UI callbacks, cancellation and changed credentials.
- Kept cloud catalog caches alive across short-lived loader instances.
- Preserved successful Ollama inventories when optional metadata calls fail or
  exceed their bounded enrichment budget.
- Removed product-specific validation categories and ASR/chat confusion from
  the standalone LLM profile example.

## Reproducible checks

From the repository root:

```sh
python3 tools/sync_aiapi.py --check
```

From `Telegram/ThirdParty/aiapi`:

```sh
cargo fmt --all --check
cargo test --workspace --all-targets
cargo clippy --workspace --all-targets -- -D warnings
cargo doc --workspace --no-deps
```

From `Telegram/ThirdParty/voice-asr`:

```sh
cargo fmt --all --check
cargo test --all-targets --no-default-features --locked
cargo clippy --all-targets --features local --locked -- -D warnings
python3 tests/abi_smoke.py target/debug/deps/libvoice_asr.so
```

The ABI smoke test uses a loopback mock provider and verifies ownership,
discovery, text generation, cleanup, segments, async completion and cancellation.
The CI workflow runs independently of the Telegram Desktop build. C++ checks
use the configured compilation database with regenerated language headers.

Local verification on 2026-10-13: 52 core tests and 39 adapter tests passed;
Clippy passed with warnings denied, including the Local Whisper feature gate.
All eight changed C++ translation units passed syntax checks. An isolated
source-only copy passed snapshot/lockfile dependency resolution without an
adjacent AiApi checkout. Installer notice copying was checked using the already
tested dynamic library as the packaging fixture.

Live authenticated Groq/Gemini transcription and in-client voice/round-video
checks require real credentials and messages. Native Windows/macOS deployment
and a full linked client run remain separate runtime verification steps.
