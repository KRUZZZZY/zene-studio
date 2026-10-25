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

Decision: REAL. `refreshClips` used the first target's upstream frames for every target in one clip. A clip has one curve read time, so a single offset cannot represent targets at different upstream positions. The control-thread refresh now keeps the clip's compensation only when all live targets have the same upstream latency; mixed-latency clips keep their saved target links and read uncompensated.

Test matrix:

- Normal/common latency: existing `W3EngineTest::automationReadsWhereTheLatentAudioIs` and `W3EngineTest::discreteAutomationCompensationKeepsItsFrameOffset` cover shared compensation.
- Mixed-target reconfiguration: `W3EngineTest::mixedLatencyTargetsDoNotShareOneCompensationShift` gives one clip a 64-frame downstream target and a 0-frame target. It expects the clip to be left uncompensated and fails against first-target stamping.
- Empty and removed targets: the preceding `latencyRefreshUsesTargetStateCapturedUnderItsLock` covers removal; empty lists remain 0. Snapshot bank selection/refusal is unchanged and `latencyRefreshKeepsOneReadOnPublishedClipValues` covers coherent publication.
- Realtime/platform: mismatch detection runs in the existing control-thread refresh over a stable target snapshot. No per-period path, file format, target link or serialization changed. The configured C++20/Qt6 Linux build passed; no other platform builder was available.

RED / reverse-patch proof:

- `git diff -- src/core/AutomationLatency.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-12.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-12.log; echo EXIT=$result` — EXIT=0.
- From `build/tests`: `ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-11.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-11.log; echo EXIT=$result` — EXIT=8; 1 CTest target ran, 12 QtTest cases passed and the new case failed (actual 64, expected 0).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/fix.patch` — EXIT=0.
- `cmake --build build --target zene W3EngineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-13.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/build-13.log; echo EXIT=$result` — EXIT=0.
- From `build/tests`: `ctest -R '^W3EngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-12.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-299.r1/logs/test-12.log; echo EXIT=$result` — EXIT=0; 1/1 CTest target passed.

Files in this commit: `src/core/AutomationLatency.cpp`, `tests/src/core/W3EngineTest.cpp`, and this report.
