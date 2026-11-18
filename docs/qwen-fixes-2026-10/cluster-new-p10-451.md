# cluster new-p10-451 — Video timecode drops the sign for negative sub-frame times

Verdict: **FIXED**

## Finding
`src/core/VideoReference.cpp:77` — the minus sign in `VideoReference::timecode` was emitted
only when the rounded whole seconds or frame number was nonzero, so `-0.01` s at 25 fps
displayed as positive `00:00:00:00` instead of `-00:00:00:00`.

## Evidence
- `src/core/VideoReference.cpp:77` (before): `.arg(seconds < 0.0 && (whole > 0 || frame > 0) ? QStringLiteral("-") : QString())`
  — for `-0.01` at 25 fps, `whole == 0` and `frame == 0`, so the sign was dropped.
- `src/core/VideoReference.cpp:77` (after): `.arg(seconds < 0.0 ? QStringLiteral("-") : QString())`
  — the sign is preserved whenever the input seconds are negative (`-0.0` and `0.0` stay unsigned).
- Regression test added to the existing family file `tests/src/core/VideoTrackTest.cpp`
  (new method `negativeSubFrameTimecodeKeepsItsSign()`):
  `QCOMPARE(VideoReference::timecode(-0.01, 25.0), QStringLiteral("-00:00:00:00"));`

Both files were already registered in `tests/fork-sources.txt` (lines 1602 and 2060); no new
files were added, so no registration change was needed.

## Commands and exit codes (LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-451.r1/logs)

RED on the original code (test added, product unchanged):
```
cmake --build build --target VideoTrackTest > $LOGS/build-1.log 2>&1; echo BUILD_EXIT=$?   # BUILD_EXIT=0
ctest -R '^VideoTrackTest$' --output-on-failure > $LOGS/test-1.log 2>&1; echo CTEST_EXIT=$?  # CTEST_EXIT=8
```
test-1.log: `FAIL! : VideoTrackTest::negativeSubFrameTimecodeKeepsItsSign() Compared values are not the same`
— Actual `(VideoReference::timecode(-0.01, 25.0))`: `"00:00:00:00"`, Expected: `"-00:00:00:00"`;
Totals: 12 passed, 1 failed, 1 skipped; ctest: `0% tests passed, 1 tests failed out of 1`.

GREEN after the fix:
```
cmake --build build --target zene VideoTrackTest > $LOGS/build-2.log 2>&1; echo BUILD_EXIT=$?  # BUILD_EXIT=0
ctest -R '^VideoTrackTest$' --output-on-failure > $LOGS/test-2.log 2>&1; echo CTEST_EXIT=$?    # CTEST_EXIT=0
```
test-2.log: `100% tests passed, 0 tests failed out of 1`.

Fix-catches-bug proof (revert, rebuild, run → FAIL; re-apply, rebuild, run → PASS):
```
git diff -- src/core/VideoReference.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch       # reverted OK
cmake --build build --target zene VideoTrackTest > $LOGS/build-3.log 2>&1; echo BUILD_EXIT=$?   # BUILD_EXIT=0
ctest -R '^VideoTrackTest$' --output-on-failure > $LOGS/test-3.log 2>&1; echo CTEST_EXIT=$?     # CTEST_EXIT=8 (same FAIL!)
git apply $LOGS/fix.patch                                                                       # applied OK
cmake --build build --target zene VideoTrackTest > $LOGS/build-4.log 2>&1; echo BUILD_EXIT=$?   # BUILD_EXIT=0
ctest -R '^VideoTrackTest$' --output-on-failure > $LOGS/test-4.log 2>&1; echo CTEST_EXIT=$?     # CTEST_EXIT=0
```

Affected-family count: `ctest -R '^VideoTrackTest$'` executed 1 test (212 - VideoTrackTest);
internally 13 test methods ran (12 passed + 1 failed before the fix; after the fix 100% passed, 1 skipped).

Gates:
```
bash tests/complexity-gate.sh --check > $LOGS/gate-1.log 2>&1; echo COMPLEXITY_EXIT=$?   # COMPLEXITY_EXIT=0
bash tests/file-length-gate.sh --check > $LOGS/gate-2.log 2>&1; echo LENGTH_EXIT=$?      # LENGTH_EXIT=0
```

## Files committed
- `src/core/VideoReference.cpp` (sign condition on line 77)
- `tests/src/core/VideoTrackTest.cpp` (regression method `negativeSubFrameTimecodeKeepsItsSign()`)
- `docs/qwen-fixes-2026-10/cluster-new-p10-451.md` (this report)
