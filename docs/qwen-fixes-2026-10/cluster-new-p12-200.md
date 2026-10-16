# Cluster new-p12-200

**Verdict: FIXED** (round 3, on top of rounds 1 and 2; addresses Luna's REWORK review
`qa/reviews/new-p12-200.r2.md`)

## Round 3 (this rework)

Luna's r2 review found a remaining timeout interleaving: in `RetroAudioCapture::push()`
the producer could observe a pending snapshot, defer the rate transition, and then have
the snapshot released before `ring.push()` admitted that period under the unchanged
old-rate tag - leaving mixed-rate frames in the ring while tag validation still
succeeded.

Fix: a period that observes a pending snapshot is now treated as deferred for its
whole lifetime and is dropped via a new realtime-safe `RetroAudioRing::dropPeriod()`
instead of being admitted to the old-rate ring. `dropPeriod()` acknowledges the
snapshot if it is still pending and silently discards the period if it has since
been released, so a deferred period can never be admitted under the old rate even
when the snapshot flag clears before admission. The intended deferred transition
and snapshot acknowledgement are preserved. The rate decision was split into
`RetroAudioCapture::transitionRateIfNeeded()` to keep `push()` within the
complexity ratchet.

A deterministic regression
(`aDeferredTransitionPeriodIsNotAdmittedAfterTheSnapshotReleases`) pauses the
producer after the pending check via a test hook, releases the snapshot, then
resumes admission, and verifies the subsequent take holds only the old-rate frames.

## Evidence

| Check | Result |
|---|---|
| RED (reversed product changes) | `cmake --build build --target RetroAudioCaptureTest` EXIT=2 (compile error: `setPushPauseTestHook` is not a member of `lmms::RetroAudioCapture`); test cannot build without the product changes. |
| GREEN (fixed) | `ctest -R '^RetroAudioCaptureTest$'` EXIT=0. |
| Live control path | `ctest -R '^ControlRecordInputs$'` EXIT=0. |
| Complexity gate | EXIT=1 (pre-existing; no RetroAudioCapture regression). |
| File-length gate | EXIT=1 (pre-existing; no RetroAudioCapture regression). |
| Files committed | `include/RetroAudioCapture.h`, `include/RetroAudioRing.h`, `src/core/RetroAudioCapture.cpp`, `tests/src/core/RetroAudioCaptureTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p12-200.md`. |

## Commands and exit codes (r3 logs: `~/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r3/logs`)

```text
cmake --build build --target zene RetroAudioCaptureTest -j4 > LOGS/build-final.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > LOGS/test-final.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^ControlRecordInputs$' --output-on-failure > LOGS/test-ci.log 2>&1; echo EXIT=$?   # EXIT=0
bash tests/complexity-gate.sh --check > LOGS/gate-ccn.log 2>&1; echo EXIT=$?   # EXIT=1 (pre-existing)
bash tests/file-length-gate.sh --check > LOGS/gate-len.log 2>&1; echo EXIT=$?   # EXIT=1 (pre-existing)
git diff -- src/core/RetroAudioCapture.cpp include/RetroAudioCapture.h include/RetroAudioRing.h > LOGS/fix.patch && git apply -R LOGS/fix.patch; cmake --build build --target RetroAudioCaptureTest -j4 > LOGS/build-red2.log 2>&1; echo EXIT=$?   # EXIT=2 RED
git apply LOGS/fix.patch; cmake --build build --target zene RetroAudioCaptureTest -j4 > LOGS/build-final.log 2>&1; echo EXIT=$?   # EXIT=0 GREEN
```
