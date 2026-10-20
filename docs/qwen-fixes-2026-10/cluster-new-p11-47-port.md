# Cluster new-p11-47-port — revision undo/redo discarded file-operation failures

**Verdict: FIXED** (real bug on this branch; fixed, with a RED→GREEN regression).

## The finding

`src/core/ControlCommandsRevisions.cpp` recorded its inverse pair with closures that
dropped the `bool` result from the restore helpers, so an unsuccessful file restore
was not reported through the action: `control.undo` answered `undone: true` and
`control.redo` answered `redone: true` over a project file that was NOT put back.

## The fix

A side channel with the discipline the device channel already keeps — clear before
the step runs, read after, so the reply describes exactly the step that just ran.

- `include/ControlReversibility.h` + `src/core/ControlReversibility.cpp`: `recordActionRestoreFailure()` / `takeActionRestoreFailure()` / `clearActionRestoreFailure()`, declared beside `addUndoStep()`.
- `src/core/ControlCommandsRevisions.cpp`: the two recorded halves become named `undoRestoreStep()` / `redoRestoreStep()` that each record the failure the helper reported (including a failed removal of a restore-created file, even a dangling symlink).
- `src/core/ControlCommandsControl.cpp`: `actionRestoreFailureOf()` wraps the record into the typed failure `ControlErrorKind::Refused`; undo and redo clear before the journal step and read after.

## Evidence table

| Step | Command | Exit | Result |
|---|---|---|---|
| RED build | `cmake --build build --target zene RevisionTimelineTest` | 0 | built |
| RED run | `ctest -R RevisionTimelineTest` (build/tests) | 8 | 24 passed, 3 failed |
| fix build | `cmake --build build --target zene RevisionTimelineTest` | 0 | built |
| GREEN run | `ctest -R RevisionTimelineTest` (build/tests) | 0 | 28 passed, 0 failed |
| reverse product diff | `git apply -R logs/fix.patch` | 0 | reverted |
| no-fix build | `cmake --build build --target zene RevisionTimelineTest` | 0 | built |
| no-fix run | `ctest -R RevisionTimelineTest` | 8 | 24 passed, 3 failed |
| re-apply | `git apply logs/fix.patch` | 0 | restored |
| refix build | `cmake --build build --target zene RevisionTimelineTest` | 0 | built |
| refix run | `ctest -R RevisionTimelineTest` | 0 | 28 passed, 0 failed |
| complexity gate | `bash tests/complexity-gate.sh --check` | 1 | pre-existing regressions only; none introduced by this change |
| file-length gate | `bash tests/file-length-gate.sh --check` | 1 | pre-existing regressions only; none introduced by this change |

## Rework (round 1)

Luna asked to close the remaining misleading-success path: `redoRestoreStep` silently
returned when `revisionHoldingBytes(target, restoredSha)` was negative, after which
`control.redo` reported `redone: true`. It now records an action-restore failure
explaining that the selected bytes are no longer retained and redo left the live
file unchanged. Regression `redoOfARestoreWhoseRotationBytesAreGoneFailsTyped`
added (rotation restore, undo, remove retained copy, redo -> typed refusal, file
unchanged): RED before (27 passed, 1 failed), GREEN after (28 passed, 0 failed).

## Files committed

- `src/core/ControlCommandsRevisions.cpp` (fork-new)
- `src/core/ControlReversibility.cpp` (fork-new)
- `src/core/ControlCommandsControl.cpp` (inherited; reason appended to `tests/upstream-modifications.txt`)
- `include/ControlReversibility.h` (inherited; reason appended to `tests/upstream-modifications.txt`)
- `tests/src/core/RevisionTimelineTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-47-port.md`
