# Cluster new-p10-337: RNNoise processing rate

Verdict: **BLOCKED**. The RNNoise bug is fixed and the regression passes, but the two required repository ratchets both fail on unrelated files already present in the clean starting worktree. Their limits and baselines were left unchanged.

## Finding and fix

The finding is real. [RnnoiseDenoiserEffect.cpp](../../plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp#L93) now reports latency only when `AudioEngine::baseSampleRate()` is 48 kHz, and bypasses the effect unchanged at other engine processing rates. The plugin description states the 48 kHz requirement. [RnnoiseDenoiserEffect.h](../../plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h#L52) declares the out-of-line latency method and records RNNoise's required sample rate.

[RnnoiseDenoiserEffectTest.cpp](../../tests/src/plugins/RnnoiseDenoiserEffectTest.cpp#L71) initializes the engine at 44.1 kHz and checks both audio passthrough and zero latency.

## Regression evidence

Commands below were run from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-337/build/tests`.

| Run | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Original implementation (RED) | `ctest -R RnnoiseDenoiserEffectTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | 1/1 CTest failed: output was 0 instead of 0.25; latency was 479 instead of 0. |
| Fixed implementation (GREEN) | `ctest -R RnnoiseDenoiserEffectTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest passed. |
| Reversed fix (RED proof) | `ctest -R RnnoiseDenoiserEffectTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | The same two assertions failed after reversing the production patch. |
| Reapplied fix (GREEN proof) | `ctest -R RnnoiseDenoiserEffectTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest passed. |

The affected family executed one CTest target, containing the passthrough and latency regression slots. No test was skipped.

## Builds and patch reversal

The initial root-level command `cmake --build build --target zene RnnoiseDenoiserEffectTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/build-1.log 2>&1; echo EXIT=$?` exited 2 because the offscreen property preceded `add_test`. After moving that property, the same command in `build-2.log` exited 2 because the top-level Makefile does not expose the test subdirectory target by its bare name. The test was then built from its configured directory.

The following exact build commands exited 0 at each RED/GREEN proof stage; each numbered log records its stage:

- From the worktree root: `cmake --build build --target zene` (`build-4.log`, `build-7.log`, `build-10.log`).
- From `build/plugins/RnnoiseDenoiser`: `cmake --build . --target rnnoisedenoiser` (`build-5.log`, `build-8.log`, `build-11.log`).
- From `build/tests`: `cmake --build . --target RnnoiseDenoiserEffectTest` (`build-3.log`, `build-6.log`, `build-9.log`, `build-12.log`).

The required reverse/reapply commands both exited 0:

```sh
git diff -- plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/fix.patch; echo EXIT=$?
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/fix.patch; echo EXIT=$?
```

## Required gates

| Gate | Exact command | Exit | Evidence |
| --- | --- | ---: | --- |
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 1 | Six over-limit functions are reported in unrelated files (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`). No RNNoise function is listed as a regression. |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-337.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 1 | Fourteen over-limit files are reported; none is changed by this fix. |

`git diff --check` exited 0. Because the ratchets fail on untouched files and must not be re-anchored, those unrelated areas were not changed. The implementation and regression are ready, but the gate requirement remains unresolved.

## Files in the commit

- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp`
- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h`
- `tests/CMakeLists.txt`
- `tests/src/plugins/RnnoiseDenoiserEffectTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-337.md`
