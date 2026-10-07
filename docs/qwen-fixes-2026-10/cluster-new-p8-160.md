# Cluster new-p8-160: surround side-channel pan ramps

**Verdict: FIXED.** The finding was real. `processChannel()` previously read `m_panModel.value()` once and reused that pan for each side-channel plane. It now reads the model's `ValueBuffer` per frame when one is available, and retains the fixed-value path when no buffer exists.

Evidence: `src/core/MixerSurroundMix.cpp:207-234` reads the pan buffer at line 213 and applies its per-frame values at lines 225-228. `tests/src/core/SurroundTest.cpp:233-257` moves a real 5.1 bus's right-surround pan and compares all hardware-output frames with the value buffer.

## Verification evidence

The affected ctest family is `SurroundTest`: **1/1 test passed** on each green run. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-160.r1/logs/`.

| Stage | Exact command | Exit | Evidence |
|---|---|---:|---|
| RED, before fix (`test-1.log`) | `ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-160.r1/logs/test-1.log 2>&1` (cwd: `build/tests`) | 8 | `aSideChannelPanFollowsItsValueBuffer` failed; 256 mismatches. |
| GREEN, fix applied (`test-2.log`) | `ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-160.r1/logs/test-2.log 2>&1` (cwd: `build/tests`) | 0 | 1/1 passed. |
| RED, production patch reversed (`test-3.log`) | `ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-160.r1/logs/test-3.log 2>&1` (cwd: `build/tests`) | 8 | Same assertion failed; 256 mismatches. |
| GREEN, patch restored (`test-4.log`) | `ctest -R SurroundTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-160.r1/logs/test-4.log 2>&1` (cwd: `build/tests`) | 0 | 1/1 passed. |
| Build (`build-1.log` through `build-4.log`) | `cmake --build build --target zene SurroundTest` | 0 each | Rebuilt `zene` and `SurroundTest` before each ctest run. |
| Complexity gate (`complexity-gate-1.log`; final recheck `complexity-gate-2.log`) | `bash tests/complexity-gate.sh --check` | 0 both runs | Passed. |
| File-length gate (`file-length-gate-1.log`; final recheck `file-length-gate-2.log`) | `bash tests/file-length-gate.sh --check` | 0 both runs | Passed. |

Patch proof commands both exited 0:

```sh
git diff -- src/core/MixerSurroundMix.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-160.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-160.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-160.r1/logs/fix.patch
```

The fff grep tool was not available in this session; build, test, and gate logs were searched with `rg -n` for `error`, `FAILED`, and `Assertion`. The changed source and test are already listed in `tests/fork-sources.txt`.

## Files committed

- `src/core/MixerSurroundMix.cpp`
- `tests/src/core/SurroundTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-160.md`
