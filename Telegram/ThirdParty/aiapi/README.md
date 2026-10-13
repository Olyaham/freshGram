# AiApi-rs

Host-agnostic AI core for desktop and bot clients: speech transcription
transports, model discovery and LLM routing. Extracted from
battle-tested product logic (see ATTRIBUTION.md); no Telegram, database or
product wiring lives here — hosts plug their own config, secrets and UI.

## Crates

- `aiapi-types` — shared transcript/segment types and cancellation.
- `aiapi-config` — transport-only config: `SecretStore` (vault
  overrides first, environment second), proxy, thinking budget.
  Voice policies, text normalizations and anything product- or
  Telegram-related live in hosts, never here.
- `aiapi-http` — cached HTTP clients (timeout + optional proxy).
- `aiapi-llm` — TOML topology (providers/models/routes), route
  resolution with capability filtering, generate_text/chat with
  fallback chains, validation retry and attempt audit. Secrets come
  from `SecretStore`, never from product structs.
- `aiapi-asr` — Groq multipart + Gemini Files/Interactions transcription
  and concurrent shadow dual-run with alternatives and fallback.
  Request-shaping helpers (MIME, language codes) are crate-private.
  OpenAI-compatible endpoints use streaming multipart requests. Gemini
   attempts to remove known uploaded files after success, failure, timeout and
   cancellation, with a separate cleanup deadline.
  Shadow providers are explicitly supplied by the host.
- `aiapi-llm::discovery` — OpenAI-compatible `/models`, paginated Gemini
  models and native Ollama tags/show. Unknown capabilities remain unknown.
  Credential-scoped TTL caching, stale results after failed refresh,
  explicit metadata overrides and optional models.dev enrichment.

Transcript cleanup, product prompts, chapters, rendering, no-speech policies
and local Whisper inference belong to application adapters.

HTTP egress is explicit: `aiapi-http::client` connects directly, and
`client_with_proxy` uses the supplied HTTP/SOCKS proxy. Environment proxy
variables are not inherited. Hosts own proxy selection and must pass the same
policy to ASR, discovery/reference metadata and LLM requests. A configured proxy
failure does not switch to direct egress.

## Discovery

`ModelDiscovery::discover` accepts a configured endpoint, protocol, secret
name and cancellation token. Set `force` to refresh; `cached` and `stale`
describe the returned inventory. Overrides take precedence over reported
metadata and can add manually configured models. A `/models` inventory alone
does not establish support for chat, transcription, vision or tools.

Optional `metadata_url` and `metadata_provider` select a models.dev-compatible
document and its provider namespace. The document is fetched without provider
credentials, cached in memory for one hour and used only to fill missing API
metadata. Provider inventory is cached for `cache_ttl_secs` (300 seconds by
default), separately for each endpoint and credential. Metadata failure leaves
the inventory usable and sets `metadata_error`; inventory refresh failure keeps
the last successful list with `stale = true` and `refresh_error`.

Metadata precedence: host overrides → provider API → reference document.
No capability is inferred from the spelling of a model ID. An explicit
`supports_text_generation` / `supports_transcription` flag and reported
modalities can reject incompatible models; a missing flag remains unknown.
`known_model` performs an in-memory lookup for hosts preparing model profiles.
Cache persistence and selection policy are owned by the host.

`GenerateTextOptions::output_validator` may borrow host data. It returns typed
`ValidationFailure` reasons, or a generic `unknown` failure code. Validation
retries and logs use these codes rather than arbitrary validator error text.
Successful generations carry `attempts`; terminal generation errors include a
downcastable `GenerationAudit` context while preserving the typed cause.

## freshGram snapshot

The client vendors a reproducible source snapshot under
`Telegram/ThirdParty/aiapi`. Update it with
`python3 tools/sync_aiapi.py --source /path/to/AiApi-rs` in freshGram.
Use `--check` to verify the snapshot before packaging. Cargo dependencies
stay inside the client checkout; no adjacent clone or unpublished Git revision
is required to build the adapter.
`SOURCE.json` records per-file SHA-256 digests and a content-addressed snapshot
identity. The client pins these files to LF on every checkout platform.

## Checks

```sh
cargo fmt --all --check
cargo test --workspace --all-targets
cargo clippy --workspace --all-targets -- -D warnings
cargo doc --workspace --no-deps
```
