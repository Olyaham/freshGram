# Attribution

`AiApi-rs` is maintained by Olyaham under **MIT** (see LICENSE-MIT).
It contains copied and adapted modules from:

- **NedoBot** — `Mar2ianen/NedoBot`, **MIT**: the shadow primary/fallback
  ASR semantics, Groq multipart flow (`verbose_json` + segment
  granularities), Gemini resumable-upload → Interactions → extract →
  delete flow, LLM TOML topology with route resolution and
  fallback chains, validation retry with attempt audit, and the axum
  mock-server test patterns. Ported modules: `llm/types.rs`,
  `llm/profiles.rs`, `llm/genai_transport.rs`, `llm/service.rs`
  (minus product config), shared HTTP client handling,
  `features/voice/asr.rs` (Groq/Gemini/shadow, secrets via store).

The original author is Mar2ianen. Preserve this attribution and the MIT
license in redistributed copies. Product prompts, transcript cleanup policies,
rendering and Telegram integration live in freshGram's host adapter.

The ASR transport also incorporates the streaming uploads, cancellation and
deadline handling developed in freshGram's MIT-licensed `voice-asr` adapter.
Model discovery is new code; its architecture follows the documented
OpenCode catalog principles (provider inventories, metadata, overrides and
refreshable caches). No OpenCode source code is included.
