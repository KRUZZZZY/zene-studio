# Cluster new-p9-200

**Verdict: FIXED**

`src/core/InstrumentFunctions.cpp:258` admitted a stacked chord key equal to `NumKeys`. Valid keys end at `NumKeys - 1`, so the loop now stops at `sub_note_key >= NumKeys`. The regression at `tests/src/core/NoteCurvesAcrossTest.cpp:257` stacks a chromatic chord from the highest valid key and checks the actual child handles.

## Evidence

CTest commands below ran with `build/tests` as the working directory. Build and test output was written to the listed new log files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline build | `cmake --build build --target zene NoteCurvesAcrossTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/build-1.log 2>&1` | 0 | Built `zene` and the affected test with the production bug present. |
| RED | `ctest -R '^NoteCurvesAcrossTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/test-1.log 2>&1` | 8 | 1/1 CTest tests failed; the regression saw 2 child keys, expected 1. |
| Fixed build | `cmake --build build --target zene NoteCurvesAcrossTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/build-2.log 2>&1` | 0 | Rebuilt `zene` and the affected test after the fix. |
| GREEN | `ctest -R '^NoteCurvesAcrossTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/test-2.log 2>&1` | 0 | 1/1 CTest tests passed. |
| Reverse production patch | `git diff -- src/core/InstrumentFunctions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/fix.patch` | 0 | Temporarily restored the buggy comparison. |
| Proof rebuild | `cmake --build build --target zene NoteCurvesAcrossTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/build-3.log 2>&1` | 0 | Rebuilt `zene` and the test with the production patch reversed. |
| Proof RED | `ctest -R '^NoteCurvesAcrossTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/test-3.log 2>&1` | 8 | Same assertion failed again: 2 child keys, expected 1. |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/fix.patch` | 0 | Restored the fix. |
| Final build | `cmake --build build --target zene NoteCurvesAcrossTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/build-4.log 2>&1` | 0 | Rebuilt `zene` and the test with the fix restored. |
| Proof GREEN | `ctest -R '^NoteCurvesAcrossTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/test-4.log 2>&1` | 0 | 1/1 CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/gate-1.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-200.r1/logs/gate-2.log 2>&1` | 0 | Passed. |

Affected family count: `NoteCurvesAcrossTest` had 1 registered CTest test, and each GREEN run executed 1/1. Each RED run reported 10 passing QtTest cases plus the failing regression.

The `fff grep` tool was not exposed in this session (`command -v fff` found no executable); the specified `error`, `FAILED`, and `Assertion` log searches were performed with `rg` against the log folder instead.

## Files committed

- `src/core/InstrumentFunctions.cpp`
- `tests/src/core/NoteCurvesAcrossTest.cpp` (already registered in `tests/fork-sources.txt`)
- `docs/qwen-fixes-2026-10/cluster-new-p9-200.md`
