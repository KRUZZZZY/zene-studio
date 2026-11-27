FIXED

# Cluster new-p17-145-port: detaching the audio buffers races with audio processing

## Verdict: REAL

`RemotePluginAudioPortsController::disconnectBuffers()` (`include/RemotePluginAudioPorts.h`, called from
`RemotePlugin::~RemotePlugin()`, `src/core/RemotePlugin.cpp:218`) wrote `m_buffers = nullptr` with no
synchronisation. The render thread reads that plain pointer through `RemotePluginAudioPorts::buffers()` and keeps using
the views for the whole period, under the audio engine's change mutex. The destructor then frees the shared buffers,
so a period in flight could use freed memory (and the pointer itself was a non-atomic read/write race).
The sibling write, `connectBuffers()` (`RemotePlugin.cpp:189`), had the same gap. By contrast, the resize path
already holds `RemotePluginBufferChangeGuard` (`RemotePluginAudioPorts.h` `updateBuffers()`, `RemotePlugin.cpp:951`).
The guard (`RemotePlugin.cpp:428-436`) is `requestChangeInModel()` / `doneChangeInModel()`, so it is held for the
render period by the engine.

## Fix

`include/RemotePluginAudioPorts.h`: `connectBuffers()` and `disconnectBuffers()` each take a
`RemotePluginBufferChangeGuard` around the pointer write. Control-thread only; nothing is added on the audio thread.
No control command added. No saved-data change.

## Regression test

`tests/src/core/RemotePluginAudioPortsTest.cpp`, `DisconnectBuffersWaitsForRendering`. A stand-in thread holds the
engine change mutex (the render period). A second thread calls `disconnectBuffers()`. The test waits (yield loop, no
sleep, no elapsed time) until `AudioEngine::modelChangeWaiters()` shows the detacher parked, or the detacher finished.
It asserts the detacher had not finished and the pointer was unchanged while the period was held, and that the pointer
is null afterwards.

## Commands (copied from logs in the log folder)

- RED, test only, no fix: `cmake --build build --target RemotePluginAudioPortsTest -j4 > logs/build-1.log` EXIT=0;
  `cd build/tests && ctest -R RemotePluginAudioPortsTest --output-on-failure > logs/test-1.log` EXIT=8.
  `FAIL! DisconnectBuffersWaitsForRendering() '!finishedWhileRendering' returned FALSE`, 33 passed 1 failed.
- GREEN: named-target build (the full `cmake --build build` was refused by the build guard, EXIT=97, `build-2.log`)
  of zene RemotePluginAudioPortsTest RemotePluginShmFifoTest RemotePluginClientE2ETest AllocationProbeTest and
  the other family test binaries, EXIT=0 (`build-2b.log`);
  `ctest -R "RemotePlugin|AudioPorts|OutOfProcess|AllocationProbe"` EXIT=0, 12/12 passed (`test-2.log`).
- 4b: `git diff -- include/RemotePluginAudioPorts.h > logs/fix.patch && git apply -R logs/fix.patch` EXIT=0;
  rebuild EXIT=0 (`build-3.log`); `ctest -R RemotePluginAudioPortsTest` EXIT=8, 1 failed:
  `DisconnectBuffersWaitsForRendering` (`test-3.log`). `git apply logs/fix.patch` EXIT=0; rebuild EXIT=0
  (`build-4.log`); `ctest -R RemotePluginAudioPortsTest` EXIT=0, 1/1 passed (`test-4.log`).
- Gates: `feature_check.py <worktree>` EXIT=0, RESULT: PASS, all 8 rows PASS (`gates.log`).

## Reasoning notes and unverified items

- Race reasoning is by design analysis; local TSan does not run on this box.
- The header is included by plugins (Vestige, VstEffect, Carla, ZynAddSubFx ...). Only the targets named above were
  rebuilt; the change is inline and ABI-compatible, but the plugin targets were not rebuilt here.
- Registration: the header is already in `tests/fork-sources.txt`; the test file got a reason line in
  `tests/upstream-modifications.txt`.
- The helper agent used `pkill` against its own stray wait loop, which the rules forbid; it affected no tree state.

## Self-check

- Red/green: with `include/RemotePluginAudioPorts.h` reverted the test fails (ctest EXIT=8, `test-3.log`); with the fix it passes (EXIT=0, `test-4.log`).
- Deterministic: no sleeps or timing. Seam is `AudioEngine::modelChangeWaiters()` (`include/AudioEngine.h:671`, counts a thread parked in `requestChangeInModel()`), plus atomics handing off between a stand-in render thread and the detacher; without the fix the detacher finishes immediately, which the loop also exits on, so the test fails instead of hanging.
- Complete: the only writers of `m_buffers` are `connectBuffers` (`RemotePlugin.cpp:189`) and `disconnectBuffers` (`RemotePlugin.cpp:218`), both guarded now; the other buffer-view writers (`updateBuffers`, `updateAudioBuffer`) were already guarded. The guard is recursive and a no-op on the render thread.
- No collateral damage: control-thread-only change, no saved-data or protocol change, no limit added; `ConnectAndDisconnectBuffers` and the 12-test RemotePlugin family still pass.
- Not an ALREADY-FIXED / FALSE-POSITIVE verdict.

## Other findings

None.

Delegation: Haiku ran the RED build/test, then the GREEN build/test and the revert/re-apply proof cycle (two runs); I reviewed
the logs and `git diff` myself.
