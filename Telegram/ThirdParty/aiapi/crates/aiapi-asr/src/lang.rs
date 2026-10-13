//! Normalize host language settings to provider BCP-47 codes.

/// An empty language lets the provider detect it.
pub fn bcp47_codes(language: &str) -> Vec<String> {
    let language = language.trim();
    if language.is_empty() || language.eq_ignore_ascii_case("auto") {
        return Vec::new();
    }
    let code = match language.to_ascii_lowercase().as_str() {
        "ru" => "ru-RU",
        "en" => "en-US",
        "uk" => "uk-UA",
        "be" => "be-BY",
        _ => language,
    };
    vec![code.to_string()]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn maps_project_language_to_bcp47() {
        assert_eq!(bcp47_codes("ru"), vec!["ru-RU"]);
        assert_eq!(bcp47_codes(""), Vec::<String>::new());
        assert_eq!(bcp47_codes("en-US"), vec!["en-US"]);
    }
}
