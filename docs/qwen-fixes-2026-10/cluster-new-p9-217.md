# Cluster new-p9-217

Verdict: **FIXED**

## Evidence

`src/core/LaunchBindings.cpp:151-158` handles a note-on consumed by learning, saves the new binding, and returns. Before the fix, the matching release then looked up that new binding and released its Gate cell. The fix stores the consumed note's channel/key and returns on its matching release before binding lookup. `include/LaunchBindings.h:31,82` holds that short-lived state.

`tests/src/core/RecordingPassTest.cpp:289-321` launches a Gate cell, learns a note while the cell is playing, then sends its release. On the original code the final phase was `Idle` (actual value 0), so the test failed; with the fix it remains `Playing`.

The affected ctest family contains **1 CTest test** (`RecordingPassTest`; CTest reported `1/1` on each run).

## Build and regression commands

All build and test output was captured under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs`.

| Stage | Exact command | EXIT |
|---|---|---:|
| Build regression on original code (`build-1.log`) | `cmake --build build --target zene RecordingPassTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/build-1.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| RED on original code (`test-1.log`) | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-217/build/tests && ctest -R '^RecordingPassTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/test-1.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 |
| Build with fix (`build-2.log`) | `cmake --build build --target zene RecordingPassTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/build-2.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| GREEN with fix (`test-2.log`) | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-217/build/tests && ctest -R '^RecordingPassTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/test-2.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 |
| Remove only product patch (`fix.patch`) | `git diff -- include/LaunchBindings.h src/core/LaunchBindings.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/fix.patch; patch_rc=$?; echo EXIT=$patch_rc` | 0 |
| Rebuild without fix (`build-3.log`) | `cmake --build build --target zene RecordingPassTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/build-3.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| RED with product patch removed (`test-3.log`) | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-217/build/tests && ctest -R '^RecordingPassTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/test-3.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/fix.patch; patch_rc=$?; echo EXIT=$patch_rc` | 0 |
| Final rebuild (`build-4.log`) | `cmake --build build --target zene RecordingPassTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/build-4.log 2>&1; build_rc=$?; echo EXIT=$build_rc` | 0 |
| Final GREEN (`test-4.log`) | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-217/build/tests && ctest -R '^RecordingPassTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/test-4.log 2>&1; test_rc=$?; echo EXIT=$test_rc` | 0 |

## Gates

| Gate | Exact command | EXIT |
|---|---|---:|
| Complexity (`gate-1.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/gate-1.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc` | 0 |
| File length (`gate-2.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-217.r1/logs/gate-2.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc` | 0 |

The requested `fff grep` tool was not exposed in this session (`command -v fff` found no executable and the available tool list had no fff search tool). I searched each captured log for `error`, `FAILED`, and `Assertion` with `rg -n`, one term per search; the only matches were the expected CTest failure summaries in the RED runs.

## Files committed

- `include/LaunchBindings.h`
- `src/core/LaunchBindings.cpp`
- `tests/src/core/RecordingPassTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-217.md`
