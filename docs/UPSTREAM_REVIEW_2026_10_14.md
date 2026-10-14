# Fresh upstream and platform failure review — 2026-10-14

## Checked range and evidence

Fetched `Snowy-Fluffy/freshGram` main from `239779b690` to `76f06bcb7e`, covering
31 new commits. Nine relevant GitHub commit patches were downloaded through the
GitHub API and compared with local git patches. Their per-file added/removed
lines all match; descriptive hunk headers and email envelopes are excluded.

The local patch evidence is outside the repository in
`/tmp/opencode/freshgram-ui-audit/fresh-upstream-20261014.json` and
`gh-fresh-*.patch`.

## Relevant changes

| Upstream patch | Result of review |
| --- | --- |
| [1846ad263b](https://github.com/Snowy-Fluffy/freshGram/commit/1846ad263b) — retry failed macOS libraries on a fresh runner | Relevant to the observed runner/archive failure. It retries only a failed `macOS libraries` job, limits attempts and skips stale push revisions. Its `workflow_run` listener requires installation on the repository's default branch, which is `main`, rather than only the downstream branch. |
| [9d6437fdec](https://github.com/Snowy-Fluffy/freshGram/commit/9d6437fdec) — Windows `near` macro collision | Equivalent issue was already corrected downstream by `2b0a984d4d`. |
| [8aa095c1ec](https://github.com/Snowy-Fluffy/freshGram/commit/8aa095c1ec), [f3c56931fc](https://github.com/Snowy-Fluffy/freshGram/commit/f3c56931fc) — Tesseract MSVC headers and instruction macros | Relevant OCR/platform fixes. The latest downstream Windows failure is in the application settings code, after its library preparation, so these patches do not explain that failure. |
| [49b40cee45](https://github.com/Snowy-Fluffy/freshGram/commit/49b40cee45) — recognition model integrity during loading | Adds load-time model checks and sealed manifest caching. Useful follow-up for model loading; the existing downstream model verification is principally download verification. Integration needs to preserve the AiApi adapter's model-loading boundaries. |
| [f2581d5068](https://github.com/Snowy-Fluffy/freshGram/commit/f2581d5068) — sealed account settings and settings-size limit | Adds separate account-data persistence and startup attachment. Requires integration with downstream's settings names and account lifecycle. |
| [5810e5af76](https://github.com/Snowy-Fluffy/freshGram/commit/5810e5af76) — deleted-message editing, preferences description and font fixes | The preferences change is the description row's margin, not the clipped design-choice labels. The font patch specifically restores Google Sans Medium behavior; downstream already restored stock Telegram font assets and selection. |
| [8e6a7e0def](https://github.com/Snowy-Fluffy/freshGram/commit/8e6a7e0def) — large saved/deleted media | Avoids passing large media values to the memory cache and prepares an opened copy asynchronously. Relevant to saved-media crashes; distinct from the downstream secret-media path. The patch's async document/session lifetime requires review during integration. |
| [96546b4500](https://github.com/Snowy-Fluffy/freshGram/commit/96546b4500) — topics-count localization | Fixes the placeholder introduced by the new topic-scoped delete-own-messages feature. The downstream branch does not contain that feature or broken key. |

The remaining commits primarily add compose stashing, deletion/search behavior,
material bubble rounding, versioning and debug packaging.

## PFS and reported color failures

The new upstream range changes none of `ayu/secret`; upstream still derives
`info.keyHash` from the current working key. The downstream correction and
regression evidence are documented in `PFS_VISUAL_HASH_REVIEW_2026_10_14.md`.

No fixes were added to the global theme colorizer, reaction renderer, floating
button color roles, story outline gradient or generated-style contrast policy.
The range changes `dialogs_layout.cpp` only to mark deleted items in fake search
rows with a trash icon; it does not restore muted mention/reaction/poll colors.
The historical UI/theme audit therefore remains relevant.

## Finished downstream CI and local correction

All three client runs for `63fe61ed8a` completed before further publication:

| Platform | Run | Finding |
| --- | --- | --- |
| Linux | [38564961141](https://github.com/Olyaham/freshGram/actions/runs/38564961141) | Success. |
| Windows | [38564960939](https://github.com/Olyaham/freshGram/actions/runs/38564960939) | Compile failure: `QJsonValue::toInteger()` is unavailable in Qt 5, in the provider-model metadata labels in `settings_general.cpp`. The subsequent uninitialized-variable/translation-call errors are cascading errors. |
| macOS | [38564960905](https://github.com/Olyaham/freshGram/actions/runs/38564960905) | `macOS libraries` failed during archive preparation; `ranlib` reports `can't write to output file (Input/output error)`. App jobs were skipped. The saved log does not establish a client-source compile failure or prove that disk exhaustion caused the I/O failure. |

The Windows expressions now use `QJsonValue::toVariant().toLongLong()`, available
in both Qt 5 and Qt 6. A standalone check compiled the actual old expressions
against Qt 5 and reproduced the missing-method error. The replacement production
expressions compiled and ran with Qt 5.15.19 and Qt 6.11.2, including metadata
above the 32-bit range. This validates the expression-level compatibility fix;
new platform CI is still required for the complete application.
