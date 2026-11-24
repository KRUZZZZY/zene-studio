FIXED

Cluster `new-p9-169`: "Re-enabling an insert can replay stale dry audio" (`src/core/HardwareInsert.cpp`).
Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-169`,
branch `bugs/qwen-new-p9-169`. Logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-169.r2/logs` (`$L` below).

This round answers the review of round 1: the round-1 fix cleared the dry delay line from
`HardwareInsert::setSettings`, which the channel-delete path runs **without** the change lock, so that clear
could race a period. The clear now runs only on the thread that owns the line. The original defect stays fixed
and stays pinned by its own test.

## The original verdict: REAL

- `src/core/HardwareInsert.cpp:227` - `process()` returns immediately while the insert is disabled, so nothing
  advances the dry line during a bypass; `src/core/HardwareIo.cpp:242` (`processInsert`) returns early too.
- `src/core/HardwareInsert.cpp:121` - `prepare()` returns as soon as the buffers are large enough, so
  `m_dry.init(...)` (`:127`) does not run again on a re-enable at the same block size.
- `src/core/HardwareInsert.cpp:383-384` - `writeOutput()` re-arms the delay from `latencyFrames()` every period,
  so the first period after a re-enable read the block buffered before the disable.

## The review's regression: REAL, and fixed here

The Delete key (`src/gui/MixerView.cpp:951`) calls `MixerView::deleteChannel`, which clears the channel at
`:781` and only then calls `Mixer::deleteChannel` at `:785` - and that is where the change lock is taken
(`Mixer::deleteChannel` starts with `Engine::audioEngine()->requestChangeInModel()`, `src/core/Mixer.cpp:1388`).
The clear reaches `src/core/HardwareIo.cpp:205` (`insert.setSettings(HardwareInsert::Settings{}, 0)`). Round 1
wrote the delay line there (`m_dry.reset()`), i.e. the ring and its non-atomic cursor `m_write` were written by
the GUI thread while the render thread read and wrote them in
`src/core/LatencyCompensation.cpp:118-135`. That is the reported regression; it is real and it was introduced by
round 1, not by the base code.

### Fix

The ring and cursor stay owned by one thread - the one that calls `process()`:

- `include/HardwareInsert.h:197-202` - new `std::atomic<bool> m_dryRestart{false}`: the settings only flag the
  restart, they never touch the line.
- `src/core/HardwareInsert.cpp:156-162` - on either transition (`settings.enabled != m_settings.enabled`)
  `setSettings` stores the flag (`std::memory_order_release`) instead of calling `m_dry.reset()`.
- `src/core/HardwareInsert.cpp:230-232` - `process()`, on the render thread, takes the flag
  (`exchange(false, std::memory_order_acq_rel)`) and calls `m_dry.reset()` before it next reads the line.
  No allocation, no free, no lock, no wait, no log: `reset()` only overwrites storage `init()` allocated
  (`src/core/LatencyCompensation.cpp:207-215`) plus two scalar stores, and the flag is a lock-free atomic.
- `include/LatencyCompensation.h:86-92` - `reset()`'s contract now states the ownership rule (the thread that
  calls `process()`, or the control thread only with no render in flight).

Why deferral rather than taking the lock: `m_changeMutex` is a `std::recursive_mutex` (`include/AudioEngine.h:888`),
so a lock in `Mixer::clearChannel` would work, but `clearChannel` is also on the channel-creation path
(`src/core/Mixer.cpp:1128`), so a song load would take one change-lock round trip per channel. Flagging the
restart keeps locking semantics unchanged and makes the new state single-writer instead.

## Test matrix

| Path | Test | Expectation |
| --- | --- | --- |
| Normal | `HardwareIoTest::clearingAnEnabledChannelLeavesTheDryLineToTheRenderThread` - two periods of 0.25, `mix` 0, latency 1 period | second period emits 0.25 (dry, one period late), `m_dry.delayFrames()` = fpp |
| Delete path (the review) | same test: `Mixer::clearChannel(index)` - the call `MixerView::deleteChannel` makes at `:781` - then the Delete key itself in the GUI test | the line is untouched (`delayFrames()` still fpp) and the restart is left to the render thread |
| Reconfiguration | same test, then re-enable and play 0.125 | first period silence, the next carries 0.125 |
| GUI / keyboard | `MixerInsertLatencyTest::theDeleteKeyRemovesAChannelWithAnEnabledInsert` - `QTest::keyClick(view, Qt::Key_Delete)` on a channel with an enabled insert holding a period of dry audio | the channel goes, and the mixer renders the next period finite |
| Original regression | `MixerInsertLatencyTest::reEnablingDoesNotReplayTheDryAudioBufferedBeforeTheDisable` (from round 1) | unchanged, still passes |
| Exhaustion | not applicable: no queue, no capacity, no clamp is touched (`MaxFrames`, `MaxManualOffsetFrames` unchanged) | - |
| Audio thread | `reset()` only overwrites existing storage; reached only from `process()`; the flag is atomic | no allocation, free, lock, wait or log |
| Platform | plain C++20 + Qt 6, no platform-specific code | built and tested on linux |

Both tests are deterministic: no threads, no clock, no sleeps. The core test drives the control side and the
period one after the other, the way the ping's two halves are driven elsewhere in the same suite; the GUI test
sends one key event and reads the mixer count.

## Commands and exit codes

### Negative control 1: the review's regression (round-1 product code, round-2 change reverted)

```
git diff -- include/HardwareInsert.h include/LatencyCompensation.h src/core/HardwareInsert.cpp \
  src/core/LatencyCompensation.cpp > $L/fix.patch            # DIFF_EXIT=0, 74 lines
git apply -R $L/fix.patch                                    # REVERSE_EXIT=0
cmake --build build --target HardwareIoTest -j4 > $L/build-7-r1only.log 2>&1        # A_BUILD_EXIT=0
cmake --build build --target MixerInsertLatencyTest -j4 >> $L/build-7-r1only.log 2>&1  # B_BUILD_EXIT=0
cd build/tests && ctest -R '^(HardwareIoTest|MixerInsertLatencyTest)$' --output-on-failure > $L/test-7-r1only.log 2>&1  # TEST_EXIT=8
```

```
FAIL!  : HardwareIoTest::clearingAnEnabledChannelLeavesTheDryLineToTheRenderThread() Compared values are not the same
   Actual   (insert.m_dry.delayFrames()): 0
   Expected (static_cast<int>(fpp))     : 256
```

i.e. with round-1's clear, the off-lock channel clear zeroed the line - the GUI thread wrote the render thread's
state. `MixerInsertLatencyTest` passed in that run (the round-1 behaviour fix is still present).

### Negative control 2: the whole product change, both rounds (base code)

```
git diff 21c2f3ee2e -- include/HardwareInsert.h include/LatencyCompensation.h src/core/HardwareInsert.cpp \
  src/core/LatencyCompensation.cpp > $L/fix-all.patch         # DIFF_EXIT=0, 92 lines
git apply -R $L/fix-all.patch                                 # REVERSE_EXIT=0
cmake --build build --target HardwareIoTest -j4 > $L/build-6-base.log 2>&1   # BUILD_EXIT=0
cd build/tests && ctest -R '^(HardwareIoTest|MixerInsertLatencyTest)$' --output-on-failure > $L/test-6-base.log 2>&1  # TEST_EXIT=8
```

```
FAIL!  : HardwareIoTest::clearingAnEnabledChannelLeavesTheDryLineToTheRenderThread() Compared floats are not the same
   Actual   (play(0.125f)): 0.25      Expected (0.f): 0
FAIL!  : MixerInsertLatencyTest::reEnablingDoesNotReplayTheDryAudioBufferedBeforeTheDisable() 'reEnabled == 0.f' returned FALSE.
0% tests passed, 2 tests failed out of 2
```

So both behaviours are pinned: at base the stale 0.25 is replayed (both tests catch it), at round-1 the off-lock
clear is caught, at HEAD both pass.

### GREEN (fix applied)

```
git apply $L/fix-all.patch   # APPLY_ALL_EXIT=0   (then)  git apply -R $L/fix.patch  # REVERSE_R2_EXIT=0
git apply $L/fix.patch       # APPLY_R2_EXIT=0    -> the fixed tree
cmake --build build --target HardwareIoTest -j4 > $L/build-8-fixed.log 2>&1          # A_BUILD_EXIT=0
cmake --build build --target MixerInsertLatencyTest -j4 >> $L/build-8-fixed.log 2>&1  # B_BUILD_EXIT=0
cd build/tests && ctest -R '^(HardwareIoTest|MixerInsertLatencyTest)$' --output-on-failure > $L/test-8-fixed.log 2>&1  # TEST_EXIT=0
```

### Family, on the final code (headers changed: `HardwareInsert.h`, `LatencyCompensation.h`)

```
cmake --build build --target zene -j4 > $L/build-9-family.log 2>&1                    # ZENE_EXIT=0
for t in MixerLatencyTextTest PdcMixerTest InstrumentLatencyPdcTest InstrumentLowLatencyMonitorTest \
         AudioBusPlayHandleQueueTest HardwareOutputBusTest RealtimeExportMenuTest MixerConcurrencyTest \
         MixerStripTest; do cmake --build build --target $t -j4 >> $L/build-9-family.log 2>&1; echo "$t=$?"; done
   # every target = 0
cd build/tests && ctest -R '^(HardwareIoTest|MixerInsertLatencyTest|MixerLatencyTextTest|PdcMixerTest|InstrumentLatencyPdcTest|InstrumentLowLatencyMonitorTest|AudioBusPlayHandleQueueTest|HardwareOutputBusTest|RealtimeExportMenuTest|MixerConcurrencyTest|MixerStripTest|ControlHardwareIo|ControlPdcCommands|ControlLowLatencyCommands)$' --output-on-failure > $L/test-9-family.log 2>&1  # CTEST_EXIT=0, 14/14 passed
```

`RealtimeExportMenuTest` includes `HardwareInsert.h`; `PdcMixerTest`, `InstrumentLatencyPdcTest`,
`InstrumentLowLatencyMonitorTest` and `AudioBusPlayHandleQueueTest` include `LatencyCompensation.h`;
`Mixer.cpp`, `MixerStrip.cpp`, `EffectChain.cpp`, `InstrumentOutputBuses.cpp`, `AudioBusHandle.cpp` and
`ControlCommandsPdc.cpp` are in `lmmsobjs`, rebuilt by the `zene` build above.

### Gates

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/gates-2.log 2>&1  # GATES_EXIT=0
```
(see `gates-1.log` and `gates-2.log`; both `RESULT: PASS`, with `complexity` and `file-length` reporting "no new
item in your files").

### GUI artifact

```
QT_QPA_PLATFORM=offscreen python3 tests/control-shell.py build/zene $L/shots > $L/gui-shell.log 2>&1  # GUI_EXIT=0
sha256sum $L/shots/shell-mix.png   # 7797710a414c7008d8a790daf03db97c05f0446d8fc344bc4122128e40361cc6
```

`gui-shell.log` ends with `PASS: shell control-surface driver (every check held)`, and the digest above is the
full `sha256` of the mix view's PNG.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Round-2 change
  reverted: `test-7-r1only.log`, `TEST_EXIT=8`, `Actual 0 / Expected 256` on `m_dry.delayFrames()`. Whole
  product change reverted: `test-6-base.log`, `TEST_EXIT=8`, both tests fail with the stale 0.25. Fixed:
  `test-8-fixed.log`, `TEST_EXIT=0`. Every run rebuilt the target first (`build-7-r1only.log`,
  `build-6-base.log`, `build-8-fixed.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races.** It uses one seam - `HardwareInsert::process()`
  called period by period with a buffer the test fills - and one control-thread call, `Mixer::clearChannel`,
  which is exactly what the Delete key's handler runs (`MixerView.cpp:781`). No thread is started, no clock is
  read, and the engine's device thread is stopped in `initTestCase`
  (`AudioDev()->stopProcessing()`, `HardwareIoTestParts1.h:29`). I first wrote the core test with a single buffer
  reused across periods; `process()` writes its output into that buffer, so the "history" was silence and the test
  could not see a stale block (`test-5-diag.log` shows `cap=16640 send=256`). It now refills the buffer each
  period, and the base run above proves it catches the replay.
- **The fix is at the cause and complete.** The single funnel for settings is `HardwareInsert::setSettings`, and
  all four callers inherit the behaviour: `ControlCommandsHardwareInsert.cpp:59` (`applyInsert`, under
  `underChangeLock`), `:74` (`undoPing`, under the lock), `HardwareIo.cpp:205` (`ChannelHardwareIo::clear`, no
  lock - the review's path) and `HardwareInsert::load`. The line is now written only by `process()`; the only
  other writer is `prepare()`'s `m_dry.init()` when the buffers grow, whose contract is unchanged (interface
  thread; commands under the lock, load with no render) and which was there before this cluster. Siblings that
  use `LatencyCompensation` are untouched by construction (the change adds a method and a flag):
  `AudioBusHandle.cpp:208` already advances its own line with silence while it has nothing to deliver.
- **No collateral damage.** `save()`/`load()` and the `<hardwareinsert>` XML are unchanged, so old projects load
  as before; no limit, clamp or validation was added (`MaxFrames`, `MaxManualOffsetFrames` untouched), so no
  caller that legitimately exceeds a limit is affected. Behaviour outside the two transitions is identical: the
  delay is still set from `latencyFrames()` every period, and `prepare()`'s growth re-init is unchanged.
  The 14-test family (mixer, PDC, instrument latency, hardware IO, control commands, realtime export) passes.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable; the verdict is FIXED and the code plus the base
  run above show the defect was live.
- **Sibling commands in the command files you changed.** No command file was changed: the product change is in
  `HardwareInsert` and `LatencyCompensation`. The commands that reach `setSettings` are `mixer.hardware_insert`
  (and the ping's undo path), which keep applying the whole settings struct, and the ping-reset rule
  (`routeChanged || !settings.enabled || waitingAtAnotherRate`) is untouched. `ControlHardwareIo`,
  `ControlPdcCommands` and `ControlLowLatencyCommands` all pass (`test-9-family.log`, CTEST_EXIT=0).

## Other findings

- `Mixer::clearChannel` still runs the rest of `HardwareInsert::setSettings` (the `m_settings` copy, the
  measurement rates) without the change lock, on the same delete path. That is pre-existing - it is what the
  base code did before this cluster - and fixing it means changing locking on the channel-creation path, which
  is out of scope for this finding. The ring and cursor, the part this cluster touched, no longer have a second
  writer.
- `src/core/Mixer.cpp:997` (`if (!frozen) { processInserts(fpp); }`): a frozen bus also skips the insert, so
  freezing a channel while its dry line holds audio and unfreezing it can replay that audio. Separate feature,
  separate state contract; left alone.
