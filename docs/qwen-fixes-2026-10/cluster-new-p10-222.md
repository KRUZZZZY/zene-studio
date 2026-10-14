# Cluster new-p10-222: mastering render count

Verdict: **FIXED**

## Evidence

`MasteringJob::run()` now resets `m_renderCount` at entry ([src/core/MasteringJob.cpp:373](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-222/src/core/MasteringJob.cpp:373)); `renderSourceMix()` still adds the render delta for that run at line 265. The regression at [tests/src/core/MasteringTest.cpp:228](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-222/tests/src/core/MasteringTest.cpp:228) calls `run()` twice on the same job, checks its count is 1, and checks that the renderer performed 2 renders.

| Evidence | Result |
| --- | --- |
| RED, final regression without the reset | `ctest -R '^MasteringTest$'`: EXIT=8. `job.renderCount()` was 2; expected 1. |
| GREEN, identical regression with the reset | `ctest -R '^MasteringTest$'`: EXIT=0; 1/1 registered CTest passed. |
| Affected-family count | One registered test, `MasteringTest`. The RED QtTest summary was 10 passed, 1 failed. |
| Complexity gate | EXIT=0 |
| File-length gate | EXIT=0 |
| Files committed | `src/core/MasteringJob.cpp`; `tests/src/core/MasteringTest.cpp`; `tests/fork-sources.txt`; this report. |

The existing fork-authored mastering test was missing from `tests/fork-sources.txt`; it is registered at line 1957. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/`. No `fff` search tool was exposed in this session, so log checks used bounded `rg` searches.

## Commands and exit codes

The initial regression run preceded the production change. The later reverse/reapply cycle below repeats the RED/GREEN proof with the final committed regression.

### Initial regression

```sh
cmake --build build --target MasteringTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/build-1.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=0

```sh
cd build/tests && ctest -R '^MasteringTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/test-1.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=8; the assertion showed actual 2, expected 1.

After the first fix, the uncompressed form of the regression passed:

```sh
cmake --build build --target zene MasteringTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/build-2.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=0

```sh
cd build/tests && ctest -R '^MasteringTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/test-2.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=0

### Reverse/reapply proof with the committed regression

```sh
git diff -- src/core/MasteringJob.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/fix.patch
```

EXIT=0

```sh
cmake --build build --target zene MasteringTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/build-3.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=0

```sh
cd build/tests && ctest -R '^MasteringTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/test-3.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=8; actual 2, expected 1.

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/fix.patch
```

EXIT=0

```sh
cmake --build build --target zene MasteringTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/build-4.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=0

```sh
cd build/tests && ctest -R '^MasteringTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/test-4.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=0; 1/1 registered CTest passed.

### Ratchet gates

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/gate-1.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=0

```sh
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-222.r1/logs/gate-2.log; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

EXIT=0
