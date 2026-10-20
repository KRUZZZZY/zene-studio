# Cluster new-p9-568-port: PulseAudio stream failure fallback

**Verdict: FIXED** (ported by hand from `bugs/qwen-new-p9-568`, commit `bbb11431e`)

## The finding was real on this line

The finding named two defects in `src/core/audio/AudioPulseAudio.cpp`, and both were present
in this tree before the change:

* the context callback reported the connection as established without looking at what
  `pa_stream_connect_playback()` answered - the pre-fix code was
  `pa_stream_connect_playback(...); _this->signalConnected( true );`;
* the `PA_STREAM_FAILED` branch of the stream callback only logged the error, so a stream
  that died after the connection was established left the device on the PulseAudio path.

`AudioPulseAudio::signalConnected()` was a latch (`if (!m_connected) { ...; release(); }`), so
a later failure could not even update the state, and `run()` iterated the mainloop in
`if (m_connected) { ... } else { render fallback }` - the fallback was unreachable once the
device had reported itself connected. `m_connected` was also left uninitialised by the
constructor.

## The port, and the collision that blocked the original commit

The original fix could not be merged into this line because `AudioEngineTeardownTest.cpp` then
carried TWO sets of test-only libpulse symbols: cluster new-p12-319 defines
`pa_mainloop_new`, `pa_mainloop_get_api`, `pa_context_new` and `pa_mainloop_free`, and
cluster new-p9-568 defined the same entry points again (`pa_mainloop_iterate`,
`pa_context_connect`, `pa_stream_new`, `pa_stream_connect_playback`, ...). Two definitions of
one symbol cannot be linked into one binary, so the two clusters collided.

The resolution here is ONE merged fake server in the same test file
(`tests/src/core/AudioEngineTeardownTest.cpp:138-337`): a `PulseScenario` enum says which
answer the fake gives, and every case selects its own scenario
(`setPulseScenario()`, `tests/src/core/AudioEngineTeardownTest.cpp:201`), so the cases are
order-independent and the new-p12-319 case keeps its original strength (one `pa_mainloop_free`
when the context creation fails).

## The change (file:line)

* `src/core/audio/AudioPulseAudio.cpp:161-165` - the playback connect result is checked and
  reported: `signalConnected(connectResult >= 0)`. A refused connection is a failure, not a
  connection.
* `src/core/audio/AudioPulseAudio.cpp:108-115` - the `PA_STREAM_FAILED` branch reports the
  failure through the connection state (`signalConnected(false)`), so the worker leaves the
  PulseAudio path.
* `src/core/audio/AudioPulseAudio.cpp:211-233` - `run()` iterates the mainloop only while the
  stream is connected (`while (AudioDevice::isRunning() && m_connected)`), releases the stream
  when there is one, and otherwise takes the engine's existing render fallback
  (`audioEngine()->renderNextPeriod()`), which is the same path the device already took when
  the context could not be established.
* `src/core/audio/AudioPulseAudio.cpp:266-280` and `include/AudioPulseAudio.h:97` -
  `m_connectionReported` tells the first answer (which releases the startup semaphore) from a
  later one (which only updates the state), so a late failure cannot release the semaphore a
  second time. `m_connected` and `m_connectionReported` are initialised in the constructor
  (`src/core/audio/AudioPulseAudio.cpp:51-52`).

Callers and siblings checked: `signalConnected()` has exactly three call sites, all in this
file (`AudioPulseAudio.cpp:115`, `:139`, `:165`, plus the context-failure call at `:175`), and
`m_connected` is read only in `run()`. The other backends (ALSA, JACK, SDL, PortAudio,
SoundIO) have their own open/close paths and no `signalConnected()` counterpart, so nothing
else had to change. `AudioEngine::initDevices()` uses `AudioDummy` in render-only mode
(`src/core/AudioEngine.cpp:558-560`), so the headless render never opens the PulseAudio
device and is untouched by this change.

## Verification evidence

| Check | Command | Exit | Evidence |
|---|---|---:|---|
| Build the regression target (test only, product code untouched) | `cmake --build build --target AudioEngineTeardownTest` | 0 | `logs/build-1.log` |
| RED on the original product code | `ctest -R '^AudioEngineTeardownTest$' --output-on-failure` (from `build/tests`) | 8 | `logs/test-1.log`: both new cases FAIL (`'fellBack' returned FALSE`), 9 passed / 2 failed of 11 QtTest cases |
| Build the product with the fix | `cmake --build build --target zene AudioEngineTeardownTest` | 0 | `logs/build-2.log` |
| GREEN after the fix | `ctest -R '^AudioEngineTeardownTest$' --output-on-failure` | 0 | `logs/test-2.log`: 1/1 ctest target passed |
| Save the product-only patch | `git diff -- include/AudioPulseAudio.h src/core/audio/AudioPulseAudio.cpp > .../logs/fix.patch` | 0 | `logs/fix.patch` (119 lines) |
| Revert the fix | `git apply -R .../logs/fix.patch` | 0 | - |
| Rebuild without the fix | `cmake --build build --target zene AudioEngineTeardownTest` | 0 | `logs/build-3.log` |
| RED with the fix reverted | `ctest -R '^AudioEngineTeardownTest$' --verbose --output-on-failure` | 8 | `logs/test-6.log`: both new cases FAIL again, 9 passed / 2 failed |
| Restore the fix | `git apply .../logs/fix.patch` | 0 | - |
| Rebuild with the fix restored | `cmake --build build --target zene AudioEngineTeardownTest` | 0 | `logs/build-10.log` |
| GREEN again | `ctest -R '^AudioEngineTeardownTest$' --verbose --output-on-failure` | 0 | `logs/test-7.log`: 11 passed / 0 failed of 11 QtTest cases |
| Affected family (audio engine and device tests) | `ctest -R '^(AudioEngineTeardownTest\|AudioAlsaWriteTest\|AudioBusHandleTest\|AudioBusTest\|AudioPortsModelTest\|AudioPortsTest\|AudioBusPlayHandleQueueTest\|RetroAudioCaptureTest\|DummyPacingTest\|AudioJackPortRestartTest)$'` | 0 | `logs/test-family-3.log`: 10/10 ctest targets passed |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | `logs/gate-1.log`: red on the train (6 pre-existing items, none in my files) |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | `logs/gate-2.log`: red on the train (14 pre-existing items, none in my files) |
| Whole-tree complexity / file-length (extra) | `GATE_SCOPE=all bash tests/complexity-gate.sh --check`, `GATE_SCOPE=all bash tests/file-length-gate.sh --check` | 1 / 1 | `logs/gate-3-all.log`, `logs/gate-4-all.log`: no item in my files |
| All gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `logs/gate-feature-check.log`: 8 PASS, 0 FAIL - "no new item in your files" for both ratchets |

Affected-family count: **1 registered ctest target** (`AudioEngineTeardownTest`), which runs
**11 QtTest cases** (2 new regressions, 7 pre-existing cases, `initTestCase` and
`cleanupTestCase`); the wider audio family is 10 ctest targets, all green.

The two ratchet gates exit 1 because the train is already red (six functions over CCN 10 and
fourteen files over 500 code lines, all in other clusters' files - see `logs/gate-1.log` and
`logs/gate-2.log`). Neither list names a file this change touches, which is what
`feature_check.py` reports ("no new item in your files"). The gate limits and baselines were
not moved.

`ControlGoldenAudio` fails in this environment with `std::bad_alloc` in the render child
process; it fails IDENTICALLY with the product patch reverted (`logs/test-golden-base.log`,
run against a rebuild without the fix), so it is a pre-existing failure of this train and not
a consequence of this change.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes).** Reverted: `ctest -R '^AudioEngineTeardownTest$' --verbose --output-on-failure`,
  EXIT=8, `logs/test-6.log` - `rejectedPulsePlaybackConnectionFallsBackToRendering` and
  `failedPulseStreamLeavesThePulseAudioPath` both report `'fellBack' returned FALSE`.
  Restored: the same command, EXIT=0, `logs/test-7.log` - 11 passed / 0 failed. The same
  RED/GREEN pair was also observed before the patch was ever reverted (`logs/test-1.log`
  EXIT=8 on the untouched product code, `logs/test-2.log` EXIT=0 after the fix).
* **The test is deterministic.** The seam is the test-only libpulse interposition: the fake
  server decides what the device is told (`PulseScenario`,
  `tests/src/core/AudioEngineTeardownTest.cpp:149-215`) - the refused connect is the `-1`
  the fake returns from `pa_stream_connect_playback()`, and the later failure is the
  `PA_STREAM_FAILED` state the fake hands the stream on its first mainloop iteration. The
  case waits on a `QSemaphore` the DEVICE releases (the engine's own `nextAudioBuffer` tap
  fires once per rendered period), so the case is released by the event, not by the clock;
  `kPulseHandshakeMs` (5000 ms) is only the bound after which the case FAILS. No sleep, no
  elapsed-time assertion, and the exact iteration counts (`mainloopIterations` 0 and 1) are
  fixed by the fake's own state machine, not by scheduling.
* **The fix is at the cause and complete.** The cause is the ignored connect result and the
  unpropagated stream failure, both in `AudioPulseAudio.cpp`; both are fixed there, and
  `signalConnected()` (the only state sink) was made consistent with its three call sites
  (`AudioPulseAudio.cpp:115`, `:165`, `:175`) through `m_connectionReported`. `m_connected` is
  read only in `run()`. `include/AudioPulseAudio.h` and `src/core/audio/AudioPulseAudio.cpp`
  were changed together and every target that includes the header was rebuilt
  (`lmmsobjs`, `zene`, `AudioEngineTeardownTest`; `src/core/AudioEngine.cpp` and
  `src/gui/modals/SetupDialog.cpp` include it and were recompiled in `logs/build-2.log`).
  The other audio backends have no equivalent state and needed no change.
* **No collateral damage.** No project, save file or saved data is touched: the change is
  confined to the PulseAudio device's connection state. No limit or threshold was added, so
  no other caller can hit one. The behaviour outside the bug is unchanged: a PulseAudio
  server that answers the connect keeps the PulseAudio path (the write callback still drives
  the render), a context that cannot be established still takes the same render fallback as
  before, and a device that is stopped still leaves the loop and returns. The render path is
  untouched because a render-only engine opens `AudioDummy`, not the PulseAudio device
  (`src/core/AudioEngine.cpp:558-560`). No control command was added, no undo step was
  touched, and the audio thread gained no allocation, free, lock, wait or log: the fallback
  loop and the `qCritical` in the failure branch are the product's pre-existing paths.
* **Verdict (not ALREADY-FIXED / FALSE-POSITIVE).** The pre-fix code in this tree reported
  the connection unconditionally (`signalConnected( true )` right after the unchecked
  `pa_stream_connect_playback(...)`) and only logged `PA_STREAM_FAILED`; the evidence above
  shows the tree failed the new regressions and passed them only with the fix.

## Files committed

* `include/AudioPulseAudio.h` - declares `m_connectionReported`.
* `src/core/audio/AudioPulseAudio.cpp` - the fix.
* `tests/src/core/AudioEngineTeardownTest.cpp` - the merged fake PulseAudio server and the
  two regression cases.
* `tests/upstream-modifications.txt` - the divergence entries for the three changed
  inherited files.
* `docs/qwen-fixes-2026-10/cluster-new-p9-568-port.md` - this report.
