# Merging upstream Telegram Desktop

freshGram keeps all of its own logic in `Telegram/SourceFiles/ayu/`. Files outside that
directory contain only short hooks: an include, a call into `ayu/`, or a condition.

Measure the current footprint:

```bash
python3 Telegram/build/ayu_touchpoints.py 20
```

## Rules for changes outside `ayu/`

- Put logic into `ayu/` and leave a single call or condition at the touchpoint.
- Do not rename upstream symbols, add `Ayu`-prefixed variants next to them instead.
- Do not reformat or reorder upstream code around a hook.
- Do not add comments, the hook name and the `ayu/` namespace already say what it is.

## Merge procedure

```bash
git remote add upstream https://github.com/telegramdesktop/tdesktop.git
git fetch upstream dev
git merge upstream/dev
```

1. Resolve conflicts keeping the upstream structure and re-applying the hooks.
2. Update submodules to the revisions recorded by the merge. `lib_ui`, `lib_tl`,
   `codegen` and `lib_icu` are vendored, so merge their upstream changes by hand.
3. Run `python3 Telegram/build/ayu_touchpoints.py` and compare with the previous
   numbers, a sudden growth means logic leaked out of `ayu/`.
4. Push the branch and wait for the `Build` workflow.
