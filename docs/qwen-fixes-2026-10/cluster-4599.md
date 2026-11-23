FIXED

4599: FIXED — JACK recording latency used the engine's configured buffer as its capture block.

## The bug (REAL, evidence from this session)

- `AudioDevice::captureBlockFrames()` returned the engine's buffer for every backend that did not
  override it: `src/core/audio/AudioDevice.cpp:119-122` returns
  `m_audioEngine->framesPerAudioBuffer()`, and `include/AudioDevice.h:100` declares only that default.
- That figure is one of the four terms of the recorder's round trip:
  `src/core/AudioEngine.cpp:843-853` (`refreshRecordingLatency()`) stores
  `inputLatencyFrames() + outputLatencyFrames() + captureBlockFrames() + m_framesPerPeriod`, and
  `SampleRecordHandle::m_compensationFrames` drops that many frames from the head of a take
  (`src/core/SampleRecordHandle.cpp:125`).
- JACK hands its process callback the SERVER's period, not the engine's buffer:
  `src/core/audio/AudioJack.cpp:414` (`processCallback(jack_nframes_t nframes)`) renders and publishes
  `nframes` (`:435`, `:460`, `:486`), `resizeInputBuffer()` sizes the capture staging from
  `jack_get_buffer_size()` (`:203`, `:265-269`), and the engine-side resync is still commented out
  (`:333` `//	jack_set_buffer_size( m_client, audioEngine()->framesPerPeriod() );`).
- `AudioJack` declared only `inputLatencyFrames()`/`outputLatencyFrames()` (`include/AudioJack.h:66-68`
  before this change), so with a server period different from the configured buffer
  (`audioengine/framesperaudiobuffer`, default `DEFAULT_BUFFER_SIZE` = 256, `include/AudioEngine.h:76`)
  the round trip was wrong by the difference and takes were shifted by it.
- Reproduced before the fix: with a private `jackd -d dummy -r 48000 -p 512` and the engine buffer at
  256, `jack->captureBlockFrames()` returned `256` (expected `512`) — see `logs/test-3-red.log`,
  `logs/test-6-red-after-revert.log`.

## The fix

- `include/AudioJack.h:69-71`: `f_cnt_t captureBlockFrames() const override;` next to the two latency
  overrides.
- `src/core/audio/AudioJack.cpp:737-745`:

  ```cpp
  f_cnt_t AudioJack::captureBlockFrames() const
  {
      if (m_client == nullptr) { return 0; }
      return static_cast<f_cnt_t>(jack_get_buffer_size(m_client));
  }
  ```

  Read live rather than cached, so a client restart (`restartAfterZombified()` →
  `initJackClient()`) or a server buffer-size change is reported as it is; `m_client == nullptr`
  is the same "no client, nothing to report" case `deviceTiming()` already guards
  (`src/core/audio/AudioJack.cpp:748`), and `shutdownCallback()` clears it (`:511-516`). No cache,
  no allocation, no lock: `captureBlockFrames()` is called only from `refreshRecordingLatency()`
  (`src/core/AudioEngine.cpp:851`), which runs off the audio thread (`include/AudioEngine.h:727-733`,
  `AudioEngine::startProcessing()`), never from `processCallback()`.

## The regression test

`tests/src/core/AudioDeviceDiagnosticsTest.cpp:358`
`AudioDeviceDiagnosticsTest::jackReportsTheServersPeriodAsTheCaptureBlock()`, in the file that already
owns the private-`jackd` harness (`:292` `jackReportsTheServersTimingAndXruns`, with
`friend class AudioDeviceDiagnosticsTest` in `include/AudioJack.h:117`). It starts
`jackd --no-realtime -n <server> -d dummy -r 48000 -p <period>` with a period the engine's configured
buffer is not (512, or 768 when the buffer is 512), joins a real client, swaps it in through
`storeAudioDevice()`/`setAudioDevice(jack, true)`, and asserts:

1. `jack->deviceTiming().periodFrames` is that period (the server granted it),
2. `jack->captureBlockFrames()` is that period (fails as 256 without the fix),
3. `engine->recordingLatencyFrames()` is `inputLatencyFrames() + outputLatencyFrames() + period +
   engine->framesPerPeriod()` — the take-shifting consequence.

Values are the negotiated figures, not measured timing: the only wait is the bounded retry that
joins the freshly started server (100 × 50 ms), the same seam the existing JACK slot uses, and a
`PrivateJackServer` guard (`:115`) restores the engine's device and stops the server even when an
assertion returns early. No injected clock and no sleeps in the assertions.

## Commands and EXIT codes (all run from this worktree)

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| baseline, unmodified test | `cmake --build build --target AudioDeviceDiagnosticsTest -j4` | 0 | `logs/build-1.log` |
| baseline, unmodified test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "^AudioDeviceDiagnosticsTest$" --output-on-failure` | 0 | `logs/test-1-baseline-diagnostics.log` |
| RED (test added, no product change) | `cmake --build build --target AudioDeviceDiagnosticsTest -j4` | 0 | `logs/build-3-red.log` |
| RED (test added, no product change) | `cd build/tests && ... ctest -R "^AudioDeviceDiagnosticsTest$" --output-on-failure` | 8 | `logs/test-3-red.log` (`Actual (jack->captureBlockFrames()): 256`, `Expected (period): 512`, line 399) |
| GREEN (fix applied) | `cmake --build build --target zene AudioDeviceDiagnosticsTest AudioJackPortRestartTest -j4` | 0 | `logs/build-4-green.log` |
| GREEN (fix applied) | `cd build/tests && ... ctest -R "AudioDeviceDiagnosticsTest\|AudioJackPortRestartTest\|RecordingLatencyTest" --output-on-failure` | 8 (`RecordingLatencyTest` not built yet: `Not Run`) then 0 after the next row | `logs/test-4-green.log` |
| sibling family | `cmake --build build --target RecordingLatencyTest -j4`; `cd build/tests && ... ctest -R "RecordingLatencyTest"` | 0; 0 (1/1 passed) | `logs/build-5-latency.log`, `logs/test-5-latency.log` |
| 4b: patch out | `git diff -- include/AudioJack.h src/core/audio/AudioJack.cpp > .../logs/fix.patch` | 0 | `logs/fix.patch` |
| 4b: revert | `git apply -R .../logs/fix.patch` | 0 | — |
| 4b: RED again | `cmake --build build --target AudioDeviceDiagnosticsTest -j4`; `cd build/tests && ... ctest -R "^AudioDeviceDiagnosticsTest$"` | 0; 8 | `logs/build-6-red-after-revert.log`, `logs/test-6-red-after-revert.log` |
| 4b: restore | `git apply .../logs/fix.patch` | 0 | — |
| 4b: GREEN again | `cmake --build build --target zene AudioDeviceDiagnosticsTest -j4`; `cd build/tests && ... ctest -R "^AudioDeviceDiagnosticsTest$"` | 0; 0 (1/1 passed) | `logs/build-7-green-reapplied.log`, `logs/test-7-green-reapplied.log` |
| stability | `cd build/tests && ... ctest -R "^AudioDeviceDiagnosticsTest$" --repeat until-fail:3` | 0 (3/3 passed) | `logs/test-8-repeat3.log` |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `logs/feature-check-1.log` — 8/8 PASS, `RESULT: PASS` |
| completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 4599` | 0 (`RESULT: PASS - submit`, 6/6 checked items PASS) | `logs/submit-check-1.log` |

`RecordingLatencyTest` was built and run because it pins the same formula on the Dummy device
(`tests/src/core/RecordingLatencyTest.cpp:104`); it is unchanged and still passes.

Registration: `include/AudioJack.h` and `src/core/audio/AudioJack.cpp` are inherited files, so both
carry a `cluster 4599` line in `tests/upstream-modifications.txt` (appended, file ends in a newline).
The test file was already registered in `tests/fork-sources.txt:3834`; no new product file was added.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverted (`git apply -R logs/fix.patch`, EXIT=0), rebuilt (EXIT=0), test EXIT=8 with
  `Actual (jack->captureBlockFrames()): 256` / `Expected (period): 512`
  (`logs/test-6-red-after-revert.log`). Restored (`git apply logs/fix.patch`, EXIT=0), rebuilt
  (EXIT=0), test EXIT=0 with `1/1` passed (`logs/test-7-green-reapplied.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is the private JACK
  server itself: `jackd -d dummy -r 48000 -p <period>` fixes the period, and the assertions compare
  the negotiated figures (`deviceTiming().periodFrames`, `captureBlockFrames()`,
  `recordingLatencyFrames()`) against that constant, not against anything measured. The only sleep is
  the bounded join retry for a server that was just spawned (100 × 50 ms, the existing
  `jackReportsTheServersTimingAndXruns` pattern), and the run passed 3/3 repeats
  (`logs/test-8-repeat3.log`).
- **The fix is at the cause and complete: every caller and sibling checked.** The only caller of
  `captureBlockFrames()` is `AudioEngine::refreshRecordingLatency()`
  (`src/core/AudioEngine.cpp:851`, found by a tree-wide search; no other reference in `src/`,
  `include/`, `tests/`). Siblings: `AudioDummy` already overrides it with its own push size
  (`include/AudioDummy.h:56`, "one capture push per rendered engine period"); `AudioAlsa`,
  `AudioSdl` and `AudioPortAudio` do not override it and are outside this finding — recorded under
  `## Other findings`, not changed here. The consumer side (`SampleRecordHandle`,
  `ControlCommandsRecordingRoutes.cpp:158`) reads the same `f_cnt_t` before and after; nothing about
  its type or range changed.
- **No collateral damage.** No saved-data, project-format, command or schema change: the fix only
  changes a virtual's return value, and only for the JACK backend (its previous figure appears
  nowhere in a `.mmp`/`.zene` file). Nothing new rejects a caller: `captureBlockFrames()` is a
  read-only getter with no limits. Behaviour outside the bug is unchanged — `AudioJack`'s other
  figures, `deviceTiming()` (so `mixer.cpu_load`'s JSON), the process callback and the capture
  staging all keep their old values; the only new behaviour is that the fourth latency term is the
  server block.
- **Realtime, undo and commands.** `captureBlockFrames()` adds no allocation, lock, wait, I/O or log
  and no Qt call: it is one null check plus `jack_get_buffer_size()`, read only from
  `refreshRecordingLatency()` on the control thread. The audio thread only ever reads the cached
  `m_recordingLatency` (`include/AudioEngine.h:495`). No undo step and no control command is added
  or changed, so the `ControlCommandsSnapshot` canary is untouched (`feature_check` PASS,
  `logs/feature-check-1.log`). Local TSan does not run on this box; the design argument is the one
  above.
- **Completion check** (`logs/submit-check-1.log`): the run and its result are recorded there; every
  FAIL it named was fixed before this report was committed, and this section answers each ASK item.
  The sibling-command ASK lists `app.*`, `arrangement.*` and others because the check diffs the whole
  branch against its merge base, and this branch carries other clusters' commits. My own commit
  (`git diff --name-only HEAD~1..HEAD`, EXIT=0) touches exactly
  `docs/qwen-fixes-2026-10/cluster-4599.md`, `include/AudioJack.h`,
  `src/core/audio/AudioJack.cpp`, `tests/src/core/AudioDeviceDiagnosticsTest.cpp` and
  `tests/upstream-modifications.txt` — no `ControlCommands*`, no command id, no reversibility class,
  so no sibling command is affected or needs to change.

## Other findings (not fixed here)

- `AudioAlsa`, `AudioSdl` and `AudioPortAudio` also inherit the base `captureBlockFrames()`
  (`framesPerAudioBuffer()`), while their capture pushes use their own granted block
  (`AudioAlsa.cpp`'s capture period, `AudioSdl.cpp`'s `m_inputAudioHandle.samples`,
  `AudioPortAudio.cpp`'s `framesPerBuffer`). Same class of defect as 4599, but each needs its own
  reproducible server/device to prove it; out of scope for this cluster.
- `AudioEngine::refreshRecordingLatency()` adds `m_framesPerPeriod` (the ENGINE period) as the output
  staging term. When a JACK server runs a period above `framesPerAudioBuffer`, the output double
  buffer that `swapBuffers()` flips is arguably the server block too. Unverified — measuring it is
  the R2.2 output-side question, not this finding, and I did not change it.
