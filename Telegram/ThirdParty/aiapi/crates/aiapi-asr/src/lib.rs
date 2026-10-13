//! ASR transports for Groq, Gemini and explicit shadow/fallback requests.
//! Request-shaping helpers for MIME and language codes are crate-private.

pub mod asr;
pub mod custom;
pub mod gemini;
pub(crate) mod lang;
pub(crate) mod mime;
