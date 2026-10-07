# cluster new-p8-113 — Out-of-range Volume and Pan values are accepted

Verdict: **FIXED**

## The finding

`src/core/DawProjectReadTracks.cpp` (finding line 268): `parseChannel` read
`<Volume>` and `<Pan>` through `doubleAttribute`, which only refuses a
non-finite number — the elements' declared bounds were not enforced, so
`Volume value="4"` / `Pan value="-1"` read fine and were clamped silently by
the FloatModels when the model was applied (DawProjectSession.cpp:528/560).

Luna's round-1 review (REWORK) added the requirement: the check must read
each element's OWN declared `unit`/`min`/`max` — 0..2 and 0..1 are NOT
universal DAWproject bounds. They are only what this build's writer emits
(`DawProjectWrite.cpp:390/458/469` — `writeValueParameter` with the models'
own reach: the mixer fader 0..2 (`Mixer.cpp:220`), the track volume
MinVolume..MaxVolume ÷ 100 (`volume.h:36`, `DawProjectSession.cpp:528`), the
panning model -100..100 ÷ 200 (`DawProjectSession.cpp:560`)). A foreign
writer may declare any range on the element, per
docs/DAWPROJECT-INTERCHANGE.md:193/198.

Luna's round-2 review (REWORK) added the unit/bound-presence gap: the
schema requires `RealParameter.unit` (Project.xsd), so a missing unit must
be refused; the mismatch fixture must use a valid schema unit (`decibel`,
not the made-up `db`); and `min=""`/`max=""` is a PRESENT-but-empty
attribute — a malformed bound, not an absent one.

## The fix

`src/core/DawProjectReadTracks.cpp` — two helpers read the element's own
metadata (round 2) plus the round-3 tightening:

- `DawProjectReadTracks.cpp:227-245` — `refusePresentEmptyBound`: a `min`/`max`
  that is PRESENT but empty (`attributes.hasAttribute` + empty value) declares a
  malformed bound, not an absent one: refused through the existing
  `malformedNumericAttribute` rule ("the attribute min carries "", which is not
  a number this reader accepts"). An absent `min`/`max` still declares no
  format bound.
- `DawProjectReadTracks.cpp:247-276` — `declaredChannelBounds`: calls
  `refusePresentEmptyBound` for `min` and `max`, then enforces the element's
  declared bounds (format-range error naming the declared bound) and the
  models' own reach (import-compatibility error naming the reach) — the two
  refusals kept distinct.
- `DawProjectReadTracks.cpp:278-294` — `declaredChannelUnit`: the schema REQUIRES
  `RealParameter.unit`, so a missing or empty unit is refused outright ("carries
  no unit; the format requires every RealParameter to declare one"); a unit the
  models do not speak (Volume is read as `linear`, Pan as `normalized` — what
  the writer emits) is refused as an import-compatibility error ("cannot
  convert").
- Call sites unchanged: `DawProjectReadTracks.cpp:346-347` Volume (`linear`,
  reach 0..2); `DawProjectReadTracks.cpp:357-358` Pan (`normalized`, reach 0..1).

## Regression

`tests/src/core/DawProjectInterchangeRoundTripTest.cpp` —
`outOfRangeChannelValuesAreRefused()` and `declaredBoundsUnitsAndModelReach()`
(fixtures schema-conformant: `unit` + explicit `min`/`max` on every element,
endpoints 0/2/0/1 still read; the mismatch fixture now uses the valid schema
unit `decibel` instead of the made-up `db`) and the new
`requiredUnitsAndPresentEmptyBoundsAreRefused()`: a missing unit (`<Volume
value="1"/>`, `<Pan value="0.5"/>`) and an empty unit (`unit=""`) are refused
with the no-unit error; `unit="decibel"` is refused as an import-compatibility
error; `min=""`/`max=""` on Volume and Pan are refused as malformed
attributes.

## Evidence

| step | command | EXIT |
|---|---|---|
| RED build | `cmake --build build --target DawProjectInterchangeRoundTripTest -j12 > LOGS/build-1.log 2>&1` | 0 |
| RED run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-1.log 2>&1` (from build/tests) | 8 |
| GREEN build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j12 > LOGS/build-2.log 2>&1` | 0 |
| GREEN run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-2.log 2>&1` (from build/tests) | 0 |
| reverse hunks | `git diff -- src/core/DawProjectReadTracks.cpp > LOGS/fix-hunks.patch; git apply -R LOGS/fix-hunks.patch` | 0 |
| reversed build | `cmake --build build --target DawProjectInterchangeRoundTripTest -j12 > LOGS/build-3.log 2>&1` | 0 |
| reversed run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-3.log 2>&1` (from build/tests) | 8 |
| restore | `git apply LOGS/fix-hunks.patch` | 0 |
| restored build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j12 > LOGS/build-4.log 2>&1` | 0 |
| restored run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-4.log 2>&1` (from build/tests) | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | 0 |

LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-113.r3/logs

RED failing assertion (test-1.log:33-34):

```
 FAIL!  : DawProjectInterchangeRoundTripTest::requiredUnitsAndPresentEmptyBoundsAreRefused() '!dawProjectModelFromXml(xml, …
 Loc: [../../tests/src/core/DawProjectInterchangeRoundTripTest.cpp(1148)]
```

— the round-2 code accepted `<Volume value="1"/>` (no unit): the schema
requires `RealParameter.unit`, and the round-2 reader treated an absent unit
as "leave the model interpretation". The reversed-hunks run (test-3.log)
fails the same assertion, and the restored run passes.

Family: `ctest -R DawProjectInterchangeRoundTripTest` matched 1 ctest test;
internally 37 Qt cases — RED: 36 passed, 1 failed (test-1.log); GREEN:
`100% tests passed, 0 tests failed out of 1` (test-2.log:5, test-4.log:5).

## Files committed

- `src/core/DawProjectReadTracks.cpp` (fix; fork-NEW, already in fork-sources.txt)
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp` (regression test, existing file)
- `docs/qwen-fixes-2026-10/cluster-new-p8-113.md` (this report)
