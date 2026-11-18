FIXED

# Cluster new-p21-113: reset forgotten undo coalescing time

## Decision and evidence

REAL. In `src/core/ControlUndoCoalescing.cpp:65-70`, `UndoCoalescer::breakRun()` cleared the key and top serial but left `m_atMs` unchanged. `sinceLastMs()` at `src/core/ControlUndoCoalescing.cpp:82-85` returns `-1` only when `m_atMs < 0` or the clock is invalid. The class contract at `include/ControlUndoCoalescing.h:89-90` documents `-1` as “none”. `breakRun()` now also sets `m_atMs = -1`.

The two callers are `setWindowMs()` at `src/core/ControlUndoCoalescing.cpp:72-79` and the control registry's recorded no-step path at `src/core/ControlRegistry.cpp:567`. `stepOpened()` at `src/core/ControlUndoCoalescing.cpp:57-63` starts a new run and timestamp. Invalid window values return before `breakRun()`, so a rejected reconfiguration preserves the active run.

## Regression matrix

| Path | Check | Result |
| --- | --- | --- |
| Normal lifecycle | Fresh coalescer reports `-1`; opening a step reports a nonnegative age. | Pass |
| Explicit forgotten run | After `breakRun()`, `sinceLastMs()` reports `-1`. | Pass with fix; failed without it |
| Valid reconfiguration | `setWindowMs(0)` breaks the run and restores `-1`. | Pass with fix; failed without it |
| Invalid reconfiguration | `-1` and `MaxUndoCoalesceWindowMs + 1` are rejected without forgetting the run. | Pass |
| Realtime and undo | Change is a `qint64` assignment in control bookkeeping; no new allocation, free, lock, wait, log, command or journal step. | Pass by code inspection |
| Platform/build | Uses existing C++20 and Qt APIs; `zene` and `UndoBoundsTest` build on the configured Linux tree. | Pass |

The regression uses no sleeps or elapsed-time threshold. It checks the sentinel after opening a run, then exercises both run-breaking routes. It uses the existing `QElapsedTimer` only to put the object in an active state; it does not measure a duration or require a clock injection seam.

## Build and test evidence

Commands ran from the worktree root unless noted. The `ctest` commands ran from `build/tests`.

| Run | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene UndoBoundsTest` | 0 | `build-1.log` |
| RED test | `ctest -R '^UndoBoundsTest$' --output-on-failure` | 8 | `test-1.log`: 1 CTest target; 13 QtTest cases passed and the two new forgotten-time cases failed |
| GREEN build | `cmake --build build --target zene UndoBoundsTest` | 0 | `build-2.log` |
| GREEN test | `ctest -R '^UndoBoundsTest$' --output-on-failure` | 0 | `test-2.log`: 1/1 affected-family target passed |
| Revert product patch | `git diff -- src/core/ControlUndoCoalescing.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-113.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-113.r1/logs/fix.patch` | 0 | `proof-1.log` |
| RED proof build | `cmake --build build --target zene UndoBoundsTest` | 0 | `build-3.log` |
| RED proof test | `ctest -R '^UndoBoundsTest$' --output-on-failure` | 8 | `test-3.log`: the same two new cases failed again |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-113.r1/logs/fix.patch` | 0 | `proof-1.log` |
| Final build | `cmake --build build --target zene UndoBoundsTest` | 0 | `build-4.log` |
| Final test | `ctest -R '^UndoBoundsTest$' --output-on-failure` | 0 | `test-4.log`: 1/1 affected-family target passed |

## Gates

The first `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-113` run exited 1 because `tests/all-sources.txt` contained 1,148 vendored `plugins/SfzPlayer/sfizz/**` paths excluded by that manifest's own recipe. `bash tests/fork-sources-gate.sh` and `bash tests/all-sources-reproduce.sh` also exited 1 for that same mismatch. The manifest entry list was regenerated from the exact published recipe; no gate limit or baseline was changed.

The regeneration command from the manifest header, `{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u`, exited 0. A subsequent `feature_check.py` run exited 0 (`feature-4.log`).

| Gate | Result after manifest repair |
| --- | --- |
| fork-sources | PASS, exit 0 |
| all-sources-reproduce | PASS, exit 0 |
| no-upstream-regression | PASS, exit 0 |
| check-namespace | PASS, exit 0 |
| no-tautology | PASS, exit 0 |
| doc-figures | PASS, exit 0 |
| complexity | PASS, no new item in changed files |
| file-length | PASS, no new item in changed files |
| `feature_check.py` overall | PASS, exit 0 (`feature-3.log`) |

## Files in the cluster commit

- `src/core/ControlUndoCoalescing.cpp`
- `tests/src/core/UndoBoundsTest.cpp`
- `tests/all-sources.txt` (re-derived from its published recipe to restore the registration gate)
- `tests/fork-sources.txt` (registers this report)
- `docs/qwen-fixes-2026-10/cluster-new-p21-113.md`

## Self-check

- **Regression proof:** `breakRunClearsLastStepTime()` and `changingWindowClearsLastStepTime()` failed with exit 8 before the fix and after the production patch was reversed; both passed with exit 0 after restoration. See `test-1.log`, `test-3.log`, `test-2.log` and `test-4.log`.
- **Determinism:** no sleep, race, or elapsed-time threshold is used. The existing timer is started through `stepOpened()`; assertions check only a nonnegative active sentinel and exact `-1` after forgetting the run.
- **Cause and completeness:** `breakRun()` now resets all three pieces of run state. Its callers are `setWindowMs()` and `ControlRegistry.cpp:567`; the latter only breaks when a recorded call produces no step. `stepOpened()` resets the timestamp for a new run, while `continues()` checks the key and serial before consulting elapsed time.
- **Collateral behavior:** this change touches no saved project format, load/render path, limit, command, or journal step. Existing projects continue to load under unchanged serialization; invalid window values still return before changing the active run. Only `sinceLastMs()` after a forgotten run changes to the documented `-1` sentinel.
- **Realtime:** the fix adds only a `qint64` assignment; it adds no allocation, free, lock, wait, I/O, signal, or logging operation.
- **Verdict branch:** the finding is FIXED, so the ALREADY-FIXED/FALSE-POSITIVE evidence request does not apply.
- **Registration:** the test was already registered in `tests/fork-sources.txt`; this new report is registered there as well.
- **Completion check:** `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-113 new-p21-113` is run after commit; expected exit 0, recorded in `submit-2.log`.
