//! voice-asr: pluggable voice transcription with an isolated C ABI.
//!
//! Providers: Groq (`/audio/transcriptions`), Gemini Transcribe (Files +
//! Interactions API), optional local Whisper (`whisper-rs`, feature `local`).
//! The only FFI surface is [`ffi`]; everything else is safe Rust.

#![deny(unsafe_code)]

pub mod audio;
pub mod config;
pub mod engine;
#[allow(unsafe_code)]
pub mod ffi;
mod providers;
pub mod types;

pub use config::{
    AsrConfig, CustomAsrConfig, DEFAULT_GEMINI_BASE, DEFAULT_GEMINI_MODEL, DEFAULT_GROQ_BASE,
    DEFAULT_GROQ_MODEL, GeminiConfig, GroqConfig, LocalConfig,
};
pub use engine::Engine;
pub use providers::TranscribeInput;
pub use types::{AsrProvider, AsrSegment, CancelToken, Transcript, TranscriptError};
