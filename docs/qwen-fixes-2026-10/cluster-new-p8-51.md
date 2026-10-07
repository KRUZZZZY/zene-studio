# Cluster new-p8-51: arm-track undo classification

Verdict: **FIXED**

`record.arm_track` created an external take, but its transaction called
`record.disarm_track` a reversible inverse even though disarming keeps the WAV.
The command now records no inverse and is classified `irreversible`; `control.undo`
returns the typed refusal, while the reply and contract name explicit disarm and
the retained file as the fallback.

## Evidence

- [ControlCommandsRecordingRoutes.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-51/src/core/ControlCommandsRecordingRoutes.cpp:201) records no inverse for the arm; lines 343 and 368 describe the arm and explicit stop accurately.
- [ControlReversibilityTableRecording.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-51/src/core/ControlReversibilityTableRecording.cpp:165) classifies the arm as irreversible and names the retained-file fallback.
- [ControlRecordingRoutesTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-51/tests/src/core/ControlRecordingRoutesTest.cpp:73) arms a route, feeds four frames, requires typed undo refusal, verifies the take remains after explicit disarm, and checks the transaction and command descriptions.
- [RELEASE-NOTES-v0.3.0-alpha.md](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-51/docs/RELEASE-NOTES-v0.3.0-alpha.md:955) updates the A16 histogram to 818 rows: 404 `true_inverse`, 39 `snapshot`, 22 `irreversible`, 353 `not_mutating`.

## Regression evidence

All `ctest` commands below ran from `build/tests`. The affected family contains
three ctests: `ControlRecordingRoutesTest`, `ReversibilityContractTest`, and
`ReversibilityUndoTest`.

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| RED, original product code | `ctest -R 'ControlRecordingRoutesTest|ReversibilityContractTest|ReversibilityUndoTest' --output-on-failure` | 8 | 3 tests ran; the route test failed because `control.undo` succeeded and kept the take. Log: `test-1.log`. |
| GREEN, fixed code | `ctest -R 'ControlRecordingRoutesTest|ReversibilityContractTest|ReversibilityUndoTest' --output-on-failure` | 0 | 3/3 ctests passed after the histogram update and final source edits. Log: `test-6.log`. |
| Sensitivity RED, final product patch reverted | `ctest -R 'ControlRecordingRoutesTest' --output-on-failure` | 8 | 1 ctest ran; the same `!undo.ok` assertion failed. Log: `test-7.log`. |
| Sensitivity GREEN, final product patch reapplied | `ctest -R 'ControlRecordingRoutesTest' --output-on-failure` | 0 | 1/1 ctest passed. Log: `test-8.log`. |

The final sensitivity sequence rebuilt `zene` and `ControlRecordingRoutesTest`
before each ctest. Reverting the patch with
`git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-51.r1/logs/fix.patch`
returned EXIT 0 (`patch-3.log`); reapplying it with
`git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-51.r1/logs/fix.patch`
returned EXIT 0 (`patch-4.log`). Both builds returned EXIT 0 (`build-8.log`,
`build-9.log`).

The A16 family was also run once while the release-note histogram still had its
old class counts: the route test and undo test passed, while the contract test
correctly failed on the stale published histogram (`test-2.log`, EXIT 8). The
histogram was updated and the family then passed (`test-3.log`, EXIT 0).

## Build and gates

- `cmake --build build --target zene ControlRecordingRoutesTest ReversibilityContractTest ReversibilityUndoTest`: EXIT 0 in `build-7.log` (final source build). The initial invocation returned EXIT 2 because the generated target was not yet present (`build-1.log`); `cmake --build build --target help` returned EXIT 0 (`targets-1.log`), and the repeated target build succeeded (`build-2.log`).
- `cmake --build build --target zene ControlRecordingRoutesTest`: EXIT 0 for both final sensitivity builds (`build-8.log` and `build-9.log`).
- `bash tests/complexity-gate.sh --check`: EXIT 0 (`complexity-gate-2.log`).
- `bash tests/file-length-gate.sh --check`: EXIT 0 (`file-length-gate-2.log`).

Build, test, and gate output is in the requested log folder. The `fff` grep tool
was not exposed in this environment (`command -v fff` returned no path), so the
logs were searched with one-term `rg` queries for `error`, `FAILED`, and
`Assertion`.

## Files in this commit

- `src/core/ControlCommandsRecordingRoutes.cpp`
- `src/core/ControlReversibilityTableRecording.cpp`
- `tests/src/core/ControlRecordingRoutesTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/RELEASE-NOTES-v0.3.0-alpha.md`
- `docs/qwen-fixes-2026-10/cluster-new-p8-51.md`
