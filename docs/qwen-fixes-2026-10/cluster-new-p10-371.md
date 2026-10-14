# Cluster new-p10-371: invalid scene indices

Verdict: **FIXED**

`sceneState` now returns `SceneState::Empty` when `scenes <= 0` or `scene` is outside `[0, scenes)`. The existing row and phase precedence remains the same for valid indices. The row validity and per-track phase checks are small helpers so the complexity ratchet passes.

## Evidence

| Check | Command / result |
|---|---|
| RED before fix | From `build/tests`: `ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/red-1.log 2>&1` — **EXIT=8**. The suite reported 9 passed, 1 failed; `sceneState(2, 2, ...)` returned `Stopped`. |
| GREEN with final fix | From `build/tests`: `ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-green-2.log 2>&1` — **EXIT=0**, 1/1 CTest tests passed. The QtTest executable has 10 cases, as shown by the pre-fix run. |
| Final remove/restore proof | Removing the final product patch, rebuilding, and rerunning the suite failed (**ctest EXIT=8**); restoring it, rebuilding, and rerunning passed (**ctest EXIT=0**). Commands and logs are listed below. |
| Affected family count | `SessionGridViewTest`: **1 CTest test executed** (not zero); the executable's 10 cases pass after the fix. |
| Complexity gate | Final `bash tests/complexity-gate.sh --check` — **EXIT=0**. An earlier guard-only draft scored CCN 13 and returned EXIT=1; the helper extraction brought the final implementation under the limit. |
| File-length gate | `bash tests/file-length-gate.sh --check` — **EXIT=0**. |
| Files in this commit | `src/gui/SessionGridCells.cpp`; `tests/src/gui/SessionGridViewTest.cpp`; `docs/qwen-fixes-2026-10/cluster-new-p10-371.md`. |

The regression assertions are at `tests/src/gui/SessionGridViewTest.cpp:282-284`. They cover a positive out-of-range index that aliases a filled cell, a negative index paired with a playing scene value, and a zero-scene grid with an active phase. The guard and preserved valid-row calculation are at `src/gui/SessionGridCells.cpp:57-86`.

## Build, test, and gate commands

CTest commands below ran from `build/tests`; build and gate commands ran from the worktree root. Output was saved in the supplied log folder. Exit codes are copied from the command results.

```text
cmake --build build --target SessionGridViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/build-1.log 2>&1 — EXIT=0
ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/red-1.log 2>&1 — EXIT=8

cmake --build build --target zene SessionGridViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/build-2.log 2>&1 — EXIT=0
ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/green-1.log 2>&1 — EXIT=0
git diff -- src/gui/SessionGridCells.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/fix.patch — EXIT=0
cmake --build build --target zene SessionGridViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-remove-build-1.log 2>&1 — EXIT=0
ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-red-1.log 2>&1 — EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/fix.patch — EXIT=0
cmake --build build --target zene SessionGridViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-restore-build-1.log 2>&1 — EXIT=0
ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-green-1.log 2>&1 — EXIT=0

bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/gate-complexity-1.log 2>&1 — EXIT=1 (intermediate guard-only draft)
cmake --build build --target zene SessionGridViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/build-3.log 2>&1 — EXIT=0
ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/green-2.log 2>&1 — EXIT=0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/gate-complexity-2.log 2>&1 — EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/gate-file-length-1.log 2>&1 — EXIT=0

git diff -- src/gui/SessionGridCells.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/fix.patch — EXIT=0
cmake --build build --target zene SessionGridViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-remove-build-2.log 2>&1 — EXIT=0
ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-red-2.log 2>&1 — EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/fix.patch — EXIT=0
cmake --build build --target zene SessionGridViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-restore-build-2.log 2>&1 — EXIT=0
ctest -R '^SessionGridViewTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-371.r1/logs/proof-green-2.log 2>&1 — EXIT=0
```

The `fff` grep tool was not exposed in this session and no `fff` executable was available, so saved logs were searched with `rg`.
