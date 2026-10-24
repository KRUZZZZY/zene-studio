FIXED

## Decision and code evidence

The wavetable race was real. The product fix is already present in ancestor commit `899ae8ea7`: `WatsynInstrument::publishWave()` fills a fresh `WatsynWave` and atomically publishes it at `plugins/Watsyn/Watsyn.cpp:289-321`; `claimWave()` pins the published snapshot for a note at `plugins/Watsyn/Watsyn.cpp:325-334`; graph updates publish after filling their staging table at `plugins/Watsyn/Watsyn.cpp:666-680`. Old snapshots are recycled only after their pin count reaches zero and no note is inside the claim window (`plugins/Watsyn/Watsyn.cpp:338-355`).

The round 3 Windows compile blocker was present in this worktree: the Watsyn regression target at `tests/CMakeLists.txt:1944` defined `PLUGIN_NAME=watsyn` without `watsyn_EXPORTS`, while the compiled plugin source defines `PLUGIN_EXPORT` symbols at `plugins/Watsyn/Watsyn.cpp:51` and `plugins/Watsyn/Watsyn.cpp:1307`. The target now defines both macros. The generated test compile flags contain `-Dwatsyn_EXPORTS`.

The deterministic regression at `tests/src/plugins/WatsynWaveSnapshotTest.cpp:183-203` poisons half of the staging table between note starts. With the historical Watsyn sources, its published-wave assertion at line 200 fails; with the current sources it passes.

## Evidence table

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs`. CTest commands were run from `build/tests`.

| Check | Exact command | Result |
|---|---|---|
| Apply historical sources for red proof | `git diff 899ae8ea7^ HEAD -- plugins/Watsyn/Watsyn.cpp plugins/Watsyn/Watsyn.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/fix.patch` | EXIT 0 |
| Build historical Watsyn test | `cmake --build build --target WatsynWaveSnapshotTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/red-build-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"` | EXIT 0 |
| RED regression | `ctest -R '^WatsynWaveSnapshotTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/red-test-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"` | EXIT 8, expected failure at assertion line 200: late note read the staging table |
| Restore current Watsyn sources | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/fix.patch` | EXIT 0 |
| GREEN focused build | `cmake --build build --target zene WatsynWaveSnapshotTest WatsynSelectedGraphTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/green-build-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"` | EXIT 0 |
| GREEN regression | `ctest -R '^WatsynWaveSnapshotTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/green-test-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"` | EXIT 0, 1/1 passed |
| Watsyn test family | `ctest -R '^Watsyn' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"` | EXIT 0, 2/2 passed |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-203 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/feature-check-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"` | EXIT 0; all eight checks PASS, including complexity and file-length |
| Whitespace check | `git diff --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-203.r4/logs/diff-check-1.log 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc"` | EXIT 0 |

## Self-check

- **Finding decision:** REAL. The red run fails specifically because the new note renders the half-written staging data; the restored implementation passes the same assertion.
- **Cause and scope:** Snapshot publication and pinning are at the source of the race. This round changes only the Watsyn test target's compile definitions and its `tests/upstream-modifications.txt` rationale; no product runtime, project serialization, saved-data, or undo behavior changes.
- **Realtime:** The compile-definition change has no audio-thread effect. The existing snapshot code pins on note start and performs recycling on the publishing side; no new audio-thread allocation, free, lock, wait, or logging was added.
- **Regression and registration:** The existing deterministic `WatsynWaveSnapshotTest` is registered in `tests/CMakeLists.txt` and fails on the historical implementation, then passes with the current implementation. The Watsyn test family passed 2/2.
- **Gates:** `feature_check.py` exited 0 with all checks passing. The completion check is recorded in the final status after commit.

## Files committed

- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p17-203.md`
