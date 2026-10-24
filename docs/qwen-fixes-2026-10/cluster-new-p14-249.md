ALREADY-FIXED

new-p14-249: ALREADY-FIXED

Decision: REAL; the weakness was in the regression test. The state-load case is already corrected in commit `8327def668c9f284c70e5795449e6367fc0035bc` and remains at `tests/src/core/ReversibilityUndoTest.cpp:383-460`. It saves the original state, changes a numeric parameter, loads the saved state, confirms the load replaced the changed value, replays the recorded inverse, and checks the changed pre-load value is restored.

RED/GREEN proof (controlled product mutation): I temporarily changed the recorded undo closure in `src/core/ControlCommandsPluginState.cpp` to restore the loaded bytes instead of `previous`, which is the wrong-state inverse described by the finding. The regression failed at its readback assertion. I reverted that mutation and rebuilt before the passing run.

Commands and results (build commands from the worktree; ctest commands from `build/tests`):

- `cmake --build build --target zene ReversibilityUndoTest ReversibilityUndoDeviceTest` — `EXIT=0` (`001-baseline-build.log`).
- `ctest -R '^ReversibilityUndoTest$|^ReversibilityUndoDeviceTest$' --output-on-failure` — `EXIT=0`, 2/2 CTest tests (`002-baseline-ctest.log`).
- `git diff -- src/core/ControlCommandsPluginState.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/003-mutation-249.patch` — `EXIT=0`.
- `cmake --build build --target zene ReversibilityUndoTest` with the wrong-state mutation — `EXIT=0` (`004-mutation-249-build.log`).
- `ctest -R '^ReversibilityUndoTest$' --output-on-failure` with the mutation — `EXIT=8`; `theStateLoadInverseIsTheCheckpointItActuallyRecorded` failed because replay left parameter 0 at the loaded value (`005-red-249-ctest.log`).
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/003-mutation-249.patch` — `EXIT=0`.
- `cmake --build build --target zene ReversibilityUndoTest` after restoring production code — `EXIT=0` (`006-green-249-build.log`).
- `ctest -R '^ReversibilityUndoTest$' --output-on-failure` — `EXIT=0`, 1/1 CTest test (`007-green-249-ctest.log`).

5281: ALREADY-FIXED

Decision: REAL; the earlier test used a newer injected structural undo step. The correction is already committed in `6a38c2aa6c843acdcde76a028986a550c412ec38` and now lives in `tests/src/core/ReversibilityUndoDeviceTest.cpp:332-400` (the device cases were split out to keep the undo test files within the source layout). The test no longer pushes a separate undo step. It confirms the happy-path undo belongs to `plugin.unload`, unloads again through the command, injects a restore refusal using `setAutomatableModelRegistrationFailureForTest`, and asserts the next `control.undo` refuses while the device remains absent.

RED/GREEN proof (controlled product mutation): I temporarily made `undoThroughJournal` discard the structural restore failure result in `src/core/ControlCommandsControl.cpp`. The test failed because `control.undo` falsely reported success. I reverted the mutation and rebuilt before the passing run.

Commands and results (build commands from the worktree; ctest commands from `build/tests`):

- `git diff -- src/core/ControlCommandsControl.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/008-mutation-5281.patch` — `EXIT=0`.
- `cmake --build build --target zene ReversibilityUndoDeviceTest` with the discarded-failure mutation — `EXIT=0` (`009-mutation-5281-build.log`).
- `ctest -R '^ReversibilityUndoDeviceTest$' --output-on-failure` with the mutation — `EXIT=8`; `unloadUndoRefusesWhenTheDeviceCannotBeRestored` failed on `!undo.ok` (`010-red-5281-ctest.log`).
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/008-mutation-5281.patch` — `EXIT=0`.
- `cmake --build build --target zene ReversibilityUndoDeviceTest` after restoring production code — `EXIT=0` (`011-green-5281-build.log`).
- `ctest -R '^ReversibilityUndoDeviceTest$' --output-on-failure` — `EXIT=0`, 1/1 CTest test (`012-green-5281-ctest.log`).

new-p8-205: NEEDS-CONTEXT

Decision: REAL, but not completed in this batch. `tests/src/core/ReversibilityUndoTest.cpp:1-12` promises apply/undo/readback coverage for every reversible command, but its slots are a fixed list; it never iterates `reversibilityRowTable`, `ReversibilityTable::entries()`, or another row iterator. The existing `partAndLayerEditsUndoToTheirPreCommandState` case covers two previously missing commands, but does not prove the requested every-row behavior. A deterministic scan of the table declarations counted 445 reversible commands (415 `true_inverse`, 30 reversible `snapshot`) and 445 unique ids. A complete fix needs an explicit fixture, applicable state change, and pre-state observer for each row. There is no per-row fixture/observer catalog or safe generic observer here, and I did not substitute a partial list or claim it proves all rows. This item returns to the queue for an agreed fixture/catalog scope.

Evidence commands and results:

- Python scan of `src/core/ControlReversibilityTable*.cpp` using `R("id", RC::<class>, true, ...)` — `EXIT=0`, 445 reversible unique commands (415 true-inverse, 30 snapshot); output in `013-table-scope.log`.
- `rg -n 'reversibilityRowTable|ReversibilityTable::instance|entries\(' tests/src/core/ReversibilityUndoTest.cpp` — `EXIT=1`, no table iteration found; output in `013-table-scope.log`.
- The existing undo-family baseline is `ctest -R '^ReversibilityUndoTest$|^ReversibilityUndoDeviceTest$' --output-on-failure` — `EXIT=0`, 2/2 tests (`002-baseline-ctest.log`). This is not a RED/GREEN proof for complete table coverage; no such regression was added because the full fixture set remains unresolved.

## Evidence table

| Cluster | RED build / test | GREEN build / test | Affected CTest family |
|---|---|---|---|
| new-p14-249 | build `EXIT=0`; test `EXIT=8` (`004`, `005`) | build `EXIT=0`; test `EXIT=0` (`006`, `007`) | `ReversibilityUndoTest`, 1 test |
| 5281 | build `EXIT=0`; test `EXIT=8` (`009`, `010`) | build `EXIT=0`; test `EXIT=0` (`011`, `012`) | `ReversibilityUndoDeviceTest`, 1 test |
| new-p8-205 | Not run: needs-context; no complete per-row regression | Not run | Shared baseline: 2 tests, 2 passed |

Gate table: `feature_check.py` — `EXIT=0` (`020-final-feature-check.log`).

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS — no new item in changed files |
| file-length | PASS — no new item in changed files |

The first completion-check run, `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-249 new-p14-249`, returned `EXIT=1` because the first line started with the cluster id instead of the verdict (`017-submit-check.log`). I moved the verdict to line one. The corrected command `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-249 new-p14-249` returned `EXIT=0` with `RESULT: PASS - submit` (`019-submit-check-corrected.log`).

Files committed for this completion: `docs/qwen-fixes-2026-10/cluster-new-p14-249.md`. The two already-fixed regression changes are in the earlier commits named above. Test/source/log evidence is under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-249.r3/logs/`; source searches used `rg` because no `fff` search tool was available.

## Self-check

- Regression proof: new-p14-249 fails with the wrong-state inverse mutation (`EXIT=8`) and passes restored (`EXIT=0`). 5281 fails when `control.undo` drops the restore failure (`EXIT=8`) and passes restored (`EXIT=0`). new-p8-205 has no complete regression proof and remains NEEDS-CONTEXT.
- Determinism: state-load uses an explicit parameter value and state readback; unload uses the existing registration-refusal test seam. Neither uses sleeps, timing, or races.
- Cause and siblings: no production change is part of this completion. I checked the state-load recorded closure and `control.undo` restore-failure propagation. The state-load and unload test shapes exercise their command-recorded inverse paths; no sibling command behavior is claimed as fixed. The table-wide coverage request remains unimplemented as stated above.
- Collateral behavior: this completion changes only the report; the two existing test fixes do not change product behavior or saved-project formats.
- Already-fixed evidence: state-load checks changed pre-load state at `tests/src/core/ReversibilityUndoTest.cpp:425-460`; unload invokes the command's recorded step and injects failure at `tests/src/core/ReversibilityUndoDeviceTest.cpp:362-395`. Prior fix commits are `8327def668c9f284c70e5795449e6367fc0035bc` and `6a38c2aa6c843acdcde76a028986a550c412ec38`.
