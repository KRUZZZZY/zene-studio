# Cluster new-p11-49

**Verdict: BLOCKED** — the defect is real and the production guard is implemented. The strengthened regression reproduced the unjournaled mutation with the guard removed. A green build/test run for that exact final test source remains unverified because the shared bunny build runner kept both build slots occupied. The first final build was cancelled after about 18 minutes, and a post-commit retry was cancelled after another minute of the same wait. The earlier green run below used the same production guard and the initial form of the regression, before its failure diagnostic was strengthened.

## Finding and fix

- [ControlCommandsScript.cpp](../../src/core/ControlCommandsScript.cpp:291) now refuses `script.run` when `takeSessionCheckpoint()` returns a skipped checkpoint. The check is before `runWithBudget()`, so Lua cannot edit the session without a valid pre-script snapshot.
- [SessionCheckpointTest.cpp](../../tests/src/core/SessionCheckpointTest.cpp:292) disables project journalling, invokes a channel-mutating script, and checks refusal, unchanged project state, and unchanged undo depth. The final regression diagnostic records `script.run ok=true, project_changed=true, undo_depth_delta=0` without the guard.
- The existing ordinary one-step undo/redo test remains at [SessionCheckpointTest.cpp](../../tests/src/core/SessionCheckpointTest.cpp:245).
- `tests/src/core/SessionCheckpointTest.cpp` is registered in `tests/fork-sources.txt`; its inherited-file change is recorded in `tests/upstream-modifications.txt`.

## Build and test evidence

All build/test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs`. The `fff` grep tool was unavailable in this tool set; logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

| Check | Exact command and result |
|---|---|
| Initial RED build | `cmake --build build --target SessionCheckpointTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0. |
| Initial RED test | From `build/tests`: `ctest -R SessionCheckpointTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=8; the new assertion failed because `ran.ok` was true (12 passed, 1 failed). |
| Reverse-patch command | `git diff -- src/core/ControlCommandsScript.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/fix.patch` — EXIT=0. |
| Final-form RED build | `cmake --build build --target SessionCheckpointTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0. |
| Final-form RED test | From `build/tests`: `ctest -R SessionCheckpointTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=8; the diagnostic confirmed `ok=true`, `project_changed=true`, and `undo_depth_delta=0` (12 passed, 1 failed). |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/fix.patch` — EXIT=0. |
| Earlier GREEN build | `cmake --build build --target zene SessionCheckpointTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0. This built `zene` and `SessionCheckpointTest` with the guard and initial regression form. |
| Earlier GREEN test | From `build/tests`: `ctest -R SessionCheckpointTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0; 1/1 CTest target passed. |
| Final-form build attempt | `cmake --build build --target zene SessionCheckpointTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=130 after cancellation; the bunny guard logged repeated `all 2 build slots busy, waiting...` messages and did not start compilation. |
| Post-commit retry | `cmake --build build --target zene SessionCheckpointTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=130 after cancellation; the same guard wait continued for another minute without starting compilation. |
| Final-form GREEN test | Not run: the final-form build could not acquire a shared build slot. |

The affected CTest family was `SessionCheckpointTest` (1 CTest target). The final-form regression was run RED with the product patch reversed; only the earlier form has a GREEN result.

## Ratchets (separate from the defect verdict)

Both required gates were run after the final source/test edits and failed on unrelated files that are not part of this change.

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/complexity-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=1. Six reported CCN regressions: `tests/src/gui/XyPadPairTest.cpp` (`dotCentreAlong`, 16), `src/core/DawProjectReadTracks.cpp` (`parseNotes`, 13), `src/core/ControlCommandsMixerTools.cpp` (`adjust`, 12), `src/core/ControlCommandsMixerStrip.cpp` (`setStrip`, 11), `src/core/ProjectRevisions.cpp` (`rotateProjectRevision`, 11), and `src/gui/ControlCommandsShortcuts.cpp` (`importedOverrides`, 11).
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r2/logs/file-length-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=1. Fourteen reported overlength files: `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, `tests/src/core/CollabTransportTest.cpp`, `tests/src/core/ControlAutomationScriptTest.cpp`, `tests/src/core/ControlDeviceCatalogueTest.cpp`, `tests/src/core/ControlEditCommandsTest.cpp`, `tests/src/core/CustomAccessTest.cpp`, `tests/src/core/DeviceDspTest.cpp`, `tests/src/core/HardwareIoTest.cpp`, `tests/src/core/NotePlaybackTest.cpp`, `tests/src/core/RackMacrosTest.cpp`, `tests/src/core/RevisionTimelineTest.cpp`, `tests/src/core/VideoTrackTest.cpp`, and `tests/src/gui/ArrangerTimelineTest.cpp`.

## Files committed

- `src/core/ControlCommandsScript.cpp`
- `tests/src/core/SessionCheckpointTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-49.md`
