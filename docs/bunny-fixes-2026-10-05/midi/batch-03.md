# midi batch-03 — SMF conductor track reader/writer

Six deduplicated findings. Five were real and are fixed, one was already fixed by
`a2426a910` on this branch.

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 3522 zero global tempo divides by zero | FIXED | `baecd1c42` |
| 5368 inactive tempo map exports dormant changes | FIXED | `942cdc3c3` |
| 5382 time-signature exponent not validated | FIXED | `f64f025a0` |
| 805 malformed End-of-Track accepted | FIXED | `4bbcf5941` |
| 5380 absolute-tick accumulator wraps | FIXED | `d6d948ab1` |
| 2547 large timeline ticks wrap on export | ALREADY-FIXED | `a2426a910` |

Plus one docs commit, `344781669`, recording the new refusals in
`docs/SMF-INTERCHANGE.md`.

### Cluster 3522 — Zero global tempo can divide by zero during SMF export

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchange.cpp:114` computed
`(SmfMicrosecondsPerMinute + bpm / 2) / bpm` from the caller's `globalTempo`, which
`writeConductorTrack()` passed through unvalidated. With an empty map the
tick-zero seed carries that value verbatim, so `globalTempo == 0` was an integer
division by zero — the regression test crashed the test binary with SIGFPE rather
than producing a wrong byte. `writeConductorTrack()` now refuses anything outside
`TempoMapMinTempo..TempoMapMaxTempo` before building anything, and writes no file.

**Commit:** `baecd1c42`

**Test:** `SmfInterchangeTest::aGlobalTempoTheEngineDoesNotHoldIsRefused`

**Red/green:**
- red `cmake --build build --target zene SmfInterchangeTest` → `EXIT=0`; `cd build/tests && ctest -R '^SmfInterchangeTest$' --output-on-failure` → `EXIT=8`, `Received signal 8 (SIGFPE)` in the new case, 5 pre-existing cases passing.
- green `cd build/tests && ctest -R '^SmfInterchangeTest$' --output-on-failure` → `EXIT=0`, 9 passed / 0 failed.

### Cluster 5368 — Inactive tempo map exports dormant changes

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchange.cpp:332` walked `map.all()` unconditionally,
so a `TempoMap` with `active() == false` still had its stored tempo and metre
events written into the file. The session plays the global tempo; a DAW reading the
file followed dormant changes — a different performance of the same project.
`exportEvents()` now iterates the stored events only when `map.active()`, so for an
inactive map the tick-zero seed is the whole conductor track.

**Commit:** `942cdc3c3`

**Test:** `SmfInterchangeTest::anInactiveMapExportsOnlyWhatTheTimelineObeys`

**Red/green:**
- red produced by reverting only the `if (map.active())` guard after the commit: `cd build/tests && ctest -R '^SmfInterchangeTest$' --output-on-failure` → `EXIT=8`, `FAIL anInactiveMapExportsOnlyWhatTheTimelineObeys`, Actual `(obeyed.size())` 2 vs Expected 1.
- green `cd build/tests && ctest -R '^SmfInterchangeTest$' --output-on-failure` → `EXIT=0`, 9 passed / 0 failed.

**Collateral:** the pre-existing `aGapTooWideForTheFormatIsRefused` built its map
without `setActive(true)`, so once dormant events stopped being exported its wide
delta never reached the file and the case would have passed vacuously. The map is now
activated, which is what that case always meant; its assertions are unchanged.

### Cluster 5382 — Invalid time-signature exponents can silently become different meters

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp:215` computed
`1 << (payload[1] & 0x1F)`. The mask rewrote out-of-range exponents instead of
rejecting them (dd 32 became dd 0, so a 2^32 metre imported as 1/1), and dd 31
shifted into the sign bit, which is undefined behaviour. dd is log2(denominator) and
the map holds a power of two up to `TempoMapLargestDenominator` (32), so dd is 0..5;
beyond that the read is refused as `invalid_args` naming the time signature, following
the existing precedent for an SMPTE division. Validation moved into
`readTimeSignaturePayload()` to keep `readMetaEvent()` inside the complexity gate.
New documented constant `SmfMaxTimeSignatureExponent` in `include/SmfInterchange.h`.

**Commit:** `f64f025a0`

**Test:** `SmfInterchangeRoundTripTest::aMeterExponentTheEngineCannotHoldIsRefused`

**Red/green:**
- red `cmake --build build --target zene SmfInterchangeRoundTripTest` → `EXIT=0`; `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=8`, 6 passed / 3 failed, this case among the three.
- green `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=0`, all cases passed.

### Cluster 805 — Malformed End-of-Track events are accepted

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp:218` set the `EndOfTrack` outcome for
any `FF 2F` event whatever its payload length, and `readTrack()` returned at once on
that outcome without consulting the cursor — so an end-of-track event with a payload,
or bytes after it, still read as a well-formed track with the rest silently
discarded. SMF 1.0 gives `FF 2F` a length of zero and ends the track there, so both
shapes are now refused. The check moved into `readEndOfTrack()` for the complexity
gate.

**Commit:** `4bbcf5941`

**Test:** `SmfInterchangeRoundTripTest::aMalformedEndOfTrackIsRefused`

**Red/green:**
- red: same batch run as above — `EXIT=8`, 3 of the new cases failed, this one included.
- green `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=0`, all cases passed.

### Cluster 5380 — Long track times wrap in the absolute-tick accumulator

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp:274` accumulated deltas in a
`quint32`. Each delta of a track is individually valid, but together they pass 2^32
file ticks and the accumulator wrapped: a tempo event at the end of seventeen maximum
deltas was imported at 268,435,439 — 4,294,967,296 ticks before where the file put
it. The walk is now 64-bit and refuses a track past `SmfMaxFileTicks`, narrowing to
the `quint32` the events carry only after that check. New documented constant
`SmfMaxFileTicks` in `include/SmfInterchange.h`.

**Commit:** `d6d948ab1`

**Test:** `SmfInterchangeRoundTripTest::aTrackTimeBeyondTheReadersRangeIsRefused`

**Red/green:**
- red: same batch run as above — `EXIT=8`, 3 of the new cases failed, this one included.
- green `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=0`, all cases passed.

### Cluster 2547 — Large timeline ticks wrap during SMF conversion

**Verdict:** ALREADY-FIXED

**Evidence:** the report's line 229 is `appendDelta()`, which takes `qint64 cursor,
qint64 tick` and is called from `trackBody()` at
`src/core/SmfInterchange.cpp:252` with `static_cast<qint64>(event.tick) *
SmfTicksPerLmmsTick` — 64-bit throughout the walk, narrowed only inside `appendDelta`
after the delta has been checked against `SmfMaxDeltaTicks`. Fixed by `a2426a910`
on this branch, which also added the refusal and the test below. No change made.

**Commit:** `a2426a910` (earlier on this branch, not in this batch)

**Test:** `SmfInterchangeTest::aGapTooWideForTheFormatIsRefused`

**Red/green:** none run in this batch — the defect was already fixed and covered when
`a2426a910` landed. The case is re-verified green as part of
`ctest -R '^SmfInterchangeTest$'` → `EXIT=0`.

## Verification

- `cd build/tests && ctest -R '^SmfInterchangeTest$' --output-on-failure` → `EXIT=0` (9 cases)
- `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=0` (9 cases)
- `cd build/tests && ctest -R '^(TempoMapTest|ControlTempoMapCommandsTest|TempoMapPersistenceTest|TempoMapPanelTest)$' --output-on-failure` → `EXIT=0` (4 executables, run because `exportEvents()` sits on the `TempoMap` path)
- `bash tests/complexity-gate.sh --check` → `EXIT=0`
- `bash tests/file-length-gate.sh --check` → `EXIT=0`

`readMetaEvent()` had to be split (into `readTimeSignaturePayload()` and
`readEndOfTrack()`) because both validations pushed it to CCN 11 against the gate.

**Leftovers:** none. Two reader decisions are documented in
`docs/SMF-INTERCHANGE.md` (`344781669`) rather than left implicit: a track the format
itself describes wrongly is refused rather than repaired, and only what the timeline
obeys is written.