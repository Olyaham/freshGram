# Product naming

`branding.json` defines the display name, source aliases and first-party paths
handled by `tools/rebrand.py`. The script normalizes product names in code,
localization keys, log labels, resource references, packaging and filenames.
It handles display, identifier, lowercase and uppercase variants.

```sh
python3 tools/rebrand.py --diff
python3 tools/rebrand.py --apply
python3 tools/rebrand.py --check
python3 tools/tests/test_rebrand.py
```

The default action is `--check`, with exit status 1 when replacements are needed.
`--diff` previews without writing. `--apply` preserves executable permissions and
line endings, rejects filename collisions and verifies each source still matches
the preview before replacing it. Source files are replaced atomically.
Only Git-listed tracked or non-ignored untracked files in the configured paths
are considered; build outputs, profiles, third-party libraries and symlink
targets are excluded. Attribution and the explicitly listed legacy-reader
strings remain intact.

For a subsequent naming change, supply the existing brand and the new name:

```sh
python3 tools/rebrand.py --from-name freshGram --name NewGram --diff
```

Update the configured `name` and application identity paths when adopting a new
brand. The legacy readers are explicit compatibility entries, not active product
names. They must retain the old spellings until the corresponding migration is
retired.

## Existing profiles

New settings writes use `tdata/freshgram_settings.json`. An existing file takes
precedence; otherwise the client reads `tdata/ayu_settings.json` and writes the
validated settings atomically under the new name with owner-only permissions.
The legacy file is retained. The early
debug-logging reader uses the same selection rule.

The standard download folder uses the application name. A saved path exactly
matching a former default folder (`AyuGram`, `AyuGram Desktop` or
`AyuGramDesktop` directly under the standard Downloads directory) is interpreted
as the current default. Other custom paths and existing downloaded files are
preserved. Generated Windows icon files and freshGram shortcuts use freshGram
names.

Old built-in `Google Day` and `Google Dark` theme paths are interpreted as the
stock Telegram Day and Tinted themes. Their old palette cache and saved accent
are discarded during loading. This transition also restores Telegram layout,
icons, switches and bubbles once for the legacy preset. FreshGram light/dark
themes now have separate resource identities and are selected explicitly.
Custom/cloud themes are handled through the regular theme loader.
