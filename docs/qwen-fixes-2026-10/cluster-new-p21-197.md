FALSE-POSITIVE
new-p21-197: FALSE-POSITIVE
Evidence: `include/InstrumentOutputBuses.h:101` documents that routes for buses missing from the current layout are retained for later. `src/core/InstrumentOutputBuses.cpp:211-212` implements this by storing a route when `route()` cannot use the current layout, and `:189-195` saves those retained routes. Bounding `saveSettings()` by `busCount()` would discard this supported deferred-route state. The actual stale route after a host layout shrink is cleared by the new-p21-196 fix; the compatibility test at `tests/src/core/InstrumentRoutingTest.cpp:265-294` proves that a deferred route survives saving and a later shrink until its bus appears.
Commands: `./InstrumentRoutingTest outputBusDeferredRoutesSurviveSaveBeforeLayoutAppears` passed against the original implementation (EXIT=0, log `31-deferred-original.log`) and with the fixes applied (EXIT=0, log `33-deferred-fixed.log`). This is characterization evidence, not a RED/GREEN bug test, because changing the save loop would break the documented contract.

new-p21-196: FIXED
Evidence: `src/core/InstrumentOutputBuses.cpp:109-118` now unroutes indices removed from the previous layout before assigning the smaller one. It limits cleanup to indices present in that previous layout, preserving deferred routes beyond it. `tests/src/core/InstrumentRoutingTest.cpp:244-262` checks route lookup, routed count, and saved XML after shrink; `:265-294` protects deferred-route compatibility.
RED: `outputBusShrinkClearsRoutesFromRemovedBuses` failed with the original code and with the product patch reversed (EXIT=1; logs `03-red-1.log` and `11-reversed-196.log`). GREEN: the same case passed after reapplying the patch (EXIT=0; logs `06-green-196.log` and `14-green-196.log`).

new-p21-198: FIXED
Evidence: `src/core/InstrumentOutputBuses.cpp:242-247` now rejects indices at or above `busCount()`, matching the existing bound in `routedIndex()` at `:232`. `send()` calls `routedBuffer()` before reading the slot (`:250-254`), and the host delivery loops use the current layout. `tests/src/core/InstrumentRoutingTest.cpp:297-314` confirms a deferred route remains stored while no buffer is exposed for an absent bus.
RED: `outputBusBufferRejectsBusOutsideCurrentLayout` failed with the original code and with the product patch reversed (EXIT=1; logs `04-red-2.log` and `12-reversed-198.log`). GREEN: the same case passed after reapplying the patch (EXIT=0; logs `07-green-198.log` and `15-green-198.log`).

## Test matrix

| Area | Coverage and result |
| --- | --- |
| Normal routing and persistence | Existing route/deliver and save/load tests; the full Qt test executable reports 15 passed, 0 failed. |
| Failure and capacity boundaries | Existing main, past-layout, inactive-bus, and malformed-route checks; `outputBusCapacityRemainsBounded` covers the last valid slot and the first overflow index. |
| Reconfiguration | Shrinking a four-bus layout clears the removed route and its XML entry. A loaded deferred route survives save and shrink, then remains available when its bus appears. |
| Audio-thread constraints | The new bound in `routedBuffer()` is a layout check plus existing pointer/atomic/buffer checks; it adds no allocation, lock, logging, wait, or Qt call. Layout cleanup remains in control-thread `setLayout()` under the existing model-change guard. |
| Platform/build | Built `zene` and `InstrumentRoutingTest` in the configured C++20 Qt Linux build. No header or platform-specific code changed; other platforms were not available in this worktree. |

## Evidence table

All direct QtTest commands ran from `build/tests`. The RED runs below are the per-test reversal proof from step 4b; the same two cases also failed in the pre-fix CTest run (`02-red-test.log`, EXIT=8; 12 passed and 2 failed).

| Cluster | State | Exact command | EXIT | Evidence log |
| --- | --- | --- | ---: | --- |
| new-p21-196 | RED, patch reversed | `./InstrumentRoutingTest outputBusShrinkClearsRoutesFromRemovedBuses > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/11-reversed-196.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/11-reversed-196.log; exit "$rc"` | 1 | Removed route remained. |
| new-p21-196 | GREEN, patch restored | `./InstrumentRoutingTest outputBusShrinkClearsRoutesFromRemovedBuses > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/14-green-196.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/14-green-196.log; exit "$rc"` | 0 | Route lookup/count/XML assertions passed. |
| new-p21-198 | RED, patch reversed | `./InstrumentRoutingTest outputBusBufferRejectsBusOutsideCurrentLayout > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/12-reversed-198.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/12-reversed-198.log; exit "$rc"` | 1 | Out-of-layout buffer was non-null. |
| new-p21-198 | GREEN, patch restored | `./InstrumentRoutingTest outputBusBufferRejectsBusOutsideCurrentLayout > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/15-green-198.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/15-green-198.log; exit "$rc"` | 0 | Null buffer assertion passed; route metadata remained stored. |
| new-p21-197 | Contract characterization, original | `./InstrumentRoutingTest outputBusDeferredRoutesSurviveSaveBeforeLayoutAppears > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/31-deferred-original.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/31-deferred-original.log; exit "$rc"` | 0 | Deferred route remains saved in the original implementation. |
| new-p21-197 | Contract characterization, fixed | `./InstrumentRoutingTest outputBusDeferredRoutesSurviveSaveBeforeLayoutAppears > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/33-deferred-fixed.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/33-deferred-fixed.log; exit "$rc"` | 0 | Deferred route remains saved with the fixes. |

## Builds, CTest, and gates

The product patch was saved and reversed with `git diff -- src/core/InstrumentOutputBuses.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/fix.patch` (EXIT=0). The reversed build `cmake --build build --target zene InstrumentRoutingTest` returned EXIT=0; both RED tests then returned EXIT=1. `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/fix.patch` returned EXIT=0; the restored build returned EXIT=0 and both GREEN tests returned EXIT=0. The final build and family CTest were:

| Purpose | Exact command | EXIT | Result |
| --- | --- | ---: | --- |
| Final build | `cmake --build build --target zene InstrumentRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/26-final-build.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/26-final-build.log; exit "$rc"` | 0 | `zene` and the test target rebuilt. |
| Final family test | `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/27-final-ctest.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/27-final-ctest.log; exit "$rc"` | 0 | 1/1 registered CTest passed. |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-197 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/28-final-feature-check.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/28-final-feature-check.log; exit "$rc"` | 0 | All eight rows PASS; complexity and file-length report no new items. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-197 new-p21-197 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/38-submit-final.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-197.r1/logs/38-submit-final.log; exit "$rc"` | 0 | `RESULT: PASS - submit`. |

`tests/all-sources.txt` had stale vendored sfizz entries despite its own recipe excluding `plugins/SfzPlayer/sfizz/`. Removing those generated-list entries made both `bash tests/fork-sources-gate.sh` and `bash tests/all-sources-reproduce.sh` pass (EXIT=0; logs `23-fork-sources-final.log` and `24-all-sources-final.log`).

## Self-check

- Regression reversal: Each real bug has a dedicated test. Both fail with the product patch reversed and pass after it is restored; the commands and exit codes are in the evidence table. The new-p21-197 characterization passes on original and fixed code because the finding conflicts with the documented deferred-route contract.
- Determinism: The new tests use synchronous layout and XML operations. They contain no sleeps, timing assumptions, or races.
- Cause and siblings: Shrink cleanup covers only previously present bus indices; `routedIndex()` and `routedBuffer()` both reject indices past the current layout. `send()` reaches `routedBuffer()` before slot use, and `deliver()`/LV2 delivery iterate the current layout.
- No collateral damage: Deferred routes absent from the current layout remain loadable and savable, including across shrink, and become available if the bus later appears. The save format is unchanged.
- Non-real verdict evidence: `include/InstrumentOutputBuses.h:101`, `src/core/InstrumentOutputBuses.cpp:211-212`, and the original/fixed characterization results document why new-p21-197 is a false positive.
- Realtime: `setLayout()` is control-thread configuration under the existing guard. `storeRoute(bus, -1)` operates on an existing slot without allocating a replacement; `routedBuffer()` adds no allocating or locking operation.
- Registration: `InstrumentRoutingTest.cpp` is already registered in CMake and `tests/fork-sources.txt`; no new product file or header was added. The corrected whole-tree source manifest now reproduces its documented generator.
- Completion check: the first post-commit `submit_check.py` run returned EXIT=1 because the verdict word was not the first line. After moving the verdict to line 1 and answering the prompts above, the check returned EXIT=0 and `RESULT: PASS - submit` (log `38-submit-final.log`). The same check is run once more after committing this report update.

## Files in this change

- `src/core/InstrumentOutputBuses.cpp` — shrink cleanup and current-layout buffer bound.
- `tests/src/core/InstrumentRoutingTest.cpp` — shrink, deferred-route, out-of-layout buffer, and capacity regressions.
- `tests/all-sources.txt` — remove stale entries excluded by the manifest's own generator.
- `docs/qwen-fixes-2026-10/cluster-new-p21-197.md` — decisions, evidence, commands, test matrix, and self-check.
