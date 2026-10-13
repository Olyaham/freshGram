//! Таймкоды сегментов и глав: `m:ss` наружу, секунды внутрь.

/// Секунды в `m:ss` для промптов и логов.
pub fn format_timestamp(seconds: f32) -> String {
    let seconds = seconds.max(0.0).round() as u32;
    format!("{}:{:02}", seconds / 60, seconds % 60)
}

/// Разбор `s`, `m:ss` и `h:mm:ss` из cleanup-ответа.
pub fn parse_timestamp(value: &str) -> Option<f32> {
    let parts = value.split(':').collect::<Vec<_>>();
    let seconds = match parts.as_slice() {
        [seconds] => seconds.parse::<f32>().ok(),
        [minutes, seconds] => {
            let minutes = minutes.parse::<f32>().ok()?;
            let seconds = seconds.parse::<f32>().ok()?;
            Some(minutes * 60.0 + seconds)
        }
        [hours, minutes, seconds] => {
            let hours = hours.parse::<f32>().ok()?;
            let minutes = minutes.parse::<f32>().ok()?;
            let seconds = seconds.parse::<f32>().ok()?;
            Some(hours * 3600.0 + minutes * 60.0 + seconds)
        }
        _ => None,
    }?;
    (seconds.is_finite() && (0.0..=86400.0).contains(&seconds)).then_some(seconds)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn formats_and_parses_round_trip() {
        assert_eq!(format_timestamp(65.0), "1:05");
        assert_eq!(format_timestamp(5.0), "0:05");
        assert_eq!(parse_timestamp("1:10"), Some(70.0));
        assert_eq!(parse_timestamp("1:02:03"), Some(3723.0));
        assert_eq!(parse_timestamp("мусор"), None);
    }
}
