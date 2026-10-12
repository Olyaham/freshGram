# Telegram Desktop base

The downstream version is **7.3.1**, based on Telegram Desktop `dev` at
`6fed91ffab9861771a75f29df65031b3c80b6941` (API layer 230).

This is a source rebase, including the upstream application sources, resources,
build changes and dependency revisions. It is not a version-only or schema-only
update. The branch starts at that upstream commit, followed by the downstream
feature snapshot and conflict adaptations.

The earlier FreshGram integration used `e0be658126c84498b8dbc3f1f300e41db5b4c31e`.
Its entire tree is identical to upstream
`33261535a0e747f125e0ed25486f01e556330677`. Because upstream rewrote these commit
identities, this identical tree was used as the three-way rebase base. The
original downstream branch and its individual commits are preserved separately.

The vendored `Telegram/lib_ui` retains downstream customizations and incorporates
the changes from upstream revision `91ff4463899f23f4227bc4cbbdbe7696d479b9bd`.
The webview gitlink follows the upstream revision. Fork branding and packaging
remain downstream-specific. Historical FreshGram version numbers in the changelog
describe older releases, not the current application version.

Verification performed during the rebase:

- All 572 upstream-changed paths untouched by downstream edits match upstream.
- Conflicting message flags, media-editor behavior, compose controls, passcode
  APIs and custom UI layouts were reconciled explicitly.
- Application version, Windows resources and the UWP manifest use 7.3.1.

The downstream FGV2/AYV1 vault, archive encryption, backup protections and PFS
changes were integrated after the rebase. ASR credentials use that vault. A
successful disk-key verification gates vault readiness, and fake-passcode updates
follow a committed passcode change independently of the settings window.

Targeted production translation units were syntax-checked using generated layer
230, language, emoji and style headers. The standalone crypto suite passed 27
CTest groups, including legacy credential migration and PFS wire checks; its
ASan/UBSan run passed 19 groups. The real Qt/Rust ASR loader passed HTTP success,
failure and in-flight cancellation scenarios. Rust mock checks passed 16 tests.

The full client was not built or run. Live GUI/MTProto behavior, complete PFS
handshakes, passcode changes and native Windows/macOS/GPU deployment still require
runtime validation. Earlier branches and the original dirty checkout are retained.
