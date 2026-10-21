FIXED

5308: ALREADY-FIXED

Evidence: [SampleRecordHandle.cpp:140](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/src/core/SampleRecordHandle.cpp:140) requests accumulator completion and publishes the take without calling clip setters. [SampleRecordHandle.cpp:164](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/src/core/SampleRecordHandle.cpp:164) finishes it on the main-thread reaper and hands it to [SampleClip.cpp:285](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/src/core/SampleClip.cpp:285), where the sample and record model are updated. The regression [RecordingRealtimeTest.cpp:408](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/tests/src/core/RecordingRealtimeTest.cpp:408) checks the clip state and signal remain unchanged at destruction and change only after explicit reaping.

RED/GREEN evidence: for RED, I temporarily restored the old direct `setSampleBuffer()` / `setRecord(false)` calls in the destructor; the test failed on `untouchedByDestructor`. For GREEN, I restored the branch's deferred path. These were test-only source mutations and are not in the committed diff.

| Phase | Command and working directory | Exit | Evidence log |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene RecordingRealtimeTest` (worktree root) | 0 | `logs/020-build-legacy-5308-final.log` |
| RED test | `ctest -R '^RecordingRealtimeTest$' --output-on-failure` (`build/tests`) | 8 | `logs/021-red-proof-5308-final.log`, failed `claimedTakeCompletionIsDeferredToTheReaper` |
| GREEN build | `cmake --build build --target zene RecordingRealtimeTest` (worktree root) | 0 | `logs/022-build-restored-all-fixes.log` |
| GREEN test | `ctest -R '^RecordingRealtimeTest$' --output-on-failure` (`build/tests`) | 0 | `logs/023-green-final-after-5308-proof.log`, 1/1 CTest target passed |

new-p12-217: FIXED

Evidence: the full primary queue used to count an overflow and lose the `PendingRecordTake*`. [SampleRecordHandle.cpp:55](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/src/core/SampleRecordHandle.cpp:55) now carries an intrusive link in each existing take object; [SampleRecordHandle.cpp:71](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/src/core/SampleRecordHandle.cpp:71) holds the atomic fallback head. The destructor diverts takes to it without allocating or freeing, and keeps later takes behind an existing fallback head ([SampleRecordHandle.cpp:149](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/src/core/SampleRecordHandle.cpp:149)). The reaper exchanges, reverses, and processes the fallback list on the main thread ([SampleRecordHandle.cpp:195](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/src/core/SampleRecordHandle.cpp:195)). Its size is bounded by existing prepared and in-flight takes, with no queue-node allocation on audio. The regression [RecordingRealtimeTest.cpp:709](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308/tests/src/core/RecordingRealtimeTest.cpp:709) fills the 256 primary slots, then proves the claimed overflow take replaces the clip's original buffer and disarms it.

RED/GREEN evidence: `fix.patch` contains only the product changes. Reversing it makes `fullRetiredTakeQueueStillInstallsClaimedTake` fail; reapplying it makes the family pass.

| Phase | Command and working directory | Exit | Evidence log |
| --- | --- | ---: | --- |
| Save product patch | `git diff -- src/core/SampleRecordHandle.cpp include/SampleRecordHandle.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5308.r2/logs/fix.patch` | 0 | patch saved at the required path |
| Remove fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5308.r2/logs/fix.patch` | 0 | product returned to the queue-drop behavior |
| RED build | `cmake --build build --target zene RecordingRealtimeTest` (worktree root) | 0 | `logs/016-build-removed-217-fix.log` |
| RED test | `ctest -R '^RecordingRealtimeTest$' --output-on-failure` (`build/tests`) | 8 | `logs/017-red-proof-new-p12-217-final.log`, failed `fullRetiredTakeQueueStillInstallsClaimedTake` |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5308.r2/logs/fix.patch` | 0 | product fix restored |
| GREEN build | `cmake --build build --target zene RecordingRealtimeTest` (worktree root) | 0 | `logs/018-build-restored-217-fix.log` |
| GREEN test | `ctest -R '^RecordingRealtimeTest$' --output-on-failure` (`build/tests`) | 0 | `logs/019-green-final-family.log`, 1/1 CTest target passed |

The affected Qt test family contains 43 QtTest cases (per-case count in the RED log); the final CTest run passed its single registered target. No save format, load path, undo step, or control command changed.

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308` exited 0 (`logs/027-feature-check-final.log`, all rows PASS).

Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5308 5308` exited 0 after the first commit (`logs/028-submit-check-after-commit.log`). Its five ASK items are answered in `## Self-check` below.

Files committed: `include/SampleRecordHandle.h`, `src/core/SampleRecordHandle.cpp`, `tests/src/core/RecordingRealtimeTest.cpp`, `tests/upstream-modifications.txt`, and this report.

## Self-check

- RED/GREEN: the 5308 regression fails with the legacy direct clip setters (build 020 exit 0, CTest 021 exit 8) and passes with the existing deferred path (build 022 exit 0, CTest 023 exit 0). The new-p12-217 regression fails with only the product diff reversed (build 016 exit 0, CTest 017 exit 8) and passes restored (build 018 exit 0, CTest 019 exit 0).
- Determinism: both regressions synchronously inject 40 fixed input periods using `pushInputFrames()` and `renderNextPeriod()`, fill the primary queue with exactly 256 handles, then explicitly call the reaper. Neither new regression sleeps or pumps events; accumulator handoff uses its existing synchronized ring and is joined before the clip assertions.
- Cause and callers: the only retired-take queue operations are the destructor and reaper in `SampleRecordHandle.cpp`. Engine render removals reach the destructor through `AudioEngine::disposePlayHandle()` (`AudioEngine.cpp:274-288`, called at `:766` and `:869`); control-side unclaimed-take release also reaches it through `SampleClip::releaseRecordTakes()` (`SampleClip.cpp:248-258`). The timer and explicit pump both call the same reaper. The fallback preserves the existing claimed/unclaimed handling and main-thread install path.
- Collateral and realtime: no serialization, loading, command, or undo code changed. The existing recording family, including its undo and save/reopen cases, passed (CTest 019 exit 0). The new fallback only links already allocated take objects with atomics on the audio path; it adds no allocation, free, lock, wait, signal, I/O, or control command.
- ALREADY-FIXED evidence for 5308: `SampleRecordHandle.cpp:140-159` does not call clip setters; `reapRetiredTakes()` at `:164-212` installs on the main thread through `SampleClip::acceptRecordedTake()` (`SampleClip.cpp:285-313`).
