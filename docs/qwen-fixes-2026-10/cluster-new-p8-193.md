# Cluster new-p8-193: malformed marker tick

Verdict: **FIXED**

`ProjectMarkers::load()` now checks `QString::toInt()` success before adding a marker. A malformed or out-of-range tick is warned about and that marker is skipped. A valid negative tick still clamps to zero as before. The loader change is at [src/core/ProjectMarkers.cpp:193](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-193/src/core/ProjectMarkers.cpp:193); the regression is at [tests/src/core/ProjectMarkersTest.cpp:283](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-193/tests/src/core/ProjectMarkersTest.cpp:283).

## Verification evidence

All build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-193.r1/logs`.

| Check | Exact command and working directory | Exit / result |
|---|---|---|
| Regression before fix | `ctest -R '^ProjectMarkersTest$' --output-on-failure` (cwd `build/tests`; `test-1.log`) | EXIT=8; 0/1 passed. The assertion saw 4 markers instead of 2. |
| Regression after fix | `ctest -R '^ProjectMarkersTest$' --output-on-failure` (cwd `build/tests`; `test-2.log`) | EXIT=0; 1/1 passed. |
| Reverse-patch proof | `git diff -- src/core/ProjectMarkers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-193.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-193.r1/logs/fix.patch` (cwd worktree) | EXIT=0. |
| Regression with fix reverted | `ctest -R '^ProjectMarkersTest$' --output-on-failure` (cwd `build/tests`; `test-3.log`) | EXIT=8; 0/1 passed. Again saw 4 instead of 2. |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-193.r1/logs/fix.patch` (cwd worktree) | EXIT=0. |
| Related test family | `ctest -R '^(ProjectMarkersTest|SessionPerformTest|AudioEditingTest)$' --output-on-failure` (cwd `build/tests`; `test-4.log`) | EXIT=0; 3/3 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (cwd worktree; `gate-1.log`) | EXIT=0. |
| File length gate | `bash tests/file-length-gate.sh --check` (cwd worktree; `gate-2.log`) | EXIT=0. |
| Whitespace check | `git diff --check` (cwd worktree) | EXIT=0. |

Build commands also exited 0: `cmake --build build --target zene` (logs `build-1.log`, `build-3.log`, `build-5.log`, `build-7.log`); `cmake --build build --target ProjectMarkersTest` (`build-2.log`, `build-4.log`, `build-6.log`); and `cmake --build build --target ProjectMarkersTest SessionPerformTest AudioEditingTest` (`build-8.log`). The final build and test ran after restoring the fix.

The `fff` grep tool was unavailable in this environment; log assertions and summaries were checked with `rg` against the individual log files.

## Files in the fix commit

- `src/core/ProjectMarkers.cpp`
- `tests/src/core/ProjectMarkersTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-193.md`
