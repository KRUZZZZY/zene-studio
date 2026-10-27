# Cluster new-p14-402 — a device restart left frames staged before the stop in the input rings

**Verdict: FIXED** (round 2, after review REWORK: the retained JACK restore path. The gates: the six simple gates pass by hand; the two ratchets fail only on items in files this branch does not change; `feature_check.py` could not run in this lane, see Gates.)

## Finding (Luna's patrol, BUG 2, `include/AudioEngine.h`, `resetInputStream`)

Resetting the input stream cleared only `m_inputStreamStarted` (and the wide-path flag). It did not empty `m_inputStage` or `m_wideInputStage`. Frames captured before a stop and never rendered were therefore drained by the first render after the restart and treated as newly captured input.

Verdict: REAL.

## Round 2: the review's blocking point (REWORK)

The review found a reachable path where the reset ran while the capture producer was still live. `RenderManager` keeps the old device and later restores it (`src/core/RenderManager.cpp:45,50`). The restore is `AudioEngine::restoreAudioDevice` (`src/core/AudioEngine.cpp:1371-1381`): it stops the file device, deletes it, and calls `startProcessing()` on the retained device. For an already-active JACK client, `AudioDevice::startProcessing` (`src/core/audio/AudioDevice.cpp:126-132`) calls `resetInputStream()` and then `AudioJack::startProcessingImpl()`, which returns early because `m_active` is true (`src/core/audio/AudioJack.cpp:314-324`). Meanwhile `AudioJack::stopProcessingImpl()` is empty (`AudioJack.cpp:348-350`), and the process callback kept publishing capture while output was stopped (`AudioJack.cpp:443-478` before this change). So a callback could push frames after the discard.

I checked every other backend's stop (the review asked for every restart path):

| Backend | Stop | Capture or render callback quiesced? |
|---|---|---|
| ALSA | `stopProcessingThread` joins (`AudioAlsa.cpp:289-292`) | yes |
| OSS | joins (`AudioOss.cpp:220`) | yes |
| PortAudio | `Pa_StopStream` (`AudioPortAudio.cpp:206`) | yes |
| PulseAudio | joins (`AudioPulseAudio.cpp:89`) | yes |
| Sndio | joins (`AudioSndio.cpp:127`) | yes |
| SDL | locks both devices around the pause (`AudioSdl.cpp:147-158`) | yes |
| JACK | empty (`AudioJack.cpp:348`) | **no** — fixed here |
| SoundIo | pauses only (`AudioSoundIo.cpp:230`) | **no** — fixed here |

### The fix

1. **`include/AudioDevice.h:144-160`, `CallbackScope`.** A backend callback that can run after `stopProcessingImpl()` returns holds one of these for its whole body. Its constructor increments an open-callback count (`AudioDevice.h:184`, `m_openCallbacks`) and fences; its destructor decrements. Lock-free, so it is safe on the audio thread.
2. **`src/core/audio/AudioDevice.cpp:135-140, 142-154`.** `stopProcessing()` clears `m_running`, calls `stopProcessingImpl()`, then `waitForOpenCallbacks()`. The wait fences and spins while the count is non-zero, so `stopProcessing()` returns only after every callback that opened before the clear has ended. Dekker pair: the callback opens its scope and then reads `isRunning()`; the stop clears `m_running` and then reads the count. Both sides fence, so a callback either sees the stop or is waited for.
3. **`src/core/audio/AudioJack.cpp:418`.** `processCallback` opens a `CallbackScope` for its whole body. Its capture publish is gated on `isRunning()` (`AudioJack.cpp:473`). A stopped device stages no input, and a restore discards what it staged. The JACK stop itself stays empty: the generic wait covers it, and the client stays active so that a restore reuses it (deactivating would disconnect the ports for every export or device switch that stops the retained device).
4. **`src/core/audio/AudioSoundIo.cpp:259`.** `writeCallback` opens a scope for its whole body. Pausing the stream does not wait for a write in progress. This file is inside `#ifdef LMMS_HAVE_SOUNDIO`, and libsoundio is not installed here, so this change is verified by reading only.
5. **`include/AudioDevice.h:164`, `onStopWait()`.** A virtual hook, empty, called on each iteration of the wait. It exists only so a deterministic test can see the wait begin. Nothing else uses it.
6. **`include/AudioEngine.h:497-511`.** The `resetInputStream()` comment now states the guarantee it relies on: called after `stopProcessing()` has waited out every open callback.

Deadlock check: the wait spins on the control thread. The callback takes the engine change mutex inside its render, so a stop must not be called while the control thread holds that mutex. I checked every stop caller: `AudioEngine::setAudioDevice` and `restoreAudioDevice` (`AudioEngine.cpp:1347, 1375`), `Engine::destroy` (`Engine.cpp:102`), `ProjectRenderer::startProcessing` and `run` (`ProjectRenderer.cpp:266, 339`), and `AudioPortAudio`'s and `AudioDummy`'s own stop. None holds the change mutex.

Behaviour change, stated plainly: a stopped JACK device no longer publishes capture, so its capture counter and staged-frame counts stop moving while it is stopped. Nothing renders while stopped, and a restore discards what was staged, so no user-visible output or recorded take changes.

### The regression tests

`tests/src/core/CapturePublisherTest.cpp`:

- **`aRestartDropsTheFramesStagedBeforeTheStop` (line 435)**: the round-1 regression, unchanged. It stages a block, resets, and checks the rings are empty and the next period is not started.
- **`aStopWaitsForAnOpenCallbackAndTheRestoreHearsNothingFromIt` (line 473)**: the lifecycle test the review asked for. `RetainedCaptureDevice` (line 73) models the retained JACK device: a no-op stop, a callback that opens a scope for its whole body, renders (the `midCallback` window), and publishes only while running. The test:
  1. starts the device and publishes block A (staged, never rendered);
  2. runs a callback thread that opens its scope and holds it inside the render window, until released;
  3. starts a stopper thread that calls `stopProcessing()`;
  4. waits (no clock) until either the stop is waiting in its loop (`onStopWait`, the fix) or the stop has returned (no fix);
  5. if the stop is still waiting, releases the callback and joins the stopper, so the stop completes after the callback has ended; otherwise it restarts the device while the callback is still open;
  6. restarts the device, releases the callback, and joins both threads;
  7. asserts the rings are empty after the restart, that no period starts from the restore, and that the first block captured after the restore is the first heard.

  Without the fix (step 4 sees the stop return at once), the callback publishes block B after the restart, and the test fails at line 512 with 64 frames staged. With the fix, the callback sees the device stopped and publishes nothing.

No sleeps, no clocks, and every thread is joined before the first assertion, so a failure cannot leave a thread behind.

## Round 1: the discard (kept)

1. `include/SampleFrameRingBuffer.h:135-141`: `discardStaged()` moves the read index to the write index. `reset()` also zeroes the overflow count, which `AudioEngine::inputFramesDropped()` reports (`src/core/ControlRecordingSupport.cpp:263`), so `discardStaged()` keeps it.
2. `include/AudioWideInputStage.h:103` and `src/core/AudioWideInputStage.cpp:87-92`: `discardStaged()` wraps the ring's `reset()`. `droppedFrames()` and `refusedBlocks()` keep their totals.
3. `include/AudioEngine.h:505-510`: `resetInputStream()` discards both staging rings, then clears the flags as before.

Not changed: `m_inputStreamStarted` and `m_captureWideInUse` semantics, the overflow and dropped counters, `drainCaptureStages()`, `InputChannelRing.h`, `SampleFrameRingBuffer::reset()`, and every command and save format. Nothing new runs on an audio path except two atomic operations in the JACK and SoundIo callbacks.

## Commands and results

ctest runs are from `build/tests`. Logs are in the round-2 LOG FOLDER (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-402.r2/logs`); round-1 evidence is in `.../new-p14-402.r1/logs`. Every build used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-new-p14-402` (see Notes).

Round 2:

| # | Command | Log | EXIT |
|---|---|---|---|
| R2-1 | `cmake --build build --target zene CapturePublisherTest AudioEngineTeardownTest LadspaPortDialogTest RnnoiseDenoiserEffectTest -j4` (fixed state) | build-r2-1.log | 0 (`grep -c "error:"` = 0) |
| R2-2 | `ctest -R '^(CapturePublisherTest\|AudioEngineTeardownTest\|LadspaPortDialogTest\|RnnoiseDenoiserEffectTest)$' --output-on-failure` | test-r2-green-family.log, test-r2-final-family.log | 0; `100% tests passed out of 4` (both runs) |
| R2-3 | `./CapturePublisherTest` (binary) | test-r2-final-binary.log | 0; `Totals: 13 passed, 0 failed` |
| R2-4 | `./CapturePublisherTest` repeated 20 times (determinism) | test-r2-green-loop-1..20.log | 20 of 20 exit 0 |
| R2-5 | `git diff -- src/core/audio/AudioDevice.cpp src/core/audio/AudioJack.cpp > fix-behaviour.patch` (2674 bytes) | fix-behaviour.patch | 0 |
| R2-6 (RED) | `git apply -R fix-behaviour.patch` (revert the behavioural product change; headers and the test stay) | | 0 |
| R2-7 | `cmake --build build --target zene CapturePublisherTest -j4` | build-r2-red.log | 0 |
| R2-8 (RED) | `ctest -R '^CapturePublisherTest$' --output-on-failure` | test-r2-red.log | 8 |
| R2-9 (RED) | `./CapturePublisherTest` | test-r2-red-binary.log | 1. `FAIL! aStopWaitsForAnOpenCallbackAndTheRestoreHearsNothingFromIt()`, `Actual (engine->inputFramesStaged()): 64`, `Expected: 0`, `Loc: CapturePublisherTest.cpp(512)`. `Totals: 12 passed, 1 failed`: the round-1 regression still passes, so the new test is the one that catches this path. |
| R2-10 | `git apply fix-behaviour.patch` (reapply) | | 0 |
| R2-11 | `cmake --build build --target zene CapturePublisherTest -j4` | build-r2-green.log | 0 |
| R2-12 (GREEN) | `ctest -R '^CapturePublisherTest$' --output-on-failure` | test-r2-green.log | 0 |

After reapplying, `git diff -- src/core/audio/AudioDevice.cpp src/core/audio/AudioJack.cpp` is byte-identical to `fix-behaviour.patch` (`cmp`). The RED run reproduces the failure the review described: block B is published after the restore.

Round 1 (the discard; its RED, GREEN and reversal proof are in the r1 logs, and the final-bytes 4b run is `test-4b3-red.log` (EXIT 8) and `test-4b3-green.log` (EXIT 0)).

## Gates

- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: EXIT 1 in this lane (`feature-check-r2.log`). It writes its logs to `<worktree>-feature-check`, a sibling directory outside the write-jail, and fails with `PermissionError` before any gate runs. The review's own run of the script passed (EXIT 0, RESULT: PASS); I could not reproduce that here.
- Run by hand, with the same commands and cwd as `feature_check.py`:
  - `tests/fork-sources-gate.sh`: EXIT 0
  - `tests/all-sources-reproduce.sh`: EXIT 0
  - `tests/no-upstream-regression-gate.sh`: EXIT 0
  - `tests/scripted/check-namespace`: EXIT 0
  - `tests/no-tautology-gate.sh`: EXIT 0
  - `tests/doc-figures-gate.py`: EXIT 0
  - `tests/complexity-gate.sh --check`: EXIT 1, 7 `REGRESSION` lines (`gate-complexity.log`)
  - `tests/file-length-gate.sh --check`: EXIT 1, 22 `REGRESSION` lines (`gate-file-length.log`)
- None of the 29 ratchet items names a file this change touches (`AudioDevice.h/.cpp`, `AudioJack.cpp`, `AudioSoundIo.cpp`, `AudioEngine.h`, `CapturePublisherTest.cpp`). Under `feature_check.py`'s rule, new items fail only in changed files. The counts are the same as in round 1 (7 and 22), so the change adds no ratchet item. The baseline listing the rule compares against would need a checkout under `PROG/zene-cachyos`, which is outside the write-jail, so the comparison itself was not run; the conclusion rests on the file names.

## Build coverage

- `src/core/audio/AudioJack.cpp` is compiled in this build (`WANT_JACK=ON`, in `compile_commands.json`), and the `zene` target built with exit 0.
- `src/core/audio/AudioSoundIo.cpp` is not compiled here (`LMMS_HAVE_SOUNDIO` is off, libsoundio not found). Its one-line change is verified by reading only.
- No real JACK server is available here, so the JACK callback is not run. Its behaviour is covered by the same mechanism through `RetainedCaptureDevice`, which models the JACK device's stop and callback.

## Notes

- The ccache temp path from the shared config (`dsh/home/ccache-tmp-r6`) is not writable from this lane. Builds used `CCACHE_TEMPDIR` as an environment override only. No config file was changed.
- Round 1's first 4b attempt was invalid: the shell is zsh, which does not word-split an unquoted `$PROD`, so the patch was empty and the revert failed (EXIT 128). Its exit file is kept as `4b-attempt1-invalid.exit`. The second attempt (`4b-attempt2-stale-whitespace.exit`) was valid for the code before an indentation fix. The round-1 final-bytes run is `test-4b3-*`.
- Round 2 uses literal paths, not a shell variable, for the same reason.

## Self-check

1. **The regression tests FAIL with the product change reverted and PASS with it.** Yes, for round 2: the behavioural product change reverted gives ctest EXIT 8 and the binary exits 1 with the staged-count failure at `CapturePublisherTest.cpp:512` (R2-6 to R2-9); restored, the test passes (R2-10 to R2-12). The round-1 regression (`aRestartDropsTheFramesStagedBeforeTheStop`) passes in both states, so the two tests cover different things. Round 1's own reversal is in the r1 logs.
2. **The tests are deterministic: no wall-clock timing, sleeps or races.** Yes. The lifecycle test uses only atomic handshakes. It waits for the callback to be open (`inCallback`), then for the stop to be waiting (`onStopWait`) or to have returned, and then releases the callback. Every thread is joined before the first assertion. It passed 20 of 20 repeated runs (R2-4). The `onStopWait` hook is the only test seam; it is empty in product code.
3. **The fix is at the cause and complete.** The cause is that the reset ran with a callback still open. The stop now waits for every open backend callback, so the reset's precondition holds on every restart path. The per-backend table in the Round 2 section shows each stop: six are already quiescent, and JACK and SoundIo are fixed here. Callers checked: `AudioDevice::startProcessing` (the only production reset), `setAudioDevice`, `restoreAudioDevice`, `Engine::destroy`, the ProjectRenderer stop, and the two existing test calls to `resetInputStream()`.
4. **No collateral damage.** The overflow and dropped counters survive a restart. No save or project data changes, and no command is added. The one behaviour change is that a stopped JACK or SoundIo device no longer stages input (stated above). The family still passes 4 of 4 (R2-2).
5. **Claim check.** Not applicable: the verdict is FIXED, and every claim cites file:line.
6. **Audio-thread constraint.** `CallbackScope` is one atomic increment and one decrement with fences, and no allocation, lock or log. It is taken on the JACK and SoundIo callbacks, which are audio threads. The wait is on the control thread, in `stopProcessing()`, and no stop caller holds the change mutex (see the deadlock check above).
7. **Undo and commands.** No command was added or changed, and no edit is user-facing.
8. **Registration.** `tests/upstream-modifications.txt` has entries for `include/AudioEngine.h` and `tests/src/core/CapturePublisherTest.cpp` (round 1). The round-2 files `include/AudioDevice.h`, `src/core/audio/AudioDevice.cpp`, `src/core/audio/AudioJack.cpp` and `src/core/audio/AudioSoundIo.cpp` are inherited, so each gets an entry in the same commit. No new test file was added.
9. **Gates.** See Gates. The six simple gates pass by hand; the two ratchets have no items in changed files; `feature_check.py` could not run in this lane.
10. **Stale artifacts.** The round-2 RED was built from the committed round-1 product code plus the new test (R2-6, R2-7). The GREEN runs (R2-2, R2-3, R2-11, R2-12) are from builds made after the final product edit. The one edit after the last build of the affected targets is the SoundIo line, which is not compiled here.

## Completion check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p14-402` on the round-2 commit `69df727c3` (EXIT 1, `submit-check-r2.log`): PASS for commits on the branch, a clean working tree, the report, and tests changed. The one FAIL is `gates (feature_check.py)`, the same write-jail `PermissionError` described under Gates. The five ASK items are answered in the Self-check above; the fifth (ALREADY-FIXED or FALSE-POSITIVE) does not apply, because the verdict is FIXED.
