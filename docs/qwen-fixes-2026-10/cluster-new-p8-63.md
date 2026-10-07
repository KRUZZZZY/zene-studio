# Cluster new-p8-63

**Verdict: FIXED**

`src/core/ControlCommandsVcaEditSet.cpp:174-220` now parses the stable `trk-<n>` address and checks the group’s stored edit-set ID before requiring a live `Track`. A stored missing ID can be removed, and the response and undo record use the same canonical track ID. Live nonmembers still return `refused`; dead IDs that were never in the group still return `not_found`.

The regression at `tests/src/core/ControlVcaCommandsTest.cpp:285-317` adds a track to an edit set, deletes it with `track.remove`, verifies the group reports the missing ID, removes it with `vca.track_remove`, and verifies undo restores the missing membership.

## Evidence

| Check | Command | Result |
|---|---|---|
| RED on original production code | `ctest -R '^ControlVcaCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/test-1.log; exit $rc` (from `build/tests`) | EXIT=8; 1 ctest failed. `staleTrackMembershipCanBeRemoved` failed because `vca.track_remove` returned `no track trk-6 (the song has 0)`; 8 Qt cases passed and 1 failed. |
| GREEN after fix | `ctest -R '^ControlVcaCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/test-2.log; exit $rc` (from `build/tests`) | EXIT=0; 1/1 ctest passed. |
| Reverse-patch proof | `git diff -- src/core/ControlCommandsVcaEditSet.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/fix.patch` | EXIT=0; rebuilding then rerunning the same regression failed again (test-3.log, EXIT=8). |
| RED after reverse patch | `ctest -R '^ControlVcaCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/test-3.log; exit $rc` (from `build/tests`) | EXIT=8; 1 ctest failed with the same stale-track `NotFound`. |
| Reapply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/fix.patch` | EXIT=0. |
| GREEN VCA family | `ctest -R '^(VcaGroupTest|VcaRefreshAtomicityTest|ControlVcaCommandsTest|ControlVcaEditGroupsTest|ControlVcaLockedEditsTest|ControlVcaCommands)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/test-5.log; exit $rc` (from `build/tests`) | EXIT=0; 6/6 ctest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/complexity-gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/complexity-gate-2.log; exit $rc` | EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/file-length-gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/file-length-gate-2.log; exit $rc` | EXIT=0. |

## Build commands

All builds exited 0:

- `cmake --build build --target ControlVcaCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-1.log; exit $rc` — EXIT=0.
- `cmake --build build --target zene ControlVcaCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-2.log; exit $rc` — EXIT=0.
- `cmake --build build --target zene ControlVcaCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-3.log; exit $rc` — EXIT=0 (production fix reversed).
- `cmake --build build --target zene ControlVcaCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-4.log; exit $rc` — EXIT=0 (fix reapplied).
- `cmake --build build --target zene VcaGroupTest VcaRefreshAtomicityTest ControlVcaCommandsTest ControlVcaEditGroupsTest ControlVcaLockedEditsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs/build-5.log; exit $rc` — EXIT=0.

The `fff grep` tool was not available in this session; build, test, and gate logs were searched with targeted `rg` queries instead. All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r1/logs`.

## Files committed

- `src/core/ControlCommandsVcaEditSet.cpp`
- `tests/src/core/ControlVcaCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-63.md`
