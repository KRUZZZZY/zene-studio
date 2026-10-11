# Cluster new-p9-311

Verdict: **FIXED**. The NAM loader accepted zero layer/head widths and nonpositive
dilations, then used those values to size inference buffers and calculate history
indices. It also performed receptive-field multiplication and accumulation in
`int` without bounds checks.

## Evidence in the source

- `plugins/NeuralAmp/nam/NamModelLoader.cpp:269-286` now refuses nonpositive
  dilation, computes each receptive field in `int64_t`, bounds it against the
  history buffer extent, and bounds the accumulated receptive field against the
  prewarm `int` limit.
- `plugins/NeuralAmp/nam/NamModelLoader.cpp:295-300` refuses nonpositive input,
  head, channel, and bottleneck dimensions before the engine's Eigen buffers are
  built at lines 336-420.
- `plugins/NeuralAmp/tests/nam_harness.cpp:307-405` adds generated, weight-complete
  loader cases. Its valid control model loads; seven unsafe shapes are rejected.

The loader self-test is part of `nam_harness`, which is an `EXCLUDE_FROM_ALL`
headless executable and is not registered with CTest. The eight cases below were
run directly from `build/tests`; all eight are the affected-family test count.

## Regression evidence

| Check | Command case | Result |
|---|---|---:|
| Original loader, before the fix | `dilation-zero` (`red-2.log`) | EXIT=1; `FAIL: loader accepted unsafe config dilation-zero` |
| Fix removed, final regression source | `dilation-zero` (`red-10.log`) | EXIT=1; `FAIL: unexpected loader result for config dilation-zero` |
| Fix removed, total receptive-field overflow | `receptive-total-overflow` (`red-11.log`) | EXIT=1; `FAIL: unexpected loader result for config receptive-total-overflow` |
| Fix reapplied | `dilation-zero` (`green-56.log`) | EXIT=0; rejected |
| Fix reapplied | `receptive-total-overflow` (`green-59.log`) | EXIT=0; rejected |
| Fix reapplied | all 8 cases (`green-55.log`–`green-62.log`) | EXIT=0 each; valid model accepted, all invalid cases rejected |

### Build, patch proof, and tests

Every command below was run from the worktree root unless it starts with
`cd build/tests`. Build and test output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs`.

```text
cmake --build build --target zene nam_harness -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/build-2.log' 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ../plugins/nam_harness --test-loader-config dilation-zero > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/red-2.log' 2>&1; echo EXIT=$?
EXIT=1  (original loader)

cmake --build build --target zene neuralamp nam_harness -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/build-16.log' 2>&1; echo EXIT=$?
EXIT=0

git diff -- plugins/NeuralAmp/nam/NamModelLoader.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/fix.patch'; echo EXIT=$?
EXIT=0

cmake --build build --target zene neuralamp nam_harness -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/build-17.log' 2>&1; echo EXIT=$?
EXIT=0  (loader patch removed)

cd build/tests && ../plugins/nam_harness --test-loader-config dilation-zero > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/red-10.log' 2>&1; echo EXIT=$?
EXIT=1

cd build/tests && ../plugins/nam_harness --test-loader-config receptive-total-overflow > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/red-11.log' 2>&1; echo EXIT=$?
EXIT=1

git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/fix.patch'; echo EXIT=$?
EXIT=0

cmake --build build --target zene neuralamp nam_harness -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/build-18.log' 2>&1; echo EXIT=$?
EXIT=0  (fix reapplied)

cd build/tests && ../plugins/nam_harness --test-loader-config valid > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/green-55.log' 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ../plugins/nam_harness --test-loader-config dilation-zero > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/green-56.log' 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ../plugins/nam_harness --test-loader-config dilation-negative > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/green-57.log' 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ../plugins/nam_harness --test-loader-config dilation-overflow > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/green-58.log' 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ../plugins/nam_harness --test-loader-config receptive-total-overflow > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/green-59.log' 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ../plugins/nam_harness --test-loader-config channels-zero > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/green-60.log' 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ../plugins/nam_harness --test-loader-config bottleneck-zero > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/green-61.log' 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ../plugins/nam_harness --test-loader-config head-size-zero > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/green-62.log' 2>&1; echo EXIT=$?
EXIT=0
```

### Required gates

```text
bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/gate-9.log' 2>&1; echo EXIT=$?
EXIT=0

bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r1/logs/gate-10.log' 2>&1; echo EXIT=$?
EXIT=0
```

Log review used `rg -n` because the fff grep tool was not exposed in this session;
the final build, test, and gate logs had no `error`, `FAILED`, or `Assertion`
matches.

## Files committed

- `plugins/NeuralAmp/nam/NamModelLoader.cpp`
- `plugins/NeuralAmp/tests/nam_harness.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-311.md`
