# cluster new-p8-113 — Out-of-range Volume and Pan values are accepted

Verdict: **FIXED**

## The finding

`src/core/DawProjectReadTracks.cpp` (finding line 268): `parseChannel` read
`<Volume>` and `<Pan>` through `doubleAttribute`, which only refuses a
non-finite number — the format's declared ranges (`<Volume min="0" max="2">`,
`<Pan min="0" max="1">`, docs/DAWPROJECT-INTERCHANGE.md:193/198) were not
enforced, so `Volume value="4"` / `Pan value="-1"` read fine and were clamped
silently by the FloatModels when the model was applied
(DawProjectSession.cpp:528/560).

## The fix

`src/core/DawProjectReadTracks.cpp` — added `channelValueRange` (readdetail
namespace, above `ChannelValues`) and called it in both `parseChannel` branches:

- `DawProjectReadTracks.cpp:246-255` — the helper: a value outside the declared
  range sets `state.error` naming the value and range (same refusal rule as
  `tempoAccepted`/`meterAccepted` in DawProjectRead.cpp), so the document is
  refused rather than clamped.
- `DawProjectReadTracks.cpp:277` — `channelValueRange(state, "Volume", values.volume, 0.0, 2.0);`
- `DawProjectReadTracks.cpp:287` — `channelValueRange(state, "Pan", values.pan, 0.0, 1.0);`

One check serves both call sites (the track's own strip and the mixer strip in
`<Structure>`), which share the 0..2 / 0..1 scales per the interchange doc.

## Regression

`tests/src/core/DawProjectInterchangeRoundTripTest.cpp:962-1021` —
`outOfRangeChannelValuesAreRefused()`: mixer strip and track channel with
`Volume value="4"/"-0.5"/"2.5"` and `Pan value="-1"/"1.5"/"-0.25"` must be
refused; the range endpoints (0, 2, 0, 1) still read.

## Evidence

| step | command | EXIT |
|---|---|---|
| RED build | `cmake --build build --target DawProjectInterchangeRoundTripTest > LOGS/build-1.log 2>&1` | 0 |
| RED run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-1.log 2>&1` (from build/tests) | 8 |
| GREEN build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > LOGS/build-2.log 2>&1` | 0 |
| GREEN run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-2.log 2>&1` (from build/tests) | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | 0 |

LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-113.r1/logs

RED failing assertion (test-1.log:31):

```
FAIL!  : DawProjectInterchangeRoundTripTest::outOfRangeChannelValuesAreRefused()
'!dawProjectModelFromXml(xml, &model, &report, &error)' returned FALSE.
(the mixer strip carried <Volume value="4"/>)
```

Family: `ctest -R DawProjectInterchangeRoundTripTest` matched 1 ctest test
(#210); internally 35 Qt cases — RED: 34 passed, 1 failed (test-1.log:48);
GREEN: `100% tests passed, 0 tests failed out of 1` (test-2.log:5).

## Files committed

- `src/core/DawProjectReadTracks.cpp` (fix; fork-NEW, already in fork-sources.txt)
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp` (regression test, existing file)
- `docs/qwen-fixes-2026-10/cluster-new-p8-113.md` (this report)
