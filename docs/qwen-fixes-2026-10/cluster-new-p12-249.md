# Cluster new-p12-249 — Malformed time signature events are accepted

Verdict: **FIXED**

## The finding

`src/core/SmfInterchangeReader.cpp:260` — `readMetaEvent` recorded a time signature
event for any `FF 58` payload of two or more bytes (`length >= 2`) and then parsed
only its first two bytes. A payload of length 2, 3, 5 (or 0, which fell through as
an unknown meta event) was therefore accepted, so a file such as `FF 58 02 04 02`
imported as 4/4 instead of being refused.

## Evidence

- `src/core/SmfInterchangeReader.cpp:260` (before the fix):
  `else if (type == SmfMetaTimeSignature && length >= 2)` accepted short and long
  payloads and `readTimeSignaturePayload()` read only `payload[0]` and `payload[1]`.
- `docs/SMF-INTERCHANGE.md` section 1 and the tempo event fix
  (cluster new-p8-232) establish the reader's own policy: a known event with a
  malformed structure is refused with a reason; `SMF 1.0` names `nn/dd/cc/bb` in
  exactly four bytes.

## The fix

`src/core/SmfInterchangeReader.cpp` — `readMetaEvent` now refuses any `FF 58`
whose payload length is not exactly 4, with a typed reason naming
"time signature", and then parses the four-byte payload. The four-byte case is
unchanged (the `cc` / `bb` bytes are still ignored, as before).

## Regression

New test slot
`aTimeSignatureEventWithAWrongPayloadLengthIsRefused()` in
`tests/src/core/SmfInterchangeRoundTripTest.cpp`: files with `FF 58` payloads of
length 0, 1, 2, 3 and 5 behind a valid end-of-track must be refused by
`interchange.smf_read` (`ControlErrorKind::InvalidArgs`, message names "time
signature") and import nothing; the same event with the format's four bytes must
still read (`event_count == 1`).

## Commands and exit codes (logs in LOG FOLDER)

RED on the original code (product reverted, test kept):

```
git apply -R logs/fix.patch  -> EXIT=0
cmake --build build --target SmfInterchangeRoundTripTest -> EXIT=0 (logs/build-nodefix.log)
ctest -R SmfInterchangeRoundTripTest --output-on-failure -> EXIT=8 (logs/test-red2.log)
   FAIL! ... '!read.ok' returned FALSE. (an FF 58 with 0 payload bytes was accepted)
```

GREEN after restoring the fix:

```
git apply logs/fix.patch -> EXIT=0
cmake --build build --target SmfInterchangeRoundTripTest -> EXIT=0 (logs/build-green2.log)
ctest -R SmfInterchange --output-on-failure -> EXIT=0 (logs/test-green2.log)
   2/2 tests passed
```

Initial RED that established the test (before any product change):

```
cmake --build build --target SmfInterchangeRoundTripTest -> EXIT=0 (logs/build-1.log)
ctest -R SmfInterchange --output-on-failure -> EXIT=8 (logs/test-1.log)
   FAIL! ... '!read.ok' returned FALSE. (an FF 58 with 0 payload bytes was accepted)
```

Gates:

```
bash tests/complexity-gate.sh --check -> EXIT=1 (logs/gate-final-complexity.log)
   no SmfInterchangeReader/readMetaEvent entries; the remaining 6 entries are
   pre-existing unrelated functions not touched by this fix
bash tests/file-length-gate.sh --check -> EXIT=1 (logs/gate-final-filelen.log)
   src/core/SmfInterchangeReader.cpp no longer appears; the remaining entries
   are pre-existing unrelated files not touched by this fix
```

## Evidence table

| item | value |
|---|---|
| RED command (reverse proof) | `ctest -R SmfInterchangeRoundTripTest` after `git apply -R fix.patch` -> EXIT=8 |
| GREEN command | `ctest -R SmfInterchange` after restoring fix.patch -> EXIT=0, 2/2 passed |
| affected-family test count | 2 (`SmfInterchangeTest`, `SmfInterchangeRoundTripTest`) |
| complexity gate | EXIT=1 (pre-existing unrelated entries only) |
| file-length gate | EXIT=1 (pre-existing unrelated entries only) |
| files committed | `src/core/SmfInterchangeReader.cpp`, `tests/src/core/SmfInterchangeRoundTripTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p12-249.md` |

No registration changes: `src/core/SmfInterchangeReader.cpp` is already in
`tests/fork-sources.txt`; the test file is an existing fork test already
registered in `tests/CMakeLists.txt` and `tests/all-sources.txt`. No new control
commands, no header changes.
