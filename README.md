# freshGram

[ English | [Русский](README-RU.md) ]

freshGram is a [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) fork that combines
the Material Design look and customization of [materialgram](https://github.com/kukuruzka165/materialgram)
with all the features of [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop).
It is based on the [Telegram API][telegram_api] and the [MTProto][telegram_proto] secure protocol.

The source code is published under GPLv3 with OpenSSL exception, the license is available [here][license].

## Features

### From AyuGram Desktop

- Full ghost mode (flexible)
- Messages history
- Anti-recall
- Font customization
- Streamer mode
- Local Telegram Premium
- Translator
- Media preview and quick reaction on force click (macOS)
- Enhanced appearance
- App icon picker

See the [AyuGram documentation](https://docs.ayugram.one/desktop/) for the full description of these features.

### From materialgram

- Own Material You themes (Google Day and Google Dark, applied on the first launch)
- **Google Sans** font everywhere (except for Arabic characters, they use the **Vazirmatn** font)
- Material icons instead of default ones
- Removed message bubble tails and reverted old paddings (enabled by default, can be turned off in the AyuGram chat settings)
- Removed "large emoji" outline
- Reduced use of uppercase in the interface
- Ability to seek round videos
- Ability to delete more than 100 messages at once
- Ability to copy the sticker set author's id and increment
- Ability to mention multiple users at once with right click
- Added admin menu and chat log buttons above the members list
- Copy usernames as @example if possible
- Use photos from @gamee in profile photo list (optional)
- Removed delay when recording voice messages
- Webview platform is reported as "android" (enabled by default, can be turned off in the AyuGram settings)
- Replaced all sounds
- Reduced jpeg compression (94-95% on photos, 100% on wallpapers)
- Reduced minimum window size and minimum brush thickness in the photo editor
- Reduced some timeouts (like when opening a chat preview)
- Increased upload speed
- Improved spoiler animation
- Improved sticker pack menu
- Improved chat export (10000 messages in one html document and faster file downloads)
- Improved voice messages bitrate
- Hide your phone number in profile and settings
- Show more recent stickers (unlimited by default)
- Show the approximate date of account creation and the datacenter in profile (optional)
- Show photo/file datacenter and original date
- Show photo platform in media viewer
- Show more info for unique gifts

## Default values

Where AyuGram and materialgram solve the same task, freshGram keeps the AyuGram implementation
and uses the materialgram look as the default value of the corresponding AyuGram setting
(for example, message tails are removed and unlimited recent stickers are enabled by default).

## Build instructions

* [Windows 64-bit](docs/building-win-x64.md)
* [macOS](docs/building-mac.md)
* [GNU/Linux using Docker](docs/building-linux.md)

freshGram needs your own Telegram `api_id` and `api_hash`, see [API credentials](docs/api_credentials.md).
Autoupdate is disabled by default because freshGram has no update server.

### GitHub Actions

1. Add the repository secrets `API_ID` and `API_HASH`.
2. Run the `Build environment` workflow once, it builds the library image and pushes it to GitHub Packages.
3. The `Build` workflow builds Linux x86_64 on every push and pull request.
   Run it manually with `release_tag` (for example `v7.2.10`) or push a `v*` tag to publish a release.

See [docs/upstream-merge.md](docs/upstream-merge.md) for the upstream merge procedure.

## Credits

### Telegram clients

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)
- [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop)
- [materialgram](https://github.com/kukuruzka165/materialgram)
- [Kotatogram](https://github.com/kotatogram/kotatogram-desktop)
- [64Gram](https://github.com/TDesktop-x64/tdesktop)
- [Forkgram](https://github.com/forkgram/tdesktop)

### Libraries used

- [JSON for Modern C++](https://github.com/nlohmann/json)
- [SQLite](https://github.com/sqlite/sqlite)
- [sqlite_orm](https://github.com/fnc12/sqlite_orm)
- [androidx sources](https://github.com/androidx/androidx)
- **Qt 6**, **OpenSSL**, **WebRTC**, **FFmpeg**, **Opus**, **OpenAL Soft** and the other libraries listed in the Telegram Desktop repository
- **Vazirmatn font** ([SIL Open Font License 1.1](https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt))

### Icons

- [Solar Icon Set](https://www.figma.com/community/file/1166831539721848736)

[//]: # (LINKS)
[telegram_api]: https://core.telegram.org
[telegram_proto]: https://core.telegram.org/mtproto
[license]: LICENSE
