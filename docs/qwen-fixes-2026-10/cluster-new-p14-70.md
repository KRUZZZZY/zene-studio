FIXED

# cluster new-p14-70: malformed imported channel volume and pan

## Verdict

REAL defect, now fixed. It was not already fixed and it is not a false positive.

Before the fix, `plainValue()` in `src/core/ControlProjectTransplantChannels.cpp` called `QString::toDouble()` with no `ok` flag. A malformed value returned 0.0 instead of the caller's fallback. A malformed `volume` therefore muted an imported channel, and a malformed `pan` was forced to 0.0. The `git diff` for the product file shows the removed line `return channel.attribute(...).toDouble();` with no ok check.

Evidence (line numbers are for the fixed file; the pre-fix line is the removed `toDouble()` line in the diff):
- `plainValue` :63-73. Attribute branch :70, child-element branch :72.
- `describeSource` header :76, body :77-89. Its `plainValue` calls are at :84 (volume) and :85 (pan).
- `applyDescription` header :103. Its volume write `plainValue(description, "volume", 1.0)` is at :114.

Line references in the packet (`describeSource` :77-78, `applyDescription` :107-108) do not match the file exactly. :107-108 are the name and display-name lines. The corrected references are above.

## Fix

`src/core/ControlProjectTransplantChannels.cpp` (13 changed lines, one product file, `fix.patch` is 1719 bytes):
- Added `#include <cmath>` (:32).
- Added a local lambda `parsed(text)` in `plainValue`. It calls `text.toDouble(&ok)` and returns the parsed value only when `ok && std::isfinite(value)`. Otherwise it returns the caller's `fallback`.
- Both branches now go through `parsed`: the attribute branch (:70) and the child-element `value` branch (:72).
- Existing fallbacks are unchanged: `1.0` for volume and `0.0` for pan, from the existing callers.
- The header comment states the non-finite rule.

## Test matrix

Test file: `tests/src/core/ImportTrackIoTest.cpp`, 472 lines (under the 500-line gate). New test `malformedImportedChannelValuesFallBackToTheDefaults` at :429-443. Helpers `setChannelValue` :165, `rewriteDrumsChannel` :185, `expectImportedChannelState` :208.

| Case | Input | Expected | Line | Covered |
|---|---|---|---|---|
| Valid attribute | volume 0.5, pan 0.25 | 0.5 / 0.25 | :434 | yes |
| Malformed attribute | `loud` / `sideways` | 1.0 / 0.0 | :435-436 | yes |
| Non-finite attribute | `inf` / `-inf` | 1.0 / 0.0 | :437-438 | yes |
| Valid child element | `0.5` / `0.25` | 0.5 / 0.25 | :439-440 | yes |
| Malformed child element | `loud` / `sideways` | 1.0 / 0.0 | :441-442 | yes |
| Non-finite `nan`, `1e999` | | 1.0 / 0.0 | no row (same `isfinite` branch as `inf`, :68) | NO, not separately tested |

## Commands and exit codes (from the logs in `runs/new-p14-70.r1/logs/`)

- build-1: `cmake --build build --target ImportTrackIoTest`, ends "Built target ImportTrackIoTest". No EXIT line in the log.
- build-2, build-3, build-4: builds end with "Built target" lines. No EXIT line in these logs.
- build-5: EXIT=0. build-6: EXIT=0.
- test-1: `ctest -R ImportTrackIoTest --output-on-failure`. Test #376 Passed. No EXIT line.
- test-2: Test #376 ***Failed. No EXIT line.
- test-3: Test #376 ***Failed. `malformedImportedChannelValuesFallBackToTheDefaults` FAIL rows (fuzzy compare). No EXIT line.
- test-4: Test #376 Passed. `test-4-verbose.log:34` PASS `malformedImportedChannelValuesFallBackToTheDefaults`. 9 PASS rows, "100% tests passed out of 1".
- test-5: ImportDetectionTest (Test #134) Passed; MixerChannelPanTest (Test #262) Passed; 2/2.
- apply-reverse: EXIT=0 (reverse patch applied to the product file).
- test-6 (RED, reversed product file): Test #376 ***Failed; FAIL rows on the malformed test. **EXIT=8**.
- apply-restore: EXIT=0 (patch restored).
- fix-after.patch.log: CMP_EXIT=0 (`cmp fix.patch fix-after.patch` identical).
- test-7 (GREEN, restored): Test #376 Passed; "100% tests passed out of 1". **EXIT=0**.
- feature-check: `feature_check.py` on this worktree, `RESULT: PASS`, all rows PASS, **EXIT=0** (appended unpiped, `feature-check.log`).

Registration: `tests/fork-sources.txt:1969` lists `src/core/ControlProjectTransplantChannels.cpp`. `tests/fork-sources.txt:2816` lists `tests/src/core/ImportTrackIoTest.cpp`. `tests/upstream-modifications.txt:52` already lists the test file. No manifest change is needed.

## Reverse-patch proof

1. The product fix is in `fix.patch` (1719 bytes, one product file).
2. `apply-reverse` (EXIT=0) reverses it. The product file returns to the pre-fix code.
3. `test-6` runs the new test on that code: EXIT=8, ImportTrackIoTest fails on the malformed case.
4. `apply-restore` (EXIT=0) re-applies the fix. `fix-after.patch` is identical to `fix.patch` (CMP_EXIT=0).
5. `test-7` runs the new test on the restored code: EXIT=0, 100% passed.

## Self-check

1. **Reversed test fails and restored passes.** Yes. Reversed: test-6 EXIT=8. Restored: test-7 EXIT=0. The reverse and restore steps both exited 0 (apply-reverse, apply-restore).
2. **Deterministic XML fixtures, no sleeps.** I read the helper bodies at `tests/src/core/ImportTrackIoTest.cpp:155-220`: `mixerChannelNamed` (body :155-162), `setChannelValue` (:165-182), `rewriteDrumsChannel` (:185-199) and `expectImportedChannelState` (:208-220). `mixerChannelNamed`, `setChannelValue` and `rewriteDrumsChannel` use `QDomDocument`/`QDomElement` on the parsed `.mmp`, and `rewriteDrumsChannel` also uses `QFile` to read and write that file. None of the four uses `QTemporaryDir` directly: the temporary directory is the `QTemporaryDir m_dir` member (:466), and the helpers receive the `.mmp` path from their caller. `expectImportedChannelState` runs `project.import_tracks` and reads the result through `stateOf`, with no XML or file code of its own. `rg -n "sleep|qWait|usleep"` on the whole test file returned no matches (RG_EXIT=1). So none of the helpers sleeps or waits.
3. **Cause and completeness across both branches and both callers, including non-finite.** Both `plainValue` branches are fixed (:70 attribute, :72 child) and both are covered by the test (rows at :434-442). Both callers use `plainValue`: `describeSource` at :84-85 and `applyDescription` at :114. The test goes through the whole import path, so both callers run. Non-finite: every parsed value goes through the same `ok && std::isfinite(value)` check at :68 (the `parsed` lambda, used by both branches at :70 and :72). The test exercises that branch only through `inf` and `-inf` (rows :437-438). `nan` and `1e999` take the same branch, but no test row exercises them separately, and this report does not check whether `toDouble` reports `ok` for them. This is a gap in the test matrix, not a claimed pass.
4. **No collateral; valid controls and existing tests.** The product diff touches only `plainValue` and adds one include. Valid attribute (:434) and valid child (:440) still give 0.5 / 0.25. `test-4` has 9 PASS rows, including the existing `anImportKeepsItsChannelsAndTheirHardwareRouting`, `anImportIntoAnEmptyProjectCreatesBothChannels`, and the other pre-existing ImportTrackIoTest cases. `test-5` passes ImportDetectionTest and MixerChannelPanTest 2/2.
5. **No ALREADY-FIXED or FALSE-POSITIVE claim.** Pre-fix code (the removed `toDouble()` line in the diff) had no `ok` check, and the reverse run (test-6, EXIT=8) reproduces the failure. The defect is real and the fix addresses it.

## Other findings (outside this packet's scope, unchanged)

- `Mixer::loadFiniteMixerSetting` (`src/core/Mixer.cpp:217-220`) rejects only non-finite values. `AutomatableModel::loadSettings` (`src/core/AutomatableModel.cpp:430`) uses `LocaleHelper::toFloat` without an `ok` check. So a malformed volume in an ordinary project file may still load as 0. This report's scope is the transplant import path. Changing old-project loading is out of scope and was not changed.
