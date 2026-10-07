# Cluster 1415: dither maximum-delta check

**Verdict: FIXED**

The old check narrowed each absolute difference to `int16_t` before comparing it with the maximum, so differences at or above 32,768 could wrap negative. The helper now computes the difference and maximum as `int`. A deterministic boundary test expects the distance from -32,768 to 0 to be 32,768.

Evidence in `tests/src/core/ExportWavDitherTest.cpp`:

- `:108` computes the maximum difference using `int`.
- `:164` adds the boundary test; `:168` expects 32,768.
- `:193` uses the helper for the exported-file check; `:212` retains its two-count bound.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `ctest -R '^ExportWavDitherTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` from `build/tests`; **EXIT=8**. Regression reported actual `0`, expected `32768`. |
| GREEN after fix | Same ctest command, output to `test-2.log`; **EXIT=0**, 1/1 CTest target passed. |
| Reversed-fix proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` **EXIT=0**; rebuild **EXIT=0**; ctest output to `test-3.log` **EXIT=8**, actual `0`, expected `32768`. |
| Restored-fix proof | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` **EXIT=0**; rebuild **EXIT=0**; ctest output to `test-4.log` **EXIT=0**, 1/1 CTest target passed. |
| Affected family | `ExportWavDitherTest`: 1 CTest test target executed; 1/1 passed on both green runs. |
| Complexity gate | **EXIT=0** |
| File-length gate | **EXIT=0** |

## Build and test commands

All builds ran from the worktree root; all ctest runs ran from `build/tests`.

```text
cmake --build build --target ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=2; initial helper type mismatch, corrected before RED
cmake --build build --target ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0, buggy calculation retained for RED
ctest -R '^ExportWavDitherTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8, RED
cmake --build build --target ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0, fixed calculation
ctest -R '^ExportWavDitherTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0, GREEN
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/fix.patch; rc=$?; echo EXIT=$rc  # EXIT=0, reverse the fix hunk while preserving the regression
cmake --build build --target ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0, fix reversed
ctest -R '^ExportWavDitherTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=8, reversed-fix RED
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/fix.patch; rc=$?; echo EXIT=$rc  # EXIT=0, restore the fix
cmake --build build --target ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0, fix restored
ctest -R '^ExportWavDitherTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0, restored-fix GREEN
```

The required gates ran from the worktree root:

```text
bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/gate-1.log' 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1415.r1/logs/gate-2.log' 2>&1; rc=$?; echo EXIT=$rc  # EXIT=0
```

Files committed:

- `tests/src/core/ExportWavDitherTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-1415.md`
