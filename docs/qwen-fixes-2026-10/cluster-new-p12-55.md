# Cluster new-p12-55: running frame count wraps

**Verdict: BLOCKED.** The truncation is fixed and the behavioral regression passes. The required complexity and file-length gates still fail on unrelated entries already outside this change; their check-mode baselines were not written.

## Finding and fix

The finding is real. `include/Controller.h:131` declared `runningFrames()` as `unsigned int`, while `src/core/Controller.cpp:188` returned the product of the accumulated periods and frames per period. The 32-bit return type truncated counts above `UINT_MAX`, making `runningTime()` jump backward. The counter, frame result, and cache stamps now use `f_cnt_t`; the LFO elapsed-period difference uses the same width. A compile-time assertion keeps the audio-thread atomic lock-free.

The regression in `tests/src/core/ControllerSurfaceTest.cpp:169` seeds a period count just beyond the unsigned-int frame boundary and checks both `runningTime()` and `runningFrames()`. With the product patch reversed, it reported `actualTime = 0` and `expectedTime = 97391`.

## Evidence

All build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED, after reversing the product patch | `ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/test-3.log 2>&1` (run from `build/tests`) | 8 | 1 ctest target executed; the running-time assertion failed as expected. |
| GREEN, after reapplying the same patch and final rebuild | `ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/test-5.log 2>&1` (run from `build/tests`) | 0 | 1/1 ctest target passed. |
| Fixed build | `cmake --build build --target zene ControllerSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/build-6.log 2>&1` | 0 | Both `zene` and `ControllerSurfaceTest` targets built. |
| Additional controller-dependent targets | `cmake --build build --target zene peakcontrollereffect MainWindowTest MidiControllerMenuTest RackDeferredLifetimeTest ControllerTemplateMenuTest AutomationPrepassProofTest ClipJoinRippleTest AccessibilityWalkTest ControllerSurfaceTest MidiLearnThreadTest MidiLearnTest UndoHonestyTest PhaseDSidechainTest UndoHonestyStateTest StemSplitPipelineTest W3EngineTest MixerStripTest MixerConcurrencyTest MixerRoutingBackwardCompatTest PhaseFChannelScaleTest PdcMixerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/build-7.log 2>&1` | 130 | Built `zene`, `peakcontrollereffect`, `MainWindowTest`, `MidiControllerMenuTest` and `RackDeferredLifetimeTest`; interrupted while compiling `ControllerTemplateMenuTest`, so the remaining listed targets were not verified. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/gate-7.log 2>&1` | 1 | Reports six unrelated functions above CCN 10, including `dotCentreAlong` and `parseNotes`; no changed function is listed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/gate-8.log 2>&1` | 1 | Reports 14 unrelated files over 500 lines; none is changed here. |

The ratchet failures name unrelated files and end with `check mode: baseline not written`. No gate or baseline was changed.

An initial redundant build attempt (`build-1.log`) and the first post-reapply attempt (`build-5.log`) lost their exec session before an exit code was observed. They are not used as evidence; the same target build was rerun as `build-2.log` and `build-6.log`, each with exit 0.

## Patch proof

`git diff -- include/Controller.h src/core/Controller.cpp src/core/LfoController.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/fix.patch` exited 0. `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/fix.patch` exited 0; the red run above followed. `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-55.r1/logs/fix.patch` exited 0; the green run above followed.

## Files in this commit

- `include/Controller.h`
- `src/core/Controller.cpp`
- `src/core/LfoController.cpp`
- `tests/src/core/ControllerSurfaceTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-55.md`
