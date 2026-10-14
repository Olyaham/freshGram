# Secret-chat visual authentication follow-up — 2026-10-14

## Confirmed defect and reference behavior

`Manager::Impl::makeInfo()` computed `KeyVisualHash(chat.key)` on every request.
Both `handleAcceptKey()` and `handleCommitKey()` replace `chat.key`, so PFS
rotated the identicon and hexadecimal verification text.

Telegram's [PFS key visualization specification](https://core.telegram.org/api/end-to-end/pfs#key-visualization)
requires the first 16 bytes of SHA-1 of the original key, followed by the first
20 bytes of SHA-256 of the key used when the chat reaches layer 46. For a chat
established by this layer-73 client, both portions use its establishment key.
Subsequent PFS exchanges must preserve that visual authentication value.

Before implementation, the preserved Nagram XF, Nagram and Extraless sources were
reviewed. `SecretChatHelper.applyPeerLayer()` preserves the stored SHA-1 prefix
when extending `key_hash`; their key-exchange handlers rotate `auth_key` without
replacing the visual hash. Nagram XF's `MessagesStorage` also persists `key_hash`
independently. The implementation follows that invariant.

The upstream snapshot `76f06bcb7e` still computes the visual hash from `chat.key`.
There are no secret-chat changes in its 31 commits after `239779b690`.

## Fix and storage compatibility

- `Chat::keyHash` holds the visual authentication value separately from the
  current key and transport fingerprint.
- `UpdateKeyVisualHash()` captures the original SHA-1 prefix once, appends the
  transition-key SHA-256 suffix at layer 46+, and preserves a complete hash.
- `becomeReady()` captures the value before its first persistence and layer
  notification; layer updates can extend a stored 16-byte value.
- `EncodeExtras()` appends the hash after all existing fields. The outer
  context-bound AES-GCM chat record seals it with the other secret-chat state.
- `DecodeExtras()` accepts previous raw-key, extras-only and usage-accounting
  formats. Startup immediately resaves a migrated record before resuming work.
- `makeInfo(withKey=true)` returns the saved value. PFS handlers continue to
  replace only working keys and the transport fingerprint.

### Legacy limitation

Previous records did not retain the original visual hash. If PFS has already
rotated their keys, the original authentication value cannot be reconstructed
from the current key. Migration freezes the value the previous implementation
would show at upgrade time; it does not recover the initial exchange's hash or
guarantee a match with a mobile peer that retained that hash. A newly established
chat captures the correct original value.

The protocol's current `key_fingerprint` remains a
maintenance identifier and changes with PFS. Telegram explicitly distinguishes
it from the visual authentication hash.

## Executable regression evidence

```sh
python3 -m unittest discover -s tools/tests -p test_native_secret_visual.py -v
```

The component regression suite passes. The probe
executes production establishment, startup migration, persistence, both PFS
commit paths, packet crypto, context-bound vault operations, `makeInfo()` and
key-image/text rendering. Session/database callbacks, vault-key derivation and
the DH host boundary are isolated adapters. The state methods themselves are
extracted unchanged; `secret_crypto.cpp` is compiled directly.

Acceptance cases:

1. Establish, capture, rotate twice, persist ciphertext and restart in a fresh
   process. Exercise both initiator-first and responder-first orders. Compare
   all 36 hash bytes, HEX and identicon pixel digest at every step. Emoji rendering
   is also checked when the downstream emoji formatter is present.
2. Ensure the working key and its packet fingerprint actually change and that
   packets encrypted with the new key decrypt successfully. After two rotations,
   neither retained working key is the establishment key.
3. At a simulated layer-45 to layer-46 transition, use different establishment
   and transition keys. Preserve the original SHA-1 portion and capture the
   transition SHA-256 portion; round-trip both partial and complete hashes.
4. Migrate three historical formats using an already-rotated working key,
   verify exactly one startup resave, then rotate twice and restart again.
5. Reject a truncated appended hash as a visual value instead of treating it
   as an absent legacy field.

An external mutation check restores the old `KeyVisualHash(chat.key)` rendering
expression. The establish/rekey regression then fails at its first post-rotation
snapshot for both roles. This demonstrates sensitivity to the reported defect.

These are native component/fresh-process checks. They do not constitute an
authenticated full-client PFS exchange or a rebuilt application launch. The
regressions are included in the independent `Secret chat checks` workflow.
