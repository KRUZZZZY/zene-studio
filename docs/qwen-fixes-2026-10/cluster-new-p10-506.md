# cluster new-p10-506 — automation target disconnect

**Verdict: BLOCKED.** The unsafe vector erase is fixed, and the regression is red on the original code and green with the fix. The required complexity and file-length gates both fail on unrelated, untouched files in this worktree. I did not change either gate or its baseline.

## Finding and change

The finding is real. `Song::processAutomations` traverses `AutomationClip::objects()` on the render thread. `objects()` releases its local clip mutex before the caller iterates, so changing the vector must be excluded by the engine render guard.

[`AutomationClipView.cpp:113`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-506/src/gui/clips/AutomationClipView.cpp:113) now calls `AutomationClip::removeObject`. [`AutomationClip.cpp:139`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-506/src/core/AutomationClip.cpp:139) takes the engine change guard before the clip mutex, then erases and publishes the change. The view uses [`AutomationClip::hasObjects()`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-506/include/AutomationClip.h:81) for its empty check.

The regression in [`AutomationClipDisconnectTest.cpp`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-506/tests/src/gui/AutomationClipDisconnectTest.cpp:98) holds the engine change guard while another thread removes a target, and separately invokes the arranger disconnect slot.

## Verification evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED on original code, from `build/tests` | `ctest -R AutomationClipDisconnectTest --output-on-failure` | 8 | Fails at `removeObjectWaitsForRenderPublication`: the clip has no synchronized removal API. Log: `test-4.log`. |
| Inverse-patch RED, from `build/tests` | `ctest -R AutomationClipDisconnectTest --output-on-failure` | 8 | Repeated after reversing only the three product files and rebuilding `zene` and the test. Same failure. Log: `test-6.log`. |
| GREEN, from `build/tests` | `ctest -R AutomationClipDisconnectTest --output-on-failure` | 0 | 1/1 registered CTest target passes; it contains two QtTest behavior slots. Log: `test-7.log`. The final rerun after the test header edit also passed (`test-8.log`, exit 0). |
| Reverted product build | `cmake --build build --target zene` | 0 | Log: `build-8.log`. |
| Fixed product build | `cmake --build build --target zene` | 0 | Log: `build-10.log`. |
| Reverted test build | `cmake --build build --target AutomationClipDisconnectTest` | 0 | Log: `build-9.log`. |
| Fixed test build | `cmake --build build --target AutomationClipDisconnectTest` | 0 | Log: `build-11.log`; final source rebuild `build-32.log`, exit 0. |
| Patch reversibility, worktree root | `git diff -- include/AutomationClip.h src/core/AutomationClip.cpp src/gui/clips/AutomationClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-506.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-506.r1/logs/fix.patch` | 0 | Reversed only product files. |
| Patch reapply, worktree root | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-506.r1/logs/fix.patch` | 0 | Restored the product fix before green rebuild. |
| Complexity ratchet, worktree root | `bash tests/complexity-gate.sh --check` | 1 | Six over-target regressions reported in untouched files: `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. Log: `gate-1.log`. |
| File-length ratchet, worktree root | `bash tests/file-length-gate.sh --check` | 1 | Fourteen over-500-line regressions reported in untouched files; none is in this patch. Log: `gate-2.log`. |

The final dependency sweep rebuilt each additional direct consumer of the changed header. Every exact command below exited 0; logs are `build-12.log` through `build-31.log` in the same order.

```text
cmake --build build --target ClapEffectIntegrationTest       EXIT=0
cmake --build build --target AutomationClipDisconnectTest   EXIT=0
cmake --build build --target AutomationTrackTest            EXIT=0
cmake --build build --target AutomationModeMenuTest         EXIT=0
cmake --build build --target AutomationEditorEditingTest    EXIT=0
cmake --build build --target W3EngineTest                   EXIT=0
cmake --build build --target AutomationWriteToEndTest       EXIT=0
cmake --build build --target AutomationThinTest             EXIT=0
cmake --build build --target AutomationModesTest            EXIT=0
cmake --build build --target AutomationLanesTest            EXIT=0
cmake --build build --target AutomationRenderAllocationTest EXIT=0
cmake --build build --target ControlAutomationModesTest     EXIT=0
cmake --build build --target ControlAutomationScriptTest    EXIT=0
cmake --build build --target EngineRenderBenchmarkTest      EXIT=0
cmake --build build --target MixingRoutingTest              EXIT=0
cmake --build build --target NoteCurvesAcrossTest           EXIT=0
cmake --build build --target SampleAccurateAutomationTest   EXIT=0
cmake --build build --target UndoHistoryPersistenceTest      EXIT=0
cmake --build build --target midiimport                     EXIT=0
cmake --build build --target ladspaeffect                   EXIT=0
```

### Superseded exploratory attempts

- `ctest -R AutomationClipDisconnectTest --output-on-failure` exited 0 in `test-2.log` and `test-3.log` on early test drafts against the original code. Those drafts were rejected; the final regression is the one that fails in `test-4.log` and `test-6.log`.
- `ctest -R AutomationEditorEditingTest --output-on-failure` exited 8 in `test-1.log` because the existing `arrowKeysMoveAPointThroughTheCommand` slot could not find an instrument plugin. `LMMS_PLUGIN_DIR` remained unset as required; the isolated regression test avoids that unrelated setup.
- `cmake --build build --target zene AutomationClipDisconnectTest` exited 2 in `build-2.log` because this build tree requires separate target invocations. Both targets subsequently built separately with exit 0.

## Files in the requested commit

- `include/AutomationClip.h`
- `src/core/AutomationClip.cpp`
- `src/gui/clips/AutomationClipView.cpp`
- `tests/src/gui/AutomationClipDisconnectTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-506.md`
