FIXED
new-p12-274: FIXED
new-p9-496: FIXED

## Evidence

**new-p12-274 — REAL.** `TrackRecorder::arm()` opened the WAV and wrote its journal before constructing the writer thread. A `std::system_error` therefore escaped with the internal armed flag still set. `TrackRecorder::isArmed()` now reports the capture-ready flag (`include/TrackRecorder.h:109`); the writer-start catch in `src/core/audio/TrackRecorder.cpp:216-247` closes the file, removes and clears the journal, resets route state, and returns `false`. A later arm can retry. The deterministic regression is `MultiTrackRecorderTest::writerStartFailureLeavesRouteRetryable()` (`tests/src/core/MultiTrackRecorderTest.cpp:82`): it injects `std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again))`, checks cleanup and the unaffected route, then retries successfully.

**new-p9-496 — REAL.** `TrackRecorder::setInputChannelCapacity()` previously only stored the new width (`src/core/audio/TrackRecorder.cpp:110`). It now disarms an active route if its selected channel is outside the reduced capacity; `MultiTrackRecorder::setInputChannelCapacity()` forwards that reconfiguration to each route (`src/core/audio/MultiTrackRecorder.cpp:71`). The setters are documented off the audio thread and are no longer `noexcept`, because disarming joins the writer. The regression `RecordingInputPathTest::shrinkingInputCapacityDisarmsUnavailableRoutes()` (`tests/src/core/RecordingInputPathTest.cpp:275`) shrinks 8 channels to 2, verifies channel 5 is disarmed while channel 0 keeps recording, and feeds stereo input. The producer allocation probe in `tests/src/core/TwoTrackRecordingHarness.cpp:349` covers the unavailable-route path. The realtime callback was not changed; it still uses bounded atomics and the ring buffer, and its delivered-width check still prevents out-of-bounds reads.

## RED/GREEN and reverse-patch evidence

All CTest commands below ran from `build/tests`; build commands ran from the worktree root. The exact command lines include each output log and the captured shell exit code.

| Bug | RED before fix | RED with fix reverted (step 4b) | GREEN with fix restored | Affected CTest family |
|---|---|---|---|---|
| new-p12-274 | `ctest -R '^MultiTrackRecorderTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/test-001.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — exit 8; the regression observed route 1 still armed. | The reverse-patch command below exited 0; rebuilt `zene` and `MultiTrackRecorderTest` (build-004.log, exit 0); the same CTest command with `test-006.log` exited 8 on `!recorder.track(1).isArmed()`. | Reapplied the patch (exit 0), rebuilt `zene` and `MultiTrackRecorderTest` (build-005.log, exit 0), then the same CTest command with `test-007.log` exited 0. | `MultiTrackRecorderTest`: 1 CTest test, passed in the final family run. |
| new-p9-496 | `ctest -R '^RecordingInputPathTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/test-002.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` — exit 8; the regression observed channel 5 still armed. The original allocation harness also failed (test-003.log, exit 8). | The reverse-patch command below exited 0; rebuilt `zene` and all three recorder targets (build-007.log, exit 0); `RecordingInputPathTest` with test-011.log and `TwoTrackRecordingHarness` with test-012.log each exited 8. | Reapplied the patch (exit 0), rebuilt `zene` and the three recorder targets (build-008.log, exit 0); `RecordingInputPathTest` with test-013.log and `TwoTrackRecordingHarness` with test-014.log each exited 0. `MultiTrackRecorderTest` with test-015.log also exited 0. | Final grouped run: `MultiTrackRecorderTest`, `RecordingInputPathTest`, `RecordClipTest`, `RecordingCrashLossTest`, `TwoTrackRecordingHarness` — 5/5 passed (test-016.log, exit 0). |

Reverse-patch commands used for step 4b:

- new-p12-274: `git diff -- include/TrackRecorder.h src/core/audio/TrackRecorder.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/fix.patch` — exit 0. Restore: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/fix.patch` — exit 0. The patch is preserved as `fix-new-p12-274.patch` in the same log folder.
- new-p9-496: `git diff -- include/TrackRecorder.h include/MultiTrackRecorder.h src/core/audio/TrackRecorder.cpp src/core/audio/MultiTrackRecorder.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/fix.patch` — exit 0. Restore: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/fix.patch` — exit 0.

The first post-fix thread test run (test-004.log, exit 8) caught the injection seam throwing just outside the catch block. The seam was moved inside the guarded thread-start operation; build-003.log exited 0 and test-005.log then passed. The RED runs above are the original behavior and the final reverse-patch proof.

## Gates and committed files

`feature_check.py` reported PASS for fork-sources, source reproduction, upstream regression, namespace, tautology, doc figures, complexity, and file length (gate-002.log, exit 0). The final five-test CTest family run is recorded above. After the fixes and report were committed, `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-274 new-p12-274 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/submit-001.log 2>&1; rc=$?; cat /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-274.r1/logs/submit-001.log; echo EXIT=$rc; exit $rc` reported `RESULT: PASS - submit`, exit 0.

Files committed for this batch:

- `include/TrackRecorder.h`
- `include/MultiTrackRecorder.h`
- `src/core/audio/TrackRecorder.cpp`
- `src/core/audio/MultiTrackRecorder.cpp`
- `tests/src/core/MultiTrackRecorderTest.cpp`
- `tests/src/core/RecordingInputPathTest.cpp`
- `tests/src/core/TwoTrackRecordingHarness.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-274.md`

## Self-check

- Regression tests fail with each product fix reverted and pass with it restored: new-p12-274 `MultiTrackRecorderTest` exit 8 / exit 0 (`test-006.log` / `test-007.log`); new-p9-496 `RecordingInputPathTest` and `TwoTrackRecordingHarness` exit 8 / exit 0 (`test-011.log`, `test-012.log` / `test-013.log`, `test-014.log`).
- The startup failure uses a one-shot `LMMS_TESTING` seam and the standard `std::errc::resource_unavailable_try_again` code. The capacity regression changes width synchronously between calls and uses no sleeps or timing. The harness's new unavailable-route probe is one producer call; its existing paced capture portion was already present.
- Caller/sibling review: `MultiTrackRecorder::setInputChannelCapacity()` forwards to every owned `TrackRecorder`; `armTrack()` still validates the selected channel before opening a take; `processInputInterleaved()` keeps its delivered-width bounds guard. `rg -n 'setInputChannelCapacity\\(' include src tests` found the constructor initialization, MultiTrack forwarding, and tests; no backend calls the setter directly. The new behavior is at the documented off-audio-thread capacity API.
- No serialized project state, channel mapping, control command, undo behavior, or audio-thread allocation path changed. The unaffected low-channel route continues recording, and `RecordClipTest` plus `RecordingCrashLossTest` pass in the 5/5 family run.
- The configured platform is Linux/GCC. Windows execution was unavailable in this worktree; the injected exception uses only standard C++ `std::system_error` and `std::errc`, with no OS-specific resource limit or thread API.
- Both verdicts are FIXED, so the not-real-verdict evidence question does not apply.
