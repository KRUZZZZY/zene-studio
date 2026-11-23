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
  (`src/core/AudioEngine.cpp:851`), which runs off the audio thread (`include/AudioEngine.h:731-738`,
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
| manifest RED (from the committed state, exact `red_command` of `logs/proof.json`) | `git diff 3fb54c6baa~1 3fb54c6baa -- include/AudioJack.h src/core/audio/AudioJack.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch && cmake --build build --target AudioDeviceDiagnosticsTest -j4 && cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^AudioDeviceDiagnosticsTest$' --output-on-failure` | 8 | `logs/test-9-proof-red.log` |
| manifest GREEN (exact `green_command` of `logs/proof.json`) | `git apply .../logs/fix.patch && cmake --build build --target zene AudioDeviceDiagnosticsTest -j4 && cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^AudioDeviceDiagnosticsTest$' --output-on-failure` | 0 (1/1 passed), `git status --porcelain` empty | `logs/test-10-proof-green.log` |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `logs/feature-check-1.log` — 8/8 PASS, `RESULT: PASS` |
| completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 4599` | 0 (`RESULT: PASS - submit`, 6/6 checked items PASS) | `logs/submit-check-1.log` |

`RecordingLatencyTest` was built and run because it pins the same formula on the Dummy device
(`tests/src/core/RecordingLatencyTest.cpp:104`); it is unchanged and still passes.

## Round 2: a RUNNING server that renegotiates its period

The round-1 fix made `captureBlockFrames()` live, but the engine CACHES its round trip:
`refreshRecordingLatency()` runs at `startProcessing()` and nowhere else
(`include/AudioEngine.h:731-738`), and `recordingLatencyFrames()` returns that cache
(`include/AudioEngine.h:495`). The JACK buffer-size callback only resized the capture staging
(`src/core/audio/AudioJack.cpp:206-211` before this round), so a `jack_set_buffer_size()` on a running
server left the cache holding the block from start-up: a take armed after the change was compensated
with the old figure. Reproduced before the fix with the private server: the server granted 1024
frames, `captureBlockFrames()` read 1024, and `recordingLatencyFrames()` stayed at **1280** instead of
**1792** — `logs/test-1-red.log`, `logs/test-4-red-after-revert.log`, exactly the numbers the review
reported.

The cause-level fix is in the callback that hears about the change:

- `include/AudioEngine.h:663-667`: `friend class AudioJack;` (line 667) — the backend, and only it, may re-read
  the engine's cached figures. No public API, no member, no behaviour change for any other caller.
- `include/AudioJack.h:124-126`: `void queueRecordingLatencyRefresh();` (line 126)
- `src/core/audio/AudioJack.cpp:206-213`: the buffer-size callback now calls it after
  `resizeInputBuffer(nframes)` (line 210).
- `src/core/audio/AudioJack.cpp:274-285`: it posts the refresh to the control thread —
  `QMetaObject::invokeMethod(this, [this]{ engine->refreshRecordingLatency(); }, Qt::QueuedConnection)`
  — so the JACK thread never touches the engine, the functor is dropped with the device, and the
  refreshed figure is the one the server just granted (both `captureBlockFrames()` and the port
  latency ranges are read fresh).

The regression slot keeps its start-up assertions and adds the live change:
`jack_set_buffer_size(jack->jackClient(), 2 × period)` (skipped only if the private `jackd` refuses
the period), then `captureBlockFrames()` becomes the new period and `recordingLatencyFrames()` becomes
`input + output + new period + engine period`, exactly.

| Round 2 step | Command | EXIT | Log |
| --- | --- | --- | --- |
| RED: resize phase added, no product change | `cmake --build build --target AudioDeviceDiagnosticsTest -j4`; `cd build/tests && ... ctest -R "^AudioDeviceDiagnosticsTest$" --output-on-failure` | 0; 8 (`recordingLatencyFrames()` 1280 vs 1792, line 419) | `logs/build-1-red.log`, `logs/test-1-red.log` |
| GREEN: callback refresh + friend | `cmake --build build --target zene AudioDeviceDiagnosticsTest AudioJackPortRestartTest -j4` | 0 | `logs/build-2-green.log` |
| GREEN family | `cd build/tests && ... ctest -R "AudioDeviceDiagnosticsTest\|AudioJackPortRestartTest\|RecordingLatencyTest" --output-on-failure` | 0 (3/3 passed) | `logs/test-2-green.log` |
| stability | `cd build/tests && ... ctest -R "^AudioDeviceDiagnosticsTest$" --repeat until-fail:3` | 0 (3/3 passed) | `logs/test-3-repeat3.log` |
| 4b revert (product files only) | `git diff -- include/AudioEngine.h include/AudioJack.h src/core/audio/AudioJack.cpp > .../logs/fix.patch`; `git apply -R .../logs/fix.patch` | 0; 0 | `logs/fix.patch` (65 lines) |
| 4b RED again | `cmake --build build --target AudioDeviceDiagnosticsTest -j4`; `cd build/tests && ... ctest -R "^AudioDeviceDiagnosticsTest$" --output-on-failure` | 0; **8** (1280 vs 1792, line 419) | `logs/build-3-red-after-revert.log`, `logs/test-4-red-after-revert.log` |
| 4b restore | `git apply .../logs/fix.patch` | 0 | — |
| 4b GREEN again | `cmake --build build --target zene AudioDeviceDiagnosticsTest AudioJackPortRestartTest -j4`; `cd build/tests && ... ctest -R "AudioDeviceDiagnosticsTest\|AudioJackPortRestartTest\|RecordingLatencyTest"` | 0; 0 (3/3 passed) | `logs/build-4-green-reapplied.log`, `logs/test-5-green-reapplied.log` |
| gates after the round-2 registration lines | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (8/8 PASS) | `logs/feature-check-1.log`, `logs/feature-check-2.log` |

Registration for this round: `include/AudioEngine.h`, `include/AudioJack.h` and
`src/core/audio/AudioJack.cpp` each carry a `cluster 4599 (round 2)` line in
`tests/upstream-modifications.txt`.

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
- **Round 2: the live period change is the same bug, and it is fixed at its cause.** The cached
  figure had exactly one writer (`AudioEngine::refreshRecordingLatency()`, private,
  `include/AudioEngine.h:737`) and only `startProcessing()` called it; the JACK buffer-size callback
  was the one event that changes the block and it did not tell the engine. The test drives the change
  through the real server (`jack_set_buffer_size`) and asserts the exact new round trip; with the
  product hunks reverted it fails at 1280 vs 1792 (`logs/test-4-red-after-revert.log`, EXIT=8).
- **Round 2 realtime constraints.** Nothing was added to `processCallback()`
  (`src/core/audio/AudioJack.cpp:430`) - the only caller of `queueRecordingLatencyRefresh()` is the
  buffer-size callback at `:210`, which JACK delivers from its own notification, not from inside our
  process callback. The invoked functor runs on the device's own thread (the control thread), where
  `refreshRecordingLatency()` already ran. The post allocates a Qt event in the
  JACK callback thread, not on the audio thread; the callback reads no model, takes no lock and logs
  nothing beyond the pre-existing `resizeInputBuffer()` work. `captureBlockFrames()` itself stays a
  null check plus `jack_get_buffer_size()`, and the audio thread still only reads the cached atomic
  (`include/AudioEngine.h:495`). Local TSan does not run on this box; the argument is the ownership
  one above: one writer, one thread, one atomic store.
- **Round 2 determinism.** The resize assertion is exact, not measured: `jack_set_buffer_size()`
  returns only once the server has granted the period, and `QTRY_COMPARE_WITH_TIMEOUT` merely runs the
  control thread's event loop until the posted refresh lands (5 s bound, the same QTRY idiom the
  file's existing JACK slot uses). No sleep decides a value, and 3/3 repeats passed
  (`logs/test-3-repeat3.log`).
- **No `state-sequence` artifact applies.** The manifest rule names files matching
  `src/core/AudioEngine*`, `*Record*`, `*Transport*`, `*Take*`, `*Undo*` and `src/core/Song*`. No such
  file changed in round 2: the only engine file is `include/AudioEngine.h` (a `friend` declaration,
  `:667`) and the backend code is `src/core/audio/AudioJack.cpp`. The two states the test drives, in
  order, are: server period 512 with round trip `in + out + 512 + engine period`, then live
  `jack_set_buffer_size` to 1024 with round trip `in + out + 1024 + engine period`
  (`logs/test-5-green-reapplied.log`).
- **Completion check** (round 2: `logs/submit-check-1.log`, EXIT=0, `RESULT: PASS - submit`, and
  `logs/submit-check-2.log` after this paragraph; round 1's run is in the r1 log folder): the run and its result are recorded there; every
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
