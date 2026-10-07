# cluster new-p8-257 — Reusing an AAF loss report accumulates results from earlier reads

Verdict: **FIXED**

## Finding

`src/core/AafImportRead.cpp:348` (historical): `readAaf` resets the model
(`*model = AafModel();`) but never resets the supplied `AafLossReport*`, so counts
and warnings from an earlier read remain in the next result.

## Evidence

- `src/core/AafImportRead.cpp:348–373` — `readAaf` reset only `*model` (line 369);
  `loss` was passed untouched to `aafi_set_debug(..., &collectLog, loss)` (line 359)
  and to `Walk walk{context.aafi, model, loss, {}}` (line 370), so a reused report
  kept the previous read's counts and warnings.
- `include/AafImport.h:215` — `AafLossReport` is a plain struct with default-member
  initializers, so `*loss = AafLossReport();` is a fresh report.
- `collectLog` (`src/core/AafImportRead.cpp:94–103`) appends LibAAF warnings to
  `loss->warnings` during `aafi_load_file`, so the reset must happen **before**
  `aafi_set_debug` — resetting next to `*model = AafModel();` would discard the
  current read's warnings instead.

## Regression

Added `reusingALossReportStartsFresh()` to the existing `tests/src/core/AafImportTest.cpp`:
reads the fixture twice into the same `AafLossReport` and requires the second read's
`videoTracks` and `warnings.size()` to equal the first's. RED on the original code:

```
FAIL!  : AafImportTest::reusingALossReportStartsFresh() Compared values are not the same
Actual   (loss.videoTracks): 2
Expected (videoTracks)     : 1
```

## Fix

`src/core/AafImportRead.cpp` — one line at the start of each read, after the OMF
refusal and before the LibAAF context/log callback is wired:

```cpp
*loss = AafLossReport(); // each read starts fresh: a caller may reuse one report across reads
```

Only the LibAAF branch changes; the OMF refusal and the unavailable-build branch keep
their existing behaviour (they never touch `loss`). Existing callers
(`ControlCommandsAafImport.cpp:208/267`) use fresh local reports, so no observable
behaviour changes for them.

## Commands and exit codes (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-257.r1/logs)

| Step | Command | EXIT |
|---|---|---|
| build test target (original code) | `cmake --build build --target AafImportTest > LOGS/build-1.log 2>&1` | 0 |
| RED run | `QT_QPA_PLATFORM=offscreen ctest -R AafImportTest --output-on-failure > LOGS/test-1.log 2>&1` | 8 |
| build after fix | `cmake --build build --target AafImportTest > LOGS/build-2.log 2>&1` | 0 |
| GREEN run | `QT_QPA_PLATFORM=offscreen ctest -R AafImportTest --output-on-failure > LOGS/test-2.log 2>&1` | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gates-1.log 2>&1` | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check >> LOGS/gates-1.log 2>&1` | 0 |

RED detail (test-1.log): `Totals: 11 passed, 1 failed, 0 skipped` — the failing
assertion is the new slot; `0% tests passed, 1 tests failed out of 1` (ctest ran 1 test).
GREEN detail (test-2.log): `1/1 Test #211: AafImportTest .... Passed`,
`100% tests passed, 0 tests failed out of 1` (ctest output is suppressed on success,
so the internal slot totals are not echoed; the RED run shows the 12-slot family).

Affected-family count: 1 ctest test (`AafImportTest`, 12 Qt slots).

## Files committed

- `src/core/AafImportRead.cpp` (already registered in `tests/fork-sources.txt:1041`)
- `tests/src/core/AafImportTest.cpp` (already registered in `tests/fork-sources.txt:1902`)
- `docs/qwen-fixes-2026-10/cluster-new-p8-257.md`
