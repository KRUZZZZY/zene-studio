FIXED

new-p16-299: FIXED

Decision: REAL. `AutomationClip::objects()` unlocks before returning its vector reference (`src/core/AutomationClip.cpp:325`); the refresh then queried `.empty()` through that reference (`src/core/AutomationLatency.cpp:262`). `objectsSnapshot()` copies the QPointers while holding `m_clipMutex`, and refresh uses that stable copy. The regression's LMMS_TESTING probe removes a target exactly after the legacy getter unlocks; the old refresh stamped 0 frames where the target's upstream latency was 64.

Test matrix:

- Normal compensation: existing `W3EngineTest::automationReadsWhereTheLatentAudioIs` exercises a latent effect and its automation target.
- Target-list reconfiguration: `W3EngineTest::latencyRefreshUsesTargetStateCapturedUnderItsLock` removes the target at the unlocked-reference boundary; this fails without the snapshot fix.
- Publication/busy-bank behavior: existing `W3EngineTest::latencyRefreshKeepsOneReadOnPublishedClipValues` checks that a reader observes one complete publication while refresh pauses between clips. The bank selection and busy-bank refusal code are unchanged.
- Realtime: `objectsSnapshot()` is called only by control-thread `refreshClips`; the audio path still reads its pinned scalar latency and adds no allocation, lock, wait, or log.
- Platform: the configured C++20/Qt6 Linux build and W3EngineTest were used. The change uses standard vector/QPointer copying and has no platform-specific branch; other platform builds were not available in this worktree.

RED / reverse-patch proof:

- `git diff -- src/core/AutomationLatency.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-6.log; echo EXIT=$result` — EXIT=0.
- From `build/tests`: `ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-5.log; echo EXIT=$result` — EXIT=8; 1 CTest target ran, 11 QtTest cases passed and the new case failed (actual 0, expected 64).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-7.log; echo EXIT=$result` — EXIT=0.
- From `build/tests`: `ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-6.log; echo EXIT=$result` — EXIT=0; 1/1 CTest target passed.

Gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-299` — EXIT=0, all gates PASS in `gate-2.log`. The first gate run found the train-12 snapshot's 1148 vendored sfizz entries had returned to `tests/all-sources.txt`; that list was re-derived from its published recipe and now reproduces. No gate limits or baselines changed.

Files in this commit: `include/AutomationClip.h`, `src/core/AutomationClip.cpp`, `src/core/AutomationLatency.cpp`, `tests/src/core/W3EngineTest.cpp`, `tests/upstream-modifications.txt`, `tests/all-sources.txt`, and this report.

5492: FIXED

Decision: REAL. A clip has one curve read time; its targets cannot receive correctly timed automation when their upstream latencies differ. With compensation enabled, refresh now marks that clip incompatible (`latencyFrames() == -1`), and every per-tick absolute, relative, and sample-accurate ramp path skips the whole clip. Target links stay saved and intact. Once target latencies match again, or compensation is disabled, the clip becomes active again.

Test matrix:

- Normal/common latency: existing `W3EngineTest::automationReadsWhereTheLatentAudioIs` and `W3EngineTest::discreteAutomationCompensationKeepsItsFrameOffset` cover shared compensation.
- Mixed-target rejection: `W3EngineTest::mixedLatencyTargetsAreRejectedUntilCompatible` builds one 64-frame target and one 0-frame target. It asserts neither target receives an absolute value, neither receives a relative offset, and neither receives a sample-accurate ramp. Removing the incompatible target and refreshing restores 64-frame compensation and automation to the remaining target.
- Common, empty, and removed targets: existing `automationReadsWhereTheLatentAudioIs` and `discreteAutomationCompensationKeepsItsFrameOffset` cover common timing; `latencyRefreshUsesTargetStateCapturedUnderItsLock` covers removal and `latencyRefreshKeepsOneReadOnPublishedClipValues` covers coherent publication. Empty and all-null target lists retain 0.
- Disabled mode and realtime/platform: `sharedTargetLatency` returns 0 before checking target agreement when compensation is off, preserving ordinary unshifted automation. The mismatch sentinel is computed during control-thread refresh; audio paths only read the pinned integer and skip an incompatible clip. No target link, project schema, or saved data changes. The configured C++20/Qt6 Linux build passed; no other platform builder was available.

RED / reverse-patch proof:

- Before the fix: `cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-25.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-25.log; echo EXIT=$result` — EXIT=0; from `build/tests`, `ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-24.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-24.log; echo EXIT=$result` — EXIT=8 at `absoluteRejected` because the old path still returned both targets.
- `git diff -- src/core/AutomationLatency.cpp src/core/TrackContainer.cpp src/core/Song.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix-5492.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix-5492.patch` — EXIT=0; this reverses the 5492 playback rejection while keeping the test intact.
- `cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-28.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-28.log; echo EXIT=$result` — EXIT=0; from `build/tests`, `ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-27.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-27.log; echo EXIT=$result` — EXIT=8 at the same assertion.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix-5492.patch` — EXIT=0; `cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-29.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-29.log; echo EXIT=$result` — EXIT=0; from `build/tests`, `ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-28.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-28.log; echo EXIT=$result` — EXIT=0.
- Final focused build: `cmake --build build --target zene W3EngineTest InstrumentLatencyPdcTest AutomationPreFirstValueTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-30.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-30.log; echo EXIT=$result` — EXIT=0. From `build/tests`, `ctest -R '^(W3EngineTest|InstrumentLatencyPdcTest|AutomationPreFirstValueTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-29.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-29.log; echo EXIT=$result` — EXIT=0; 3/3 focused targets passed.

Files in this commit: `include/AutomationClip.h`, `include/AutomationLatency.h`, `src/core/AutomationLatency.cpp`, `src/core/TrackContainer.cpp`, `src/core/Song.cpp`, `tests/src/core/W3EngineTest.cpp`, `tests/upstream-modifications.txt`, and this report.

new-p9-575: FIXED

Decision: REAL. Before the fix, `Song::loadProject` released the engine model-change guard and emitted `projectLoaded` before the mixer's first `prepareMasterMix()` published channel PDC. The one-shot automation refresh therefore stamped clips from the prior channel alignment (normally 0). The load path now publishes the mixer PDC and refreshes automation clips after target resolution while it still holds the guard, before a new audio period can read the project.

Test matrix:

- Immediate load/render path: `AutomationPreFirstValueTest::loadedProjectPublishesPdcBeforeAutomationIsStamped` saves an enabled automation clip on the master fader, reloads it, and injects a 64-frame effect at the load-time PDC update hook. It checks the stamp before rendering; it uses no sleeps, wall-clock waits or concurrent race.
- Failure and reconfiguration: the update uses the mixer's existing preallocated PDC scratch and the clip refresh uses its existing wait-for-free-bank publication. Disabled compensation still publishes 0. The periodic refresh and its busy-bank path remain in place for later changes.
- Realtime: both new calls run on the project-load control thread under the existing model-change guard. PDC recomputation is allocation-free; target-vector copying stays off the audio thread. The audio read path remains unchanged.
- Platform: the configured C++20/Qt6 Linux build passed for `zene`, `AutomationPreFirstValueTest`, `W3EngineTest` and `InstrumentLatencyPdcTest`. The added probe is compiled only for LMMS_TESTING; no other platform builder was available.

RED / reverse-patch proof:

- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix.patch; result=$?; echo EXIT=$result` — EXIT=0; this reverses only the Song load call and retains the test seam.
- `LOG=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-23.log; cmake --build build --target zene AutomationPreFirstValueTest > "$LOG" 2>&1; result=$?; echo EXIT=$result >> "$LOG"; echo EXIT=$result` — EXIT=0.
- From `build/tests`: `LOG=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-22.log; ctest -R '^AutomationPreFirstValueTest$' --output-on-failure > "$LOG" 2>&1; result=$?; echo EXIT=$result >> "$LOG"; echo EXIT=$result` — EXIT=8; 1 CTest target failed at the new case (actual 0, expected 64).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix.patch; result=$?; echo EXIT=$result` — EXIT=0.
- `LOG=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-24.log; cmake --build build --target zene W3EngineTest InstrumentLatencyPdcTest AutomationPreFirstValueTest > "$LOG" 2>&1; result=$?; echo EXIT=$result >> "$LOG"; echo EXIT=$result` — EXIT=0.
- From `build/tests`: `LOG=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-23.log; ctest -R '^(W3EngineTest|InstrumentLatencyPdcTest|AutomationPreFirstValueTest)$' --output-on-failure > "$LOG" 2>&1; result=$?; echo EXIT=$result >> "$LOG"; echo EXIT=$result` — EXIT=0; 3/3 affected CTest targets passed.
- Final gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-299` — EXIT=0; all gate rows PASS in `gate-8.log`.

Files in this commit: `include/AutomationLatency.h`, `include/Mixer.h`, `src/core/AutomationLatency.cpp`, `src/core/Mixer.cpp`, `src/core/Song.cpp`, `tests/src/core/AutomationPreFirstValueTest.cpp`, `tests/upstream-modifications.txt`, and this report.

## Self-check

- **RED/GREEN proof:** `new-p16-299` reverse-patch build `build-6.log` exited 0 and `test-5.log` exited 8; restored `build-7.log` and `test-6.log` both exited 0. `5492` reverse-patch `build-28.log` exited 0 and `test-27.log` exited 8 because the stronger rejection assertion failed; restored `build-29.log` and `test-28.log` both exited 0. The final focused build `build-30.log` and combined `test-29.log` exited 0, with 3/3 targets passing. `new-p9-575` reverse-patch build `build-23.log` exited 0 and `test-22.log` exited 8; restored `build-24.log` and `test-23.log` both exited 0.
- **Determinism:** the target-list test removes a target synchronously at the `objects()` unlock probe; the mixed-latency test builds fixed 64-frame and 0-frame targets and checks all three application paths; the project-load test injects 64 frames from the `LMMS_TESTING` mixer update hook. None uses a sleep, timestamp, or racing thread.
- **Completeness:** `AutomationLatency::refreshClips` snapshots the target vector and marks incompatible clips with -1. The per-tick absolute and relative callers in `TrackContainer.cpp` and sample-accurate ramp caller in `Song.cpp` skip such clips. The existing `Song::process` reference path is guarded by the render-period change lock documented at `src/core/AutomationClip.cpp:179`; target add/remove takes that guard first. The test-only target counter is not a runtime refresh caller. Both periodic and `projectLoaded` refreshes remain; `Song::loadProject` publishes PDC before render and template loading enters that loader (`src/core/Song.cpp:1860`).
- **Compatibility and realtime:** no project schema or target links changed. When latency compensation is enabled, mixed clips are rejected as a whole; they resume if compatible, and disabled mode still uses the old unshifted read. The PDC load update remains on the control thread. Audio paths read only the pinned integer and skip; no allocation, lock, wait, log or signal was added. No command or undo behavior changed. The final gate table in `gate-8.log` is all PASS with EXIT=0.
- **Verdicts:** all three are REAL and FIXED. No ALREADY-FIXED or FALSE-POSITIVE verdict needs a separate contract citation.
