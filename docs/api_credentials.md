## API credentials

freshGram needs its own Telegram API credentials to connect to Telegram.

1. Log in to [my.telegram.org](https://my.telegram.org) with your phone number.
2. Open **API development tools** and create an application.
3. Copy the `api_id` and `api_hash` values.

Pass them to the build as `-D TDESKTOP_API_ID=YOUR_API_ID` and `-D TDESKTOP_API_HASH=YOUR_API_HASH`.

Keep the `api_hash` private and do not commit it to the repository.
