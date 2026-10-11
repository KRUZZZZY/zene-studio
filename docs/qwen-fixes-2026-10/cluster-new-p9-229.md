# Cluster new-p9-229 — reversed unsigned random bounds

Verdict: **FIXED**

## Evidence

- `include/lmms_math.h:184-190` orders the two endpoints before subtracting. For unsigned bounds, the span is then representable; the sampled offset is within that span, so adding it to the lower endpoint stays within the requested endpoints.
- `tests/src/core/MathTest.cpp:51-59` calls `fastRandInc(10u, 3u)` 32 times and checks every result is in `[3, 10]`.
- On the original implementation, the regression failed at the `QVERIFY` assertion. Reversing only the production patch reproduced the same failure; restoring the patch made it pass.

## Evidence table

| Evidence | Exact command | Exit |
|---|---|---:|
| RED: original regression | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/test-2-red.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 8 |
| GREEN: fixed regression | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/test-5-green.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 0 |
| RED: reversed-patch proof | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/test-7-redproof.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 8 |
| GREEN: restored-patch proof | `ctest -R '^MathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/test-9-greenproof.log 2>&1; echo EXIT=$?` (run from `build/tests`) | 0 |
| Affected family | `ctest -R '^MathTest$'` | 1 test executed; 1/1 passed on GREEN |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/gate-12-complexity-final.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/gate-13-file-length-final.log 2>&1; echo EXIT=$?` | 0 |

## Build and patch-proof commands

Build commands ran from the worktree root. CTest commands above ran from `build/tests`.

- RED test build: `cmake --build build --target MathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/build-1-red.log 2>&1; echo EXIT=$?` — EXIT=0.
- Initial full rebuild attempt: `cmake --build build -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/build-3-green.log 2>&1; echo EXIT=$?` — EXIT=97; `bunny-guard` requires a named target.
- GREEN named-target build: `cmake --build build --target MathTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/build-4-green.log 2>&1; echo EXIT=$?` — EXIT=0.
- Reverse only the production patch: `git diff -- include/lmms_math.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/fix.patch; echo EXIT=$?` — EXIT=0.
- Reversed-patch build: `cmake --build build --target MathTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/build-6-redproof.log 2>&1; echo EXIT=$?` — EXIT=0.
- Restore production patch: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/fix.patch; echo EXIT=$?` — EXIT=0.
- Restored-patch build: `cmake --build build --target MathTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-229.r1/logs/build-8-greenproof.log 2>&1; echo EXIT=$?` — EXIT=0.

`fff` grep was not available in this session; build and test logs were searched with `rg`, using one search term per invocation.

## Files in this commit

- `include/lmms_math.h`
- `tests/src/core/MathTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-229.md`
