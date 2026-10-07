# Cluster new-p8-27

Verdict: **FIXED**

## Finding and change

`stateSnapshot()` in `src/core/ControlCommandsPlugin.cpp:71` used to cut over-limit XML at 65,536 characters. It now returns an empty `state_xml` and keeps `state_truncated: true`. The `plugin.unload` inverse and mechanism text at `src/core/ControlCommandsPlugin.cpp:381` and `:405` report that no usable snapshot was captured. The `plugin.unload` reversibility contract at `src/core/ControlReversibilityTableStructure.cpp:129` now documents the same behavior.

The regression at `tests/src/core/ReversibilityUndoTest.cpp:486` adds a deterministic test effect whose valid state document exceeds the cap, unloads it through the real command, and checks the snapshot and fallback text.

## Verification evidence

CTest commands ran from `build/tests`. The affected family is one CTest target containing 12 QtTest cases; the green runs had 9 passed and 3 skipped.

| Stage | Command | Exit | Result |
|---|---|---:|---|
| Initial RED build | `cmake --build build --target zene ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/build-1.log 2>&1` | 0 | Rebuilt the app and affected test target before the regression run. |
| Initial RED | `ctest -V -R '^ReversibilityUndoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/test-1.log 2>&1` | 8 | 8 passed, 1 failed, 3 skipped. The new assertion failed because `before.state_xml` was nonempty. |
| Initial GREEN build | `cmake --build build --target zene ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/build-2.log 2>&1` | 0 | Rebuilt the app and affected test target after the fix. |
| Initial GREEN | `ctest -V -R '^ReversibilityUndoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/test-2.log 2>&1` | 0 | 9 passed, 0 failed, 3 skipped. |
| Revert product patch | `git diff -- src/core/ControlCommandsPlugin.cpp src/core/ControlReversibilityTableStructure.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/fix.patch` | 0 | Product patch reversed; test source stayed in place. |
| Revert-proof build | `cmake --build build --target zene ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/build-3.log 2>&1` | 0 | Rebuilt app and affected test target. |
| Revert-proof RED | `ctest -V -R '^ReversibilityUndoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/test-3.log 2>&1` | 8 | Same assertion failed; 8 passed, 1 failed, 3 skipped. |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/fix.patch` | 0 | Product patch restored. |
| Reapply build | `cmake --build build --target zene ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/build-4.log 2>&1` | 0 | Rebuilt app and affected test target. |
| Reapply GREEN | `ctest -V -R '^ReversibilityUndoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/test-4.log 2>&1` | 0 | 9 passed, 0 failed, 3 skipped. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/gate-1.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r1/logs/gate-2.log 2>&1` | 0 | Passed. |

Each build and test log also records its captured `EXIT` value. The `fff` log-search tool was not available in this environment; the logs were checked with single-term `rg` searches.

## Round 1 files committed

- `src/core/ControlCommandsPlugin.cpp`
- `src/core/ControlReversibilityTableStructure.cpp`
- `tests/src/core/ReversibilityUndoTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-27.md`

## Luna QA round 2

Verdict remains **FIXED**. The round 1 behavior fix already returned an empty `state_xml` for over-cap state, but the `plugin.unload` description at `src/core/ControlCommandsPlugin.cpp:312-318` still implied the `before` XML could always be restored by hand. It now describes when undo is available, states that over-cap state has empty `state_xml` and `state_truncated=true`, and says its settings cannot be manually restored from that snapshot. The related comment at `src/core/ControlStructuralSupport.cpp:445-450` now says the over-cap path reports state unavailable with no usable snapshot. The existing oversized-unload regression at `tests/src/core/ReversibilityUndoTest.cpp:486-524` now asserts the public description as well as the result fields. The inherited-file reason was updated in `tests/upstream-modifications.txt:683`.

All build and test logs below are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs`; each includes its captured `EXIT` value. CTest ran from `build/tests`.

| Stage | Command | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/build-1.log 2>&1` | 0 | Built the assertion before changing the description. |
| RED test | `ctest -R ReversibilityUndoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/test-1.log 2>&1` | 8 | The new `state_xml` description assertion failed. This CTest family has one registered test; QtTest totals were 8 passed, 1 failed, 3 skipped. |
| GREEN build | `cmake --build build --target zene ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/build-2.log 2>&1` | 0 | Built the app and affected test after the wording change. |
| GREEN test | `ctest -R ReversibilityUndoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/test-2.log 2>&1` | 0 | The single registered CTest test passed. |
| Reverse product patch | `git diff -- src/core/ControlCommandsPlugin.cpp src/core/ControlStructuralSupport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/fix.patch` | 0 | Temporarily removed only the round 2 production edits. |
| Negative-control build | `cmake --build build --target ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/build-3.log 2>&1` | 0 | Rebuilt with the description reverted and regression retained. |
| Negative-control test | `ctest -R ReversibilityUndoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/test-3.log 2>&1` | 8 | The same description assertion failed again; 8 passed, 1 failed, 3 skipped. |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/fix.patch` | 0 | Restored the production edits. |
| Final build | `cmake --build build --target zene ReversibilityUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/build-4.log 2>&1` | 0 | Rebuilt the app and test after restoring the patch. |
| Final test | `ctest -R ReversibilityUndoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/test-4.log 2>&1` | 0 | The single registered CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/gate-3.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-27.r2/logs/gate-4.log 2>&1` | 0 | Passed. |

The `fff` grep tool was not exposed in this environment. I searched the logs with single-term `rg` queries for `error`, `FAILED`, and `Assertion`, plus `FAIL!`, `Totals:`, and the CTest summary.

## Round 2 files committed

- `src/core/ControlCommandsPlugin.cpp`
- `src/core/ControlStructuralSupport.cpp`
- `tests/src/core/ReversibilityUndoTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-27.md`
