//! Audio upload MIME inference, prioritizing explicit types over suffixes.
//! Video requires a separate host routing decision.

pub fn infer_audio_mime(filename: &str, mime_type: Option<&str>) -> Option<String> {
    if mime_type.is_some_and(|value| value.starts_with("audio/")) {
        return mime_type.map(ToOwned::to_owned);
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn infers_suffix_and_rejects_video() {
        assert_eq!(
            infer_audio_mime("voice.oga", None).as_deref(),
            Some("audio/ogg")
        );
        assert_eq!(infer_audio_mime("note.mp4", Some("video/mp4")), None);
        assert_eq!(
            infer_audio_mime("audio.bin", Some("audio/mpeg")).as_deref(),
            Some("audio/mpeg")
        );
    }
}
