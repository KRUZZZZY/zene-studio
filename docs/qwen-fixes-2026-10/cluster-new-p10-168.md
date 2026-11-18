# Cluster new-p10-168: nonfinite WASM soft clip output

**Verdict: FIXED**

## Evidence

- `modules/wasm/demo/gain_clip.c:59-60` replaces a nonfinite gain with zero.
- `modules/wasm/demo/gain_clip.c:64-77` maps NaN samples to zero and signed infinities to the corresponding clip limit; finite samples keep the existing soft-clip formula.
- `tests/src/core/AudioBufferTest.cpp:162-217` calls the actual `process()` implementation with infinite and NaN samples and NaN/infinite gains. The original code failed at line 193: actual `output[0]` was `nan`, expected `1`.
- `tests/upstream-modifications.txt:1054` records the inherited test-file change. The native ABI harness runs on GNU-compatible Linux x86_64 with `MAP_32BIT`; it skips where that ABI harness is unavailable.

## Test evidence

All `ctest` commands below ran from `build/tests`. The affected CTest family contains one registered executable, `AudioBufferTest`. Its QtTest totals were 32 passed / 1 failed before the fix and 33 passed / 0 failed after it.

| Stage | Exact command | Result |
|---|---|---|
| Original-code RED (`test-1.log`) | `ctest -R '^AudioBufferTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-168.r1/logs/test-1.log 2>&1` | EXIT=8; regression failed with NaN at `output[0]` |
| Fixed-code GREEN (`test-2.log`) | `ctest -R '^AudioBufferTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-168.r1/logs/test-2.log 2>&1` | EXIT=0; 33 passed, 0 failed |
| Fix-removal RED (`test-6.log`) | `ctest -R '^AudioBufferTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-168.r1/logs/test-6.log 2>&1` | EXIT=8; same assertion failed with NaN |
| Fix-restored GREEN (`test-7.log`) | `ctest -R '^AudioBufferTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-168.r1/logs/test-7.log 2>&1` | EXIT=0; 33 passed, 0 failed |

The fix-removal check used these commands, each with EXIT=0:

```sh
git diff -- modules/wasm/demo/gain_clip.c > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-168.r1/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-168.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-168.r1/logs/fix.patch
```

The build command was `cmake --build build --target zene AudioBufferTest > "$logfile" 2>&1`, with `$logfile` set to each numbered build log: `build-1.log` EXIT=2 while the native harness initially treated WASM attributes and pointer-width casts as errors; after adding test-local diagnostic handling, `build-2.log` through `build-8.log` each EXIT=0.

Final gates:

- `bash tests/complexity-gate.sh --check > "$logfile" 2>&1` — EXIT=0 (`complexity-gate-2.log`)
- `bash tests/file-length-gate.sh --check > "$logfile" 2>&1` — EXIT=0 (`file-length-gate-2.log`)

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-168.r1/logs`. The `fff grep` tool was not available in this session; each build, test, and gate log was searched with `rg -n -i` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `modules/wasm/demo/gain_clip.c`
- `tests/src/core/AudioBufferTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-168.md`
