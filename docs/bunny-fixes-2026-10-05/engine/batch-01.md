# Space Bunny fixer lane `engine` — batch-01

Worktree `zene-bunny-engine`, branch `bugs/bunny-engine-2026-10-05`.
Six findings triaged: 2 FIXED, 1 REAL-UNTESTED, 3 SKIPPED-SCOPE, 0 FALSE-POSITIVE, 0 ALREADY-FIXED, 0 DUPLICATE.

### Cluster 5491 — Debug sanitization logs from the audio processing path

Verdict: REAL-UNTESTED

Evidence: `src/core/AudioBus.cpp:110-116` — `AudioBus::sanitize()` writes to `std::cout` when it meets a
non-finite sample, but the writes sit inside `#ifdef LMMS_DEBUG`. `LMMS_DEBUG` is only added for
`CMAKE_BUILD_TYPE=Debug` (`CMakeLists.txt:869-870`) and this tree configures `RelWithDebInfo`, so the logging
is not compiled into the binary under test. The defect is real for Debug builds, but it is not observable in
this build configuration, so per the procedure no product code was changed.

Commit: none

Test: none

Red/green: none — the path is `#ifdef LMMS_DEBUG`-gated and not compiled in `RelWithDebInfo`; no deterministic
headless RED is possible without reconfiguring the build tree (which the lane rules forbid).

Leftovers: Debug-build console I/O on the sanitize path is still there. Deferred to whoever can build and
exercise a Debug configuration.

### Cluster 4591 — Silence detection disagrees at the exact threshold

Verdict: FIXED

Evidence: `src/core/AudioBus.cpp:212-247` — the stereo-pair branch marks a channel non-quiet only when
`std::abs(sample) > SilenceThreshold`, so a sample exactly at the threshold is quiet. The single-channel
branches at `src/core/AudioBus.cpp:249-268` used `std::abs(frame[0]) < SilenceThreshold` and
`frame[1] < SilenceThreshold`, making the same sample non-quiet. `SilenceThreshold` is `0.0001431f`
(`src/core/AudioBus.cpp:61`). Fixed by changing both single-channel lambdas from `<` to `<= SilenceThreshold`,
aligning them with the pair path (the majority branch) and with the intended "silent at or below threshold"
semantic.

Commit: `f3ab30372` — `fix(audiobus): silence threshold disagreed between the paired and single-channel paths`

Test: `AudioBusTest::UpdateThresholdSampleIsQuietOnBothPaths` (added to the existing `tests/src/core/AudioBusTest.cpp`)

Red/green:
```
cmake --build build --target AudioBusTest > build/bunny-b.log 2>&1
cd build/tests && QT_QPA_PLATFORM=offscreen ./AudioBusTest UpdateThresholdSampleIsQuietOnBothPaths
  EXIT=1 — FAIL!  'l.bus().update(*leftModel)' returned FALSE: tests/src/core/AudioBusTest.cpp(523)

cmake --build build --target AudioBusTest zene > build/bunny-b.log 2>&1
  EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^AudioBusTest$' --output-on-failure
  EXIT=0 (1/1 passed)
```
Related suites after the change: `cmake --build build --target AudioBusHandleTest MixerChannelPanTest DeviceDspTest AudioPluginTest`
EXIT=0, `ctest -R '^(AudioBusHandleTest|MixerChannelPanTest|DeviceDspTest|AudioPluginTest)$'` EXIT=0 (4/4).
Gates: `complexity-gate.sh --check` EXIT=0, `file-length-gate.sh --check` EXIT=0.

### Cluster 5467 — Play-handle list protection can block the audio worker

Verdict: SKIPPED-SCOPE

Evidence: `src/core/AudioBusHandle.cpp:161-193` — `doProcessing()` takes `m_playHandleLock` (line 171) before
walking the play-handle list, and the lock is deliberate: the audit comment at
`src/core/AudioBusHandle.cpp:163-170` records it as protection against reallocation or erase-during-iteration
by `addPlayHandle()`/`removePlayHandle()` (`src/core/AudioBusHandle.cpp:227`+) from other threads. Removing the
lock from the processing path is a lock-free redesign of mixer handle ownership, not a minimal fix, and the
realtime rule forbids trading the lock for an allocation or an unbounded queue instead.

Commit: none

Test: none

Red/green: none — no change made.

Leftovers: the processing path still takes a mutex; a lock-free play-handle ownership scheme remains open and
needs its own design pass.

### Cluster 5465 — Punch gating maps a capture batch to the wrong song interval

Verdict: SKIPPED-SCOPE

Evidence: `src/core/AudioEngine.cpp:257-272` — `drainInputStage()` drains up to `InputStageCapacityFrames`
(16384, `include/AudioEngine.h:303`) into one batch, which can exceed a single engine period, while
`punchWindow(frames)` (`src/core/AudioEngine.cpp:277-283`) derives the whole batch's timeline from
`m_periodStartFrame` (`include/AudioEngine.h:566`). The punch gate at `src/core/AudioEngine.cpp:551-575`
therefore maps every frame of an oversized batch to the current period start. A correct fix requires tracking
each batch's real timeline origin (or gating period-sized chunks) — a change to the input staging and capture
timeline on the realtime path, which the lane rules put out of scope.

Commit: none

Test: none

Red/green: none — no change made. `PunchWindowTest` (the pure `punchFramesForPeriod` helper,
`include/PunchWindow.h`) passes unchanged and covers the window math, not the batch origin.

Leftovers: punch gating of a drained capture batch larger than one period is still mapped to the period start.
Needs a batch-timeline design change on the realtime path.

### Cluster 3716 — Dummy loopback latency settings can request tens of gigabytes

Verdict: FIXED

Evidence: `src/core/AudioEngine.cpp:1297-1302` (pre-fix) — `latencyFromEnv()` returned
`qEnvironmentVariableIntValue()` verbatim for any positive value, up to `INT_MAX`, and
`configureLoopback()` (`src/core/AudioEngine.cpp:1310-1326`) sums both figures into `m_loopLine.assign(...)`
in `AudioDummy.h`, so two values near the limit requested more than 30 GB of `SampleFrame`. Confirmed
empirically: with `LMMS_DUMMY_INPUT_LATENCY`/`LMMS_DUMMY_OUTPUT_LATENCY` set to `2000000000`, the unfixed
build was OOM-killed (exit 137). Fixed by clamping each figure to `MaxLoopbackLatencyFrames` (`1 << 20`,
over 20 s at 48 kHz — no device reports more) before the sum, bounding the cable at ~16 MB.

Commit: `595c69261` — `fix(engine): bound dummy loopback latency read from the environment`

Test: `DummyPacingTest::loopbackLatencyFromTheEnvironmentIsBounded` (added to the existing `tests/src/core/DummyPacingTest.cpp`)

Red/green:
```
cmake --build build --target DummyPacingTest > build/bunny-b.log 2>&1
  EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ./DummyPacingTest loopbackLatencyFromTheEnvironmentIsBounded
  EXIT=1 — FAIL!  Compared values are not the same
    Actual   (device.inputLatencyFrames()): 5000000
    Expected (f_cnt_t(1 << 20)): 1048576
    tests/src/core/DummyPacingTest.cpp(82)

cmake --build build --target zene DummyPacingTest > build/bunny-b.log 2>&1
  EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^DummyPacingTest$' --output-on-failure
  EXIT=0 (1/1 passed)
```
The test uses 5000000 frames rather than the reported 2000000000 so the pre-fix build fails by assertion on
a ~640 MB allocation instead of being OOM-killed; the same code path is exercised either way.
Related suites after the change: `ctest -R '^(AudioEngineTeardownTest|RecordingLatencyTest|PunchWindowTest|RecordingInputPathTest|RecordingRealtimeTest)$'`
EXIT=0 (5/5). Gates: `complexity-gate.sh --check` EXIT=0, `file-length-gate.sh --check` EXIT=0.

Leftovers: the clamp covers configuration only. A failing `m_loopLine.assign()` on an exhausted host still
throws rather than degrading; that is unchanged behaviour outside this defect.

### Cluster 5468 — Legacy macOS worker wake-up locks and allocates on the render path

Verdict: SKIPPED-SCOPE

Evidence: `src/core/AudioEngineWorkerThread.cpp:187-218` — the mutex/condition-variable wake path in
`notifyWorkers()` is compiled only when `LMMS_WORKER_WAKE_CONDVAR` is defined, i.e. `__APPLE__` with
`__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 110000`. That block does not exist on this Linux host, so the
finding cannot be observed or regression-tested here. Independently, `std::condition_variable::wait` requires
the mutex to be held by the waiting thread, so the lock at `src/core/AudioEngineWorkerThread.cpp:194-203`
is required for correct wake semantics; removing it is a redesign of the worker wake protocol on the render
path, plus the legacy target is no longer buildable or verifiable in this tree.

Commit: none

Test: none

Red/green: none — platform-only code, not compiled on this host.

Leftovers: the pre-macOS-11 worker wake path still locks per period. Needs either dropping the legacy target
or a preinitialized realtime wake mechanism, neither verifiable here.

Leftovers: all six findings triaged; cluster 5491 is unfixed and untestable in this build configuration
(Debug-only code), and clusters 5467, 5465 and 5468 need design or platform work outside this lane. Tree clean
apart from this report.