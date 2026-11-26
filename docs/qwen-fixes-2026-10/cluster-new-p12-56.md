FIXED

# Cluster new-p12-56: controller frame notifications allocated on the audio thread

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-56`
(branch `bugs/qwen-new-p12-56`). Product commit: `c2c8d1ce74` (product code unchanged after it; the
report commit follows it). Log folder, referred to below as `LOGS`:
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-56.r1/logs`.

The finding is real, and it is an allocation on the audio thread, per controller per period. It is
now a control-thread notification, throttled to one tick per 10 ms, and the render thread only
stores the period.

## The defect (evidence read this session)

- `src/core/AudioEngine.cpp:1215` - `Controller::triggerFrameCounter();` is called at the end of
  `AudioEngine::renderStageMix()`, i.e. on the render thread, inside
  `AudioEngine::renderNextPeriod()`'s `std::lock_guard{m_changeMutex}` and its `const RealtimeScope
  realtime;` (`src/core/AudioEngine.cpp:1221-1232`). It is the only production caller
  (`ControllerSurfaceTest`, `LfoControllerUserWaveTest`, `PeakControllerNullEffectTest`,
  `MidiLearnTest`, `ControllerSurfaceTest` and `PhaseDMixerTestSupport.h` are the test callers).
- `src/core/Controller.cpp:263-275` before the fix: that function looped `s_controllers` and
  `emit controller->valueChanged();` for every registered controller, once per period.
- The GUI's end of that signal is a queued delivery, not a direct call. The chain is all
  `Qt::DirectConnection` - `src/core/ControllerConnection.cpp:153-154` (controller signal to
  connection signal) and `src/core/AutomatableModel.cpp:744-745` (connection signal to
  `AutomatableModel::dataChanged()`) - and the last hop is a widget slot in the GUI thread,
  `src/gui/widgets/FloatModelEditorBase.cpp:646` (`model(), SIGNAL(dataChanged())` to
  `friendlyUpdate()`), an `Qt::AutoConnection`. From the render thread that hop is queued, so Qt
  allocates a `QMetaCallEvent` and appends it to the GUI thread's posted-event list on the audio
  thread. Measured, not assumed: the regression's first slot reported "1 queued controller
  notification(s) to the GUI thread and allocated 2 time(s) on the render thread" with the old code
  (`LOGS/test-11-red-final.log`).
- Nothing else in the tree was throttling it: `Controller::frequentUpdates()`
  (`include/Controller.h:97-108`) is only consulted by `FloatModelEditorBase::friendlyUpdate()` to
  skip *repaints* (`src/gui/widgets/FloatModelEditorBase.cpp:594-604`); the queued event and its
  allocation were already made by then.

## The fix (same commit, declaration and definition together)

- `src/core/Controller.cpp:263-274` - `triggerFrameCounter()` now does what the render thread is
  allowed to do and nothing else: `s_periods ++` (one relaxed atomic store), still under the
  existing change guard, which is empty on the render thread (`src/core/Controller.cpp:48-53`).
- `src/core/Controller.cpp:277-310` - new `Controller::dispatchFrameNotifications()`, the control
  thread's half. It returns immediately inside a `RealtimeScope` (so the render thread can never
  notify), returns when no period was published since the last tick, stamps `s_notifiedPeriod`
  *before* notifying (a receiver that calls back in cannot re-enter the walk), snapshots
  `s_controllers` under the change guard and then emits `valueChanged()` for each controller
  **outside** the guard, so GUI slots never run while the render thread waits for the lock. The
  snapshot is a control-thread allocation, not an audio-thread one.
- `src/core/Controller.cpp:57-107` - `FrameNotificationDispatcher` (TU-local `QObject` owning a
  `QTimer`, object name `controllerFrameNotificationTimer`, `Qt::CoarseTimer`, 10 ms), created on
  the thread the `QCoreApplication` lives on, parented to the application, armed by the first
  registered controller (`src/core/Controller.cpp:132-134`). This is the pattern the tree already
  uses for its other control-thread drain (`src/core/Song.cpp:153-158`, the 10 ms
  `automationWriteDrainTimer`), and the object name is the seam those tests already use.
- `include/Controller.h:136-143` and `:187-189` - the declaration of
  `dispatchFrameNotifications()` and the `s_notifiedPeriod` atomic.
- Why 10 ms: a period is 5.8 ms at 44.1 kHz / 256 frames and 23 ms at 1024 frames, and a screen
  repaints at 60 Hz (16.7 ms), so a 10 ms tick loses nothing visible and bounds the work to one
  signal per controller per tick however fast the engine runs. `Qt::CoarseTimer`, not
  `Qt::VeryCoarseTimer`, because Qt rounds a very coarse interval to whole seconds
  (Qt 6 docs, `qt.html` `Qt::TimerType`, read this session).

## Regression test and the matrix it covers

`tests/src/core/ControllerFrameNotificationTest.cpp` (new, 294 lines, registered in
`tests/CMakeLists.txt` next to the late appends, in `tests/fork-sources.txt` and in
`tests/all-sources.txt`):

| path | slot | what it pins |
|---|---|---|
| normal, GUI path | `theControlThreadTimerNotifiesTheGuiChain` | the real chain `Controller` -> `ControllerConnection` -> `AutomatableModel::dataChanged` -> a queued receiver is notified exactly once per tick, and not at all on a tick with no new period |
| normal, audio path | `renderThreadPeriodPostsNoQueuedNotificationAndAllocatesNothing` | one period inside a `RealtimeScope`: 0 queued notifications and 0 allocations on the calling thread (`AllocationProbe`), and the period still advanced |
| throttle | `oneTimerTickReportsEveryPeriodSinceTheLastTick` | 1 period -> 1 notification, a tick with no period -> none, 3 periods -> 1 (coalesced) |
| audio-thread constraint | `theRenderThreadNotifiesNothing` | firing the dispatcher inside a `RealtimeScope` notifies nothing; the period stays pending and the next control-thread tick reports it |
| reconfiguration | `aControllerDestroyedBeforeTheTickIsNotNotified` | a controller destroyed after the period and before the tick is not walked (no dangling notification); the survivor still is |
| exhaustion / platform | n/a | the path has no queue, no capacity and no platform code; the fix is Qt Core only (`QTimer`, `QCoreApplication`, `QThread`), no Qt-6-only API (valid for the Qt 5.15 / MSVC / macOS builds). Built here on GCC 16.2.1 / Qt 6.12.0 only - the Qt 5 and MSVC builds were not run in this session. |

## RED / GREEN with the exact committed patch (step 4b)

`LOGS/proof-4b.log`: `PATCH_EXIT=0`, `REVERSE_EXIT=0`, `APPLY_EXIT=0`.

```
cd <worktree>
git diff c2c8d1ce74^ c2c8d1ce74 -- include/Controller.h src/core/Controller.cpp > LOGS/fix.patch; echo PATCH_EXIT=$?
PATCH_EXIT=0
git apply -R LOGS/fix.patch; echo REVERSE_EXIT=$?
REVERSE_EXIT=0
cmake --build build --target ControllerFrameNotificationTest -j4 > LOGS/build-13-red-final.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R '^ControllerFrameNotificationTest$' --output-on-failure > LOGS/test-11-red-final.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=8          # 2 passed, 5 failed: "posted 1 queued controller notification(s) ... allocated 2 time(s) on the render thread";
                     # "no control-thread notification timer was created for the controller" x4
cd <worktree> && git apply LOGS/fix.patch; echo APPLY_EXIT=$?
APPLY_EXIT=0
cmake --build build --target ControllerFrameNotificationTest zene -j4 > LOGS/build-14-green-final.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R '^ControllerFrameNotificationTest$' --output-on-failure > LOGS/test-12-green-final.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0
```

The same cycle was run once before the final comment-only edit to `include/Controller.h`
(`LOGS/build-11-red-reverted.log` `BUILD_EXIT=0`, `LOGS/test-7-red-reverted.log` `TEST_EXIT=8`;
`LOGS/build-12-green-restored.log` `BUILD_EXIT=0`, `LOGS/test-8-green-restored.log`
`ControllerFrameNotificationTest` Passed), and the RED run before the fix existed is
`LOGS/test-2-red.log` `EXIT=8`.

## The rest of the family, and the gates

```
cd <worktree>
cmake --build build --target ControllerFrameNotificationTest ControllerSurfaceTest zene -j4 > LOGS/build-12-green-restored.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cmake --build build --target zene ControllerSurfaceTest LfoControllerUserWaveTest PeakControllerNullEffectTest MidiLearnTest AutomationPrepassProofTest AutomationPreFirstValueTest MixerStripTest AutomationModesTest MixerConcurrencyTest -j4 > LOGS/build-9-family-green.log 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ctest -R '^(ControllerFrameNotificationTest|ControllerSurfaceTest|LfoControllerUserWaveTest|PeakControllerNullEffectTest|MidiLearnTest|AutomationPrepassProofTest|AutomationPreFirstValueTest|MixerStripTest|AutomationModesTest|MixerConcurrencyTest)$' --output-on-failure > LOGS/test-9-family-final.log 2>&1; echo EXIT=$?
EXIT=8    # 9 of 10 pass; the one failure is ControllerSurfaceTest::TemplatesSaveListReadAndApply (see below)
cd build/tests && ctest -R '^(RtSafetySweep|RtSafetySelfTest)$' --output-on-failure > LOGS/test-10-rt-safety.log 2>&1; echo EXIT=$?
EXIT=0
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-2.log 2>&1; echo GATES_EXIT=$?
GATES_EXIT=0
```

`LOGS/gates-2.log`: `fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS,
check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in your
files), file-length PASS (no new item in your files)`, `RESULT: PASS`, and the same table re-run at the tip after the report and
ledger commit is `LOGS/gates-4-final.log`, `GATES_EXIT=0`, all eight rows PASS. An earlier run before the
commit (`LOGS/gates-1.log`, `GATES_EXIT=1`) failed `fork-sources` and `all-sources-reproduce` only
because `all-sources.txt`'s recipe is `git diff --diff-filter=A <base> HEAD` and the new test file
was still untracked; it reproduces once the file is committed.

`ControllerSurfaceTest::TemplatesSaveListReadAndApply` fails for a reason outside this change:
`ControllerSurface::saveTemplate()` (`src/core/ControllerSurface.cpp:115-137`) writes a real file
under `ConfigManager::userPresetsDir()`, which is `~/Documents/Zene Studio/presets/...`
(`src/core/ConfigManager.cpp:758-766`), and `~/Documents` is a **read-only filesystem** in this
session: `mkdir: cannot create directory '/home/kruzzzzy/Documents/Zene Studio': Read-only file
system`. The same slot fails identically with the product change reverted
(`LOGS/test-7-red-reverted.log`: "Totals: 14 passed, 1 failed") and with it applied
(`LOGS/test-9-family-final.log`: the same single failure), so it is environmental.

## Self-check

1. **The regression test fails with the product change reverted and passes with it.** Yes:
   `git apply -R LOGS/fix.patch` (REVERSE_EXIT=0), rebuild (BUILD_EXIT=0), `ctest -R
   '^ControllerFrameNotificationTest$'` -> `TEST_EXIT=8`, 5 of 7 slots failed, the first naming the
   bug ("posted 1 queued controller notification(s) ... allocated 2 time(s) on the render thread");
   `git apply LOGS/fix.patch` (APPLY_EXIT=0), rebuild (BUILD_EXIT=0), same ctest -> `TEST_EXIT=0`.
   Logs: `LOGS/test-11-red-final.log`, `LOGS/test-12-green-final.log`.
2. **The test is deterministic.** No sleeps, no event loop, no wall clock. The seam is the timer's
   object name: `QCoreApplication::instance()->findChild<QTimer*>("controllerFrameNotificationTimer")`
   (the pattern `AutomationRenderLockTest`, `AutomationModesTest` and `GranularPitchShifterRealtimeTest`
   already use for their timers), fired synchronously with
   `QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection)`. Periods are injected by
   calling `Controller::triggerFrameCounter()`; queued deliveries are drained with
   `QCoreApplication::sendPostedEvents(receiver)`, which is synchronous; allocations are counted on
   the calling thread by `AllocationProbe`; and `initTestCase()` calls
   `Engine::audioEngine()->audioDev()->stopProcessing()` so no background device thread publishes a
   period the test did not ask for.
3. **The fix is at the cause and complete.** The cause was an emit from the render thread; the
   render thread no longer emits anything, and the notification is produced on the control thread.
   Callers and siblings checked: the only production caller
   (`src/core/AudioEngine.cpp:1215`); `resetFrameCounter()` (`src/core/Controller.cpp:312-320`) still
   walks the list under the guard and emits nothing - deliberately unchanged, it has no production
   caller; `MidiController`'s own `emit valueChanged()` on a control change
   (`src/core/midi/MidiController.cpp:106`) is untouched, which is what the MIDI-learn and
   soft-takeover slots of `ControllerSurfaceTest` and `MidiLearnTest` exercise (they pass);
   both direct connection sites (`ControllerConnection.cpp:153`, `AutomatableModel.cpp:744`) are
   unchanged and now receive the signal from the control thread; GUI receivers of `dataChanged()`
   read `currentValue()` on demand, so they paint the newest value at whatever rate they are told.
   The header declaration and its definition are in one commit; every target that includes
   `Controller.h` was rebuilt for the runs above (`zene` plus the nine family test binaries).
4. **No collateral damage.** No serialization, no project format, no undo and no command changed
   (`ControlCommandsSnapshot` untouched, no command added). `s_periods` still advances exactly once
   per period, so `value()`, `valueBuffer()`, `prepareValueBufferForRender()`, `runningPeriods()`,
   `runningFrames()` and `runningTime()` behave exactly as before -
   `ControllerSurfaceTest::resettingFrameCounterRefreshesControllerBuffer`,
   `runningFrameCountDoesNotWrapAtUnsignedIntLimit` and `AutomationPrepassProofTest` pass. The only
   behavioural difference is that a *notification* now arrives at most every 10 ms instead of every
   period, from the control thread; a paint is coalesced by Qt anyway and the widget already
   throttles its repaint by `runningFrames()` (`FloatModelEditorBase.cpp:594-604`). No new limit or
   check rejects any caller: `dispatchFrameNotifications()` has no capacity, no queue and no
   refusal path other than "nothing new" and "not on a realtime thread".
5. **Verdict evidence** (not applicable - the verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE);
   the pre-fix code is quoted above by file:line and the RED run is the measurement.

## Other findings

- `~/Documents` is read-only in this session, so any test that writes under
  `ConfigManager::userPresetsDir()` fails here regardless of the change under test
  (`ControllerSurfaceTest::TemplatesSaveListReadAndApply`).
- `ControlAutomationModesTest` fails in this tree because no instrument module is visible to it
  ("the binary needs the instrument modules (LMMS_TEST_PLUGIN_DIR, see tests/CMakeLists.txt)",
  `LOGS/test-6-ctlautomodes.log`). It is not part of this fix's family; the session rule forbids
  setting `LMMS_PLUGIN_DIR` because it leaks into `PluginScanCacheTest`.
- Same class of hazard, not touched here: `emit nextAudioBuffer(m_outputBufferRead.get())`
  (`src/core/AudioEngine.cpp:1211`) is also emitted from the render thread, and `Oscilloscope`
  connects it with an `Qt::AutoConnection` slot (`src/gui/widgets/Oscilloscope.cpp:97,106`), so an
  open oscilloscope also queues an allocated event per period from the audio thread. Out of this
  finding's scope; worth its own cluster.
- `Controller::resetFrameCounter()` has no production caller (tests only); left as found.
- `AutomatableModel::s_periodCounter` is a plain `long` advanced on the audio thread and read from
  the GUI (`src/core/AutomatableModel.cpp:122`, `:806`, `:932`); pre-existing and outside this
  finding.
