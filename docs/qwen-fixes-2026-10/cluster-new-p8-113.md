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

## The fix

`src/core/DawProjectReadTracks.cpp` — replaced the round-1 hardcoded
`channelValueRange` with two helpers that read the element's own metadata:

- `DawProjectReadTracks.cpp:227-264` — `declaredChannelBounds`: reads the
  element's optional `min`/`max` (malformed ones refuse through the existing
  `doubleAttribute` rule). A value outside the element's declared bounds is
  refused with a **format-range** error naming the declared bound
  ("below the document's declared minimum" / "above the document's declared
  maximum"). Absent `min`/`max` declare no format bound. A value inside the
  declared (or absent) bounds that Zene's models cannot hold is refused with
  an **import-compatibility** error naming the models' reach ("which Zene's
  models cannot hold - their reach is ...") — kept distinct from the
  format-range error.
- `DawProjectReadTracks.cpp:265-281` — `declaredChannelUnit`: a declared
  `unit` the models do not speak (Volume is read as `linear`, Pan as
  `normalized` — what the writer emits) is refused as an import-compatibility
  error; an absent `unit` leaves that interpretation.
- `DawProjectReadTracks.cpp:326-327` — Volume call sites
  (`linear`, model reach 0..2); `DawProjectReadTracks.cpp:337-338` — Pan
  call sites (`normalized`, model reach 0..1).

## Regression

`tests/src/core/DawProjectInterchangeRoundTripTest.cpp` —
`outOfRangeChannelValuesAreRefused()` (fixtures now schema-conformant:
`unit` + explicit `min`/`max` on every element, endpoints 0/2/0/1 still read)
and the new `declaredBoundsUnitsAndModelReach()`: narrower declared bounds
(`min="0.5" max="1.5"`) refuse 1.75/0.25/0.9 as format-range errors; a wider
one (`min="-5" max="5" value="3"`) refuses past the models' reach as an
import-compatibility error; a foreign unit (`unit="db"`) is refused; absent
`min`/`max` declare no format bound (value 1 reads, value 3 refuses); the
custom endpoints 0.5 and 1.5 read.

## Evidence

| step | command | EXIT |
|---|---|---|
| RED build | `cmake --build build --target DawProjectInterchangeRoundTripTest > LOGS/build-1.log 2>&1` | 0 |
| RED run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-1.log 2>&1` (from build/tests) | 8 |
| GREEN build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > LOGS/build-2.log 2>&1` | 0 |
| GREEN run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-2.log 2>&1` (from build/tests) | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | 0 |

LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-113.r2/logs

RED failing assertion (test-1.log:32):

```
FAIL!  : DawProjectInterchangeRoundTripTest::declaredBoundsUnitsAndModelReach()
'!dawProjectModelFromXml(xml, &model, &rep…
Loc: [../../tests/src/core/DawProjectInterchangeRoundTripTest.cpp(1052)]
```

— the round-1 code accepted `<Volume unit="linear" min="0.5" max="1.5"
value="1.75"/>` because it checked the hardcoded 0..2 instead of the
element's declared bounds.

Family: `ctest -R DawProjectInterchangeRoundTripTest` matched 1 ctest test
(#210); internally 36 Qt cases — RED: 35 passed, 1 failed (test-1.log);
GREEN: `100% tests passed, 0 tests failed out of 1` (test-2.log:5).

## Files committed

- `src/core/DawProjectReadTracks.cpp` (fix; fork-NEW, already in fork-sources.txt)
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp` (regression test, existing file)
- `docs/qwen-fixes-2026-10/cluster-new-p8-113.md` (this report)
