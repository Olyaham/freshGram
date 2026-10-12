//! Local Whisper via the `whisper-rs` bindings (tazz4843) over
//! whisper.cpp (ggml-org).
//!
//! Deliberately `unsafe`-free: only the safe `whisper-rs` API is used
//! (`set_abort_callback_safe`, bounded `get_segment`, lossy text access).
//! GPU use follows the linked build flavor (`local-cuda`, `local-vulkan`, …);
//! `use_gpu` only decides whether the GPU path may be attempted.

use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};

use whisper_rs::{FullParams, SamplingStrategy, WhisperContext, WhisperContextParameters};

use crate::audio::read_wav_mono16_f32;
use crate::config::{AsrConfig, LocalConfig};
use crate::types::{AsrSegment, CancelToken, Transcript, TranscriptError};

use super::{TranscribeInput, TranscriptionProvider};

pub struct LocalWhisper {
    cfg: LocalConfig,
    language: String,
}

impl LocalWhisper {
    pub fn new(cfg: &AsrConfig) -> Self {
        Self {
            cfg: cfg.local.clone(),
            language: cfg.language.clone(),
        }
    }
}

struct LocalOut {
    text: String,
    segments: Vec<AsrSegment>,
}

impl TranscriptionProvider for LocalWhisper {
    fn name(&self) -> &'static str {
        "local"
    }

    async fn transcribe(
        &self,
        input: &TranscribeInput,
        cancel: &CancelToken,
    ) -> Result<Transcript, TranscriptError> {
        cancel.check()?;
        if self.cfg.model_path.trim().is_empty() {
            return Err(TranscriptError::Unsupported(
                "local model path is empty".to_string(),
            ));
        }
        // 16 kHz mono 16-bit PCM WAV; anything else is the host's FFmpeg job.
        let samples = read_wav_mono16_f32(&input.audio_path)?;
        cancel.check()?;

        let flag = cancel.flag();
        let cfg = self.cfg.clone();
        let language = self.language.clone();
        let joined = tokio::task::spawn_blocking(move || {
            let out = run_whisper(&cfg, &language, samples, flag);
            (cfg.model_path.clone(), out)
        })
        .await
        .map_err(|e| TranscriptError::Model(format!("worker stopped: {e}")))?;
        let (model_path, result) = joined;
        match result {
            Ok(out) => Ok(Transcript {
                // Labeled by `Engine` from the registry; see `Provider::name`.
                provider: String::new(),
                model: model_path,
                request_id: None,
                text: out.text,
                segments: out.segments,
            }),
            Err(error) => {
                if cancel.is_cancelled() {
                    Err(TranscriptError::Cancelled)
                } else {
                    Err(error)
                }
            }
        }
    }
}

fn run_whisper(
    cfg: &LocalConfig,
    language: &str,
    samples: Vec<f32>,
    flag: Arc<AtomicBool>,
) -> Result<LocalOut, TranscriptError> {
    if samples.is_empty() {
        return Err(TranscriptError::Unsupported("no audio samples".to_string()));
    }
    let mut ctx_params = WhisperContextParameters::new();
    ctx_params.use_gpu(cfg.use_gpu);
    let ctx = WhisperContext::new_with_params(&cfg.model_path, ctx_params)
        .map_err(|e| TranscriptError::Model(format!("load model: {e:?}")))?;
    let mut state = ctx
        .create_state()
        .map_err(|e| TranscriptError::Model(format!("create state: {e:?}")))?;

    let mut params = FullParams::new(SamplingStrategy::Greedy { best_of: 1 });
    params.set_n_threads(cfg.n_threads.max(1) as std::os::raw::c_int);
    params.set_translate(false);
    params.set_no_context(true);
    params.set_suppress_blank(true);
    params.set_suppress_nst(true);
    params.set_print_progress(false);
    params.set_print_realtime(false);
    params.set_print_timestamps(false);
    params.set_print_special(false);
    let lang = language.trim();
    if lang.is_empty() || lang.eq_ignore_ascii_case("auto") {
        params.set_language(None);
    } else {
        params.set_language(Some(lang));
    }
    params.set_abort_callback_safe(move || flag.load(Ordering::Relaxed));

    state
        .full(params, &samples)
        .map_err(|e| TranscriptError::Model(format!("decode: {e:?}")))?;

    let count = state.full_n_segments();
    let mut text = String::new();
    let mut segments = Vec::new();
    for i in 0..count {
        let Some(segment) = state.get_segment(i) else {
            continue;
        };
        let part = segment
            .to_str_lossy()
            .map_err(|e| TranscriptError::Model(format!("segment text: {e:?}")))?
            .trim()
            .to_string();
        if part.is_empty() {
            continue;
        }
        if !text.is_empty() {
            text.push(' ');
        }
        text.push_str(&part);
        segments.push(AsrSegment {
            start_sec: segment.start_timestamp() as f32 / 100.0,
            end_sec: segment.end_timestamp() as f32 / 100.0,
            text: part,
        });
    }
    Ok(LocalOut {
        text: text.trim().to_string(),
        segments,
    })
}
