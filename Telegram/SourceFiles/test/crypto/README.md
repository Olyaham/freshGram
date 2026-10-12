# Freshgram encryption checks

This standalone CMake project compiles the production vault, archive database,
backup, credential and secret-chat protocol code. It uses the real sqlite_orm
schema, SQLite and shared `ayu_encryption.h`. It does not build Telegram Desktop.

Adapters replace the application, a synthetic 256-byte local key, settings,
logging, clocks, timers, scheduling and sender-ID parsing. AES/GCM, KDF, RNG,
Qt file I/O, row envelopes, backfill and SQLite recovery use production code.
The scheduler is synchronous and the timer does not run. Sender parsing covers
bare numeric IDs only. The AYV1 decoder is extracted from the checked-out storage
implementation during configuration, so migration tests use its actual format.

## Run

Requirements: C++20, CMake 3.25+, Qt6 Core, OpenSSL Crypto, SQLite3 and Python3.
On Ubuntu, install `cmake ninja-build g++ python3 qt6-base-dev libssl-dev libsqlite3-dev`.

From the repository root:

```sh
cmake -S Telegram/SourceFiles/test/crypto -B "$HOME/freshgram-crypto-checks" \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/freshgram-crypto-checks" --parallel 1
ctest --test-dir "$HOME/freshgram-crypto-checks" --output-on-failure
```

Tests use their own temporary directories, never a real account's `tdata`, and
make no network requests. Unknown cases, cryptographic failures and mismatches
produce a nonzero exit status. Individual checks can be selected with CTest `-R`,
`crypto_tests --case rows/identity-binding` or `crypto_database_tests search_ties`.

## Coverage

- Empty, binary and UTF-8 data; nonce generation and round trips.
- Independent FGV1/FGV2 golden AESGCM vectors, tampered envelope bytes,
  truncation, extra bytes, wrong keys and wrong AAD contexts.
- Inline/stream compatibility, chunk boundaries, file-size bounds,
  short writes, invalid paths and preservation of existing destinations.
- Archived text/binary fields, strict/fallback policy, idempotence and binding
  to user, dialog, message, table and column identities.
- Real SQLite backups and restoration, unavailable keys, staging recovery,
  legacy envelope migration, regex identity binding and prefix collisions.
- Database replacement after encryption refusal, stable search pagination
  across equal timestamps, filters, Unicode and positive/unlimited limits.
- Linux publication/flush fault injection and backup/snapshot clear races;
  POSIX execution with a 1 MiB stack.
- ASR credential persistence before unlock, wrong-provider/key rejection,
  explicit clearing, real AYV1 decoding and migration to bound FGV2 envelopes.
- Secret-chat PFS control-message wire round trips, truncated inputs,
  concurrent exchange arbitration and rotation predicates.

The ordinary Linux run contains 27 CTest groups. The sanitizer run contains 19;
small-stack and publication-preload cases run separately without sanitizers.
Nonce tests are smoke checks, not a statistical assessment of the RNG.

The full Linux Debug client separately passed disposable-profile checks of
domain startup, disk-key verification, worker-based fake-passcode changes,
disk-write failures and provider-dialog worker lifetimes. See
`docs/transcription-providers.md` for that run's scope. The complete PFS Manager
handshake still requires authenticated client testing. The standalone adapters
do not establish those runtime properties.

Streaming publication requires a fresh destination and same-filesystem hard
links. Unsupported filesystems return failure. A seal/publication failure with
an available key never falls back to a plaintext backup. Inline `SealToFile`
uses QSaveFile and permits replacement. Atomic visibility does not imply crash
durability: these tests do not establish file/directory fsync behavior.

Archive content fields are individually sealed; numeric IDs, dates and flags
remain visible in the working database. Backups are sealed as complete files
when the key is available. Windows/macOS execution is not covered by this run.

## ASan and UBSan

```sh
cmake -S Telegram/SourceFiles/test/crypto -B "$HOME/freshgram-crypto-asan" \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DFRESHGRAM_CRYPTO_SANITIZERS=address,undefined
cmake --build "$HOME/freshgram-crypto-asan" --parallel 1
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir "$HOME/freshgram-crypto-asan" --output-on-failure
```

These checks do not replace TSan or instrument prebuilt Qt/OpenSSL dependencies.

## Benchmarks

```sh
python3 Telegram/SourceFiles/test/crypto/bench.py \
  "$HOME/freshgram-crypto-checks/crypto_bench" > "$HOME/freshgram-crypto-bench.csv"
```

Each size/operation runs in its own process. CSV reports plaintext size,
repetitions, median/p95 latency, MiB/s and process peak RSS. Fixtures are generated
and verified incrementally; setup, warmup, removal of the preceding benchmark
destination and final verification are outside measured operations. RSS includes
Qt/OpenSSL and warmup. Warm page-cache measurements exclude fsync and do not
describe disk throughput or crash durability. Use identical compiler flags,
hardware and load for comparisons; `--quick` is a smoke check.
