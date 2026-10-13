use crate::types::Transcript;

pub fn has_speech(transcript: &Transcript) -> bool {
    meaningful(&transcript.text)
        || transcript
            .segments
            .iter()
            .any(|segment| meaningful(&segment.text))
}

fn meaningful(text: &str) -> bool {
    let text = text.trim();
    if !text.chars().any(char::is_alphanumeric) {
        return false;
    }
    let marked = (text.starts_with('[') && text.ends_with(']'))
        || (text.starts_with('(') && text.ends_with(')'));
    if !marked {
        return true;
    }
    let normalized = text
        .to_lowercase()
        .chars()
        .map(|ch| if ch.is_alphanumeric() { ch } else { ' ' })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ");
    !matches!(
        normalized.as_str(),
        "silence"
            | "no speech"
            | "music"
            | "background music"
            | "тишина"
            | "нет речи"
            | "музыка"
            | "аплодисменты"
            | "laughter"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_non_speech_markers_hide_text_and_real_segments_win() {
        let mut transcript = Transcript {
            provider: "mock".into(),
            model: "mock".into(),
            request_id: None,
            text: "[Silence]".into(),
            segments: Vec::new(),
        };
        assert!(!has_speech(&transcript));
        for text in [
            "Музыка",
            "[Музыка мне нравится]",
            "Спасибо за просмотр",
            "I",
        ] {
            transcript.text = text.into();
            assert!(has_speech(&transcript));
        }
        transcript.text = "[Silence]".into();
        transcript.segments.push(crate::types::AsrSegment {
            start_sec: 0.0,
            end_sec: 1.0,
            text: "hello".into(),
        });
        assert!(has_speech(&transcript));
    }
}
