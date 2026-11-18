# Cluster new-p9-113

**Verdict: FIXED**

`applyDawProjectModel` reuses mixer channels by position. When an imported
channel has an empty name, it now restores that position's localized mixer
default (`Master` or `Channel N`) instead of retaining the prior session's
name.

Evidence:

- `src/core/DawProjectSession.cpp:99` defines the position-based default name;
  `src/core/DawProjectSession.cpp:506` applies it for an empty imported name.
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:396` covers an unnamed
  imported channel over a previously named channel. Before the fix it failed:
  actual `Previous project channel`, expected `Channel 1`.

The affected CTest family is one target, `DawProjectInterchangeRoundTripTest`.
Its pre-fix run reported 35 QtTest cases (34 passed and the regression failed);
the passing CTest runs execute the full target.

## Evidence table

Commands below were run from the worktree root unless marked `build/tests`.
Build/test logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs`.

| Stage | Exact command | Result |
| --- | --- | --- |
| Original-code RED build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Original-code RED test (`build/tests`) | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8` |
| Fixed-code GREEN build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Fixed-code GREEN test (`build/tests`) | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Reverse product patch | `git diff -- src/core/DawProjectSession.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Removal-check build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Removal-check RED test (`build/tests`) | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8` |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Restored-code build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Restored-code GREEN test (`build/tests`) | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/complexity-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-113.r1/logs/file-length-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |

The requested `fff` log-search tool was unavailable in this environment, so
the log folder was searched with single-term `rg` queries instead.

## Files committed

- `src/core/DawProjectSession.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-113.md`
