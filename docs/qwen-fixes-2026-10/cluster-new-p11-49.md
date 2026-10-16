# Cluster new-p11-49

**Verdict: BLOCKED** — the reported behavior is already fixed on this branch, but the required ratchet gates fail on unrelated existing files, so the normal completion gates cannot be met without broadening this one-bug task.

## Finding evidence

- [ControlCommandsScript.cpp](../../src/core/ControlCommandsScript.cpp:286) takes a session checkpoint before running Lua and calls `finishScriptCheckpoint` after queued commands are applied ([line 304](../../src/core/ControlCommandsScript.cpp:304)). The transaction reports the checkpoint result at lines 316–328.
- [SessionCheckpoint.cpp](../../src/core/SessionCheckpoint.cpp:212) captures the post-script project state. On a change that fits the undo budget, it discards the script's finer nested steps, pushes one whole-session restore step, and marks the checkpoint pushed ([lines 243–246](../../src/core/SessionCheckpoint.cpp:243)).
- [SessionCheckpointTest.cpp](../../tests/src/core/SessionCheckpointTest.cpp:244) already covers a mutating script without Lua `addCheckPoint()` calls. It asserts exactly one new journal step, then verifies undo restores and redo reapplies the full project ([lines 264–288](../../tests/src/core/SessionCheckpointTest.cpp:264)).

No production or test source was changed. The finding is already addressed by the current implementation and existing regression, so a new RED test and reverse-patch proof do not apply.

## Evidence

| Check | Exact command / result |
|---|---|
| RED | N/A — finding already fixed; no production patch to reverse. |
| Build | `cmake --build build --target SessionCheckpointTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0 |
| GREEN | From `build/tests`: `ctest -R SessionCheckpointTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=0; 1/1 CTest target passed. |
| Affected family | `SessionCheckpointTest`: 1 CTest target executed; includes the script undo/redo regression above. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r1/logs/complexity-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=1. It reports six new CCN regressions in unrelated files: `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-49.r1/logs/file-length-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — EXIT=1. It reports 14 existing files over 500 lines, including `ControlAutomationScriptTest.cpp`; none were changed for this finding. |

The `fff grep` tool was not available in the current tool set, and `fff` was not installed as a shell command. The required log terms were checked with `rg`; `error`, `FAILED`, and `Assertion` had no matches in the build, test, or gate logs. The test log records `1/1 Test #177: SessionCheckpointTest ... Passed`.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p11-49.md` — this blocked outcome report only.
