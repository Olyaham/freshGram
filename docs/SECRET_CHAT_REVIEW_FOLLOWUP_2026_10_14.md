# Secret-chat review follow-up — 2026-10-14

This implementation and its regression checks were AI generated and reviewed
through direct source inspection and executable component checks.

## Reference review

Nagram, Nagram XF and Extraless `SecretChatHelper` implementations were inspected
before these changes. Their key epochs use real installation time, counters
follow actual traffic, key changes reset counters, and visual authentication
hashes survive PFS. Their incoming-chat lifecycle does not interpret leaving a
screen as an explicit rejection. Nagram XF is the primary UI reference.

## Maintainer requests addressed in code

- A failed inbound DH request releases only its own empty reservation and saves
  the repaired state. A stale failure cannot erase another exchange or a prepared
  pending key.
- A restored exchange without a pending key is dropped and resaved at startup.
- Both raw-key and extras-only legacy formats default to the current time and
  zero usage, avoiding upgrade-triggered rotations.
- Trigger, explicit start and asynchronous DH completion require peer layer 46+.
- The sealed, append-only extension retains outgoing exchange id, secret exponent,
  prime, start time and the responder's duplicate-request answer. Both exchange
  roles resume in fresh processes.
- Only our uncommitted initiating exchange expires. An accepted responding
  exchange has no timeout. A fresh peer request supersedes its abandoned exchange;
  late callbacks and commits for the older instance cannot adopt a key.
- CommitKey is encrypted with the old key; the following Noop uses the new key.
- The visual hash is persisted separately and remains stable across rotations.
- `Secret chat checks` runs the native production-method regressions and the pure
  decision suite without depending on AiApi or a full client build.

## Incoming request dialog

The user reported an Android-initiated chat immediately ending in the desktop
client. Inspection found that `Bridge::promptRequest()` wired generic box closing
to `Manager::end()`: `ConfirmBox` invokes cancellation on closing unless
`strictCancel` is enabled. This is a concrete accidental-rejection path; the exact
cause of that live occurrence still needs a repeated authenticated run.

The dialog now rejects only through its explicit Decline button. Presentation is
deferred until navigation finishes, checks the still-active chat, and follows
the controller lifetime. Passive close leaves the request available for reopening.

## Executable evidence

```sh
python3 -m unittest tools.tests.test_native_secret_visual tools.tests.test_secret_prompt -v
g++ -std=c++20 -Wall -Wextra -Werror -I Telegram/SourceFiles \
  Telegram/SourceFiles/ayu/secret/secret_rekey_test.cpp -o /tmp/secret-rekey-test
/tmp/secret-rekey-test
```

The state/vault suite exercises production handlers, serialization, migration,
packet crypto and visual rendering with isolated database, DH and vault-root-key
adapters. The dialog suite executes production dialog arguments, cancellation
wiring and active-chat callback with isolated UI/event adapters. These tests do
not simulate Telegram's remote servers or constitute an authenticated UI run.

## Pending official-client acceptance evidence

The official Telegram Android application was reachable over authorized ADB.
The phone was then disconnected while the user stepped away. The following
authenticated matrix is still pending and is not claimed as passing:

1. Repeat Android-initiated establishment and desktop Accept/dismiss behavior.
2. Rotation initiated by freshGram, with bidirectional delivery afterward.
3. Rotation initiated by official Android, with bidirectional delivery afterward.
4. Restart freshGram in the middle of an exchange and verify delivery and the
   preserved visual hash afterward.

Private profiles, screenshots and live account identifiers stay outside the
repository. Public live evidence will identify peers only as A/B.
