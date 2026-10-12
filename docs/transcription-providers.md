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
ABI/capability rejection. These checks do not validate the running client's UI,
real provider credentials, native GPU/NPU execution, or Windows/macOS deployment.
The crypto harness also exercises credential persistence with the real legacy
AYV1 decoder and migration to FGV2. Production settings, routing, provider dialogs,
model downloads, passcode integration and transcription UI translation units
were syntax-checked against the generated 7.3.1 API, language and style headers.
The client itself was not built for this integration.
