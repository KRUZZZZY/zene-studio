# Cluster new-p12-200

**Verdict: FIXED** (round 4, rework on top of rounds 1-3; addresses Luna's REWORK
review `qa/reviews/new-p12-200.r3.md`)

## Round 4 (this rework)

Luna's r3 review accepted the production fix but asked for two changes:

1. **Test cleanup on every path.** `aDeferredTransitionPeriodIsNotAdmittedAfterTheSnapshotReleases`
   asserted `QVERIFY(state.paused)` *before* releasing the snapshot, signalling the
   producer, joining it, and clearing the hook - so a timed-out pause wait would
   terminate the test while the producer thread was still spinning inside the hook
   and the ring was left mid-snapshot. The test now releases the snapshot, signals
   the producer to resume, joins it, and clears the hook on **every** path (including
   a timed-out wait) *before* the `QVERIFY(state.paused)` assertion, so a failure can
   only terminate the test cleanly.
2. **Behavioral RED proof.** The r3 report's RED evidence was a compile error
   (`setPushPauseTestHook` not a member without the product changes), which does not
   prove the regression. The RED below is a reproducible **behavioral** failure: the
   bug is reintroduced by swapping the deferred-period `dropPeriod()` for a
   `m_ring.push()` of the new-rate frames into the still-old-rate ring, the test is
   rebuilt, and the focused test fails because the window holds 400 frames (300
   old-rate + 100 new-rate) where 300 are expected - exactly the mixed-rate window
   the fix refuses.

The production change (r3) is unchanged: a period that observes a pending snapshot is
treated as deferred for its whole lifetime and is dropped via the realtime-safe
`RetroAudioRing::dropPeriod()` instead of being admitted to the old-rate ring.

## Evidence

| Check | Result |
|---|---|
| RED (reintroduce bug: deferred period `m_ring.push()` instead of `dropPeriod()`) | `cmake --build ... --target zene RetroAudioCaptureTest` EXIT=0; `ctest -R '^RetroAudioCaptureTest$'` EXIT=8. Behavioral failure: `window.frames.size()` Actual 400, Expected 300 (`aDeferredTransitionPeriodIsNotAdmittedAfterTheSnapshotReleases`). Not a crash, not a compile error. |
| GREEN (fixed: deferred period dropped) | `ctest -R '^RetroAudioCaptureTest$'` EXIT=0 (all 12 QTest functions pass). |
| Live control path | `ctest -R '^ControlRecordInputs$'` EXIT=0. |
| Complexity gate | EXIT=1 (pre-existing; regressions in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp` - none touched here). |
| File-length gate | EXIT=1 (pre-existing; files >500 lines unrelated to this change). |
| Files committed | `tests/src/core/RetroAudioCaptureTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p12-200.md`. |

## Commands and exit codes (r4 logs: `~/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r4/logs`)

```text
# Build the fixed tree and the focused test (GREEN baseline).
cmake --build build --target zene RetroAudioCaptureTest -j4 > LOGS/build-green.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > LOGS/test-green.log 2>&1; echo EXIT=$?   # EXIT=0

# RED: reintroduce the bug by admitting the deferred period to the old-rate ring.
git diff -- src/core/RetroAudioCapture.cpp > LOGS/red.patch        # fixed -> buggy (push instead of dropPeriod)
git apply LOGS/red.patch
cmake --build build --target zene RetroAudioCaptureTest -j4 > LOGS/build-red.log 2>&1; echo BUILD_EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > LOGS/test-red.log 2>&1; echo TEST_EXIT=$?   # EXIT=8 (FAIL: 400 vs 300)

# GREEN: restore the fix and rerun.
git apply -R LOGS/red.patch
cmake --build build --target zene RetroAudioCaptureTest -j4 > LOGS/build-green2.log 2>&1; echo BUILD_EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^RetroAudioCaptureTest$' --output-on-failure > LOGS/test-green2.log 2>&1; echo TEST_EXIT=$?   # EXIT=0

# Unaffected family and gates.
cd build/tests && ctest -R '^ControlRecordInputs$' --output-on-failure > LOGS/test-ci.log 2>&1; echo EXIT=$?   # EXIT=0
bash tests/complexity-gate.sh --check > LOGS/gate-ccn.log 2>&1; echo EXIT=$?   # EXIT=1 (pre-existing, unrelated)
bash tests/file-length-gate.sh --check > LOGS/gate-len.log 2>&1; echo EXIT=$?   # EXIT=1 (pre-existing, unrelated)
```

## Round 3 (prior, retained for context)

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
