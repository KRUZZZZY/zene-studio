# Cluster new-p14-402 — a device restart left the frames staged before the stop in the input rings

**Verdict: FIXED** (gates: the six simple gates pass when run by hand; `feature_check.py` itself could not run here, see Gates)

## Finding (Luna's patrol, BUG 2, `include/AudioEngine.h`, `resetInputStream`)

Resetting the input stream cleared only `m_inputStreamStarted` (and the wide-path flag). It did not empty `m_inputStage` or `m_wideInputStage`. Frames captured before a stop and never rendered were therefore drained by the first render after the restart and treated as newly captured input.

## Evidence (current tree, file:line)

- `include/AudioEngine.h:504-508`: `resetInputStream()` is the only reset on the capture path. Before the fix its body was the two flag stores alone.
- `src/core/audio/AudioDevice.cpp:126-132`: `startProcessing()` calls `resetInputStream()` before `startProcessingImpl()`. `stopProcessing()` (134-138) clears `m_running` and calls `stopProcessingImpl()`. The producer is therefore stopped when a restart resets.
- `src/core/AudioEngine.cpp:840-881`: `drainCaptureStages()` moves everything staged into this period's read buffer, and sets `m_inputStreamStarted` at line 872 when it moved any frames. `swapBuffers()` (1241-1260) runs it once per period, from `renderNextPeriod()` (1016).
- Before the fix nothing emptied the two rings, so the first period after a restart drained the stale frames. The RED run below shows 64 frames still staged after `resetInputStream()`.
- Consequence: `inputStreamStarted()` (`include/AudioEngine.h`, next to `resetInputStream`) became true on stale frames. That breaks the BUGS 11.29 contract ("false until the capture delivers"). Its readers are `src/core/SampleRecordHandle.cpp:355` and `src/core/Song.cpp:481`.

Verdict: REAL.

## Fix

1. `include/SampleFrameRingBuffer.h:135-138`: new `discardStaged()` moves the read index to the write index. `reset()` also zeroes the overflow count, and that count is reported as `inputFramesDropped()`. It is shown in the record input status at `src/core/ControlRecordingSupport.cpp:263`, so `discardStaged()` keeps it. Clearing it on every restart would change a visible figure outside this bug.
2. `include/AudioWideInputStage.h:103` and `src/core/AudioWideInputStage.cpp:87-92`: new `discardStaged()` wraps the ring's `reset()`. `droppedFrames()` and `refusedBlocks()` keep their totals.
3. `include/AudioEngine.h:504-508`: `resetInputStream()` discards both staging rings, then clears the flags as before.

Where the clear happens: the finding suggested clearing "after stopping the capture producer". The restart path already guarantees that, because `startProcessing()` is the only production reset. Every caller that starts a device has stopped the previous one first:
- `AudioEngine::setAudioDevice` stops before it starts (`src/core/AudioEngine.cpp:1347-1354`). `restoreAudioDevice` does the same (1371-1382).
- `AudioJack::restartAfterZombified` (`src/core/audio/AudioJack.cpp:139-144`) sets `m_active = false` on a client JACK has already dropped.
- `ProjectRenderer::startProcessing` (`src/core/ProjectRenderer.cpp:266`) calls `setAudioDevice(m_fileDev, false)`, which stops the previous device (`AudioEngine.cpp:1347`). `ProjectRenderer::run` starts the file device at line 300.

So the rings are cleared with no producer or consumer running. That is the precondition the existing `reset()` docs state. Clearing on the start path also gives one choke point, so no stop path changes.

Not changed: `m_inputStreamStarted` and `m_captureWideInUse` semantics, the overflow and dropped counters, `drainCaptureStages()`, `InputChannelRing.h`, `SampleFrameRingBuffer::reset()`, and every command and save format. No allocation, lock, log or I/O is added to any audio-thread path. The new functions run only on the control thread at device start, and they touch only the ring indices.

## Regression test

`tests/src/core/CapturePublisherTest.cpp:404-435`, `aRestartDropsTheFramesStagedBeforeTheStop`:
1. Drains any leftover frames from earlier cases (bounded loop).
2. Publishes one four-channel block through `FakeCaptureDevice`. It is staged in both rings and never rendered. Asserts 64 frames in each ring.
3. Calls `resetInputStream()`. Asserts both rings are empty (line 423, the RED failure).
4. Renders one period. Asserts `inputStreamStarted()` is false and `inputBufferFrames()` is 0.
5. Publishes a fresh block at frame 64 and renders. `comparePeriod(engine, 64)` must return 64: the first thing heard is the new block, with no stale frames in front.

No clocks, sleeps or threads. Placement: this file owns the `FakeCaptureDevice` harness and already calls `resetInputStream()` at lines 301 and 336, both still passing. The finding listed `AudioEngineTeardownTest` instead. That test was built and run as part of the family (below).

## Commands and results

All ctest runs are from `build/tests`. Logs are in the LOG FOLDER (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-402.r1/logs`). Every build used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-new-p14-402` (see Notes).

| # | Command | Log | EXIT |
|---|---|---|---|
| 1 | `cmake --build build --target CapturePublisherTest -j4` | build-1.log | 2 (ccache could not create its temp file; no compile error) |
| 2 | same, with the ccache override (test edit only, product code still original) | build-2.log | 0 |
| 3 (RED) | `ctest -R '^CapturePublisherTest$' --output-on-failure` (product code original) | test-red-1.log | 8. `FAIL! aRestartDropsTheFramesStagedBeforeTheStop`, `Actual (engine->inputFramesStaged()): 64`, `Expected: 0`, `Loc: CapturePublisherTest.cpp(423)` |
| 4 | `cmake --build build --target zene CapturePublisherTest -j4` (after the fix) | build-3.log | 0 |
| 5 (GREEN) | `ctest -R '^CapturePublisherTest$' --output-on-failure` | test-green-1.log | 0 (100% passed) |
| 5b | `./CapturePublisherTest` (binary, for the count) | test-green-2-binary.log | 0. `Totals: 12 passed, 0 failed, 0 skipped` |
| 6 (4b, final bytes) | `git diff -- include/AudioEngine.h include/AudioWideInputStage.h include/SampleFrameRingBuffer.h src/core/AudioWideInputStage.cpp > fix.patch` (3449 bytes) | fix.patch | 0 |
| 6a | `git apply -R fix.patch` (revert the product change) | | 0 |
| 6b | `cmake --build build --target zene CapturePublisherTest -j4` | build-4b3-red.log | 0 |
| 6c (RED) | `ctest -R '^CapturePublisherTest$' --output-on-failure` | test-4b3-red.log | 8. Same assertion, `Actual: 64`, line 423 |
| 6d | `git apply fix.patch` (reapply) | | 0 |
| 6e | `cmake --build build --target zene CapturePublisherTest AudioEngineTeardownTest LadspaPortDialogTest RnnoiseDenoiserEffectTest -j4` | build-4b3-green.log | 0 |
| 6f (GREEN) | `ctest -R '^CapturePublisherTest$' --output-on-failure` | test-4b3-green.log | 0 |

The `git diff` taken after reapplying is byte-identical to `fix.patch` (`cmp`).

Family run (the cluster's family plus the regression test):
`ctest -R '^(CapturePublisherTest|AudioEngineTeardownTest|LadspaPortDialogTest|RnnoiseDenoiserEffectTest)$' --output-on-failure`: EXIT 0, `100% tests passed out of 4` (test-family-green.log). Those are the four ctest tests in the family.

## Gates

- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: EXIT 1 (gates-1.log), not a gate failure. The script writes its logs to `<worktree>-feature-check`, a sibling directory outside the write-jail, and fails with `PermissionError` before it runs any gate. No table was produced. The fix is not claimed as gate-verified by the script.
- Run by hand, with the same commands and cwd as `feature_check.py`, logs in the LOG FOLDER:
  - `tests/fork-sources-gate.sh`: EXIT 0 (gate-fork-sources.log)
  - `tests/all-sources-reproduce.sh`: EXIT 0 (gate-all-sources.log)
  - `tests/no-upstream-regression-gate.sh`: EXIT 0 (gate-no-upstream-regression.log)
  - `tests/scripted/check-namespace`: EXIT 0 (gate-check-namespace.log)
  - `tests/no-tautology-gate.sh`: EXIT 0 (gate-no-tautology.log)
  - `tests/doc-figures-gate.py`: EXIT 0 (gate-doc-figures.log)
  - `tests/complexity-gate.sh --check`: EXIT 1, 7 `REGRESSION` lines (gate-complexity.log)
  - `tests/file-length-gate.sh --check`: EXIT 1, 22 `REGRESSION` lines (gate-file-length.log)
- The 29 ratchet regression items name files this branch does not change (WasmSandbox.cpp, XyPadPairTest.cpp, DawProjectReadTracks.cpp, ControlCommands*.cpp, Vst3Host.cpp, TakeLane.cpp and a set of tests/src/core and tests/src/gui files). Under `feature_check.py`'s rule, new items fail only in changed files, so none of these would fail the feature check. The baseline listing that rule compares against would need a checkout under `PROG/zene-cachyos`, which is outside the write-jail, so the "new vs base" comparison itself was not run. The conclusion rests on the file names.

## Notes

- The ccache temp path from the shared config (`dsh/home/ccache-tmp-r6`) is not writable from this lane. The build used `CCACHE_TEMPDIR` as an environment override only. No config file was changed.
- The first 4b attempt is invalid and is kept only as its exit file (`4b-attempt1-invalid.exit`). The shell is zsh, which does not word-split an unquoted `$PROD`, so `git diff` produced an empty patch and the revert failed with EXIT 128. That run's ctest then ran on the fixed tree, and its log files were overwritten by the next attempt. Its exit file is kept for transparency.
- The second 4b attempt (`4b-attempt2-stale-whitespace.exit`) was valid, but it ran before a whitespace-only indentation fix in `include/AudioWideInputStage.h`. Its exit codes (REVERT 0, RED 8, REAPPLY 0, GREEN 0) match the final run. The final run above is the one cited, since it was made on the final bytes.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** Yes. The RED run was made before the product edit: ctest EXIT 8 (test-red-1.log). The 4b run on the final bytes: revert then ctest EXIT 8 (test-4b3-red.log), reapply then ctest EXIT 0 (test-4b3-green.log).
2. **The test is deterministic: no wall-clock timing, sleeps or races.** Yes. It makes no clock calls, sleeps or threads. It uses the existing `FakeCaptureDevice` publish path, `resetInputStream()`, and `renderNextPeriod()` on the test thread. The Dummy device is stopped in `initTestCase`, so the test is the only renderer.
3. **The fix is at the cause and complete.** The cause is that the restart reset never emptied the rings. The fix covers both rings (stereo and wide), and the wide stage keeps its own width and refusal rules. Callers checked: `AudioDevice::startProcessing` (the only production caller) and the two existing test calls, which still pass. The other users of `SampleFrameRingBuffer` (`SampleRecordAccumulator`) do not call the new method.
4. **No collateral damage.** The overflow counter and dropped totals survive a restart, so `inputFramesDropped()` and `inputWideFramesDropped()` keep their values. No save or project data changes. No command is added. Existing tests in the family still pass (4/4).
5. **Claim check (ALREADY-FIXED or FALSE-POSITIVE only).** Not applicable. The verdict is FIXED, and the code evidence is at the file:line citations above.
6. **Audio-thread constraint.** The changed functions are not on any audio path. `resetInputStream()` runs from `AudioDevice::startProcessing()` on the control thread (and from tests), before the backend starts. `discardStaged()` only stores a relaxed/acquire index. `drainCaptureStages()` and `swapBuffers()` are unchanged.
7. **Undo and commands.** No command was added or changed, and no edit is user-facing, so there is no undo step to check.
8. **Registration.** `tests/upstream-modifications.txt` has two new lines: `include/AudioEngine.h` and `tests/src/core/CapturePublisherTest.cpp`, both tagged `cluster new-p14-402`. The product-new files `include/AudioWideInputStage.h`, `src/core/AudioWideInputStage.cpp` and `include/SampleFrameRingBuffer.h` are already in `tests/fork-sources.txt` (lines 770, 1345, 1086). No new test file was added.
9. **Gates.** See Gates above. The six simple gates pass by hand. The two ratchets fail only on items in files this branch does not change. `feature_check.py` could not run here (write-jail on its log folder).
10. **Stale artifacts.** The RED result (`test-red-1.log`) came from a build with the original product code and the new test. The GREEN results in table rows 4-5b (`build-3.log`, `test-green-1.log`, `test-green-2-binary.log`) predate the whitespace-only indentation fix in `include/AudioWideInputStage.h`. The final GREEN and the family run (`build-4b3-green.log`, `test-4b3-green.log`, `test-family-green.log`) were made after that fix and are the evidence to cite.

## Completion check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p14-402` (EXIT 1, submit-check-1.log): PASS for commits on the branch, a clean working tree, the report, and tests changed. The one FAIL is `gates (feature_check.py)`, which is the write-jail PermissionError described under Gates (gates-1.log). The five ASK items are answered in the Self-check above. The fifth (ALREADY-FIXED or FALSE-POSITIVE) does not apply, because the verdict is FIXED.
