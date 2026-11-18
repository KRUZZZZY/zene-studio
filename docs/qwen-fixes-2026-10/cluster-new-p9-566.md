# Cluster new-p9-566: `valuesAfter()` increments `end()`

**Verdict: FIXED**

`src/core/AutomationClip.cpp:833-839` now returns `nullptr` when `lowerBound()` yields `end()` before calling `std::next(v)`. The existing `nv == end()` check still handles the final point. Regression coverage is in `tests/src/tracks/AutomationTrackTest.cpp:90-118`: one public-path check queries after the final point, and one guard-order check pins the UB prevention.

The public-path check alone passed on the original release build because this `std::map` iterator implementation happens to leave the returned result as `nullptr` despite the invalid increment. The source-order regression deterministically checks that the guard precedes the increment; it failed on the original source and when the production patch was reversed.

## Verification evidence

The affected CTest family ran one registered CTest test (`AutomationTrackTest`). The failing QtTest transcript reported 10 passing cases and the guard-order regression failing; the family therefore covers 11 QtTest cases.

| Stage | Exact command | Exit |
|---|---|---:|
| Initial public-path test, original implementation | `ctest -R '^AutomationTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`) | 0 |
| RED, original implementation plus guard-order regression | `ctest -R '^AutomationTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`) | 8 |
| GREEN, fixed implementation | `ctest -R '^AutomationTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`) | 0 |
| Reverse-patch proof, production patch removed | `ctest -R '^AutomationTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`) | 8 |
| Reapplied-patch proof, fixed implementation restored | `ctest -R '^AutomationTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`) | 0 |

Each run rebuilt `zene` and `AutomationTrackTest` first with `cmake --build build --target zene AutomationTrackTest`; build logs `build-1.log` through `build-5.log` each reported `EXIT=0`.

The required gates both passed:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/complexity-1.log 2>&1; rc=$?; echo EXIT=$rc` — `EXIT=0`.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/file-length-1.log 2>&1; rc=$?; echo EXIT=$rc` — `EXIT=0`.

The reverse-patch check and restoration both succeeded:

- `git diff -- src/core/AutomationClip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` — `EXIT=0`.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-566.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` — `EXIT=0`.

## Files committed

- `src/core/AutomationClip.cpp`
- `tests/src/tracks/AutomationTrackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-566.md`
