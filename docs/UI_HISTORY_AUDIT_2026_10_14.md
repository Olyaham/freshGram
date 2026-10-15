# UI and theme patch-history audit — 2026-10-14

## Baselines and mechanical inventory

The stock application baseline is Telegram Desktop `dev-7.3.1`, `6fed91ffab`.
The stock UI library is `desktop-app/lib_ui` `dev-7.3.1`,
`91ff4463899f23f4227bc4cbbdbe7696d479b9bd`. The stock code generator is the
Telegram baseline's pinned `desktop-app/codegen` commit,
`dbd1d53137cd581cfdbeebe93bcaf54abf55ec74`. Upstream freshGram main is a fork
baseline, not a substitute for these stock revisions.

The mechanical application inventory collected at downstream `f27d5ae565`
contains 418 modified existing stock SourceFiles, 1,787 diff hunks and 349 visual
candidates in 156 files. The library comparison covers 432 stock files, 75
differing files and 32 changed source/resource-definition files. These candidate
counts are search coverage, not counts of confirmed mandatory UI changes.

Evidence is stored outside the checkout in `/tmp/opencode/freshgram-ui-audit`:
the source/library inventories, `visual-hunks.json`, stock diffs, downloaded
GitHub patches and origin-snapshot comparisons. Private profiles and screenshots
are outside the repository.

## Verified priority findings

| Behavior | Introducing history / GitHub patch | Current conclusion |
| --- | --- | --- |
| Global accent hue matching | [f5e95edadc](https://github.com/Snowy-Fluffy/freshGram/commit/f5e95edadc) | `hueThreshold` changed from 15 to 35 in the embedded-theme colorizer. Restoring palette files alone does not restore this policy. |
| Night accent lightness | [3a2867a7ce](https://github.com/Snowy-Fluffy/freshGram/commit/3a2867a7ce) | Night `lightnessMin` changed from 64 to 115. The current policy still differs from stock. |
| Night contrast preservation | [e2d9f451e9](https://github.com/Snowy-Fluffy/freshGram/commit/e2d9f451e9) | Removed Night `keepContrast` entries. This is distinct from stock codegen's unchanged contrast rules. |
| Default generated wallpaper fallback | [e2d9f451e9](https://github.com/Snowy-Fluffy/freshGram/commit/e2d9f451e9) | Replaces `QColor(213, 223, 233)` with white. The fallback is used only when a background generator receives no colors; it is not evidence that every chat background becomes white. Several callers guard against empty colors or supply black explicitly. |
| Reaction background and opacity | [6c3d2cfb15](https://github.com/Snowy-Fluffy/freshGram/commit/6c3d2cfb15) | The fork's tint/opacity path is still mandatory. The opacity slider is not a stock-renderer selector. |
| Incoming reaction author color | [6e3738a56b](https://github.com/Snowy-Fluffy/freshGram/commit/6e3738a56b) | Uses the incoming author's color; disabling the chosen-reaction mark does not disable this tinting. |
| Mention/reaction/poll dialog badges | [d252f27368](https://github.com/Snowy-Fluffy/freshGram/commit/d252f27368) | Forces `muted=false` for these badges, changing their stock muted palette selection. |
| Floating navigation/unread buttons and recording lock | [28ae30b7e2](https://github.com/Snowy-Fluffy/freshGram/commit/28ae30b7e2) | Replaces color roles with `sideBarBg` / `windowBgActive`. Existing generated design variants cover geometry, not these role substitutions. |
| Story unread gradient | [8f043beff3](https://github.com/Snowy-Fluffy/freshGram/commit/8f043beff3) | Changes stock `groupCallLive1 → groupCallMuted1` to `groupCallLive1 → groupCallLive1`. |
| Chosen-reaction outline and checkmark | Downstream `72ce736f34`, then `c37e17c9d0` / `f27d5ae565` | The downstream stock-restoration patch introduced the selected outline/checkmark behavior in this branch. It is now gated by `showChosenReactionMark`, default false; layout width is gated with painting. |
| Service-message time badge | Downstream `c37e17c9d0` | Gated by `showServiceTimeBadge`, retaining its true default. |

GitHub/local patch comparisons confirmed the relevant historical changes.
Initial raw comparisons for some patches differed in binary representations or
descriptive hunk context; those differences alone are not evidence of a
substantive source mismatch.

## UI-library provenance recovered from submodule history

`git log --follow` alone did not expose the imported library's history. Running
`--full-history` identifies `d7e2dea9c7` and `d6f090fadd` in the upstream tree,
followed by the downstream preservation/rebase commits. The first merge combines
an AyuGram parent with a Materialgram parent. The Materialgram parent's
`.gitmodules` points at `KUKURUZKA165/lib_ui`, pinned to
`7c1186438af89e3b3b9cd46ab3b35324ea54906a`.

The three imported files below match that GitHub submodule snapshot exactly at
`d7e2dea9c7`. This establishes provenance before the downstream 7.3.1 rebase.

| Behavior | Original library patches | Snapshot/current comparison |
| --- | --- | --- |
| Radial/noisy button ripple | [e0b7e42578](https://github.com/KUKURUZKA165/lib_ui/commit/e0b7e42578), [af72a3b828](https://github.com/KUKURUZKA165/lib_ui/commit/af72a3b828) | The current `ripple_animation.cpp` still matches the imported snapshot exactly. The renderer applies radial edges and a noise layer independently of the design selector. |
| Active folder capsule | [20f652731c](https://github.com/KUKURUZKA165/lib_ui/commit/20f652731c), [d813191791](https://github.com/KUKURUZKA165/lib_ui/commit/d813191791) | Replaces the full active background with an icon capsule, then widens its padding. The current file differs from the imported snapshot only in accessibility-role updates; the unconditional painting change remains. |
| Spoiler descriptors | [29666b5210](https://github.com/KUKURUZKA165/lib_ui/commit/29666b5210), [ebd7f926c9](https://github.com/KUKURUZKA165/lib_ui/commit/ebd7f926c9) | Changes fade durations, particle counts/speeds and image canvas size. Current differences from the import add lifecycle/cache handling and an optional static-spoiler path; the altered animation descriptors remain. The later particle-size change returns those sizes to the stock values, so particle size alone is not an outstanding forced difference. |

The six original patches and raw snapshot comparisons are captured in
`lib-ui-origin-history.json` and `gh-lib-ui-*.patch`.

Other remaining library candidates include `RoundButton::resizeToText()` ignoring
the ToUpper transformation, scale default 100→110 and animation timings. Their
precise introducing history and runtime settings coverage still require review.

## Corrections to earlier candidates

1. **Generated contrast is stock behavior.** The stock pinned codegen already
   defines `kMustBeContrast` for `dialogsMenuIconFg/dialogsBg` and
   `windowBoldFg/windowBg`, and emits the same `EnsureContrast` calls. The pinned
   AyuGram codegen also contains them. Their presence is not a new forced fork
   color patch. The complete stock/current generator diff is retained as
   `codegen-stock-7.3.1.patch`.
2. **Many metrics are already optional.** `kDesignVariants` supplies Telegram-mode
   values for message padding, scrollbars, online badges, video-message size,
   button/menu radii, ripple areas and settings padding. A `.style` difference
   without inspecting generated/runtime selection is insufficient to classify
   it as mandatory.
3. **The rounded story-outline limitation is inherited.** Stock
   `PaintOutlineSegments(rect, radius, segments)` already has the unconditional
   single-outline branch. The fork's additional noncircular-avatar routing is
   a separate difference; the branch itself is not a newly introduced bug.
4. **The stock story gradient's second role is `groupCallMuted1`.** Earlier
   identification as `groupCallLive2` was incorrect.
5. **The chosen mark and reaction tint are separate policies.** Turning off
   the mark can remove the outline/checkmark without restoring stock reaction
   backgrounds or author-color behavior.

## Fresh upstream and remaining acceptance work

The 31 new commits through upstream `76f06bcb7e` do not fix the confirmed global
colorizer, reaction tinting, badge muted-state, floating-button or story-gradient
differences. The patch-level review is retained with the external audit evidence.

The mechanical inventory is broader than the verified table above. Remaining
candidates need historical attribution and generated/runtime gate checks before
claiming a complete forced-UI inventory. Further optionalization must preserve
the established defaults except for the separately agreed chosen-reaction mark,
and coordinate painting, layout, hit testing, cached images and palette/style
invalidation. Mobile-reference review and user-facing conclusions precede each
new feature implementation.

Existing visual evidence demonstrates the settings and shortened design labels.
The saved real profile has `showChosenReactionMark=false`; appearance of actual
reaction badges under the rebuilt mark-off client is still an acceptance item.
