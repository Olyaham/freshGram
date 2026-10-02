# freshGram

[ [English](README.md) | Русский ]

freshGram — форк [Telegram Desktop](https://github.com/telegramdesktop/tdesktop), который объединяет
оформление и кастомизацию в стиле Material Design из [materialgram](https://github.com/kukuruzka165/materialgram)
со всеми возможностями [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop).
Работает на основе [Telegram API][telegram_api] и защищённого протокола [MTProto][telegram_proto].

Исходный код опубликован под лицензией GPLv3 с исключением для OpenSSL, текст лицензии находится [здесь][license].

## Возможности

### Из AyuGram Desktop

- Полный режим призрака (гибко настраивается)
- История сообщений
- Защита от удаления сообщений (anti-recall)
- Настройка шрифтов
- Режим стримера
- Локальный Telegram Premium
- Переводчик
- Предпросмотр медиа и быстрая реакция по force click (macOS)
- Улучшенное оформление
- Выбор иконки приложения

Подробное описание этих функций смотрите в [документации AyuGram](https://docs.ayugram.one/desktop/).

### Из materialgram

- Собственные темы Material You (Google Day и Google Dark, применяются при первом запуске)
- Шрифт **Google Sans** везде (кроме арабских символов, для них используется **Vazirmatn**)
- Material-иконки вместо стандартных
- Убраны «хвостики» у пузырей сообщений и возвращены старые отступы (включено по умолчанию, отключается в настройках чатов AyuGram)
- Убрана обводка у «больших эмодзи»
- Меньше заглавных букв в интерфейсе
- Перемотка круглых видео
- Удаление более 100 сообщений за раз
- Копирование ID автора набора стикеров и инкремента
- Упоминание нескольких пользователей сразу правым кликом
- Кнопки меню администраторов и журнала действий над списком участников
- Копирование юзернеймов в виде @example
- Фото из @gamee в списке фото профиля (опционально)
- Убрана задержка при записи голосовых сообщений
- Платформа веб-приложений сообщается как «android» (включено по умолчанию, отключается в настройках AyuGram)
- Заменены все звуки
- Меньшее сжатие JPEG (94–95% для фото, 100% для обоев)
- Уменьшены минимальный размер окна и минимальная толщина кисти в редакторе фото
- Уменьшены некоторые задержки (например, при открытии предпросмотра чата)
- Увеличена скорость загрузки
- Улучшена анимация спойлера
- Улучшено меню набора стикеров
- Улучшен экспорт чатов (10000 сообщений в одном html-документе и более быстрая загрузка файлов)
- Улучшен битрейт голосовых сообщений
- Скрытие номера телефона в профиле и настройках
- Больше недавних стикеров (по умолчанию без ограничения)
- Примерная дата создания аккаунта и датацентр в профиле (опционально)
- Датацентр и исходная дата фото/файла
- Платформа, с которой отправлено фото, в просмотрщике медиа
- Больше информации об уникальных подарках

## Значения по умолчанию

Там, где AyuGram и materialgram решают одну и ту же задачу, freshGram использует реализацию AyuGram,
а внешний вид materialgram задаёт как значение по умолчанию соответствующей настройки AyuGram
(например, «хвостики» сообщений убраны, а недавние стикеры не ограничены).

## Сборка

* [Windows 64-bit](docs/building-win-x64.md)
* [macOS](docs/building-mac.md)
* [GNU/Linux через Docker](docs/building-linux.md)

Для сборки нужны ваши собственные `api_id` и `api_hash` Telegram, см. [API credentials](docs/api_credentials.md).
Автообновление по умолчанию отключено, так как у freshGram нет сервера обновлений.

## Благодарности

### Telegram-клиенты

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)
- [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop)
- [materialgram](https://github.com/kukuruzka165/materialgram)
- [Kotatogram](https://github.com/kotatogram/kotatogram-desktop)
- [64Gram](https://github.com/TDesktop-x64/tdesktop)
- [Forkgram](https://github.com/forkgram/tdesktop)

### Используемые библиотеки

- [JSON for Modern C++](https://github.com/nlohmann/json)
- [SQLite](https://github.com/sqlite/sqlite)
- [sqlite_orm](https://github.com/fnc12/sqlite_orm)
- [androidx sources](https://github.com/androidx/androidx)
- **Qt 6**, **OpenSSL**, **WebRTC**, **FFmpeg**, **Opus**, **OpenAL Soft** и другие библиотеки из репозитория Telegram Desktop
- Шрифт **Vazirmatn** ([SIL Open Font License 1.1](https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt))

### Иконки

- [Solar Icon Set](https://www.figma.com/community/file/1166831539721848736)

[//]: # (LINKS)
[telegram_api]: https://core.telegram.org
[telegram_proto]: https://core.telegram.org/mtproto
[license]: LICENSE
