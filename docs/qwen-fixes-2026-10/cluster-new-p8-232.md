# Cluster new-p8-232 — Reader silently drops malformed tempo events

Verdict: **FIXED**

## The finding

`src/core/SmfInterchangeReader.cpp:234` — an `FF 51` tempo meta event was
recorded only when its payload length was exactly 3; any other length fell
through to the end of `readMetaEvent` and was treated as an unknown meta
event: skipped, and the file still read as valid. A file with a two-byte
tempo payload and a valid end-of-track therefore imported with the tempo
change silently lost.

## Evidence

- `src/core/SmfInterchangeReader.cpp:234` (before the fix):
  `if (type == SmfMetaTempo && length == 3)` — lengths 0, 1, 4, ... skipped
  the event with no word, while `readEndOfTrack` (line 206) and the
  malformed-EOT / bad-dd refusals show this reader's own policy: a malformed
  structure the format describes is refused with a reason, not dropped.
- `docs/SMF-INTERCHANGE.md` section 1 records the reader's rule that unknown
  meta events are skipped — an `FF 51` with a non-three-byte payload is not
  unknown, it is a known event malformed; SMF 1.0 names microseconds-per-quarter
  in exactly three bytes.

## The fix

`src/core/SmfInterchangeReader.cpp` — `readMetaEvent` now refuses any
`FF 51` whose payload length is not exactly 3, with a typed reason
("a tempo meta event carries N payload bytes; the format names exactly
three microseconds-per-quarter bytes..."). The three-byte case is unchanged.
The condition count is unchanged (`type == SmfMetaTempo` + `length != 3`
replaces `type == SmfMetaTempo && length == 3`), so no complexity ratchet
moves.

## Regression

New test slot `aTempoEventWithAWrongPayloadLengthIsRefused()` in
`tests/src/core/SmfInterchangeRoundTripTest.cpp` (the existing test file for
this area): files with `FF 51` payloads of length 0, 2 and 4 behind a valid
end-of-track must be refused by `interchange.smf_read`
(`ControlErrorKind::InvalidArgs`, message names "tempo"), must import nothing,
and the same event with the format's three bytes must still read
(`event_count == 1`).

## Commands and exit codes (logs in the run's LOG FOLDER)

RED on the original code:

```
cmake --build build --target SmfInterchangeRoundTripTest > LOGS/build-1.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R SmfInterchange --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?                 # EXIT=8
```

`test-1.log`: `FAIL! : SmfInterchangeRoundTripTest::aTempoEventWithAWrongPayloadLengthIsRefused()
'!read.ok' returned FALSE. (an FF 51 with 0 payload bytes was accepted)` —
Totals: 15 passed, 1 failed. (`#208 SmfInterchangeTest` was Not Run there: its
binary had never been built in this tree; it is built below.)

After the fix:

```
cmake --build build --target SmfInterchangeRoundTripTest zene > LOGS/build-2.log 2>&1; echo EXIT=$?   # EXIT=0
cmake --build build --target SmfInterchangeTest > LOGS/build-3.log 2>&1; echo EXIT=$?                 # EXIT=0
ctest -R SmfInterchange --output-on-failure > LOGS/test-3.log 2>&1; echo EXIT=$?                      # EXIT=0
```

`test-2.log` (EXIT=8) is the honest intermediate: after the fix the family
still reported `208 - SmfInterchangeTest (Not Run)` because that binary did
not exist yet; `SmfInterchangeRoundTripTest` itself already passed there.
`test-3.log`: `1/2 #208 Passed, 2/2 #209 Passed — 100% tests passed, 0 failed
out of 2`.

Gates:

```
bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; echo EXIT=$?   # EXIT=0
bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1; echo EXIT=$?  # EXIT=0
```

## Evidence table

| item | value |
|---|---|
| RED command | `ctest -R SmfInterchange --output-on-failure` (from build/tests, after build-1) → EXIT=8, `FAIL! ... (an FF 51 with 0 payload bytes was accepted)` |
| GREEN command | `ctest -R SmfInterchange --output-on-failure` (after build-2 + build-3) → EXIT=0, 2/2 passed |
| affected-family test count | 2 (`SmfInterchangeTest`, `SmfInterchangeRoundTripTest`) |
| complexity gate | EXIT=0 |
| file-length gate | EXIT=0 |
| files committed | `src/core/SmfInterchangeReader.cpp`, `tests/src/core/SmfInterchangeRoundTripTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p8-232.md` |

No registration changes: `src/core/SmfInterchangeReader.cpp` is already in
`tests/fork-sources.txt` (line 1577); the test file is an existing fork test
already registered in `tests/CMakeLists.txt` and `tests/all-sources.txt`.
No new control commands, no header changes.
