# Cluster new-p8-273 — Fixed

Verdict: **FIXED**

## Finding and change

Default Discrete automation clips do not request sample-accurate ramps. With latency compensation enabled, their per-tick read rounds sub-tick frame latency away, so an automation step stays early. `Song::buildAutomationRamps()` now also publishes the existing frame-shifted ramp for a default-setting clip with positive latency while compensation is enabled. Explicit `RampSetting::Off` remains respected.

## File evidence

- `src/core/AutomationLatency.cpp:207-218`: the per-tick `readTime()` path converts frame latency to integer ticks; the existing `shiftBlock()` path carries the frame offset.
- `src/core/Song.cpp:1250-1262`: default-setting clips with positive compensated latency are admitted to the block-ramp path.
- `tests/src/core/W3EngineTest.cpp:197-227`: a Discrete step with 64 frames of latency asserts the shifted ramp holds the old value through the frame before the shifted boundary and has the new value at that boundary.
- `tests/upstream-modifications.txt:713`: records the inherited `Song.cpp` change and its regression test.

## Evidence table

| Evidence | Command exit | Result |
|---|---:|---|
| Initial RED, original production code | Build 0; ctest 8 | `hasRamp` was false. CTest ran 1 `W3EngineTest`; QtTest reported 7 passed, 1 failed, 2 skipped. |
| GREEN, fixed production code | Build 0; ctest 0 | CTest ran 1/1 `W3EngineTest`. |
| Reverse-patch RED proof | Reverse apply 0; rebuild 0; ctest 8 | The same `hasRamp` assertion failed with the production fix removed. |
| Reapplied-patch GREEN proof | Apply 0; rebuild 0; ctest 0 | The same test passed with the production fix reapplied. |
| `complexity-gate.sh --check` | 0 | PASS. |
| `file-length-gate.sh --check` | 0 | PASS. |

The affected-family count is **1 CTest test executed** (`W3EngineTest`, from `build/tests`).

## Exact command ledger

All test commands below ran with working directory `build/tests`; all other commands ran from the worktree root.

### Initial RED: regression against original production code

```sh
cmake --build build --target W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/build-1.log; echo EXIT=$rc
```

EXIT=0

```sh
QT_QPA_PLATFORM=offscreen ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/test-1.log; echo EXIT=$rc
```

EXIT=8

### GREEN: production fix applied

```sh
cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/build-2.log; echo EXIT=$rc
```

EXIT=0

```sh
QT_QPA_PLATFORM=offscreen ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/test-2.log; echo EXIT=$rc
```

EXIT=0

### Reverse-patch proof

```sh
git diff -- src/core/Song.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/fix.patch; rc=$?; echo EXIT=$rc
```

EXIT=0

```sh
cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/build-3.log; echo EXIT=$rc
```

EXIT=0

```sh
QT_QPA_PLATFORM=offscreen ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/test-3.log; echo EXIT=$rc
```

EXIT=8

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/fix.patch; rc=$?; echo EXIT=$rc
```

EXIT=0

```sh
cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/build-4.log; echo EXIT=$rc
```

EXIT=0

```sh
QT_QPA_PLATFORM=offscreen ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/test-4.log; echo EXIT=$rc
```

EXIT=0

### Final ratchet gates

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/gate-3.log; echo EXIT=$rc
```

EXIT=0

```sh
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-273.r1/logs/gate-4.log; echo EXIT=$rc
```

EXIT=0

## Files to commit

- `src/core/Song.cpp`
- `tests/src/core/W3EngineTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-273.md`
