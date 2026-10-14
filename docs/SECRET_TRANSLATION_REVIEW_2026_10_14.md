# Secret-chat translation review — 2026-10-14

This implementation and its regression checks were AI generated.

Nagram, Nagram XF and Extraless translation-sheet and provider code was reviewed
before implementation. Their sheet/request boundary is used as the design
reference; the desktop consent policy is explicit rather than assuming a mobile
provider can safely receive secret-chat plaintext.

## Behavior

- All eight message-menu entry points call `ShowTranslateBox()`.
- The precise `IsSecretPeerId()` predicate triggers a Cancel/Translate warning
  naming the active provider before the translation box or provider is created.
- Google, Yandex, Telegram, custom URL templates and the system translator all
  pass through the same boundary. Native unavailability resolves to Telegram.
- Cancel/dismiss makes no request. Translate opens the box and starts translation.
- Each new manual invocation requires fresh consent.
- Changing the provider or custom URL while a warning is open requires a new
  warning before any request, including changes between identically named custom
  servers. The callback follows the controller's lifetime.
- Secret whole-chat auto-translation clears its queue and returns before rich
  translation or provider dispatch. It remains disabled after manual consent.

## Executable checks

```sh
python3 -m unittest tools.tests.test_secret_translation -v
```

The native request-spy test executes production provider resolution, naming,
request preparation, consent callback and the auto-translation guard. It covers
all provider/availability/custom-override combinations, Cancel, repeated consent,
provider and URL changes, local message text and ordinary user/channel ids that
could be mistaken for secret ids by an imprecise mask. A source inventory checks
that the eight message call sites route through consent.

Provider factories, UI controls and outbound requests are isolated host adapters.
This proves consent-boundary dispatch ordering, not successful requests to live
translation services or full authenticated UI behavior. The independent
`Translation consent checks` workflow runs these checks in CI.
