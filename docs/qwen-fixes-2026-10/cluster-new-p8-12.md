# cluster new-p8-12 — `trim_set` refusal text says an invalid offset is in range

Verdict: **FIXED**

## Finding
Luna's patrol: `trim_set` rejects offsets outside `[-range, range]`, but the refusal
text said the offset "is within" the range — the opposite explanation.

## Evidence
- `src/core/ControlCommandsAutomationTools.cpp:98` (before):
  `if (offset < -range || offset > range) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'offset' is within the control's range")); }`
  The condition rejects an offset OUTSIDE the range; the message claimed it was inside.
- Fix: the message now reads `'offset' must be within the control's range` — it states
  the requirement the refusal is about. Behaviour (kind `InvalidArgs`, the range check
  itself, everything else in `trimSet`) is unchanged.

## Regression
New slot `trimSetOutOfRangeExplainsTheRefusal()` in the existing family file
`tests/src/core/ControlAutomationModesTest.cpp`: sets an offset at `range + 1` and
`-(range + 1)` through `automation.trim_set` and asserts the refusal is
`InvalidArgs` with the corrected message. It fails on the original text and passes
after the fix.

## Commands and exit codes (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-12.r1/logs)
RED (original code, new test added first):
- `cmake --build build --target ControlAutomationModesTest > LOGS/build-1.log 2>&1` EXIT=0
- `cmake --build build --target zene > LOGS/build-2.log 2>&1` EXIT=0
- `cmake --build build --target tripleoscillator > LOGS/build-3.log 2>&1` EXIT=0
  (the plugin module was not built in the tree yet; without it the whole family
  reports "no automatable device parameter" — test-1.log/test-2.log show that
  environmental failure, not the regression)
- `QT_QPA_PLATFORM=offscreen ctest -R ControlAutomationModesTest --output-on-failure > LOGS/test-3.log 2>&1` EXIT=8
  test-3.log: `FAIL! : ControlAutomationModesTest::trimSetOutOfRangeExplainsTheRefusal() Compared values are not the same`
  — `Actual (refused.errorMessage) : "'offset' is within the control's range"`;
  `Totals: 5 passed, 1 failed, 0 skipped, 0 blacklisted, 1422ms` (6 test cases executed).

GREEN (after the fix):
- `cmake --build build --target zene ControlAutomationModesTest > LOGS/build-4.log 2>&1` EXIT=0
- `QT_QPA_PLATFORM=offscreen ctest -R ControlAutomationModesTest --output-on-failure > LOGS/test-4.log 2>&1` EXIT=0
  test-4.log: `100% tests passed, 0 tests failed out of 1` (the one ctest entry runs all 6 test cases).

Gates (after the final edit):
- `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` EXIT=0
- `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` EXIT=0

## Files committed
- `src/core/ControlCommandsAutomationTools.cpp` (the message fix)
- `tests/src/core/ControlAutomationModesTest.cpp` (the regression slot)
- `docs/qwen-fixes-2026-10/cluster-new-p8-12.md` (this report)

Both sources were already registered in `tests/fork-sources.txt`
(`src/core/ControlCommandsAutomationTools.cpp` at line 1100; the test file among the
three fork test sources), so no registry change was needed.
