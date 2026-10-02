# Roadmap

## In progress

- Stability of viewing and saving deleted messages: database access, media without text, self-destructing media and background caching of media are done, manual testing is pending.

## Planned

1. Weekly workflow that tries to merge `upstream/dev`, runs `Build` and opens an issue on failure.
2. Smoke test of the built binary in CI (`--version` or a short run under `xvfb`).
3. Fast `gcc -fsyntax-only` check of changed files before the full build.
4. Unit tests for the AyuGram logic that does not depend on UI (storage, mapping, formatting).
5. Explicit schema versioning and safe migrations of the local AyuGram database.
6. Split `ayu_settings.cpp` and `telegram_helpers.cpp` by topic.
7. Remove the dead `unsupportedTTL()` field and the code that reads it.

## Later

- Release signing and attestations.
- AppImage or Flatpak for Linux.
- Update notification based on the GitHub releases API.
- Russian translation of the AyuGram strings.
