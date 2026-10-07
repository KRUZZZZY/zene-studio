# Cluster new-p8-238

**Verdict: FIXED**

`TrackSpacers::setSpacer()` limits labels to `MaxLabelLength` (120), but the project-section loader previously copied the XML label unchanged. `TrackSpacers::load()` now truncates the label before inserting it. The regression in `ArrangingCatchUpTest::aLoadedSpacerLabelIsLimited()` saves a project, injects a 121-character label into its `<track-spacers>` section, opens the project, checks the loaded label, then saves again and checks the persisted label.

Evidence: [src/core/TrackSpacers.cpp:89](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-238/src/core/TrackSpacers.cpp:89), [tests/src/core/ArrangingCatchUpTest.cpp:277](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-238/tests/src/core/ArrangingCatchUpTest.cpp:277). Both files are already registered in `tests/fork-sources.txt`.

## Regression evidence

| Check | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| RED with production patch reversed | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` (cwd: `build/tests`) | 8 | 1/1 CTest test ran. `aLoadedSpacerLabelIsLimited()` failed as intended: actual length 121, expected 120. QtTest summary: 17 passed, 1 failed, 1 skipped. |
| GREEN with production patch restored | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result` (cwd: `build/tests`) | 0 | 1/1 CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` (cwd: worktree root) | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` (cwd: worktree root) | 0 | Passed. |

## Build and patch-proof commands

All build commands ran from the worktree root. Build output was redirected to the named new log file.

| Command | Exit |
|---|---:|
| `cmake --build build --target ArrangingCatchUpTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target ArrangingCatchUpTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target zene ArrangingCatchUpTest tripleoscillator > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `git diff -- src/core/TrackSpacers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target zene ArrangingCatchUpTest tripleoscillator > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target zene ArrangingCatchUpTest tripleoscillator > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` | 0 |

Additional initial test runs, before the final regression setup and plugin prerequisite were corrected:

| Command | Exit | Observed result |
|---|---:|---|
| `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` (cwd: `build/tests`) | 8 | The initial XML lookup in the new test did not find the nested section; this was corrected before the meaningful RED run. |
| `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` (cwd: `build/tests`) | 8 | The intended label assertion failed at 121 vs 120; an unrelated existing handle test also failed because the configured `tripleoscillator` test plugin had not yet been built. |
| `ctest -R '^ArrangingCatchUpTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-238.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` (cwd: `build/tests`) | 0 | Passed after building the configured plugin target. |

The log-search tool `fff grep` was not exposed in this session; targeted one-term log searches used `rg` instead. No environment override for `LMMS_PLUGIN_DIR` was used.

## Files in the cluster commit

- `src/core/TrackSpacers.cpp`
- `tests/src/core/ArrangingCatchUpTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-238.md`
