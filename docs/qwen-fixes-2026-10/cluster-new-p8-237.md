# cluster new-p8-237 — A missing spacer track ID attaches the spacer to track ID 0

Verdict: FIXED

## Evidence

- `src/core/TrackSpacers.cpp:90` (before the fix):
  `if (height > 0) { m_spacers.insert(one.attribute(QStringLiteral("track")).toInt(), {height, ...}); }`
  `QString::toInt()` without an `ok` flag returns 0 for a missing or non-numeric `track`
  attribute, and 0 is a valid track id — so `<spacer height="40"/>` attached the spacer to
  the track whose id is 0.
- Fix: parse the id with `toInt(&idOk)` and only insert when `idOk` is true, skipping
  entries whose id is missing or invalid. Valid spacers load unchanged (behaviour-preserving).

## Regression

Added `ArrangingCatchUpTest::aSpacerWithoutItsTrackIdAttachesToNothing` to the existing
test file `tests/src/core/ArrangingCatchUpTest.cpp` (already registered in
`tests/fork-sources.txt:1905`; `src/core/TrackSpacers.cpp` already registered at
`tests/fork-sources.txt:1592` — no registration changes needed). The test builds a
`<track-spacers>` DOM with a spacer missing its `track` attribute, one with a
non-numeric id, and one well-formed spacer, loads it, and asserts nothing lands on
track 0 while the well-formed spacer still loads.

## Commands and exit codes (logs in this run's LOG FOLDER)

| Step | Command | EXIT |
|---|---|---|
| build (compile error, QDomDocument has no loadFromString) | `cmake --build build --target ArrangingCatchUpTest` → logs/build-1.log | 2 |
| build test target (fixed test source) | `cmake --build build --target ArrangingCatchUpTest` → logs/build-2.log | 0 |
| RED on original code | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure` (from build/tests) → logs/test-1.log | 8 |
| fix rebuild (zene + test target) | `cmake --build build --target zene ArrangingCatchUpTest` → logs/build-3.log | 0 |
| intermediate run (plugin not yet built, unrelated pre-existing failure) | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure` → logs/test-2.log | 8 |
| build plugin the binary needs | `cmake --build build --target tripleoscillator` → logs/build-4.log | 0 |
| GREEN after fix | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure` (from build/tests) → logs/test-3.log | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check` → logs/gate-1.log | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check` → logs/gate-2.log | 0 |

RED assertion (test-1.log): `FAIL! ArrangingCatchUpTest::aSpacerWithoutItsTrackIdAttachesToNothing()` —
`Actual (TrackSpacers::instance().spacer(0).height): 50`, `Expected (0): 0`.

GREEN (test-3.log): `1/1 Test #192: ArrangingCatchUpTest ... Passed`, `100% tests passed, 0 tests failed out of 1`.
Affected-family test count: 1 ctest test (ArrangingCatchUpTest, all its private-slot cases pass).

Note: the test-2 run failed only on the pre-existing `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie`
case because the `tripleoscillator` plugin had not been built in this tree yet; building the plugin target
(test-3 run) resolved it — unrelated to this fix, and the spacer assertion itself already passed in test-2.

## Files committed

- `src/core/TrackSpacers.cpp` (the fix)
- `tests/src/core/ArrangingCatchUpTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p8-237.md` (this report)
