# Transcription providers

Settings → General → Voice transcription controls the default provider and
independent overrides for chats, secret chats and private messages. **Use
default** inherits the global choice. Secret peers are classified before private
users. Telegram's server transcription is unavailable for secret/local messages;
select an external or local backend for those messages. Explicit Telegram selection
reports that the server provider is unavailable instead of switching providers.

Each non-Telegram provider has a settings row even when it is selected only by a
scope override. Groq and Gemini expose a masked key and editable model. Gemini
also exposes its transcription prompt. Custom exposes an OpenAI-compatible base
URL, model and optional masked key. Language is shared across providers (`auto`
selects detection). Local settings expose the component path and GPU permission;
models are managed through the existing Whisper model picker.

**Test connection** sends one second of generated silent WAV through the selected
provider, using unsaved field values. A second press cancels it. Closing the box
cancels the active test and suppresses its completion UI. Local tests also show
compiled backend capabilities; this is not a report of detected hardware.

**Automatic** uses Telegram for eligible premium cloud messages, then a configured
Groq, Gemini or Custom provider, then an installed local component with a selected
model. Explicit selections report failures rather than silently changing provider.
Both voice messages and round videos are decoded to 16 kHz mono PCM WAV on a
worker before transcription, including Gemini, which cannot accept a video MIME.

## Components

The Rust module is vendored under `Telegram/ThirdParty/voice-asr`. The application
loads its C ABI through `QLibrary`; it never links Whisper/ggml into the client.
The cloud component is bundled by `FRESHGRAM_BUNDLE_CLOUD_ASR` (default ON). It is
cloud-only and has no ML runtime. Set the option OFF for externally provided
components. Cross builds can set `FRESHGRAM_ASR_TARGET` to a Rust target triple.

The local component is separately installed. These commands build only the
component in the development profile, not Telegram Desktop. `--profile release`
selects an optimized component; CMake follows the client configuration:

```sh
python3 tools/install_voice_asr.py --flavor cloud \
  --build-dir "$HOME/.cache/freshgram-asr/cloud" \
  --destination "/path/to/client/voice-asr/cloud"
python3 tools/install_voice_asr.py --flavor local \
  --build-dir "$HOME/.cache/freshgram-asr/local" \
  --destination "/path/to/client/voice-asr/local"
```

For a GPU build, replace `local` with `cuda`, `vulkan`, `metal`, `hipblas` or
`sycl`. Platform SDKs/drivers and the matching Rust target must be installed.
`coreml` is a macOS encoder flavor requiring a matching encoder model. Current
capability reporting covers compiled flavors only; generic Windows/Linux NPU
execution is not implemented. Platform signing and driver execution require
platform-specific validation.

The build requires Rust/Cargo. The Linux Docker environment installs a minimal
Rust toolchain and stores the writable Cargo cache under `out/asr-cargo`; rebuild
the environment image before using an older cached image. Windows portable ZIPs
include the cloud DLL; macOS bundles include the cloud dylib before signing.

Default search locations are `voice-asr/{cloud,local}` beside the executable and
`tdata/voice-asr/{cloud,local}` in the working directory. The exact library filename
is `libvoice_asr.so`, `libvoice_asr.dylib` or `voice_asr.dll`. Each provider's settings
can specify an absolute component path. Cloud and local paths are independent.
Discovery checks file existence without loading a library. Local loading requires
ABI 1 and the local capability; a cloud-only build cannot act as a local engine.

Each request owns its library, cancellation state and temporary directory. Session
teardown, message deletion and a repeated transcribe-button press cancel relevant
jobs. Cleanup follows worker completion; stale callbacks cannot overwrite a new
request. A library is released when its last request/test owner is released.

## Credentials and attribution

Keys are sealed using the downstream FGV2 vault (AES-256-GCM, local key, distinct
AAD context per provider). Earlier AYV1 credentials remain readable. Sealed values
are retained during startup before the local key is available and decrypted lazily;
saving unrelated settings cannot erase them. Credentials are never serialized as
plaintext. The module receives a settings snapshot only at request time.

Interface principles follow Nagram XF: preserve the normal transcription button,
select a provider in settings and configure it in its own dialog. Nagram's Telegram
flow and Extraless's deferred local-model loading were reviewed as additional
references. No mobile source was copied. The Rust transport attribution, authors
Mar2ianen and Olyaham, and NedoBot references are retained in
`Telegram/ThirdParty/voice-asr/ATTRIBUTION.md`.

## Verification

Isolated checks cover Rust mock protocols, in-flight cancellation, Gemini cleanup,
strict response parsing, the real Qt loader → C ABI → local HTTP mock path, and
ABI/capability rejection. The crypto harness also exercises credential persistence
with the real legacy AYV1 decoder and migration to FGV2.

The complete 7.3.1 Linux client was built and linked in Debug using Qt 6.11.2.
A disposable-profile run passed 23 stages and a separate process restart passed
five more. These exercised the real Groq, Gemini, Custom and Local dialogs,
masked keys, FGV2 persistence without plaintext, independent scope persistence,
models, prompt, language, separate module paths and explicit key clearing.
The actual Custom connection-test button submitted unsaved fields and WAV data
to a local HTTP mock; closing the dialog cancelled the transport and suppressed
late result UI. Startup verified the disk key and downstream vault, discovered
the bundled cloud component and left ASR unloaded while idle.

A follow-up offline regression exercised the production `Main::Session`
constructor with a synthetic user, persisted that session and restored it in a
second process. Both runs passed transcription-cache removal checks. The
message-removal subscription is queued with a session lifetime guard because
`ApiWrap` is constructed before `Data::Session`; subscribing in the transcription
constructor would abort during login or session restoration.

After removing the temporary runtime scenario, the final client was rebuilt.
Both the build-tree executable and the staged CMake installation painted the
startup screen, persisted their disposable-profile local key and exited normally.
The installed cloud library matched the bundled artifact, exposed ABI 1 with
zero Local capabilities and had resolvable runtime dependencies. The executable
contained no native Whisper/ggml engine definitions.

The same client run checked worker-based passcode creation and change, fake
unlock recovery, removal, and injected disk-write failures. The standalone
adapters do not substitute for these runtime checks.

Separate Local components transcribed the whisper.cpp JFK sample with the
SHA-256-verified `ggml-tiny.en.bin` model on CPU and AMD Radeon 780M through Vulkan.
Both builds passed in-flight cancellation and missing-model checks. Local
cancellation uses the isolated FFI bridge to avoid the incorrect closure-pointer
cast in `whisper-rs 0.16.0`'s safe-callback helper.

Live Groq/Gemini credentials, authenticated voice-message routing, complete
secret-chat PFS exchanges, NPU execution and native Windows/macOS deployment
remain unverified by this run.
