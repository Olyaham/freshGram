# Roadmap

## In progress

- Stability of viewing and saving deleted messages: database access, media without text, self-destructing media, background caching of media and restoring deleted messages in chats after a restart are done, manual testing is pending.

## Planned

1. Weekly workflow that tries to merge `upstream/dev`, runs `Build` and opens an issue on failure.
2. Smoke test of the built binary in CI (`--version` or a short run under `xvfb`).
3. Fast `gcc -fsyntax-only` check of changed files before the full build.
4. Unit tests for the AyuGram logic that does not depend on UI (storage, mapping, formatting).
5. Explicit schema versioning and safe migrations of the local AyuGram database.
6. Split `ayu_settings.cpp` and `telegram_helpers.cpp` by topic.
7. Remove the dead `unsupportedTTL()` field and the code that reads it.
8. Keep the link to the original message when restoring deleted replies and forwards in chats after a restart.
9. Restore deleted messages in forums (chats with topics) after a restart.
10. Show deleted messages in the regular chat search and in the shared media tabs.
11. Kept chats: restore the senders' names and userpics in restored supergroups, keep legacy groups after being removed, and show an explicit "removed" status on a kept chat.
12. Make the rest of the materialgram look (paddings, sizes, Google Sans font) switchable together with the icons; for now only the icons, message rounding and tails, and the reply background are switchable.

## Later

- Release signing and attestations.
- AppImage or Flatpak for Linux.
- Update notification based on the GitHub releases API.
- Russian translation of the AyuGram strings.
