FIXED

# Cluster new-p15-90: user LFO waveform replacement lifetime

## Decision

REAL. The original GUI path replaced `m_userDefSampleBuffer` directly in
`src/gui/LfoControllerDialog.cpp:216`; render read that same owner and passed
its raw pointer to `Oscillator::userWaveSample()` in
`src/core/LfoController.cpp:130`. Replacing the shared pointer could destroy
the sample while render still used it.

## Fix on this branch

Commit `06ba34289` routes the waveform picker and the project-load path through
`LfoController::setUserDefSampleBuffer()` (`src/gui/LfoControllerDialog.cpp:216`,
`src/core/LfoController.cpp:310`). Rendering takes a lock-free reader lease
before loading the atomic raw pointer and releases it after sampling
(`src/core/LfoController.cpp:120-157`). The setter retains replaced owners and
publishes the new pointer; the control-thread reclaimer clears retired owners
only when the reader count reaches zero (`src/core/LfoController.cpp:164-209`).
The one-second Qt timer retries reclamation if a render lease remains active.

The regression in `tests/src/core/LfoControllerUserWaveTest.cpp:57-96` now
builds against both the fixed and original product APIs. With the fix it uses
the production setter/acquire/release/reclaim path. With the product patch
reverted it models the original raw pointer and direct owner replacement and
asserts the same lifetime contract. It fails on the original code because the
weak owner expires at replacement. The test manually controls the lease, uses
no thread, sleep, race, or wall-clock wait, and checks reclamation after release.

## Evidence

All commands below ran in the stated worktree or build directory. Build and
test logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/`.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code regression build | `cmake --build build --target zene LfoControllerUserWaveTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt `zene` and the regression with the original product implementation. |
| Original-code RED | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | 1 test failed at the weak-owner lifetime assertion. |
| Reverse-patch proof | `git diff HEAD^ HEAD -- include/LfoController.h src/core/LfoController.cpp src/gui/LfoControllerDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/fix.patch` | 0 | The product fix was already committed on this branch, so the proof patch was extracted from that commit instead of an empty worktree diff. Tests were left untouched. |
| Reverted-code build | `cmake --build build --target zene LfoControllerUserWaveTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt `zene` and the regression after reversing the product patch. |
| Reverted-code RED | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/test-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | 1 test failed specifically because replacement freed the buffer while its raw pointer was held. |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 | Restored the committed handoff fix. |
| Fixed-code build | `cmake --build build --target zene LfoControllerUserWaveTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt the fixed product and regression. |
| Fixed-code GREEN | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/test-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Regression passed, 1/1. |
| Header-dependent family build | `cmake --build build --target zene LfoControllerUserWaveTest LfoGraphTest RackDeferredLifetimeTest MidiControllerMenuTest UndoHonestyTest UndoHonestyStateTest PhaseDSidechainTest AutomationPrepassProofTest amplifier > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/build-7.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt `zene`, all eight related tests, and the Rack test's amplifier fixture. |
| Header-dependent family | `ctest -R '^(LfoControllerUserWaveTest|LfoGraphTest|RackDeferredLifetimeTest|MidiControllerMenuTest|UndoHonestyTest|UndoHonestyStateTest|PhaseDSidechainTest|AutomationPrepassProofTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/test-7.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | 8/8 tests passed. |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-90 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | All eight gate rows passed; complexity and file-length found no new violations. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-90 new-p15-90 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-90.r2/logs/completion-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | `submit_check.py` passed and the working tree was clean. |

The reverse-patch proof used `git diff HEAD^ HEAD` because the product fix was
already in `HEAD`; a default worktree diff would have been empty. Both RED and
GREEN runs rebuilt the affected executable before ctest.

## Self-check

- Regression: `LfoControllerUserWaveTest::replacedWaveRemainsAliveWhileRenderLeaseIsHeld` fails on the original implementation and with the product patch reversed (ctest exit 8), then passes restored (ctest exit 0, 1/1). The same final test source is used in both runs.
- Determinism: the test controls the render lease directly through the production acquire/release seam and observes `weak_ptr` lifetime; the original-code branch holds the corresponding raw pointer. It uses no timing, sleeps, threads, or races.
- Completeness: `rg` found the picker at `src/gui/LfoControllerDialog.cpp:216` and project loader at `src/core/LfoController.cpp:310` as the only replacement callers; both use the setter. The render read/release are at `src/core/LfoController.cpp:120,139,157`. `saveSettings()` still writes the same `userwavefile` attribute at `src/core/LfoController.cpp:290`.
- Collateral and persistence: project loading still checks the same `userwavefile`, resolves the same path, and calls `SampleBuffer::fromFile()`; missing-file reporting is unchanged at `src/core/LfoController.cpp:306-312`. Saving still serializes `m_userDefSampleBuffer->audioFile()`. Non-user wave shapes follow the existing switch path. No command or undo behavior changed.
- Realtime: the audio path only increments/decrements the lock-free reader counter and loads the atomic raw pointer (`src/core/LfoController.cpp:120,175-185`); compile-time assertions require both atomics to be always lock-free (`src/core/LfoController.cpp:42-43`). Buffer-owner vector mutation, destruction, and timer handling occur in the control-thread setter/reclaimer.
- Verdict evidence: the reversed-product test failure is the runtime evidence that the original direct assignment/read path was a real bug; this is a FIXED verdict, so ALREADY-FIXED/FALSE-POSITIVE evidence is not applicable.
- Completion check: `submit_check.py` exited 0 and returned `RESULT: PASS - submit`; its five judgment prompts are answered by the evidence above.

## Files committed

The product fix is in commit `06ba34289`: `include/LfoController.h`,
`src/core/LfoController.cpp`, and `src/gui/LfoControllerDialog.cpp`. That commit
also registered the regression in `tests/CMakeLists.txt`,
`tests/fork-sources.txt`, and `tests/all-sources.txt`, with reasons in
`tests/upstream-modifications.txt`.

The QA follow-up commits update `tests/src/core/LfoControllerUserWaveTest.cpp`
and this report so the test remains runnable and fails after reversing the
product patch; the report commit records the completion-check result.
