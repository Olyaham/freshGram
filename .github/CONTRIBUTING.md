# Contributing

This document describes how you can contribute to freshGram.

## What contributions are accepted

Bug fixes, optimizations, documentation improvements and new features are welcome.
Push to your fork and [submit a pull request][pr].

freshGram is built from three sources: [Telegram Desktop][tdesktop] as the base,
[AyuGram Desktop][ayugram] for the functionality and [materialgram][materialgram]
for the Material Design look. If a problem is not specific to freshGram, consider
reporting it to the corresponding upstream project too.

## Build instructions

See the [folder with instructions][build_instructions] for details on the various
build environments.

## Pull upstream changes into your fork regularly

To pull in upstream changes:

    git remote add upstream https://github.com/Snowy-Fluffy/freshGram.git
    git fetch upstream

Check the log to be sure that you actually want the changes before merging.

## How to get your pull request accepted

* Keep your pull request limited to a single issue.
* Squash your commits to a single commit.
* Do not mix code changes with whitespace cleanup.
* Keep your code simple and follow the style of the surrounding code.
* Test your changes.
* Write a good commit message: a short summary line, an empty line, then details.

[//]: # (LINKS)
[pr]: https://help.github.com/articles/using-pull-requests/
[tdesktop]: https://github.com/telegramdesktop/tdesktop
[ayugram]: https://github.com/AyuGram/AyuGramDesktop
[materialgram]: https://github.com/kukuruzka165/materialgram
[build_instructions]: https://github.com/Snowy-Fluffy/freshGram/tree/HEAD/docs
