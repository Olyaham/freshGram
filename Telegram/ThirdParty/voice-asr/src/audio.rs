//! Audio helpers: MIME inference and local-engine WAV loading.
//!
//! Decoding compressed audio (ogg/opus/mp3/m4a) to PCM is the host's job
//! (the desktop client already ships FFmpeg). The local engine accepts
//! 16 kHz mono 16-bit PCM WAV only, and says so loudly.

#[cfg(feature = "local")]
use crate::types::TranscriptError;

/// Best-effort audio MIME type. Returns `None` for video containers and
/// unknown suffixes; callers route those to a capable provider.
pub fn infer_audio_mime(filename: &str, mime: Option<&str>) -> Option<String> {
    if let Some(value) = mime
        && value.starts_with("audio/")
    {
        return Some(value.to_string());
    }
    match filename
        .rsplit_once('.')
        .map(|(_, suffix)| suffix.to_ascii_lowercase())
    {
        Some(suffix) if suffix == "ogg" || suffix == "oga" => Some("audio/ogg".to_string()),
        Some(suffix) if suffix == "opus" => Some("audio/opus".to_string()),
        Some(suffix) if suffix == "mp3" => Some("audio/mp3".to_string()),
        Some(suffix) if suffix == "m4a" => Some("audio/m4a".to_string()),
        Some(suffix) if suffix == "wav" => Some("audio/wav".to_string()),
        Some(suffix) if suffix == "flac" => Some("audio/flac".to_string()),
        Some(suffix) if suffix == "webm" => Some("audio/webm".to_string()),
        _ => None,
    }
}

/// Load 16 kHz mono 16-bit PCM WAV into f32 samples for whisper.cpp.
#[cfg(feature = "local")]
pub fn read_wav_mono16_f32(path: &std::path::Path) -> Result<Vec<f32>, TranscriptError> {
    let reader = hound::WavReader::open(path).map_err(|e| TranscriptError::Io(e.to_string()))?;
    let spec = reader.spec();
    if spec.channels != 1
        || spec.sample_rate != 16000
        || spec.bits_per_sample != 16
        || spec.sample_format != hound::SampleFormat::Int
    {
        return Err(TranscriptError::Unsupported(format!(
            "local engine needs 16 kHz mono 16-bit PCM WAV, got {spec:?}"
        )));
    }
    let samples: Result<Vec<i16>, _> = reader.into_samples::<i16>().collect();
    let samples = samples.map_err(|e| TranscriptError::Io(e.to_string()))?;
    let mut float = vec![0.0f32; samples.len()];
    whisper_rs::convert_integer_to_float_audio(&samples, &mut float)
        .map_err(|e| TranscriptError::Model(format!("pcm convert: {e:?}")))?;
    Ok(float)
}
